#include <xlog_encode/xlog_encode.h>

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

typedef std::vector<unsigned char> Bytes;

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        return false; \
    } \
} while (0)

// Public test receiver: secp256k1 scalar 1, whose public key is generator G.
const char generator_hex[] =
    "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"
    "483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8";

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

uint32_t load_le32(const unsigned char *data)
{
    return static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) |
        (static_cast<uint32_t>(data[3]) << 24);
}

void store_le32(unsigned char *data, uint32_t value)
{
    for (unsigned int i = 0; i != 4; ++i)
        data[i] = static_cast<unsigned char>(value >> (8 * i));
}

// Independent, 16-round little-endian TEA oracle. Receiver scalar 1 means the
// ECDH shared X coordinate is the ephemeral public key's first 32 header bytes.
Bytes decrypt_payload(const Bytes &encoded)
{
    Bytes result(encoded.begin() + 73, encoded.end() - 1);
    uint32_t key[4];
    for (unsigned int i = 0; i != 4; ++i)
        key[i] = load_le32(&encoded[9 + i * 4]);
    const uint32_t delta = UINT32_C(0x9e3779b9);
    for (size_t offset = 0; offset + 8 <= result.size(); offset += 8) {
        uint32_t left = load_le32(&result[offset]);
        uint32_t right = load_le32(&result[offset + 4]);
        uint32_t sum = delta * 16u;
        for (unsigned int round = 0; round != 16; ++round) {
            right -= ((left << 4) + key[2]) ^ (left + sum) ^
                ((left >> 5) + key[3]);
            left -= ((right << 4) + key[0]) ^ (right + sum) ^
                ((right >> 5) + key[1]);
            sum -= delta;
        }
        store_le32(&result[offset], left);
        store_le32(&result[offset + 4], right);
    }
    return result;
}

// A sync-flush segment is not a finished stream. Append an independent empty
// final block so zlib must reach STREAM_END after consuming every payload byte.
bool raw_inflate_equals(const Bytes &payload, const Bytes &expected)
{
    CHECK(payload.size() >= 4);
    CHECK(payload[payload.size() - 4] == 0x00);
    CHECK(payload[payload.size() - 3] == 0x00);
    CHECK(payload[payload.size() - 2] == 0xff);
    CHECK(payload[payload.size() - 1] == 0xff);
    Bytes stream_bytes = payload;
    stream_bytes.push_back(0x03);
    stream_bytes.push_back(0x00);
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    CHECK(inflateInit2(&stream, -MAX_WBITS) == Z_OK);
    stream.next_in = stream_bytes.data();
    stream.avail_in = static_cast<uInt>(stream_bytes.size());
    Bytes actual;
    int status = Z_OK;
    do {
        unsigned char chunk[4096];
        stream.next_out = chunk;
        stream.avail_out = sizeof(chunk);
        const uLong before_in = stream.total_in;
        const uLong before_out = stream.total_out;
        status = inflate(&stream, Z_NO_FLUSH);
        actual.insert(actual.end(), chunk, chunk + sizeof(chunk) - stream.avail_out);
        if ((status != Z_OK && status != Z_STREAM_END) ||
            (status == Z_OK && before_in == stream.total_in &&
             before_out == stream.total_out) || actual.size() > expected.size()) {
            inflateEnd(&stream);
            CHECK(false);
        }
    } while (status != Z_STREAM_END);
    const bool consumed_all = stream.total_in == stream_bytes.size() && stream.avail_in == 0;
    CHECK(inflateEnd(&stream) == Z_OK);
    CHECK(consumed_all);
    CHECK(actual == expected);
    return true;
}

bool success_error_is_reset(const xlog_encode_error &error)
{
    CHECK(error.code == XLOG_ENCODE_OK);
    CHECK(error.message[0] == '\0');
    return true;
}

