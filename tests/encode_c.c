#define XLOG_ENCODE_DEFAULT_PUBLIC_KEY_HEX \
    "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798" \
    "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"
#define XLOG_ENCODE_DEFAULT_MODE XLOG_ENCODE_DEFLATE_TEA
#include <xlog_encode/xlog_encode_types.h>
#include <xlog_encode/xlog_encode.h>

#include <stdio.h>
#include <string.h>

/* Deliberately needs only installed encode headers and the encode library.
 * It is compiled as both C99 and C++11 after relocating the package. */
static uint32_t load_le32(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void store_le32(unsigned char *data, uint32_t value)
{
    unsigned int i;
    for (i = 0; i != 4; ++i)
        data[i] = (unsigned char)(value >> (8 * i));
}

static void decrypt_one_block(unsigned char *data, const unsigned char *key_bytes)
{
    uint32_t key[4];
    uint32_t left = load_le32(data);
    uint32_t right = load_le32(data + 4);
    const uint32_t delta = UINT32_C(0x9e3779b9);
    uint32_t sum = delta * 16u;
    unsigned int i;
    for (i = 0; i != 4; ++i)
        key[i] = load_le32(key_bytes + i * 4);
    for (i = 0; i != 16; ++i) {
        right -= ((left << 4) + key[2]) ^ (left + sum) ^ ((left >> 5) + key[3]);
        left -= ((right << 4) + key[0]) ^ (right + sum) ^ ((right >> 5) + key[1]);
        sum -= delta;
    }
    store_le32(data, left);
    store_le32(data + 4, right);
}

int main(void)
{
    const unsigned char input[3] = {'a', 'b', 'c'};
    const unsigned char expected[9] = {0x4a, 0x4c, 0x4a, 0x06, 0, 0, 0, 0xff, 0xff};
    unsigned char decrypted[9];
    unsigned int i;
    int public_key_nonzero = 0;
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    xlog_encode_buffer_result output = {NULL, 0};
    xlog_encode_error error = {XLOG_ENCODE_OK, {0}};
    if (options.mode != XLOG_ENCODE_DEFLATE_TEA ||
        strcmp(options.public_key_hex, XLOG_ENCODE_DEFAULT_PUBLIC_KEY_HEX) != 0) {
        fputs("C consumer: configuration macros did not initialize caller options\n", stderr);
        return 1;
    }
    options.seq = 0x1234;
    options.begin_hour = 23;
    options.end_hour = 1;
    memset(&error, 0x7f, sizeof(error));
    if (xlog_encode_bytes(input, sizeof(input), &options, &output, &error) != XLOG_ENCODE_OK ||
        error.code != XLOG_ENCODE_OK || error.message[0] != '\0' ||
        output.data == NULL || output.size != 83) {
        fputs("C consumer: caller macro defaults did not encode encrypted bytes\n", stderr);
        xlog_encode_free_buffer(&output);
        return 1;
    }
    if (output.data[0] != 0x07 || output.data[1] != 0x34 || output.data[2] != 0x12 ||
        output.data[3] != 23 || output.data[4] != 1 ||
        load_le32(output.data + 5) != 9 || output.data[82] != 0) {
        fputs("C consumer: complete block header or terminator is incorrect\n", stderr);
        xlog_encode_free_buffer(&output);
        return 1;
    }
    for (i = 9; i != 73; ++i)
        public_key_nonzero = public_key_nonzero || output.data[i] != 0;
    /* Receiver scalar 1 makes ECDH shared X equal the header's public X.
     * This checks real encrypted output without requiring OpenSSL or zlib. */
    memcpy(decrypted, output.data + 73, sizeof(decrypted));
    decrypt_one_block(decrypted, output.data + 9);
    if (!public_key_nonzero || memcmp(decrypted, expected, sizeof(expected)) != 0) {
        fputs("C consumer: key override or TEA payload is incorrect\n", stderr);
        xlog_encode_free_buffer(&output);
        return 1;
    }
    xlog_encode_free_buffer(&output);
    xlog_encode_free_buffer(&output);
    xlog_encode_free_buffer(NULL);
    if (output.data != NULL || output.size != 0) {
        fputs("C consumer: releasing output did not reset it\n", stderr);
        return 1;
    }
    puts("C encode API consumer passed");
    return 0;
}
