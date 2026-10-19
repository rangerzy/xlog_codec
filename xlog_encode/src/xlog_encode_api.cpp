#include <xlog_encode/xlog_encode.h>
#include "encode_types.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

import xlog_encode.core;

const char *xlog_encode_version(void)
{
    return xlog::encode::version();
}

namespace {
xlog_encode_status to_public_status(xlog::encode::Status status) noexcept
{
    using xlog::encode::Status;
    switch (status) {
    case Status::invalid_argument: return XLOG_ENCODE_INVALID_ARGUMENT;
    case Status::unsupported_mode: return XLOG_ENCODE_UNSUPPORTED_MODE;
    case Status::key_required: return XLOG_ENCODE_KEY_REQUIRED;
    case Status::invalid_key: return XLOG_ENCODE_INVALID_KEY;
    case Status::input_limit: return XLOG_ENCODE_INPUT_LIMIT;
    case Status::output_limit: return XLOG_ENCODE_OUTPUT_LIMIT;
    case Status::compression_error: return XLOG_ENCODE_COMPRESSION_ERROR;
    case Status::crypto_error: return XLOG_ENCODE_CRYPTO_ERROR;
    case Status::out_of_memory: return XLOG_ENCODE_OUT_OF_MEMORY;
    case Status::internal_error: return XLOG_ENCODE_INTERNAL_ERROR;
    }
    return XLOG_ENCODE_INTERNAL_ERROR;
}

xlog::encode::Options effective_options(const xlog_encode_options *options)
{
    const xlog_encode_options defaults = XLOG_ENCODE_OPTIONS_INIT;
    const auto& source = options ? *options : defaults;
    using xlog::encode::Mode;
    Mode mode;
    switch (source.mode) {
    case XLOG_ENCODE_PLAIN: mode = Mode::plain; break;
    case XLOG_ENCODE_DEFLATE: mode = Mode::deflate; break;
    case XLOG_ENCODE_DEFLATE_TEA: mode = Mode::deflate_tea; break;
    default: throw xlog::encode::Failure{xlog::encode::Status::unsupported_mode, "unsupported encode mode"};
    }
    return {mode, source.public_key_hex, source.seq, source.begin_hour, source.end_hour, source.max_output_size};
}

template <class Function>
xlog_encode_status invoke(xlog_encode_error *error, Function&& function) noexcept
{
    if (error) *error = {};
    const auto record = [error](xlog_encode_status code, const char *message) {
        if (error) {
            error->code = code;
            std::snprintf(error->message, sizeof error->message, "xlog encode: %s", message);
        }
        return code;
    };
    try {
        function();
        return XLOG_ENCODE_OK;
    } catch (const xlog::encode::Failure& failure) {
        return record(to_public_status(failure.code), failure.message);
    } catch (const std::bad_alloc&) {
        return record(XLOG_ENCODE_OUT_OF_MEMORY, "could not allocate encoded buffer");
    } catch (const std::length_error&) {
        return record(XLOG_ENCODE_OUTPUT_LIMIT, "output exceeds the addressable buffer size");
    } catch (...) {
        return record(XLOG_ENCODE_INTERNAL_ERROR, "unexpected internal encode error");
    }
}
}

xlog_encode_status xlog_encode_bytes(const void *data, size_t size,
    const xlog_encode_options *options, xlog_encode_buffer_result *output, xlog_encode_error *error)
{
    if (output) *output = {};
    return invoke(error, [&] {
        using xlog::encode::Failure;
        using xlog::encode::Status;
        if (!output) throw Failure{Status::invalid_argument, "buffer output pointer is NULL"};
        if (!data && size != 0)
            throw Failure{Status::invalid_argument, "input pointer is NULL for a nonempty buffer"};
        // Reject impossible single-block lengths before constructing or reading a span.
        if (size > std::numeric_limits<std::uint32_t>::max())
            throw Failure{Status::input_limit, "input exceeds the single-block limit"};
        const auto bytes = xlog::encode::encode_bytes({static_cast<const unsigned char *>(data), size},
            effective_options(options));
        auto *buffer = static_cast<unsigned char *>(std::malloc(bytes.size()));
        if (!buffer) throw std::bad_alloc{};
        std::memcpy(buffer, bytes.data(), bytes.size());
        *output = {buffer, bytes.size()};
    });
}

void xlog_encode_free_buffer(xlog_encode_buffer_result *output)
{
    if (output) {
        std::free(output->data);
        *output = {};
    }
}
