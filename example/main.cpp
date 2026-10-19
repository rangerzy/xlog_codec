#include <xlog_encode/xlog_encode.h>
#include <xlog_decode/xlog_decode.h>

#include <cstring>
#include <format>
#include <span>
#include <iostream>
#include <string_view>

int main()
{
    constexpr std::string_view input = "Hello xlog! Hello xlog! Hello xlog!";
    std::cout << "Original (" << input.size() << " bytes): " << input << '\n';
    xlog_encode_buffer_result encoded{};
    xlog_decode_buffer_result decoded{};
    xlog_encode_error encode_error{};
    xlog_decode_error decode_error{};

    // nullptr selects the header defaults compiled into the encode library.
    if (xlog_encode_bytes(input.data(), input.size(), nullptr, &encoded, &encode_error)
        != XLOG_ENCODE_OK) {
        std::cerr << "Encode failed: " << encode_error.message << '\n';
        xlog_encode_free_buffer(&encoded);
        return 1;
    }

    std::cout << "Encoded (" << encoded.size << " bytes, hex):";
    for (const auto byte : std::span(encoded.data, encoded.size)) {
        std::cout << ' ' << std::format("{:02x}", byte);
    }
    std::cout << '\n';

    // nullptr selects the default private key compiled from the decode header.
    if (xlog_decode_bytes(encoded.data, encoded.size, nullptr, &decoded, &decode_error)
        != XLOG_DECODE_OK) {
        std::cerr << "Decode failed: " << decode_error.message << '\n';
        xlog_decode_free_buffer(&decoded);
        xlog_encode_free_buffer(&encoded);
        return 1;
    }

    std::cout << "Decoded (" << decoded.size << " bytes): ";
    if (decoded.size != 0) {
        std::cout.write(reinterpret_cast<const char *>(decoded.data),
            static_cast<std::streamsize>(decoded.size));
    }
    std::cout << '\n';

    const bool matches = decoded.size == input.size() && decoded.data != nullptr
        && std::memcmp(input.data(), decoded.data, input.size()) == 0;
    std::cout << "input=" << input.size() << ", xlog=" << encoded.size
              << ", decoded=" << decoded.size << ", match=" << std::boolalpha << matches << '\n';
    xlog_decode_free_buffer(&decoded);
    xlog_encode_free_buffer(&encoded);
    return matches ? 0 : 1;
}
