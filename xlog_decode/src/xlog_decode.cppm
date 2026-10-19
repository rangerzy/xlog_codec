module;
#include "buffer_codec.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <meta>
#include <memory>
#include <new>
#include "xlog_version_config.h"

export module xlog_decode.core;

namespace {
static_assert(std::meta::nonstatic_data_members_of(
    ^^xlog::decode::VersionMetadata, std::meta::access_context::current()).size() == 3);
static_assert(std::meta::nonstatic_data_members_of(
    ^^xlog::decode::BlockHeader, std::meta::access_context::current()).size() == 6);

constexpr std::size_t header_size = 73;

std::uint32_t read32(const unsigned char *bytes)
{
    return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8)
        | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
}
}

export namespace xlog::decode {
const char *version() noexcept
{
    return XLOG_VERSION_STRING;
}

BlockView parse_block(std::span<const unsigned char> bytes, std::size_t offset)
{
    const auto fail = [offset](Status code, const char *message) -> void {
        throw Failure{code, offset, message};
    };
    if (offset > bytes.size())
        fail(Status::invalid_argument, "block offset is outside the input buffer");
    if (bytes.size() - offset < header_size)
        fail(Status::truncated_input, "truncated xlog block header (requires 73 bytes)");
    const auto *data = bytes.data() + offset;
    if (data[0] < 0x06 || data[0] > 0x0d)
        fail(Status::invalid_format, "invalid modern xlog magic");
    const std::size_t payload_size = read32(data + 5);
    const std::size_t remaining = bytes.size() - offset - header_size;
    if (payload_size > remaining)
        fail(Status::truncated_input, "truncated xlog payload");
    if (payload_size == remaining)
        fail(Status::truncated_input, "missing xlog block terminator");
    if (data[header_size + payload_size] != 0)
        fail(Status::invalid_format, "invalid xlog block terminator");
    BlockView block{};
    block.header.magic = data[0];
    block.header.seq = std::uint16_t(data[1]) | (std::uint16_t(data[2]) << 8);
    block.header.begin_hour = data[3];
    block.header.end_hour = data[4];
    block.header.payload_length = static_cast<std::uint32_t>(payload_size);
    std::copy_n(data + 9, block.header.public_key.size(), block.header.public_key.begin());
    block.payload = {data + header_size, payload_size};
    block.offset = offset;
    block.encoded_size = header_size + payload_size + 1;
    return block;
}

bool is_xlog_buffer(std::span<const unsigned char> bytes)
{
    if (bytes.empty()) return false;
    std::size_t offset = 0;
    while (offset < bytes.size()) offset += parse_block(bytes, offset).encoded_size;
    return true;
}

Bytes decode_block(const BlockView& block, const Options& options, std::size_t budget)
{
    try {
        if (block.payload.size() != block.header.payload_length)
            throw Failure{Status::invalid_argument, 0, "invalid xlog block payload view"};
        const auto payload = block.payload;
        switch (block.header.magic) {
        case 0x06:
        case 0x08:
            if (payload.size() > budget)
                throw Failure{Status::output_limit, 0, "decoded output exceeds the configured limit"};
            if (payload.empty()) return {};
            return Bytes(payload.begin(), payload.end());
        case 0x09:
            return detail::decompress(payload, budget);
        case 0x07: {
            auto key = detail::make_tea_key(block.header.public_key, options.private_key_hex);
            const auto erase_key = [](TeaKey *value) { detail::wipe(value->data(), value->size()); };
            const std::unique_ptr<TeaKey, decltype(erase_key)> cleanup(&key, erase_key);
            const auto compressed = detail::tea_decrypt(payload, key);
            return detail::decompress(compressed, budget);
        }
        default:
            throw Failure{Status::unsupported_format, 0, "unsupported xlog decoding magic"};
        }
    } catch (Failure& failure) {
        failure.offset = block.offset;
        throw;
    } catch (const std::bad_alloc&) {
        throw Failure{Status::out_of_memory, block.offset, "could not allocate decoded buffer"};
    }
}

Bytes decode_bytes(std::span<const unsigned char> bytes, const Options& options)
{
    if (bytes.empty())
        throw Failure{Status::invalid_format, 0, "empty input is not an xlog buffer"};
    const std::size_t limit = options.max_output_size != 0
        ? options.max_output_size : std::numeric_limits<std::size_t>::max();
    Bytes output;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto block = parse_block(bytes, offset);
        const auto decoded = decode_block(block, options, limit - output.size());
        try {
            output.insert(output.end(), decoded.begin(), decoded.end());
        } catch (const std::bad_alloc&) {
            throw Failure{Status::out_of_memory, offset, "could not allocate merged decoded buffer"};
        }
        offset += block.encoded_size;
    }
    return output;
}
}