bool encode(const Bytes &input, const xlog_encode_options *options, Bytes &encoded,
            bool null_empty = false)
{
    xlog_encode_buffer_result output = {NULL, 0};
    xlog_encode_error error;
    std::memset(&error, 0x7f, sizeof(error));
    const void *data = null_empty && input.empty() ? NULL : input.data();
    const xlog_encode_status status =
        xlog_encode_bytes(data, input.size(), options, &output, &error);
    if (status != XLOG_ENCODE_OK)
        std::fprintf(stderr, "encoding failed: %d %s\n", static_cast<int>(status), error.message);
    CHECK(status == XLOG_ENCODE_OK);
    CHECK(success_error_is_reset(error));
    CHECK(output.data != NULL && output.size >= 74);
    encoded.assign(output.data, output.data + output.size);
    xlog_encode_free_buffer(&output);
    CHECK(output.data == NULL && output.size == 0);
    xlog_encode_free_buffer(&output);
    CHECK(output.data == NULL && output.size == 0);
    return true;
}

bool expect_error(const void *input, size_t size, const xlog_encode_options *options,
                  xlog_encode_status expected)
{
    xlog_encode_buffer_result output = {NULL, 123};
    xlog_encode_error error;
    std::memset(&error, 0x7f, sizeof(error));
    const xlog_encode_status status = xlog_encode_bytes(input, size, options, &output, &error);
    const bool cleared = output.data == NULL && output.size == 0;
    // Never free an unexpected partial result before recording the violation.
    xlog_encode_free_buffer(&output);
    CHECK(status == expected);
    CHECK(cleared);
    CHECK(error.code == expected && error.message[0] != '\0');
    CHECK(std::memchr(error.message, '\0', sizeof(error.message)) != NULL);
    return true;
}

bool header_matches(const Bytes &encoded, unsigned char magic,
                    const xlog_encode_options &options)
{
    CHECK(encoded.size() >= 74);
    CHECK(encoded[0] == magic);
    CHECK(encoded[1] == static_cast<unsigned char>(options.seq));
    CHECK(encoded[2] == static_cast<unsigned char>(options.seq >> 8));
    CHECK(encoded[3] == options.begin_hour && encoded[4] == options.end_hour);
    CHECK(load_le32(&encoded[5]) == encoded.size() - 74);
    CHECK(encoded.back() == 0);
    if (magic != 0x07)
        for (size_t i = 9; i != 73; ++i)
            CHECK(encoded[i] == 0);
    return true;
}

bool validates_plain_bytes_and_metadata()
{
    const Bytes input = hex("00ff800041616263");
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    options.mode = XLOG_ENCODE_PLAIN;
    options.seq = 0xabcd;
    options.begin_hour = 23;
    options.end_hour = 1; // Crossing midnight is explicitly valid.
    Bytes encoded;
    CHECK(encode(input, &options, encoded));
    Bytes expected(73, 0);
    expected[0] = 0x08;
    expected[1] = 0xcd;
    expected[2] = 0xab;
    expected[3] = 23;
    expected[4] = 1;
    expected[5] = static_cast<unsigned char>(input.size());
    expected.insert(expected.end(), input.begin(), input.end());
    expected.push_back(0);
    CHECK(encoded == expected);
    CHECK(header_matches(encoded, 0x08, options));
    CHECK(encode(Bytes(), &options, encoded, true));
    CHECK(encoded.size() == 74 && load_le32(&encoded[5]) == 0);
    CHECK(header_matches(encoded, 0x08, options));
    return true;
}

