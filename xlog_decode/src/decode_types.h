#ifndef XLOG_DECODE_INTERNAL_TYPES_H
#define XLOG_DECODE_INTERNAL_TYPES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace xlog::decode {

using Bytes = std::vector<unsigned char>;
using TeaKey = std::array<unsigned char, 16>;
using PrivateKey = std::array<unsigned char, 32>;

// Internal failures are deliberately independent of the public C status values.
enum class Status {
    invalid_argument,
    invalid_format,
    truncated_input,
    unsupported_format,
    key_required,
    invalid_key,
    decompression_error,
    output_limit,
    out_of_memory,
    internal_error
};

struct Failure {
    Status code;
    std::size_t offset;
    const char *message;
};

struct BlockHeader {
    std::uint8_t magic;
    std::uint16_t seq;
    std::uint8_t begin_hour;
    std::uint8_t end_hour;
    std::uint32_t payload_length;
    std::array<unsigned char, 64> public_key;
};

struct BlockView {
    BlockHeader header;
    std::span<const unsigned char> payload;
    std::size_t offset;
    std::size_t encoded_size;
};

struct Options {
    const char *private_key_hex;
    std::size_t max_output_size;
};

struct VersionMetadata {
    int major;
    int minor;
    int patch;
};

}

#endif
