#include <xlog_decode/xlog_decode.h>
#include "decode_types.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

import xlog_decode.core;

const char *xlog_decode_version(void)
{
    return xlog::decode::version();
}

namespace {
xlog_decode_status to_public_status(xlog::decode::Status status) noexcept
{
    using xlog::decode::Status;
    switch (status) {
    case Status::invalid_argument: return XLOG_DECODE_INVALID_ARGUMENT;
    case Status::invalid_format: return XLOG_DECODE_INVALID_FORMAT;
    case Status::truncated_input: return XLOG_DECODE_TRUNCATED_INPUT;
    case Status::unsupported_format: return XLOG_DECODE_UNSUPPORTED_FORMAT;
    case Status::key_required: return XLOG_DECODE_KEY_REQUIRED;
    case Status::invalid_key: return XLOG_DECODE_INVALID_KEY;
    case Status::decompression_error: return XLOG_DECODE_DECOMPRESSION_ERROR;
    case Status::output_limit: return XLOG_DECODE_OUTPUT_LIMIT;
    case Status::out_of_memory: return XLOG_DECODE_OUT_OF_MEMORY;
    case Status::internal_error: return XLOG_DECODE_INTERNAL_ERROR;
    }
    return XLOG_DECODE_INTERNAL_ERROR;
}

template <class Function>
xlog_decode_status invoke(xlog_decode_error *error, std::size_t offset, Function&& function) noexcept
{
    if (error) *error = {};
    const auto record = [error](xlog_decode_status code, std::size_t position, const char *message) {
        if (error) {
            error->code = code;
            error->offset = position;
            std::snprintf(error->message, sizeof error->message,
                "xlog block at offset %zu: %s", position, message);
        }
        return code;
    };
    try {
        function();
        return XLOG_DECODE_OK;
    } catch (const xlog::decode::Failure& failure) {
        return record(to_public_status(failure.code), failure.offset, failure.message);
    } catch (const std::bad_alloc&) {
        return record(XLOG_DECODE_OUT_OF_MEMORY, offset, "could not allocate output buffer");
    } catch (const std::length_error&) {
        return record(XLOG_DECODE_OUTPUT_LIMIT, offset, "output exceeds the addressable buffer size");
    } catch (...) {
        return record(XLOG_DECODE_INTERNAL_ERROR, offset, "unexpected internal decode error");
    }
}

void require(bool condition, std::size_t offset, const char *message)
{
    if (!condition)
        throw xlog::decode::Failure{xlog::decode::Status::invalid_argument, offset, message};
}

xlog::decode::Options effective_options(const xlog_decode_options *options)
{
    const xlog_decode_options defaults = XLOG_DECODE_OPTIONS_INIT;
    const auto& source = options ? *options : defaults;
    return {source.private_key_hex, source.max_output_size};
}

xlog_decode_block_view to_public_view(const xlog::decode::BlockView& source)
{
    xlog_decode_block_view block{};
    block.header.magic = source.header.magic;
    block.header.seq = source.header.seq;
    block.header.begin_hour = source.header.begin_hour;
    block.header.end_hour = source.header.end_hour;
    block.header.payload_length = source.header.payload_length;
    std::copy(source.header.public_key.begin(), source.header.public_key.end(), block.header.public_key);
    block.payload = source.payload.data();
    block.payload_size = source.payload.size();
    block.offset = source.offset;
    block.encoded_size = source.encoded_size;
    return block;
}

xlog::decode::BlockView to_internal_view(const xlog_decode_block_view& source)
{
    // Validate the C pointer/size pair before constructing a C++ span.
    require((source.payload != nullptr || source.payload_size == 0)
        && source.payload_size == source.header.payload_length,
        source.offset, "invalid xlog block payload view");
    xlog::decode::BlockView block{};
    block.header.magic = source.header.magic;
    block.header.seq = source.header.seq;
    block.header.begin_hour = source.header.begin_hour;
    block.header.end_hour = source.header.end_hour;
    block.header.payload_length = source.header.payload_length;
    std::copy_n(source.header.public_key, block.header.public_key.size(), block.header.public_key.begin());
    block.payload = {source.payload, source.payload_size};
    block.offset = source.offset;
    block.encoded_size = source.encoded_size;
    return block;
}

void deliver(const std::vector<unsigned char>& bytes, xlog_decode_buffer_result& output)
{
    if (bytes.empty()) return;
    auto *data = static_cast<unsigned char *>(std::malloc(bytes.size()));
    if (!data) throw std::bad_alloc{};
    std::memcpy(data, bytes.data(), bytes.size());
    output = {data, bytes.size()};
}
}

int xlog_decode_is_xlog_buffer(const void *data, size_t size)
{
    if (!data || size == 0) return 0;
    try {
        return xlog::decode::is_xlog_buffer({static_cast<const unsigned char *>(data), size}) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

xlog_decode_status xlog_decode_parse_block(const void *data, size_t size, size_t offset,
    xlog_decode_block_view *block, xlog_decode_error *error)
{
    if (block) *block = {};
    return invoke(error, offset, [&] {
        require(block != nullptr, offset, "block output pointer is NULL");
        require(data != nullptr || size == 0, offset, "input pointer is NULL for a nonempty buffer");
        *block = to_public_view(xlog::decode::parse_block({static_cast<const unsigned char *>(data), size}, offset));
    });
}

xlog_decode_status xlog_decode_block(const xlog_decode_block_view *block,
    const xlog_decode_options *options, xlog_decode_buffer_result *output, xlog_decode_error *error)
{
    if (output) *output = {};
    const size_t offset = block ? block->offset : 0;
    return invoke(error, offset, [&] {
        require(output != nullptr, offset, "buffer output pointer is NULL");
        require(block != nullptr, offset, "block pointer is NULL");
        const auto effective = effective_options(options);
        const size_t budget = effective.max_output_size != 0
            ? effective.max_output_size : std::numeric_limits<size_t>::max();
        deliver(xlog::decode::decode_block(to_internal_view(*block), effective, budget), *output);
    });
}

xlog_decode_status xlog_decode_bytes(const void *data, size_t size,
    const xlog_decode_options *options, xlog_decode_buffer_result *output, xlog_decode_error *error)
{
    if (output) *output = {};
    return invoke(error, 0, [&] {
        require(output != nullptr, 0, "buffer output pointer is NULL");
        require(data != nullptr || size == 0, 0, "input pointer is NULL for a nonempty buffer");
        deliver(xlog::decode::decode_bytes({static_cast<const unsigned char *>(data), size},
            effective_options(options)), *output);
    });
}

void xlog_decode_free_buffer(xlog_decode_buffer_result *output)
{
    if (output) {
        std::free(output->data);
        *output = {};
    }
}
