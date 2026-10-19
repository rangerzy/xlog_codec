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

const char key_one[] =
    "0000000000000000000000000000000000000000000000000000000000000001";
const char key_two[] =
    "0000000000000000000000000000000000000000000000000000000000000002";

Bytes hex(const char *text)
{
    Bytes result;
    while (*text) {
        unsigned int value = 0;
        std::sscanf(text, "%2x", &value);
        result.push_back(static_cast<unsigned char>(value));
        text += 2;
    }
    return result;
}

// Hand-built modern frames; no production encoder or compression helper is used.
Bytes frame(unsigned char magic, const Bytes &payload)
{
    Bytes result(73, 0);
    result[0] = magic;
    result[1] = 0x34;
    result[2] = 0x12;
    result[3] = 7;
    result[4] = 9;
    for (unsigned int i = 0; i != 4; ++i)
        result[5 + i] = static_cast<unsigned char>(payload.size() >> (8 * i));
    result.insert(result.end(), payload.begin(), payload.end());
    result.push_back(0);
    return result;
}

Bytes encrypted_frame()
{
    // Public scalar 1, generator G (X || Y), and an independent TEA known vector.
    // Decryption gives raw DEFLATE stored block 010300fcff616263, or "abc".
    Bytes result = frame(0x07, hex("0710bfb1e3458082"));
    const Bytes generator = hex(
        "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
        "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8");
    std::memcpy(&result[9], generator.data(), generator.size());
    return result;
}

