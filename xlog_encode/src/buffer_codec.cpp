#include "buffer_codec.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <zlib.h>

namespace xlog::encode::detail {
namespace {
[[noreturn]] void fail(Status code, const char *message)
{
    throw Failure{code, message};
}

int hex_value(char digit) noexcept
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

std::array<unsigned char, 65> parse_public_key(const char *hex)
{
    if (!hex || !*hex) fail(Status::key_required, "receiver public key is required");
    std::array<unsigned char, 65> encoded{};
    encoded[0] = POINT_CONVERSION_UNCOMPRESSED;
    for (std::size_t index = 0; index < 64; ++index) {
        // Check each character before accessing its successor on short strings.
        const int high = hex_value(*hex);
        if (high < 0) fail(Status::invalid_key, "public key must contain exactly 128 hex digits");
        ++hex;
        const int low = hex_value(*hex);
        if (low < 0) fail(Status::invalid_key, "public key must contain exactly 128 hex digits");
        ++hex;
        encoded[index + 1] = static_cast<unsigned char>((high << 4) | low);
    }
    if (*hex) fail(Status::invalid_key, "public key must contain exactly 128 hex digits");
    return encoded;
}

std::uint32_t read_le32(const unsigned char *data) noexcept
{
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

void write_le32(unsigned char *data, std::uint32_t value) noexcept
{
    for (unsigned int index = 0; index < 4; ++index)
        data[index] = static_cast<unsigned char>(value >> (8 * index));
}

void tea_encrypt(Bytes& bytes, const TeaKey& key) noexcept
{
    std::array<std::uint32_t, 4> words{};
    const auto clean = [](auto *value) { wipe(value->data(), sizeof *value); };
    const std::unique_ptr<decltype(words), decltype(clean)> guard(&words, clean);
    for (std::size_t index = 0; index < words.size(); ++index)
        words[index] = read_le32(key.data() + 4 * index);
    for (std::size_t offset = 0; bytes.size() - offset >= 8; offset += 8) {
        std::uint32_t left = read_le32(bytes.data() + offset);
        std::uint32_t right = read_le32(bytes.data() + offset + 4);
        std::uint32_t sum = 0;
        for (unsigned int round = 0; round < 16; ++round) {
            sum += 0x9e3779b9u;
            left += ((right << 4) + words[0]) ^ (right + sum) ^ ((right >> 5) + words[1]);
            right += ((left << 4) + words[2]) ^ (left + sum) ^ ((left >> 5) + words[3]);
        }
        write_le32(bytes.data() + offset, left);
        write_le32(bytes.data() + offset + 4, right);
    }
}
}

void wipe(void *data, std::size_t size) noexcept
{
    OPENSSL_cleanse(data, size);
}

Bytes compress(std::span<const unsigned char> input, std::size_t budget)
{
    z_stream stream{};
    const int initialized = deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS,
        MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
    if (initialized == Z_MEM_ERROR) throw std::bad_alloc{};
    if (initialized != Z_OK) fail(Status::compression_error, "could not initialize raw DEFLATE");
    const auto finish = [](z_stream *state) { deflateEnd(state); };
    const std::unique_ptr<z_stream, decltype(finish)> guard(&stream, finish);
    Bytes output;
    std::array<unsigned char, 64 * 1024> chunk{};
    std::size_t offset = 0;
    do {
        if (stream.avail_in == 0 && offset < input.size()) {
            const auto count = std::min(input.size() - offset, chunk.size());
            stream.next_in = const_cast<Bytef *>(input.data() + offset);
            stream.avail_in = static_cast<uInt>(count);
            offset += count;
        }
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        const bool last = offset == input.size();
        const int result = deflate(&stream, last ? Z_SYNC_FLUSH : Z_NO_FLUSH);
        if (result == Z_MEM_ERROR) throw std::bad_alloc{};
        if (result != Z_OK && result != Z_BUF_ERROR)
            fail(Status::compression_error, "raw DEFLATE failed");
        const std::size_t produced = chunk.size() - stream.avail_out;
        if (produced > budget - output.size())
            fail(Status::output_limit, "encoded payload exceeds the output limit");
        output.insert(output.end(), chunk.begin(), chunk.begin() + produced);
        // A partially filled output buffer means the final flush is complete.
        if (last && stream.avail_in == 0 && stream.avail_out != 0) break;
        if (result == Z_BUF_ERROR && produced == 0)
            fail(Status::compression_error, "raw DEFLATE made no progress");
    } while (true);
    return output;
}

PublicKey encrypt(Bytes& payload, const char *receiver_public_key_hex)
{
    const auto receiver_bytes = parse_public_key(receiver_public_key_hex);
    const auto free_group = [](EC_GROUP *value) { EC_GROUP_free(value); };
    const auto free_context = [](BN_CTX *value) { BN_CTX_free(value); };
    const auto free_point = [](EC_POINT *value) { EC_POINT_clear_free(value); };
    const auto free_number = [](BIGNUM *value) { BN_clear_free(value); };
    const std::unique_ptr<EC_GROUP, decltype(free_group)> group(
        EC_GROUP_new_by_curve_name(NID_secp256k1), free_group);
    const std::unique_ptr<BN_CTX, decltype(free_context)> context(BN_CTX_secure_new(), free_context);
    if (!group || !context) throw std::bad_alloc{};
    const std::unique_ptr<EC_POINT, decltype(free_point)> receiver(EC_POINT_new(group.get()), free_point);
    const std::unique_ptr<EC_POINT, decltype(free_point)> client(EC_POINT_new(group.get()), free_point);
    const std::unique_ptr<EC_POINT, decltype(free_point)> shared(EC_POINT_new(group.get()), free_point);
    const std::unique_ptr<BIGNUM, decltype(free_number)> scalar(BN_secure_new(), free_number);
    const std::unique_ptr<BIGNUM, decltype(free_number)> x(BN_secure_new(), free_number);
    if (!receiver || !client || !shared || !scalar || !x) throw std::bad_alloc{};
    if (EC_POINT_oct2point(group.get(), receiver.get(), receiver_bytes.data(), receiver_bytes.size(), context.get()) != 1
        || EC_POINT_is_on_curve(group.get(), receiver.get(), context.get()) != 1
        || EC_POINT_is_at_infinity(group.get(), receiver.get()) != 0)
        fail(Status::invalid_key, "public key is not a valid secp256k1 point");
    BN_set_flags(scalar.get(), BN_FLG_CONSTTIME);
    do {
        if (BN_priv_rand_range(scalar.get(), EC_GROUP_get0_order(group.get())) != 1)
            fail(Status::crypto_error, "could not generate ephemeral private key");
    } while (BN_is_zero(scalar.get()));
    if (EC_POINT_mul(group.get(), client.get(), scalar.get(), nullptr, nullptr, context.get()) != 1
        || EC_POINT_mul(group.get(), shared.get(), nullptr, receiver.get(), scalar.get(), context.get()) != 1
        || EC_POINT_is_at_infinity(group.get(), shared.get()) != 0
        || EC_POINT_get_affine_coordinates(group.get(), shared.get(), x.get(), nullptr, context.get()) != 1)
        fail(Status::crypto_error, "ECDH key agreement failed");
    SharedSecret secret{};
    TeaKey key{};
    const auto clean = [](auto *value) { wipe(value->data(), value->size()); };
    const std::unique_ptr<SharedSecret, decltype(clean)> secret_guard(&secret, clean);
    const std::unique_ptr<TeaKey, decltype(clean)> key_guard(&key, clean);
    if (BN_bn2binpad(x.get(), secret.data(), static_cast<int>(secret.size())) != static_cast<int>(secret.size()))
        fail(Status::crypto_error, "could not serialize ECDH shared secret");
    std::copy_n(secret.begin(), key.size(), key.begin());
    std::array<unsigned char, 65> client_bytes{};
    if (EC_POINT_point2oct(group.get(), client.get(), POINT_CONVERSION_UNCOMPRESSED,
        client_bytes.data(), client_bytes.size(), context.get()) != client_bytes.size())
        fail(Status::crypto_error, "could not serialize ephemeral public key");
    PublicKey result{};
    std::copy(client_bytes.begin() + 1, client_bytes.end(), result.begin());
    tea_encrypt(payload, key);
    return result;
}

}