bool verifies_encrypted_defaults()
{
    const Bytes input = hex("616263");
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    CHECK(options.mode == XLOG_ENCODE_DEFLATE_TEA);
    CHECK(options.seq == 0 && options.begin_hour == 0 && options.end_hour == 0);
    CHECK(options.public_key_hex != NULL);
    CHECK(std::strcmp(options.public_key_hex, XLOG_ENCODE_DEFAULT_PUBLIC_KEY_HEX) == 0);
    CHECK(std::strlen(options.public_key_hex) == 128);
    CHECK(options.max_output_size == size_t(256) * 1024 * 1024);
    Bytes explicit_defaults;
    Bytes compiled_defaults;
    CHECK(encode(input, &options, explicit_defaults));
    CHECK(encode(input, NULL, compiled_defaults));
    CHECK(header_matches(explicit_defaults, 0x07, options));
    CHECK(header_matches(compiled_defaults, 0x07, options));
    // Fresh ephemeral keys change each header and ciphertext, but TEA keeps
    // the raw DEFLATE payload length, including its unpadded byte tail.
    CHECK(explicit_defaults.size() == 74 + 9);
    CHECK(compiled_defaults.size() == explicit_defaults.size());
    CHECK(encode(Bytes(), &options, explicit_defaults, true));
    CHECK(encode(Bytes(), NULL, compiled_defaults, true));
    CHECK(header_matches(explicit_defaults, 0x07, options));
    CHECK(header_matches(compiled_defaults, 0x07, options));
    CHECK(compiled_defaults.size() > 74);
    CHECK(compiled_defaults.size() == explicit_defaults.size());
    return true;
}

bool verifies_raw_deflate_known_bytes()
{
    const Bytes input = hex("616263");
    const Bytes literal = hex("4a4c4a06000000ffff");
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    options.mode = XLOG_ENCODE_DEFLATE;
    Bytes encoded;
    CHECK(encode(input, &options, encoded));
    CHECK(header_matches(encoded, 0x09, options));
    CHECK(Bytes(encoded.begin() + 73, encoded.end() - 1) == literal);
    CHECK(raw_inflate_equals(literal, input));
    CHECK(encode(Bytes(), &options, encoded, true));
    CHECK(header_matches(encoded, 0x09, options));
    CHECK(encoded.size() > 74);
    CHECK(raw_inflate_equals(Bytes(encoded.begin() + 73, encoded.end() - 1), Bytes()));
    return true;
}

bool verifies_reference_tea_and_random_ephemeral_keys()
{
    // Fixed vector validates the oracle itself, separately from encoder output.
    Bytes known(73, 0);
    const Bytes generator = hex(generator_hex);
    std::memcpy(&known[9], generator.data(), generator.size());
    const Bytes ciphertext = hex("0710bfb1e3458082");
    known.insert(known.end(), ciphertext.begin(), ciphertext.end());
    known.push_back(0);
    CHECK(decrypt_payload(known) == hex("010300fcff616263"));

    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    options.mode = XLOG_ENCODE_DEFLATE_TEA;
    options.public_key_hex = generator_hex;
    options.seq = 0x1234;
    options.begin_hour = 7;
    options.end_hour = 9;
    const Bytes input = hex("616263");
    const Bytes literal = hex("4a4c4a06000000ffff");
    Bytes first;
    Bytes second;
    CHECK(encode(input, &options, first));
    CHECK(encode(input, &options, second));
    CHECK(header_matches(first, 0x07, options));
    CHECK(header_matches(second, 0x07, options));
    bool first_nonzero = false;
    bool second_nonzero = false;
    for (size_t i = 9; i != 73; ++i) {
        first_nonzero = first_nonzero || first[i] != 0;
        second_nonzero = second_nonzero || second[i] != 0;
    }
    CHECK(first_nonzero && second_nonzero);
    CHECK(std::memcmp(&first[9], &second[9], 64) != 0);
    CHECK(decrypt_payload(first) == literal);
    CHECK(decrypt_payload(second) == literal);
    CHECK(first.size() == 74 + literal.size());
    CHECK(first[73 + 8] == literal[8]); // One byte tail is retained without padding.
    CHECK(raw_inflate_equals(decrypt_payload(first), input));
    return true;
}

