# xlog_codec

xlog 编解码工程。根 CMake 项目管理两个互不依赖的动态库：

| 子目录 / 项目 / 库 | 私有模块 | C ABI | CMake target |
| --- | --- | --- | --- |
| `xlog_encode` | `xlog_encode.core` | `xlog_encode_*()` | `xlog::encode` |
| `xlog_decode` | `xlog_decode.core` | `xlog_decode_*()` | `xlog::decode` |

当前版本 `0.1.0`。`xlog_encode` 将一次输入 buffer 编为一个完整 xlog 块，支持 `0x08` 原文、
`0x09` raw DEFLATE 和 `0x07` secp256k1 ECDH/16 轮 TEA 加密压缩；默认使用 `0x07`。
输入可包含任意二进制字节，空 buffer 也会生成完整块。`xlog_decode` 提供结构识别、逐块解析、
单块与整段解码。两库的输入和输出都是 buffer，不读写文件，不提供 CLI 或 MCP。
完整接口、密钥配置和内存所有权见 [编码 API](xlog_encode/doc/README.md) 与
[解码 API](xlog_decode/doc/README.md)。

公开配置、数据类型和方法分别位于各库的 `*_config.h`、`*_types.h` 和主 API 头文件；类型头可独立包含。
私有类型与公开 C 类型独立，由 C ABI 适配层转换，内部类型和方法同样分文件。

## 工具链

- CMake **4.2.2+**，Ninja **1.11+**（单配置）。
- GNU GCC **16+**，C++26、`<meta>` 静态反射、`-freflection`。
- macOS ARM64、Windows x64 MinGW/GCC **MSYS2 UCRT64** 或 Linux x64。

构建使用 CMake `FILE_SET CXX_MODULES` 跟踪私有模块，不要求 `import std`。
默认 AppleClang、MSVC、其他架构、Ninja Multi-Config 不在支持范围。
公开头文件和安装包的消费者不需要 C++26 或 Modules。

## macOS 构建

先将 `gcc-16`、`g++-16` 和新版 CMake、Ninja 加入 PATH：

```sh
cmake --version
g++-16 --version
ninja --version
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug
ctest --test-dir build/macos-arm64-debug --output-on-failure --no-tests=error
```

Release 使用 `macos-arm64-release`，构建目录为 `build/macos-arm64-release`。
本机已有 CLion 时，其新版 CMake 可位于
`/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/`；应使用同目录的 `ctest`。
个人环境覆盖放在未提交的 `CMakeUserPresets.json` 中。

任一编码或解码库启用时，都需匹配 ARM64 的 OpenSSL Crypto 3.0+ 和 zlib。可安装 ARM64 开发包，或自行编译依赖，
再配置 `-DOPENSSL_ROOT_DIR=/absolute/path/to/openssl`；也支持标准
`OPENSSL_INCLUDE_DIR`、`OPENSSL_CRYPTO_LIBRARY`、`ZLIB_ROOT` 和 `CMAKE_PREFIX_PATH`。
默认静态链接 Crypto，优先静态 zlib，macOS 可使用系统 zlib，不复制参考项目的预编译依赖。
个人依赖位置放入 `CMakeUserPresets.json`，不写入公共 preset。
OpenSSL/zlib 为各库的 PRIVATE 依赖，编码库与解码库互不链接；静态链接时，安装消费者无需其开发包。

## Windows 构建

在 MSYS2 **UCRT64** shell 安装工具链，然后进入本仓库：

```sh
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-zlib
cmake --preset windows-x64-mingw-release
cmake --build --preset windows-x64-mingw-release
ctest --test-dir build/windows-x64-mingw-release --output-on-failure --no-tests=error
```

若 MSYS2 更新要求关闭终端，先完成更新再继续安装。GCC 必须达到 16；项目会在配置时检查反射能力。
Debug 使用 `windows-x64-mingw-debug`。Windows CI 将运行同一 Release 构建与 CTest。

## Linux 构建

Linux x64 使用 GCC 16+、CMake 4.2.2+、Ninja 和匹配的 Crypto/zlib 开发包。
默认 `linux-x64-release` preset 使用 PATH 中的 `gcc`、`g++`；也可覆盖编译器路径。
CI 在 Ubuntu 24.04 上使用 Homebrew 提供的 GCC 16 和静态依赖：

```sh
eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"
brew install gcc cmake ninja openssl@3 zlib
cmake --preset linux-x64-release \
  -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16 \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
  -DOPENSSL_INCLUDE_DIR="$(brew --prefix openssl@3)/include" \
  -DOPENSSL_CRYPTO_LIBRARY="$(brew --prefix openssl@3)/lib/libcrypto.a" \
  -DZLIB_INCLUDE_DIR="$(brew --prefix zlib)/include" \
  -DZLIB_LIBRARY="$(brew --prefix zlib)/lib/libz.a"
cmake --build --preset linux-x64-release
ctest --test-dir build/linux-x64-release --output-on-failure --no-tests=error
```

Debug 使用 `linux-x64-debug`。普通发行版也可使用自己的 GCC 16 和依赖，无需安装 Homebrew。

## 输出与独立安装

