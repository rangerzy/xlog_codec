module;
#include "buffer_codec.h"
#include <algorithm>
#include <limits>
#include <meta>
#include "xlog_version_config.h"

export module xlog_encode.core;

namespace {
static_assert(std::meta::nonstatic_data_members_of(
    ^^xlog::encode::VersionMetadata, std::meta::access_context::current()).size() == 3);
static_assert(std::meta::nonstatic_data_members_of(
    ^^xlog::encode::BlockHeader, std::meta::access_context::current()).size() == 6);
}

export namespace xlog::encode {
const char *version() noexcept
{
    return XLOG_VERSION_STRING;
}

Bytes encode_bytes(std::span<const unsigned char> input, const Options& options)
{
    constexpr std::size_t overhead = 74;
    constexpr auto wire_limit = std::numeric_limits<std::uint32_t>::max();
    if (input.size() > wire_limit)
        throw Failure{Status::input_limit, "input exceeds the single-block limit"};
    if (options.begin_hour > 23 || options.end_hour > 23)
        throw Failure{Status::invalid_argument, "hours must be in the range 0..23"};
    const auto limit = options.max_output_size ? options.max_output_size : std::numeric_limits<std::size_t>::max();
    if (limit < overhead)
        throw Failure{Status::output_limit, "output limit cannot hold the xlog header and terminator"};
    const auto budget = std::min(limit - overhead, static_cast<std::size_t>(wire_limit));
    BlockHeader header{0, options.seq, options.begin_hour, options.end_hour, 0, {}};
    Bytes payload;
    switch (options.mode) {
    case Mode::plain:
        header.magic = 0x08;
        if (input.size() > budget)
            throw Failure{Status::output_limit, "encoded payload exceeds the output limit"};
        if (!input.empty()) payload.assign(input.begin(), input.end());
        break;
    case Mode::deflate:
        header.magic = 0x09;
        payload = detail::compress(input, budget);
        break;
    case Mode::deflate_tea:
        header.magic = 0x07;
        payload = detail::compress(input, budget);
        header.public_key = detail::encrypt(payload, options.public_key_hex);
        break;
    default:
        throw Failure{Status::unsupported_mode, "unsupported encode mode"};
    }
    header.payload_length = static_cast<std::uint32_t>(payload.size());
    Bytes output(overhead + payload.size(), 0);
    output[0] = header.magic;
    output[1] = static_cast<unsigned char>(header.seq);
    output[2] = static_cast<unsigned char>(header.seq >> 8);
    output[3] = header.begin_hour;
    output[4] = header.end_hour;
    for (unsigned int index = 0; index < 4; ++index)
        output[5 + index] = static_cast<unsigned char>(header.payload_length >> (8 * index));
    std::copy(header.public_key.begin(), header.public_key.end(), output.begin() + 9);
    std::copy(payload.begin(), payload.end(), output.begin() + 73);
    return output;
}
}
