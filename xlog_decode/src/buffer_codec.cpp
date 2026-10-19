#include "buffer_codec.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <zlib.h>

namespace xlog::decode::detail {
namespace {
[[noreturn]] void fail(Status code, const char *message)
{
    throw Failure{code, 0, message};
}

std::uint32_t read32(const unsigned char *bytes)
{
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8)
        | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}

int hex_digit(unsigned char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
}

void wipe(void *data, std::size_t size) noexcept
{
    OPENSSL_cleanse(data, size);
}

Bytes decompress(std::span<const unsigned char> payload, std::size_t budget)
{
    if (payload.empty())
        fail(Status::decompression_error, "compressed payload contains no raw DEFLATE stream");
    if (payload.size() > std::numeric_limits<uInt>::max())
        fail(Status::invalid_argument, "compressed payload exceeds zlib's input limit");

    z_stream stream{};
    const int initialized = inflateInit2(&stream, -MAX_WBITS);
    if (initialized == Z_MEM_ERROR) throw std::bad_alloc{};
    if (initialized != Z_OK)
        fail(Status::decompression_error, "raw DEFLATE initialization failed");
    const auto end_inflate = [](z_stream *value) { inflateEnd(value); };
    const std::unique_ptr<z_stream, decltype(end_inflate)> cleanup(&stream, end_inflate);
    stream.next_in = const_cast<Bytef *>(payload.data());
    stream.avail_in = static_cast<uInt>(payload.size());

    Bytes output;
    std::array<unsigned char, 65536> chunk{};
    std::size_t block_start_bit = 0;
    std::size_t block_output_start = 0;
    for (;;) {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        // Block boundaries distinguish an actual sync flush from a matching suffix
        // inside a truncated block. Merely inspecting the final four bytes is unsafe.
        const int status = inflate(&stream, Z_BLOCK);
        const std::size_t produced = chunk.size() - stream.avail_out;
        if (produced > budget - output.size())
            fail(Status::output_limit, "decoded output exceeds the configured limit");
        output.insert(output.end(), chunk.begin(), chunk.begin() + produced);

        if (status == Z_STREAM_END) {
            if (stream.avail_in != 0)
                fail(Status::decompression_error, "trailing data after raw DEFLATE stream");
            return output;
        }
        if (status == Z_MEM_ERROR) throw std::bad_alloc{};
        if (status != Z_OK)
            fail(Status::decompression_error, "malformed or truncated raw DEFLATE stream");

        if ((stream.data_type & 128) != 0) {
            if (stream.avail_in == 0 && stream.data_type == 128
                && output.size() == block_output_start
                && block_start_bit + 2 < payload.size() * 8) {
                const auto bit = [&](std::size_t position) {
                    return (payload[position / 8] >> (position % 8)) & 1U;
                };
                // zlib has validated the stored block's LEN/NLEN. A byte-aligned,
                // nonfinal empty stored block is the xlog Z_SYNC_FLUSH ending.
                if (bit(block_start_bit + 1) == 0 && bit(block_start_bit + 2) == 0)
                    return output;
            }
            block_start_bit = (payload.size() - stream.avail_in) * 8 - (stream.data_type & 63);
            block_output_start = output.size();
        }
    }
}

TeaKey make_tea_key(std::span<const unsigned char, 64> public_key, const char *private_key_hex)
{
    if (!private_key_hex || private_key_hex[0] == '\0')
        fail(Status::key_required, "encrypted xlog block requires a private key");
    const auto erase_secret = [](PrivateKey *value) { wipe(value->data(), value->size()); };
    PrivateKey private_key{};
    const std::unique_ptr<PrivateKey, decltype(erase_secret)> private_cleanup(&private_key, erase_secret);
    for (std::size_t index = 0; index < 64; ++index) {
        // Check the terminator before advancing, so short strings are not overread.
        if (private_key_hex[index] == '\0')
            fail(Status::invalid_key, "private key must contain exactly 64 hexadecimal digits");
        const int digit = hex_digit(static_cast<unsigned char>(private_key_hex[index]));
        if (digit < 0)
            fail(Status::invalid_key, "private key contains a non-hexadecimal digit");
        private_key[index / 2] |= static_cast<unsigned char>(digit << (index % 2 == 0 ? 4 : 0));
    }
    if (private_key_hex[64] != '\0')
        fail(Status::invalid_key, "private key must contain exactly 64 hexadecimal digits");

    const std::unique_ptr<EC_GROUP, decltype(&EC_GROUP_free)> curve(
        EC_GROUP_new_by_curve_name(NID_secp256k1), EC_GROUP_free);
    const std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)> context(BN_CTX_new(), BN_CTX_free);
    const std::unique_ptr<BIGNUM, decltype(&BN_clear_free)> scalar(
        BN_bin2bn(private_key.data(), static_cast<int>(private_key.size()), nullptr), BN_clear_free);
    if (!curve || !context || !scalar) throw std::bad_alloc{};
    if (BN_is_zero(scalar.get()) || BN_cmp(scalar.get(), EC_GROUP_get0_order(curve.get())) >= 0)
        fail(Status::invalid_key, "private key scalar is outside the secp256k1 range");
    BN_set_flags(scalar.get(), BN_FLG_CONSTTIME);