普通构建直接生成动态库及完整交付目录：

```text
<CMAKE_BINARY_DIR>/outputs/<平台>/<子项目名称>/
├── include/<子项目名称>/   # 配置、类型、API 与导出宏 .h
├── lib/                  # dylib / so / Windows 导入库
├── bin/                  # Windows DLL
├── cmake/                # Config.cmake / ConfigVersion.cmake
└── doc/                  # 本库 API 说明与 OpenSSL/zlib 许可证
```

平台标签为 `macos-arm64`、`windows-x64-mingw`、`linux-x64`。Debug/Release 使用不同构建目录，输出中不再增加配置层。
单独构建任意一个库，同样得到该库的完整交付目录：

```sh
cmake --build build/macos-arm64-debug --target xlog_encode
cmake --build build/macos-arm64-debug --target xlog_decode
```

`XLOG_BUILD_ENCODE` 与 `XLOG_BUILD_DECODE` 独立控制两个库，默认都为 ON。
只构建编码库使用 `-DXLOG_BUILD_DECODE=OFF`；只构建解码库使用 `-DXLOG_BUILD_ENCODE=OFF`。
任一库启用都需要 Crypto/zlib；两个开关都为 OFF 时跳过这两项依赖的发现与链接检查。

构建自动同步公开头文件、导出宏头、文档和 CMake 引用配置；仅修改文档不会重新链接库，删除输出副本后再次构建会补回文件。
安装使用相同文件清单。先构建所需库，再安装到自选目录：

```sh
cmake --install build/macos-arm64-debug --component xlog_encode --prefix /absolute/path/encode-sdk
cmake --install build/macos-arm64-debug --component xlog_decode --prefix /absolute/path/decode-sdk
```

每个交付目录可单独移动。模块源、BMI、对象文件留在构建树，不作为分发内容。

子项目的 `CMakeLists.txt` 直接声明动态库与私有模块；`cmake/XlogLibrary.cmake` 集中库属性、文件同步及安装规则。
产品库目标为 `xlog_encode`、`xlog_decode`；可选调用示例位于同级的 `example/`。

## 加密往返示例