void append(Bytes &destination, const Bytes &source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

bool cleared(const xlog_decode_block_view &block)
{
    if (block.header.magic || block.header.seq || block.header.begin_hour ||
        block.header.end_hour || block.header.payload_length || block.payload ||
        block.payload_size || block.offset || block.encoded_size)
        return false;
    for (size_t i = 0; i < sizeof(block.header.public_key); ++i)
        if (block.header.public_key[i])
            return false;
    return true;
}

bool error_is(const xlog_decode_error &error, xlog_decode_status status, size_t offset)
{
    CHECK(error.code == status);
    CHECK(error.offset == offset);
    CHECK(error.message[0] != '\0');
    CHECK(std::memchr(error.message, '\0', sizeof(error.message)) != NULL);
    return true;
}

bool success_error_is_reset(const xlog_decode_error &error)
{
    CHECK(error.code == XLOG_DECODE_OK);
    CHECK(error.offset == 0);
    CHECK(error.message[0] == '\0');
    return true;
}

bool expect_decode(const Bytes &input, const Bytes &expected,
                   const xlog_decode_options *options = NULL)
{
    xlog_decode_buffer_result output = {NULL, 0};
    xlog_decode_error error;
    std::memset(&error, 0x7f, sizeof(error));
    const xlog_decode_status status = xlog_decode_bytes(
        input.data(), input.size(), options, &output, &error);
    CHECK(status == XLOG_DECODE_OK);
    CHECK(success_error_is_reset(error));
    const bool matches = output.size == expected.size() &&
        (expected.empty() || (output.data &&
         std::memcmp(output.data, expected.data(), expected.size()) == 0));
    xlog_decode_free_buffer(&output);
    CHECK(matches);
    CHECK(output.data == NULL && output.size == 0);
    xlog_decode_free_buffer(&output);
    CHECK(output.data == NULL && output.size == 0);
    return true;
}

bool expect_decode_error(const Bytes &input, xlog_decode_status expected,
                         size_t offset = 0,
                         const xlog_decode_options *options = NULL)
{
    xlog_decode_buffer_result output = {NULL, 0};
    xlog_decode_error error;
    std::memset(&error, 0x7f, sizeof(error));
    const xlog_decode_status status = xlog_decode_bytes(
        input.data(), input.size(), options, &output, &error);
    const bool no_partial_output = output.data == NULL && output.size == 0;
    xlog_decode_free_buffer(&output);
    CHECK(status == expected);
    CHECK(no_partial_output);
    CHECK(error_is(error, expected, offset));
    return true;
}

bool recognizes_complete_structure_without_decoding()
{
    Bytes all;
    for (unsigned char magic = 0x06; magic <= 0x0d; ++magic) {
        const Bytes input = frame(magic, hex("ff0080"));
        CHECK(xlog_decode_is_xlog_buffer(input.data(), input.size()) == 1);
        append(all, input);
    }
    CHECK(xlog_decode_is_xlog_buffer(all.data(), all.size()) == 1);
    all.back() = 1;
    CHECK(xlog_decode_is_xlog_buffer(all.data(), all.size()) == 0);
    all.back() = 0;
    all.push_back(0x06);
    CHECK(xlog_decode_is_xlog_buffer(all.data(), all.size()) == 0);
    CHECK(xlog_decode_is_xlog_buffer(NULL, 0) == 0);
    CHECK(xlog_decode_is_xlog_buffer(NULL, 1) == 0);
    const Bytes input = frame(0x06, Bytes());
    CHECK(xlog_decode_is_xlog_buffer(input.data(), 0) == 0);
    for (unsigned char magic = 0x0a; magic <= 0x0d; ++magic)
        CHECK(expect_decode_error(frame(magic, Bytes()), XLOG_DECODE_UNSUPPORTED_FORMAT));
    CHECK(expect_decode_error(frame(0x05, Bytes()), XLOG_DECODE_INVALID_FORMAT));
    CHECK(expect_decode_error(frame(0x0e, Bytes()), XLOG_DECODE_INVALID_FORMAT));
    return true;
}

bool parses_header_offset_and_borrowed_payload()
{
    const Bytes first = frame(0x08, hex("00ff"));
    Bytes second = frame(0x06, hex("800041"));
    for (size_t i = 0; i != 64; ++i)
        second[9 + i] = static_cast<unsigned char>(i + 1);
    Bytes input = first;
    append(input, second);
    xlog_decode_block_view block = {};
    xlog_decode_error error;
    std::memset(&error, 0x7f, sizeof(error));
    CHECK(xlog_decode_parse_block(input.data(), input.size(), first.size(),
                                  &block, &error) == XLOG_DECODE_OK);
    CHECK(success_error_is_reset(error));
    CHECK(block.header.magic == 0x06);
    CHECK(block.header.seq == 0x1234);
    CHECK(block.header.begin_hour == 7 && block.header.end_hour == 9);
    CHECK(block.header.payload_length == 3 && block.payload_size == 3);
    CHECK(block.offset == first.size() && block.encoded_size == 77);
    CHECK(block.payload == input.data() + first.size() + 73);
    CHECK(std::memcmp(block.header.public_key, &second[9], 64) == 0);
    CHECK(std::memcmp(block.payload, "\x80\0A", 3) == 0);
    CHECK(xlog_decode_parse_block(input.data(), input.size(), 0,
                                  &block, NULL) == XLOG_DECODE_OK);
    CHECK(block.encoded_size == first.size());
    CHECK(block.payload == input.data() + 73);
    return true;
}

bool rejects_every_truncation_and_clears_block()
{
    const Bytes input = frame(0x06, hex("616263"));
    for (size_t length = 0; length < input.size(); ++length) {
        xlog_decode_block_view block;
        xlog_decode_error error;
        std::memset(&block, 0x7f, sizeof(block));
        CHECK(xlog_decode_parse_block(input.data(), length, 0, &block, &error)
              == XLOG_DECODE_TRUNCATED_INPUT);
        CHECK(cleared(block));
        CHECK(error_is(error, XLOG_DECODE_TRUNCATED_INPUT, 0));
        CHECK(xlog_decode_is_xlog_buffer(input.data(), length) == 0);
    }
    Bytes bad = input;
    bad.back() = 0x01;
    CHECK(expect_decode_error(bad, XLOG_DECODE_INVALID_FORMAT));
    bad = input;
    bad[5] = 4; // The terminator cannot satisfy the declared payload length.
    CHECK(expect_decode_error(bad, XLOG_DECODE_TRUNCATED_INPUT));
    bad = input;
    bad[5] = 2; // The third payload byte is not a terminator.
    CHECK(expect_decode_error(bad, XLOG_DECODE_INVALID_FORMAT));
    bad = input;
    bad[5] = bad[6] = bad[7] = bad[8] = 0xff;
    CHECK(expect_decode_error(bad, XLOG_DECODE_TRUNCATED_INPUT));
    const Bytes empty = frame(0x08, Bytes());
    CHECK(xlog_decode_is_xlog_buffer(empty.data(), empty.size()) == 1);
    CHECK(expect_decode(empty, Bytes()));
    CHECK(xlog_decode_is_xlog_buffer(empty.data(), empty.size() - 1) == 0);
    return true;
}

bool validates_arguments_and_resets_errors()
{
    const Bytes input = frame(0x06, hex("41"));
    xlog_decode_block_view block;
    xlog_decode_error error;
    std::memset(&block, 0x7f, sizeof(block));
    CHECK(xlog_decode_parse_block(NULL, 1, 0, &block, &error)
          == XLOG_DECODE_INVALID_ARGUMENT);
    CHECK(cleared(block));
    CHECK(error_is(error, XLOG_DECODE_INVALID_ARGUMENT, 0));
    CHECK(xlog_decode_parse_block(NULL, 0, 0, &block, &error)
          == XLOG_DECODE_TRUNCATED_INPUT);
    CHECK(cleared(block));
    CHECK(xlog_decode_parse_block(input.data(), input.size(), input.size(), &block, &error)
          == XLOG_DECODE_TRUNCATED_INPUT);
    CHECK(cleared(block));
    CHECK(error_is(error, XLOG_DECODE_TRUNCATED_INPUT, input.size()));
    CHECK(xlog_decode_parse_block(input.data(), input.size(), input.size() + 1, &block, &error)
          == XLOG_DECODE_INVALID_ARGUMENT);
    CHECK(cleared(block));
    CHECK(xlog_decode_parse_block(input.data(), input.size(), 0, NULL, &error)
          == XLOG_DECODE_INVALID_ARGUMENT);
    xlog_decode_buffer_result output = {NULL, 0};
    CHECK(xlog_decode_bytes(NULL, 0, NULL, &output, &error) == XLOG_DECODE_INVALID_FORMAT);
    CHECK(output.data == NULL && output.size == 0);
    CHECK(error_is(error, XLOG_DECODE_INVALID_FORMAT, 0));
    CHECK(xlog_decode_bytes(input.data(), 0, NULL, &output, NULL) == XLOG_DECODE_INVALID_FORMAT);
    CHECK(xlog_decode_bytes(NULL, 1, NULL, &output, &error) == XLOG_DECODE_INVALID_ARGUMENT);
    CHECK(output.data == NULL && output.size == 0);
    CHECK(xlog_decode_bytes(input.data(), input.size(), NULL, NULL, &error)
          == XLOG_DECODE_INVALID_ARGUMENT);
    CHECK(xlog_decode_block(NULL, NULL, &output, &error) == XLOG_DECODE_INVALID_ARGUMENT);
    CHECK(output.data == NULL && output.size == 0);
    CHECK(xlog_decode_parse_block(input.data(), input.size(), 0, &block, &error)
          == XLOG_DECODE_OK);
    CHECK(xlog_decode_block(&block, NULL, NULL, &error) == XLOG_DECODE_INVALID_ARGUMENT);
    // A C caller can provide an invalid view. Reject it before making a span,
    // preserving the failure offset and never exposing a partial result.
    block.offset = 29;
    block.payload = NULL;
    CHECK(xlog_decode_block(&block, NULL, &output, &error) == XLOG_DECODE_INVALID_ARGUMENT);
    CHECK(output.data == NULL && output.size == 0);
    CHECK(error_is(error, XLOG_DECODE_INVALID_ARGUMENT, 29));
    block.payload = input.data() + 73;
    block.payload_size = 0;
    CHECK(xlog_decode_block(&block, NULL, &output, &error) == XLOG_DECODE_INVALID_ARGUMENT);
    CHECK(output.data == NULL && output.size == 0);
    CHECK(error_is(error, XLOG_DECODE_INVALID_ARGUMENT, 29));
    block.payload_size = static_cast<size_t>(-1);
    CHECK(xlog_decode_block(&block, NULL, &output, &error) == XLOG_DECODE_INVALID_ARGUMENT);
    CHECK(output.data == NULL && output.size == 0);
    CHECK(error_is(error, XLOG_DECODE_INVALID_ARGUMENT, 29));
    xlog_decode_free_buffer(NULL);
    xlog_decode_free_buffer(&output);
    xlog_decode_free_buffer(&output);
    return true;
}

bool decodes_four_kinds_in_order_as_binary()
{
    Bytes input = frame(0x06, hex("00ff"));
    append(input, frame(0x08, hex("800041")));
    append(input, frame(0x09, hex("4b4c4a0600")));
    append(input, encrypted_frame());
    xlog_decode_options options = XLOG_DECODE_OPTIONS_INIT;
    options.private_key_hex = key_one;
    CHECK(expect_decode(input, hex("00ff800041616263616263"), &options));
    const Bytes kinds[] = {frame(0x06, hex("00ff80")), frame(0x08, hex("00ff80")),
                           frame(0x09, hex("4b4c4a0600")), encrypted_frame()};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        const Bytes &encoded = kinds[i];
        const Bytes expected = i < 2 ? hex("00ff80") : hex("616263");
        xlog_decode_block_view block = {};
        CHECK(xlog_decode_parse_block(encoded.data(), encoded.size(), 0, &block, NULL)
              == XLOG_DECODE_OK);
        xlog_decode_buffer_result output = {NULL, 0};
        xlog_decode_error error;
        std::memset(&error, 0x7f, sizeof(error));
        CHECK(xlog_decode_block(&block, &options, &output, &error) == XLOG_DECODE_OK);
        CHECK(success_error_is_reset(error));
        const bool matches = output.size == expected.size() && output.data &&
            std::memcmp(output.data, expected.data(), expected.size()) == 0;
        xlog_decode_free_buffer(&output);
        CHECK(matches);
    }
    return true;
}

