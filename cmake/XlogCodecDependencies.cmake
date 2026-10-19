include_guard(GLOBAL)

# Each enabled codec links these dependencies privately. Promote imported
# targets so both sibling libraries and the independent reference tests can
# use them, even though discovery runs in the first enabled codec directory.
# The standard FindOpenSSL/FindZLIB cache variables and CMAKE_PREFIX_PATH remain
# available for custom installations.
if(NOT DEFINED OPENSSL_USE_STATIC_LIBS)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif()
if(NOT DEFINED ZLIB_USE_STATIC_LIBS)
    set(ZLIB_USE_STATIC_LIBS TRUE)
    find_package(ZLIB QUIET GLOBAL)
    if(NOT ZLIB_FOUND)
        # Apple supplies zlib as part of the OS, without a static archive.
        set(ZLIB_USE_STATIC_LIBS FALSE)
    endif()
endif()
find_package(ZLIB REQUIRED GLOBAL)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto GLOBAL)
if(MINGW AND OPENSSL_USE_STATIC_LIBS)
    # OpenSSL's mingw-common configuration also lists gdi32; FindOpenSSL
    # supplies ws2_32 and crypt32, but does not add this system library.
    set_property(TARGET OpenSSL::Crypto APPEND PROPERTY INTERFACE_LINK_LIBRARIES gdi32)
endif()

include(CheckCSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES OpenSSL::Crypto ZLIB::ZLIB)
# Recheck when dependency paths or architectures change in an existing build.
unset(XLOG_CODEC_DEPENDENCIES_LINK CACHE)
check_c_source_compiles("#include <openssl/evp.h>
    #include <zlib.h>
    int main(void) {
        EVP_MD_CTX *context = EVP_MD_CTX_new();
        EVP_MD_CTX_free(context);
        return zlibVersion() == 0;
    }" XLOG_CODEC_DEPENDENCIES_LINK)
cmake_pop_check_state()
if(NOT XLOG_CODEC_DEPENDENCIES_LINK)
    message(FATAL_ERROR
        "OpenSSL Crypto and zlib must link with the selected compiler and target architecture. "
        "Set OPENSSL_ROOT_DIR/OPENSSL_CRYPTO_LIBRARY/OPENSSL_INCLUDE_DIR and "
        "ZLIB_ROOT/ZLIB_LIBRARY/ZLIB_INCLUDE_DIR to matching libraries. Both enabled codecs "
        "require these dependencies; disable both with -DXLOG_BUILD_ENCODE=OFF "
        "-DXLOG_BUILD_DECODE=OFF for a dependency-free configuration. "
        "See CMakeConfigureLog.yaml for the linker error.")
endif()