bool handles_binary_empty_and_large_inputs_in_all_modes()
{
    Bytes large(131073);
    uint32_t state = UINT32_C(0x31415926);
    for (size_t i = 0; i != large.size(); ++i) {
        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        large[i] = static_cast<unsigned char>(state >> 24);
    }
    std::vector<Bytes> inputs;
    inputs.push_back(Bytes());
    inputs.push_back(hex("00ff80004100fe7f"));
    const size_t boundary_sizes[] = {65535, 65536, 65537, 131071, 131072, 131073};
    for (size_t i = 0; i != sizeof(boundary_sizes) / sizeof(boundary_sizes[0]); ++i)
        inputs.push_back(Bytes(large.begin(), large.begin() + boundary_sizes[i]));
    for (int mode = 0; mode != 3; ++mode) {
        xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
        options.mode = static_cast<xlog_encode_mode>(mode);
        options.public_key_hex = generator_hex;
        options.seq = 0xfedc;
        options.begin_hour = 0;
        options.end_hour = 23;
        for (size_t i = 0; i != inputs.size(); ++i) {
            Bytes encoded;
            CHECK(encode(inputs[i], &options, encoded, true));
            const unsigned char magic = mode == 0 ? 0x08 : mode == 1 ? 0x09 : 0x07;
            CHECK(header_matches(encoded, magic, options));
            const Bytes payload(encoded.begin() + 73, encoded.end() - 1);
            if (mode == 0) {
                CHECK(payload == inputs[i]);
            } else {
                const Bytes compressed = mode == 1 ? payload : decrypt_payload(encoded);
                CHECK(raw_inflate_equals(compressed, inputs[i]));
                const size_t whole_blocks = compressed.size() / 8 * 8;
                if (mode == 2)
                    for (size_t j = whole_blocks; j != compressed.size(); ++j)
                        CHECK(payload[j] == compressed[j]);
            }
        }
    }
    return true;
}

bool validates_arguments_and_key_scope()
{
    const unsigned char input[] = {'a', 'b', 'c'};
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    CHECK(expect_error(NULL, 1, &options, XLOG_ENCODE_INVALID_ARGUMENT));
    xlog_encode_error error;
    std::memset(&error, 0x7f, sizeof(error));
    CHECK(xlog_encode_bytes(input, sizeof(input), &options, NULL, &error)
          == XLOG_ENCODE_INVALID_ARGUMENT);
    CHECK(error.code == XLOG_ENCODE_INVALID_ARGUMENT && error.message[0] != '\0');
    CHECK(std::memchr(error.message, '\0', sizeof(error.message)) != NULL);
    CHECK(xlog_encode_bytes(NULL, 1, &options, NULL, NULL) == XLOG_ENCODE_INVALID_ARGUMENT);
    options.mode = static_cast<xlog_encode_mode>(3);
    CHECK(expect_error(input, sizeof(input), &options, XLOG_ENCODE_UNSUPPORTED_MODE));
    options.mode = XLOG_ENCODE_DEFLATE;
    options.begin_hour = 24;
    CHECK(expect_error(input, sizeof(input), &options, XLOG_ENCODE_INVALID_ARGUMENT));
    options.begin_hour = 0;
    options.end_hour = 24;
    CHECK(expect_error(input, sizeof(input), &options, XLOG_ENCODE_INVALID_ARGUMENT));
    options.end_hour = 0;
    options.mode = XLOG_ENCODE_DEFLATE_TEA;
    options.public_key_hex = NULL;
    CHECK(expect_error(input, sizeof(input), &options, XLOG_ENCODE_KEY_REQUIRED));
    options.public_key_hex = "";
    CHECK(expect_error(input, sizeof(input), &options, XLOG_ENCODE_KEY_REQUIRED));
    std::string invalid_keys[] = {
        std::string(generator_hex, 127), std::string(generator_hex) + "0",
        std::string(128, '0'), std::string(128, 'f'), std::string(generator_hex)
    };
    invalid_keys[4][17] = 'g';
    for (size_t i = 0; i != sizeof(invalid_keys) / sizeof(invalid_keys[0]); ++i) {
        options.public_key_hex = invalid_keys[i].c_str();
        CHECK(expect_error(input, sizeof(input), &options, XLOG_ENCODE_INVALID_KEY));
    }
    // (1, 1) is a finite in-range pair, but is not a point on secp256k1.
    const std::string one = std::string(63, '0') + "1";
    const std::string off_curve = one + one;
    options.public_key_hex = off_curve.c_str();
    CHECK(expect_error(input, sizeof(input), &options, XLOG_ENCODE_INVALID_KEY));
    for (int mode = 0; mode != 2; ++mode) {
        options.mode = static_cast<xlog_encode_mode>(mode);
        options.public_key_hex = "not a public key";
        Bytes encoded;
        CHECK(encode(hex("616263"), &options, encoded));
    }
    // Success with no error argument and release remain valid C ABI operations.
    xlog_encode_buffer_result output = {NULL, 0};
    CHECK(xlog_encode_bytes(input, sizeof(input), &options, &output, NULL) == XLOG_ENCODE_OK);
    xlog_encode_free_buffer(&output);
    xlog_encode_free_buffer(&output);
    xlog_encode_free_buffer(NULL);
    CHECK(output.data == NULL && output.size == 0);
    return true;
}

