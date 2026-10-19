#ifndef XLOG_DECODE_BUFFER_CODEC_H
#define XLOG_DECODE_BUFFER_CODEC_H

#include "decode_types.h"

namespace xlog::decode::detail {
// The internal budget is an exact byte count; zero permits only empty output.
Bytes decompress(std::span<const unsigned char> payload, std::size_t budget);
TeaKey make_tea_key(std::span<const unsigned char, 64> public_key, const char *private_key_hex);
Bytes tea_decrypt(std::span<const unsigned char> payload, const TeaKey& key);
void wipe(void *data, std::size_t size) noexcept;
}

#endif