bool handles_raw_deflate_completion_precisely()
{
    const Bytes complete = hex("4b4c4a0600");
    const Bytes flushed = hex("4a4c4a06000000ffff");
    CHECK(expect_decode(frame(0x09, complete), hex("616263")));
    CHECK(expect_decode(frame(0x09, flushed), hex("616263")));
    CHECK(expect_decode(frame(0x09, hex("63f8dfc0e00800")), hex("00ff800041")));
    CHECK(expect_decode(frame(0x09, hex("0300")), Bytes()));
    CHECK(expect_decode(frame(0x09, hex("000000ffff")), Bytes()));
    for (size_t i = 0; i < complete.size(); ++i)
        CHECK(expect_decode_error(frame(0x09, Bytes(complete.begin(), complete.begin() + i)),
                                  XLOG_DECODE_DECOMPRESSION_ERROR));
    for (size_t i = 0; i < flushed.size(); ++i)
        CHECK(expect_decode_error(frame(0x09, Bytes(flushed.begin(), flushed.begin() + i)),
                                  XLOG_DECODE_DECOMPRESSION_ERROR));
    const char *invalid[] = {
        "4f4c4a0600",                 // Reserved BTYPE.
        "000300fcff616263",           // Non-final stored block, no flush.
        "000400fbff0000ffff",         // Marker bytes are stored data, not a flush.
        "4b4c4a06000000ffff",         // Fake flush suffix after Z_STREAM_END.
        "4b4c4a060000",               // Trailing byte after stream end.
        "4b4c4a0600ff",
        "4b4c4a06004b4c4a0600",       // Concatenated finished streams.
        "4a4c4a06000000ffff00",       // Partial block after a real flush.
        "4a4c4a06000000ffffff",
        "789c4b4c4a0600024d0127"      // zlib wrapper is not raw DEFLATE.
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        CHECK(expect_decode_error(frame(0x09, hex(invalid[i])),
                                  XLOG_DECODE_DECOMPRESSION_ERROR));
    return true;
}

bool validates_encryption_keys_and_public_points()
{
    const Bytes input = encrypted_frame();
    xlog_decode_options options = XLOG_DECODE_OPTIONS_INIT;
    CHECK(options.private_key_hex != NULL);
    CHECK(std::strcmp(options.private_key_hex, XLOG_DECODE_DEFAULT_PRIVATE_KEY_HEX) == 0);
    CHECK(std::strlen(options.private_key_hex) == 64);
    options.private_key_hex = key_one;
    CHECK(expect_decode(input, hex("616263"), &options));
    // An encrypted frame shorter than one TEA block keeps its tail unchanged.
    Bytes short_tail = frame(0x07, hex("4b4c4a0600"));
    std::memcpy(&short_tail[9], &input[9], 64);
    CHECK(expect_decode(short_tail, hex("616263"), &options));
    options.private_key_hex = NULL;
    CHECK(expect_decode_error(input, XLOG_DECODE_KEY_REQUIRED, 0, &options));
    options.private_key_hex = "";
    CHECK(expect_decode_error(input, XLOG_DECODE_KEY_REQUIRED, 0, &options));
    const char *invalid_keys[] = {
        "1",
        "000000000000000000000000000000000000000000000000000000000000000g",
        "0000000000000000000000000000000000000000000000000000000000000000",
        "fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141",
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
        "00000000000000000000000000000000000000000000000000000000000000001"
    };
    for (size_t i = 0; i < sizeof(invalid_keys) / sizeof(invalid_keys[0]); ++i) {
        options.private_key_hex = invalid_keys[i];
        CHECK(expect_decode_error(input, XLOG_DECODE_INVALID_KEY, 0, &options));
    }
    options.private_key_hex = key_one;
    Bytes invalid_point = input;
    std::memset(&invalid_point[9], 0, 64);
    CHECK(expect_decode_error(invalid_point, XLOG_DECODE_INVALID_KEY, 0, &options));
    std::memset(&invalid_point[9], 0xff, 64);
    CHECK(expect_decode_error(invalid_point, XLOG_DECODE_INVALID_KEY, 0, &options));
    options.private_key_hex = key_two;
    CHECK(expect_decode_error(input, XLOG_DECODE_DECOMPRESSION_ERROR, 0, &options));
    return true;
}

bool enforces_single_and_aggregate_limits_without_partial_bytes()
{
    xlog_decode_options options = XLOG_DECODE_OPTIONS_INIT;
    options.private_key_hex = key_one;
    options.max_output_size = 2;
    const Bytes kinds[] = {frame(0x06, hex("616263")), frame(0x08, hex("616263")),
                           frame(0x09, hex("4b4c4a0600")), encrypted_frame()};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        CHECK(expect_decode_error(kinds[i], XLOG_DECODE_OUTPUT_LIMIT, 0, &options));
        xlog_decode_block_view block = {};
        CHECK(xlog_decode_parse_block(kinds[i].data(), kinds[i].size(), 0, &block, NULL)
              == XLOG_DECODE_OK);
        xlog_decode_buffer_result output = {NULL, 0};
        xlog_decode_error error;
        CHECK(xlog_decode_block(&block, &options, &output, &error) == XLOG_DECODE_OUTPUT_LIMIT);
        CHECK(output.data == NULL && output.size == 0);
        CHECK(error_is(error, XLOG_DECODE_OUTPUT_LIMIT, 0));
    }
    Bytes combined = frame(0x06, hex("6162"));
    const size_t second_offset = combined.size();
    append(combined, frame(0x09, hex("4b4c4a0600")));
    options.max_output_size = 4;
    CHECK(expect_decode_error(combined, XLOG_DECODE_OUTPUT_LIMIT, second_offset, &options));
    xlog_decode_block_view second = {};
    CHECK(xlog_decode_parse_block(combined.data(), combined.size(), second_offset, &second, NULL)
          == XLOG_DECODE_OK);
    xlog_decode_buffer_result block_output = {NULL, 0};
    xlog_decode_error block_error;
    options.max_output_size = 2;
    CHECK(xlog_decode_block(&second, &options, &block_output, &block_error)
          == XLOG_DECODE_OUTPUT_LIMIT);
    CHECK(block_output.data == NULL && block_output.size == 0);
    CHECK(error_is(block_error, XLOG_DECODE_OUTPUT_LIMIT, second_offset));
    options.max_output_size = 5;
    CHECK(expect_decode(combined, hex("6162616263"), &options));
    options.max_output_size = 0;
    CHECK(expect_decode(combined, hex("6162616263"), &options));
    return true;
}