    const std::unique_ptr<EC_POINT, decltype(&EC_POINT_clear_free)> peer(
        EC_POINT_new(curve.get()), EC_POINT_clear_free);
    const std::unique_ptr<EC_POINT, decltype(&EC_POINT_clear_free)> shared(
        EC_POINT_new(curve.get()), EC_POINT_clear_free);
    const std::unique_ptr<BIGNUM, decltype(&BN_clear_free)> x(BN_new(), BN_clear_free);
    if (!peer || !shared || !x) throw std::bad_alloc{};

    std::array<unsigned char, 65> encoded{0x04};
    std::copy(public_key.begin(), public_key.end(), encoded.begin() + 1);
    if (EC_POINT_oct2point(curve.get(), peer.get(), encoded.data(), encoded.size(), context.get()) != 1
        || EC_POINT_is_at_infinity(curve.get(), peer.get()) != 0
        || EC_POINT_is_on_curve(curve.get(), peer.get(), context.get()) != 1)
        fail(Status::invalid_key, "invalid secp256k1 public point in xlog header");
    if (EC_POINT_mul(curve.get(), shared.get(), nullptr, peer.get(), scalar.get(), context.get()) != 1
        || EC_POINT_is_at_infinity(curve.get(), shared.get()) != 0
        || EC_POINT_get_affine_coordinates(curve.get(), shared.get(), x.get(), nullptr, context.get()) != 1)
        fail(Status::invalid_key, "ECDH shared point calculation failed");
    PrivateKey secret{};
    const std::unique_ptr<PrivateKey, decltype(erase_secret)> shared_cleanup(&secret, erase_secret);
    if (BN_bn2binpad(x.get(), secret.data(), static_cast<int>(secret.size())) != 32)
        fail(Status::internal_error, "ECDH shared coordinate encoding failed");
    TeaKey key{};
    std::copy_n(secret.begin(), key.size(), key.begin());
    return key;
}

Bytes tea_decrypt(std::span<const unsigned char> payload, const TeaKey& key)
{
    if (payload.empty()) return {};
    Bytes output(payload.begin(), payload.end());
    const std::uint32_t words[]{read32(key.data()), read32(key.data() + 4),
        read32(key.data() + 8), read32(key.data() + 12)};
    for (std::size_t index = 0; index < output.size() / 8 * 8; index += 8) {
        std::uint32_t left = read32(output.data() + index);
        std::uint32_t right = read32(output.data() + index + 4);
        std::uint32_t sum = 0xE3779B90U;
        for (int round = 0; round < 16; ++round) {
            right -= ((left << 4) + words[2]) ^ (left + sum) ^ ((left >> 5) + words[3]);
            left -= ((right << 4) + words[0]) ^ (right + sum) ^ ((right >> 5) + words[1]);
            sum -= 0x9E3779B9U;
        }
        for (unsigned byte = 0; byte < 4; ++byte) {
            output[index + byte] = static_cast<unsigned char>(left >> (8 * byte));
            output[index + 4 + byte] = static_cast<unsigned char>(right >> (8 * byte));
        }
    }
    return output;
}
}
