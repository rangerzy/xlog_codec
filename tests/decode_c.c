#define XLOG_DECODE_DEFAULT_PRIVATE_KEY_HEX \
    "0000000000000000000000000000000000000000000000000000000000000001"
#include <xlog_decode/xlog_decode_types.h>
#include <xlog_decode/xlog_decode.h>

#include <stdio.h>
#include <string.h>

/* Also compiled as C++11: keep this consumer within the common C99 subset. */
int main(void)
{
    unsigned char frame[77] = {0};
    const unsigned char expected[3] = {0x00, 0xff, 0x41};
    unsigned char encrypted[82] = {0};
    const unsigned char generator[64] = {
        0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac,
        0x55, 0xa0, 0x62, 0x95, 0xce, 0x87, 0x0b, 0x07,
        0x02, 0x9b, 0xfc, 0xdb, 0x2d, 0xce, 0x28, 0xd9,
        0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8, 0x17, 0x98,
        0x48, 0x3a, 0xda, 0x77, 0x26, 0xa3, 0xc4, 0x65,
        0x5d, 0xa4, 0xfb, 0xfc, 0x0e, 0x11, 0x08, 0xa8,
        0xfd, 0x17, 0xb4, 0x48, 0xa6, 0x85, 0x54, 0x19,
        0x9c, 0x47, 0xd0, 0x8f, 0xfb, 0x10, 0xd4, 0xb8
    };
    const unsigned char ciphertext[8] = {0x07, 0x10, 0xbf, 0xb1, 0xe3, 0x45, 0x80, 0x82};
    xlog_decode_options options = XLOG_DECODE_OPTIONS_INIT;
    xlog_decode_block_view block = {{0, 0, 0, 0, 0, {0}}, NULL, 0, 0, 0};
    xlog_decode_buffer_result output = {NULL, 0};
    xlog_decode_error error = {XLOG_DECODE_OK, 0, {0}};
    frame[0] = 0x06;
    frame[5] = 3;
    memcpy(frame + 73, expected, sizeof(expected));
    if (!xlog_decode_is_xlog_buffer(frame, sizeof(frame))) {
        fputs("C consumer: plain frame was not recognized\n", stderr);
        return 1;
    }
    if (xlog_decode_parse_block(frame, sizeof(frame), 0, &block, &error) != XLOG_DECODE_OK ||
        block.payload != frame + 73 || block.payload_size != 3 || block.encoded_size != 77) {
        fputs("C consumer: frame parsing failed\n", stderr);
        return 1;
    }
    if (xlog_decode_bytes(frame, sizeof(frame), &options, &output, &error) != XLOG_DECODE_OK ||
        output.size != sizeof(expected) || output.data == NULL ||
        memcmp(output.data, expected, sizeof(expected)) != 0) {
        fputs("C consumer: macro-initialized options did not decode binary bytes\n", stderr);
        xlog_decode_free_buffer(&output);
        return 1;
    }
    xlog_decode_free_buffer(&output);
    xlog_decode_free_buffer(&output);
    xlog_decode_free_buffer(NULL);
    if (output.data != NULL || output.size != 0) {
        fputs("C consumer: releasing output did not reset it\n", stderr);
        return 1;
    }
    /* The caller's macro overrides the library's compiled receiver key. */
    encrypted[0] = 0x07;
    encrypted[5] = 8;
    memcpy(encrypted + 9, generator, sizeof(generator));
    memcpy(encrypted + 73, ciphertext, sizeof(ciphertext));
    if (xlog_decode_bytes(encrypted, sizeof(encrypted), &options, &output, &error)
        != XLOG_DECODE_OK || output.size != 3 || output.data == NULL ||
        memcmp(output.data, "abc", 3) != 0) {
        fputs("C consumer: caller header key override did not decode encrypted fixture\n", stderr);
        xlog_decode_free_buffer(&output);
        return 1;
    }
    xlog_decode_free_buffer(&output);
    options.max_output_size = 2;
    if (xlog_decode_block(&block, &options, &output, &error) != XLOG_DECODE_OUTPUT_LIMIT ||
        error.code != XLOG_DECODE_OUTPUT_LIMIT || output.data != NULL || output.size != 0) {
        fputs("C consumer: a block limit exposed partial output\n", stderr);
        xlog_decode_free_buffer(&output);
        return 1;
    }
    puts("C decode API consumer passed");
    return 0;
}