仓库根目录下的 [`example/main.cpp`](example/main.cpp) 是只有 `main()` 的 C++26 示例，使用两个库的公开 C API，
编码与解码均传入 `nullptr` options，使用各库编译时从配置头取得的默认值：
以 `0x07` 模式和默认接收方公钥编码一个 buffer，再使用默认接收方私钥解码，
通过长度与 `memcmp` 比较原文，最后分别调用两个库的释放函数。示例不声明选项变量，也不显式设置模式。
示例在 `main()` 中生成 128 KiB（131072 字节）的日志原文；这一规模参考
[zlib 官方示例](https://www.zlib.net/zlib_how.html) 对高效 I/O 缓冲区采用 128 KiB 或 256 KiB 的建议。
该建议针对 I/O 处理效率，并非最小输入长度；完整编码结果的压缩效果仍取决于原文内容和块开销。
运行时依次打印原文、完整编码 buffer（每字节两位十六进制）和解码文本，方便直观对照，并输出长度与字节比较结果。
配置头中的默认公钥和私钥属于为本项目新生成的同一对 secp256k1 密钥；示例不在运行时生成接收方密钥，
也不直接链接 OpenSSL。

```sh
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug --target example
./build/macos-arm64-debug/example/example
ctest --test-dir build/macos-arm64-debug -R '^example_roundtrip$' --output-on-failure --no-tests=error
```

两个库均启用时，`XLOG_BUILD_EXAMPLES` 默认 `ON`，IDE 中可直接选择 `example` 构建目标。
只启用一个库或两个库均关闭时，新配置默认关闭示例；显式设为 `ON` 时要求同时启用两个库。
官方 preset 显式启用示例；通过 preset 配置单库时需增加 `-DXLOG_BUILD_EXAMPLES=OFF`。
示例可用 `-DXLOG_BUILD_EXAMPLES=OFF` 关闭，输出到 `build/<preset>/example/example`（Windows 为 `example.exe`），
不进入 SDK 交付目录，也不参与安装。
Windows 命令与 DLL 运行路径见[示例说明](example/README.md)。

## 外部调用

```cmake
cmake_minimum_required(VERSION 4.2.2)
project(example LANGUAGES C)
find_package(xlog_encode CONFIG REQUIRED)
add_executable(example main.c)
target_link_libraries(example PRIVATE xlog::encode)
```

```c
#include <xlog_encode/xlog_encode.h>

/* 此例兼容 C99 与 C++11。 */
int main(void) {
    const unsigned char input[] = {0x41, 0x00, 0xff};
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    xlog_encode_buffer_result output = {NULL, 0};
    xlog_encode_error error;
    xlog_encode_status status = xlog_encode_bytes(
        input, sizeof(input), &options, &output, &error);
    if (status != XLOG_ENCODE_OK)
        return (int)status;
    /* 使用 output.data[0..output.size) 中的完整 xlog 块。 */
    xlog_encode_free_buffer(&output);
    return 0;
}
```

配置消费者时传入 `-DCMAKE_PREFIX_PATH=/absolute/path/encode-sdk`。
解码库使用 `find_package(xlog_decode CONFIG REQUIRED)`、`xlog::decode` 和
`#include <xlog_decode/xlog_decode.h>`，调用 `xlog_decode_version()`。
返回字符串由库持有，库加载期间有效，不得修改或释放。

两个配置头内置为本项目新生成的一对 secp256k1 接收方密钥：编码默认公钥为 128 个十六进制字符的
`X||Y`，不带 `04` 前缀；解码默认私钥为 64 个十六进制字符。配置宏均由 `#ifndef` 保护，
可在包含头文件前覆盖，也可通过 options 字段覆盖。仅 `XLOG_ENCODE_DEFLATE_TEA` 模式需要公钥；
缺失或无效时失败，不降级为明文。
编码结果必须用 `xlog_encode_free_buffer()` 释放，复用输出变量前先释放旧结果。
默认最大编码输出为 256 MiB，包含 73 字节头和 1 字节结束标记；0 表示关闭此配置上限，
输入和 payload 仍各受 `UINT32_MAX` 限制。序号和起止小时默认 0，小时仅接受 `0..23`。
`XLOG_ENCODE_OPTIONS_INIT` 与 `XLOG_DECODE_OPTIONS_INIT` 在调用方展开，重新编译调用方即可应用配置头中的宏覆盖。
传 NULL options 使用库编译时默认值；修改配置头后，要使 NULL options 应用新默认值，需重新构建对应动态库。

macOS 库使用 `@rpath` install name，外部 CMake target 提供对应链接位置。
Linux 库使用版本化 SONAME 和 `$ORIGIN` 安装 RPATH；CMake target 使调用程序定位搬迁后的包。
Windows 运行时将本包 `bin` 加入 PATH；同时需要匹配的 GCC 运行库。
若显式选择动态 Crypto/zlib，宿主环境还须提供对应动态库。工程不复制 Windows 依赖运行库，
不静态捆绑 GCC 运行库。Unix 宿主需提供匹配的 GCC 运行库；Linux 还需兼容构建环境的 glibc，
不承诺未经验证的旧版 macOS 或 Linux 发行版兼容性。

CTest 验证 C/C++11 调用、交付目录移动后的消费、单库构建、头文件和文档增量同步，以及 build/install 文件清单和内容一致性。
macOS 还检查实际加载的 dylib 路径。
解码测试通过公开 API 验证完整结构、四种块类型、独立加密样本、DEFLATE 边界、错误偏移、
密钥覆盖、输出限制及内存释放；搬迁包的 C/C++11 消费者实际执行 buffer 解码。

## 持续集成

日常开发在 `dev` 分支进行，本地 `dev` 跟踪 `origin/dev`，完成修改后推送 `dev`。
需要发布分发包时，将 `dev` 合并到 `main` 并推送，触发三平台 Release 构建；完成后切回 `dev` 继续开发：

```sh
git switch dev
# 开发、验证并提交修改后
git push origin dev
git switch main
git pull --ff-only origin main
git merge --ff-only dev
git push origin main
git switch dev
```

直接推送 `dev` 不触发 Release 工作流；向 `main` 提交 PR 时会构建和验证，合并并推送 `main` 后上传分发包。

推送 `main`、向 `main` 提交 PR 或手动触发时，[Release SDKs 工作流](.github/workflows/build.yml)
分别在 `macos-15` ARM64、`windows-2025` UCRT64 x64、`ubuntu-24.04` x64 上构建 Release。
两个库、默认密钥加密往返示例和完整 CTest 均参与构建验证；CI 检查静态 Crypto/zlib 和两个库互不依赖。
工作流记录实际 GCC/CMake/Ninja 版本，工具链与依赖来源见[调研记录](docs/research/2026-10-20-ci-toolchains.md)。

每次向 `main` 推送，或在 `main` 手动运行工作流，成功后会上传三个 Actions artifact：

| Artifact | 包内 ZIP（当前版本） |
| --- | --- |
| `xlog-codec-macos-arm64` | `xlog-codec-0.1.0-macos-arm64.zip` |
| `xlog-codec-windows-x64-mingw` | `xlog-codec-0.1.0-windows-x64-mingw.zip` |
| `xlog-codec-linux-x64` | `xlog-codec-0.1.0-linux-x64.zip` |

ZIP 根目录包含 `xlog_encode/`、`xlog_decode/`，各有公开头、动态库、CMake 配置和 API 文档及许可证。
打包后会重新解压，核对文件清单和库别名，并分别编译、运行两个独立 SDK 的 C/C++11 调用程序；验证通过才上传。
PR 同样构建、测试和验证 ZIP，上传仅对 main 的推送或手动运行启用。可从仓库 Actions 运行页下载产物。

本地可在完成两个库的 Release 构建后执行相同打包脚本，版本号自动取自构建 cache：

```sh
cmake -DXLOG_BUILD_DIR=build/macos-arm64-release \
  -DXLOG_PLATFORM=macos-arm64 -P cmake/PackageRelease.cmake
```

ZIP 输出到对应构建目录的 `dist/`，不包含 example、测试或私有 Modules。
