#ifndef XLOG_ENCODE_BUFFER_CODEC_H
#define XLOG_ENCODE_BUFFER_CODEC_H

#include "encode_types.h"

namespace xlog::encode::detail {
// The private budget is an exact payload byte count, including when it is zero.
Bytes compress(std::span<const unsigned char> input, std::size_t budget);
PublicKey encrypt(Bytes& payload, const char *receiver_public_key_hex);
void wipe(void *data, std::size_t size) noexcept;
}

#endif
