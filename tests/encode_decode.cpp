#include <xlog_encode/xlog_encode.h>
#include <xlog_decode/xlog_decode.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

typedef std::vector<unsigned char> Bytes;

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return false; \
    } \
} while (0)

const char receiver_private_key[] =
    "0000000000000000000000000000000000000000000000000000000000000001";
const char receiver_public_key[] =
    "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
    "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8";
const char receiver_two_private_key[] =
    "0000000000000000000000000000000000000000000000000000000000000002";
// Public scalar 2 and point 2G, independently derived with OpenSSL's pkey CLI.
const char receiver_two_public_key[] =
    "c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5"
    "1ae168fea63dc339a3c58419466ceaeef7f632653266d0e1236431a950cfe52a";

bool encode(const Bytes &input, const xlog_encode_options *options, Bytes &encoded)
{
    xlog_encode_buffer_result result = {NULL, 0};
    xlog_encode_error error;
    std::memset(&error, 0x7f, sizeof(error));
    CHECK(xlog_encode_bytes(input.empty() ? NULL : input.data(), input.size(),
                           options, &result, &error) == XLOG_ENCODE_OK);
    CHECK(error.code == XLOG_ENCODE_OK && error.message[0] == '\0');
    CHECK(result.data != NULL && result.size >= 74);
    encoded.assign(result.data, result.data + result.size);
    xlog_encode_free_buffer(&result);
    CHECK(result.data == NULL && result.size == 0);
    return true;
}

bool decode_equals(const Bytes &encoded, const Bytes &expected,
                   const char *private_key = receiver_private_key)
{
    xlog_decode_options options = XLOG_DECODE_OPTIONS_INIT;
    options.private_key_hex = private_key;
    xlog_decode_buffer_result result = {NULL, 0};
    xlog_decode_error error;
    std::memset(&error, 0x7f, sizeof(error));
    CHECK(xlog_decode_bytes(encoded.data(), encoded.size(), &options, &result, &error)
          == XLOG_DECODE_OK);
    CHECK(error.code == XLOG_DECODE_OK && error.offset == 0 && error.message[0] == '\0');
    const bool matches = result.size == expected.size() &&
        (expected.empty() || (result.data != NULL &&
         std::memcmp(result.data, expected.data(), expected.size()) == 0));
    xlog_decode_free_buffer(&result);
    CHECK(matches);
    CHECK(result.data == NULL && result.size == 0);
    return true;
}

bool parse_and_check(const Bytes &encoded, size_t offset,
                     const xlog_encode_options &options, const Bytes &expected,
                     const char *private_key = receiver_private_key)
{
    xlog_decode_block_view block = {};
    xlog_decode_error error;
    std::memset(&error, 0x7f, sizeof(error));
    CHECK(xlog_decode_parse_block(encoded.data(), encoded.size(), offset, &block, &error)
          == XLOG_DECODE_OK);
    CHECK(error.code == XLOG_DECODE_OK && error.offset == 0 && error.message[0] == '\0');
    CHECK(block.header.magic == (options.mode == XLOG_ENCODE_PLAIN ? 0x08 :
                                options.mode == XLOG_ENCODE_DEFLATE ? 0x09 : 0x07));
    CHECK(block.header.seq == options.seq);
    CHECK(block.header.begin_hour == options.begin_hour && block.header.end_hour == options.end_hour);
    CHECK(block.header.payload_length == block.payload_size);
    CHECK(block.offset == offset && block.encoded_size == block.payload_size + 74);
    CHECK(block.payload == encoded.data() + offset + 73);
    CHECK(std::memcmp(block.header.public_key, encoded.data() + offset + 9, 64) == 0);
    if (options.mode != XLOG_ENCODE_DEFLATE_TEA)
        for (size_t i = 0; i != sizeof(block.header.public_key); ++i)
            CHECK(block.header.public_key[i] == 0);
    xlog_decode_options decode_options = XLOG_DECODE_OPTIONS_INIT;
    decode_options.private_key_hex = private_key;
    xlog_decode_buffer_result result = {NULL, 0};
    CHECK(xlog_decode_block(&block, &decode_options, &result, &error) == XLOG_DECODE_OK);
    const bool matches = result.size == expected.size() &&
        (expected.empty() || (result.data != NULL &&
         std::memcmp(result.data, expected.data(), expected.size()) == 0));
    xlog_decode_free_buffer(&result);
    CHECK(matches);
    return true;
}