bool reports_failing_block_offset_and_discards_earlier_output()
{
    const Bytes first = frame(0x06, hex("00ff"));
    const Bytes failures[] = {frame(0x0a, Bytes()), frame(0x09, hex("4f4c4a0600")),
                              encrypted_frame()};
    const xlog_decode_status statuses[] = {XLOG_DECODE_UNSUPPORTED_FORMAT,
        XLOG_DECODE_DECOMPRESSION_ERROR, XLOG_DECODE_KEY_REQUIRED};
    xlog_decode_options options = XLOG_DECODE_OPTIONS_INIT;
    options.private_key_hex = "";
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        Bytes input = first;
        append(input, failures[i]);
        CHECK(expect_decode_error(input, statuses[i], first.size(), &options));
    }
    Bytes truncated = first;
    append(truncated, frame(0x08, hex("41")));
    truncated.pop_back();
    CHECK(expect_decode_error(truncated, XLOG_DECODE_TRUNCATED_INPUT, first.size()));
    Bytes garbage = first;
    append(garbage, frame(0xff, Bytes()));
    CHECK(expect_decode_error(garbage, XLOG_DECODE_INVALID_FORMAT, first.size()));
    return true;
}

} // namespace

int main()
{
    struct Test { const char *name; bool (*run)(); };
    const Test tests[] = {
        {"recognition checks complete modern structure", recognizes_complete_structure_without_decoding},
        {"parser copies header and borrows payload", parses_header_offset_and_borrowed_payload},
        {"all truncation and length boundaries", rejects_every_truncation_and_clears_block},
        {"null/empty arguments and release", validates_arguments_and_resets_errors},
        {"four supported codecs and binary ordering", decodes_four_kinds_in_order_as_binary},
        {"raw deflate completion and flush boundaries", handles_raw_deflate_completion_precisely},
        {"known encryption vector and key validation", validates_encryption_keys_and_public_points},
        {"single and aggregate output limits", enforces_single_and_aggregate_limits_without_partial_bytes},
        {"atomic errors and block offsets", reports_failing_block_offset_and_discards_earlier_output}
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].run()) {
            std::fprintf(stderr, "decode buffer test failed: %s\n", tests[i].name);
            return 1;
        }
    }
    std::puts("decode buffer tests passed");
    return 0;
}