bool enforces_total_output_and_wire_input_limits()
{
    const Bytes input = hex("616263");
    for (int mode = 0; mode != 3; ++mode) {
        xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
        options.mode = static_cast<xlog_encode_mode>(mode);
        options.public_key_hex = generator_hex;
        options.max_output_size = 0;
        Bytes encoded;
        CHECK(encode(input, &options, encoded));
        const size_t exact_size = encoded.size();
        options.max_output_size = exact_size;
        CHECK(encode(input, &options, encoded));
        CHECK(encoded.size() == exact_size);
        options.max_output_size = exact_size - 1;
        CHECK(expect_error(input.data(), input.size(), &options, XLOG_ENCODE_OUTPUT_LIMIT));
        options.max_output_size = 73;
        CHECK(expect_error(NULL, 0, &options, XLOG_ENCODE_OUTPUT_LIMIT));
        options.max_output_size = 0;
        CHECK(encode(Bytes(), &options, encoded, true));
    }
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    options.mode = XLOG_ENCODE_PLAIN;
    options.max_output_size = 74;
    Bytes encoded;
    CHECK(encode(Bytes(), &options, encoded, true));
    CHECK(encoded.size() == 74);
    options.max_output_size = 76;
    CHECK(expect_error(input.data(), input.size(), &options, XLOG_ENCODE_OUTPUT_LIMIT));
    options.max_output_size = 77;
    CHECK(encode(input, &options, encoded));
    if (sizeof(size_t) > sizeof(uint32_t)) {
        // Only one readable byte is supplied. The wire limit must reject this
        // length before attempting to inspect, copy or compress the input.
        const unsigned char one_byte = 0;
        const size_t too_large = static_cast<size_t>(UINT32_MAX) + size_t(1);
        options.max_output_size = 0;
        options.public_key_hex = generator_hex;
        for (int mode = 0; mode != 3; ++mode) {
            options.mode = static_cast<xlog_encode_mode>(mode);
            CHECK(expect_error(&one_byte, too_large, &options, XLOG_ENCODE_INPUT_LIMIT));
        }
    }
    return true;
}

} // namespace

int main()
{
    struct Test { const char *name; bool (*run)(); };
    const Test tests[] = {
        {"plain bytes and little-endian metadata", validates_plain_bytes_and_metadata},
        {"encrypted defaults and payload lengths", verifies_encrypted_defaults},
        {"independent raw DEFLATE vector", verifies_raw_deflate_known_bytes},
        {"TEA reference and secure ephemeral public keys", verifies_reference_tea_and_random_ephemeral_keys},
        {"empty, binary and large inputs in all modes", handles_binary_empty_and_large_inputs_in_all_modes},
        {"arguments, public keys and idempotent release", validates_arguments_and_key_scope},
        {"complete output and wire input limits", enforces_total_output_and_wire_input_limits}
    };
    for (size_t i = 0; i != sizeof(tests) / sizeof(tests[0]); ++i) {
        if (!tests[i].run()) {
            std::fprintf(stderr, "encode buffer test failed: %s\n", tests[i].name);
            return 1;
        }
    }
    std::puts("encode buffer tests passed");
    return 0;
}