bool roundtrips_repeated_independent_blocks()
{
    Bytes binary;
    for (unsigned int i = 0; i != 256; ++i)
        binary.push_back(static_cast<unsigned char>(i));
    binary.push_back(0);
    Bytes large(3 * 65536 + 127);
    uint32_t state = UINT32_C(0xc001d00d);
    for (size_t i = 0; i != large.size(); ++i) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        large[i] = static_cast<unsigned char>(state >> 24);
    }
    const unsigned char abc[] = {'a', 'b', 'c'};
    const Bytes inputs[] = {Bytes(), Bytes(abc, abc + 3), binary, large};
    for (unsigned int repetition = 0; repetition != 3; ++repetition) {
        for (int mode = 0; mode != 3; ++mode) {
            xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
            options.mode = static_cast<xlog_encode_mode>(mode);
            options.public_key_hex = receiver_public_key;
            options.seq = static_cast<uint16_t>(0xa100 + repetition * 16 + mode);
            options.begin_hour = 23;
            options.end_hour = static_cast<uint8_t>(repetition);
            for (size_t i = 0; i != sizeof(inputs) / sizeof(inputs[0]); ++i) {
                Bytes encoded;
                CHECK(encode(inputs[i], &options, encoded));
                CHECK(xlog_decode_is_xlog_buffer(encoded.data(), encoded.size()) == 1);
                CHECK(parse_and_check(encoded, 0, options, inputs[i]));
                CHECK(decode_equals(encoded, inputs[i]));
            }
        }
    }
    return true;
}

bool roundtrips_configured_default_receiver()
{
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    CHECK(options.mode == XLOG_ENCODE_DEFLATE_TEA);
    CHECK(options.public_key_hex != NULL);
    CHECK(std::strcmp(options.public_key_hex, XLOG_ENCODE_DEFAULT_PUBLIC_KEY_HEX) == 0);
    const xlog_encode_options *encode_options[] = {NULL, &options};
    const xlog_decode_options decode_defaults = XLOG_DECODE_OPTIONS_INIT;
    CHECK(decode_defaults.private_key_hex != NULL);
    CHECK(std::strcmp(decode_defaults.private_key_hex, XLOG_DECODE_DEFAULT_PRIVATE_KEY_HEX) == 0);
    const xlog_decode_options *decode_options[] = {NULL, &decode_defaults};
    const unsigned char binary[] = {
        0, 0xff, 0x80, 'A', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xfe, 0x7f, 0
    };
    const Bytes inputs[] = {Bytes(), Bytes(binary, binary + sizeof(binary))};
    for (size_t i = 0; i != sizeof(inputs) / sizeof(inputs[0]); ++i) {
        for (size_t j = 0; j != sizeof(encode_options) / sizeof(encode_options[0]); ++j) {
            Bytes encoded;
            CHECK(encode(inputs[i], encode_options[j], encoded));
            CHECK(encoded[0] == 0x07);
            CHECK(parse_and_check(encoded, 0, options, inputs[i],
                                  XLOG_DECODE_DEFAULT_PRIVATE_KEY_HEX));
            if (!inputs[i].empty())
                CHECK(encoded.size() >= 74 + 8); // Exercise at least one encrypted TEA block.
            for (size_t k = 0; k != sizeof(decode_options) / sizeof(decode_options[0]); ++k) {
                xlog_decode_buffer_result result = {NULL, 0};
                xlog_decode_error error;
                std::memset(&error, 0x7f, sizeof(error));
                CHECK(xlog_decode_bytes(encoded.data(), encoded.size(), decode_options[k],
                                        &result, &error) == XLOG_DECODE_OK);
                CHECK(error.code == XLOG_DECODE_OK && error.offset == 0 && error.message[0] == '\0');
                const bool matches = result.size == inputs[i].size() &&
                    (inputs[i].empty() || (result.data != NULL &&
                     std::memcmp(result.data, inputs[i].data(), inputs[i].size()) == 0));
                xlog_decode_free_buffer(&result);
                CHECK(matches);
                CHECK(result.data == NULL && result.size == 0);
            }
        }
    }
    return true;
}

