#ifndef XLOG_ENCODE_INTERNAL_TYPES_H
#define XLOG_ENCODE_INTERNAL_TYPES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace xlog::encode {

using Bytes = std::vector<unsigned char>;
using PublicKey = std::array<unsigned char, 64>;
using TeaKey = std::array<unsigned char, 16>;
using SharedSecret = std::array<unsigned char, 32>;

enum class Mode { plain, deflate, deflate_tea };

// Internal values are independent of the public C status and option types.
enum class Status {
    invalid_argument,
    unsupported_mode,
    key_required,
    invalid_key,
    input_limit,
    output_limit,
    compression_error,
    crypto_error,
    out_of_memory,
    internal_error
};

struct Failure {
    Status code;
    const char *message;
};

struct Options {
    Mode mode;
    const char *public_key_hex;
    std::uint16_t seq;
    std::uint8_t begin_hour;
    std::uint8_t end_hour;
    std::size_t max_output_size;
};

struct BlockHeader {
    std::uint8_t magic;
    std::uint16_t seq;
    std::uint8_t begin_hour;
    std::uint8_t end_hour;
    std::uint32_t payload_length;
    PublicKey public_key;
};

struct VersionMetadata {
    int major;
    int minor;
    int patch;
};

}

#endif