bool concatenates_blocks_and_preserves_each_header()
{
    const unsigned char binary[] = {0, 0xff, 0x80, 'A', 0};
    const unsigned char text[] = {'a', 'b', 'c'};
    const Bytes inputs[] = {Bytes(binary, binary + sizeof(binary)), Bytes(),
                           Bytes(text, text + sizeof(text)), Bytes(binary, binary + sizeof(binary))};
    const xlog_encode_mode modes[] = {XLOG_ENCODE_PLAIN, XLOG_ENCODE_DEFLATE_TEA,
                                    XLOG_ENCODE_DEFLATE, XLOG_ENCODE_DEFLATE_TEA};
    xlog_encode_options options[4];
    size_t offsets[4];
    Bytes joined;
    Bytes expected;
    for (size_t i = 0; i != 4; ++i) {
        const xlog_encode_options defaults = XLOG_ENCODE_OPTIONS_INIT;
        options[i] = defaults;
        options[i].mode = modes[i];
        options[i].public_key_hex = receiver_public_key;
        options[i].seq = static_cast<uint16_t>(0xfffc + i);
        options[i].begin_hour = static_cast<uint8_t>(22 + i % 2);
        options[i].end_hour = static_cast<uint8_t>(i);
        Bytes encoded;
        CHECK(encode(inputs[i], &options[i], encoded));
        offsets[i] = joined.size();
        joined.insert(joined.end(), encoded.begin(), encoded.end());
        expected.insert(expected.end(), inputs[i].begin(), inputs[i].end());
    }
    CHECK(xlog_decode_is_xlog_buffer(joined.data(), joined.size()) == 1);
    for (size_t i = 0; i != 4; ++i)
        CHECK(parse_and_check(joined, offsets[i], options[i], inputs[i]));
    CHECK(decode_equals(joined, expected));
    return true;
}

bool uses_receiver_public_key_for_ecdh()
{
    // Receiver scalar 1 alone cannot distinguish real ECDH from incorrectly
    // using the client's public X directly. Scalar 2 requires the actual 2P X.
    Bytes input(4097);
    uint32_t state = UINT32_C(0x27182818);
    for (size_t i = 0; i != input.size(); ++i) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        input[i] = static_cast<unsigned char>(state >> 24);
    }
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    options.mode = XLOG_ENCODE_DEFLATE_TEA;
    options.public_key_hex = receiver_two_public_key;
    options.seq = 0x0202;
    options.begin_hour = 21;
    options.end_hour = 3;
    for (unsigned int repetition = 0; repetition != 3; ++repetition) {
        Bytes encoded;
        CHECK(encode(input, &options, encoded));
        CHECK(parse_and_check(encoded, 0, options, input, receiver_two_private_key));
        CHECK(decode_equals(encoded, input, receiver_two_private_key));
        xlog_decode_options wrong_receiver = XLOG_DECODE_OPTIONS_INIT;
        wrong_receiver.private_key_hex = receiver_private_key;
        xlog_decode_buffer_result output = {NULL, 0};
        xlog_decode_error error;
        std::memset(&error, 0x7f, sizeof(error));
        const xlog_decode_status status = xlog_decode_bytes(
            encoded.data(), encoded.size(), &wrong_receiver, &output, &error);
        const bool matches = output.size == input.size() && output.data != NULL &&
            std::memcmp(output.data, input.data(), input.size()) == 0;
        // TEA is unauthenticated: a wrong key may fail inflation or produce
        // different bytes. It must never recover this receiver's input.
        if (status == XLOG_DECODE_OK) {
            CHECK(error.code == XLOG_DECODE_OK && error.message[0] == '\0');
            CHECK(!matches);
        } else {
            CHECK(status == XLOG_DECODE_DECOMPRESSION_ERROR);
            CHECK(error.code == status && error.offset == 0 && error.message[0] != '\0');
            CHECK(output.data == NULL && output.size == 0);
        }
        xlog_decode_free_buffer(&output);
    }
    return true;
}

} // namespace

int main()
{
    if (!roundtrips_repeated_independent_blocks() ||
        !roundtrips_configured_default_receiver() ||
        !concatenates_blocks_and_preserves_each_header() || !uses_receiver_public_key_for_ecdh())
        return 1;
    std::puts("encode/decode interoperability tests passed");
    return 0;
}
