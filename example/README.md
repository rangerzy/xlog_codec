# 编码、解码与原文比较示例

`example/` 与 `xlog_encode/`、`xlog_decode/` 同在仓库根目录。
[`main.cpp`](main.cpp) 是只有 `main()` 的 C++26 示例，使用两个库的公开 C API，展示一次完整的加密往返：

1. 将一个原文 buffer 编码成完整 xlog 块，编码 options 传 `nullptr`，使用编码库编译时从配置头取得的
   默认 `XLOG_ENCODE_DEFLATE_TEA`（`0x07`）模式与接收方公钥。
2. 解码 options 同样传 `nullptr`，使用解码库编译时从配置头取得的默认接收方私钥解码。
3. 依次打印原文、完整编码 buffer（每字节两位十六进制）和解码文本，方便直观对照。
   比较解码结果与原文的长度和每个字节，打印长度与比较结果，并分别调用
   `xlog_encode_free_buffer()` 和 `xlog_decode_free_buffer()` 释放输出。

两个库的配置头内置为本项目新生成的同一对 secp256k1 接收方密钥：编码公钥为不带 `04` 前缀的
128 个十六进制字符 `X||Y`，解码私钥为 64 个十六进制字符。示例使用这组默认值，
不在运行时生成接收方密钥，不直接链接 OpenSSL，也不读写样本文件。
示例不声明选项变量，也不显式设置模式，编码模式与密钥完全取自库编译时的配置头默认值。
任一编码、解码或比较步骤失败时，进程返回非零退出码。

配置宏由 `#ifndef` 保护，可在包含头文件前覆盖，或直接设置 options 中的密钥字段。
`XLOG_ENCODE_OPTIONS_INIT` 和 `XLOG_DECODE_OPTIONS_INIT` 在调用方展开，使用这些宏的调用方可通过重新编译应用宏覆盖。
本示例传 `nullptr`（C 调用中的 NULL）options，使用库编译时默认值；修改配置头后需重新构建对应动态库，
才能更新本示例使用的默认值。

## macOS

在仓库根目录执行，工具链与依赖要求见[根 README](../README.md)：

```sh
cmake --preset macos-arm64-debug
cmake --build --preset macos-arm64-debug --target example
./build/macos-arm64-debug/example/example
ctest --test-dir build/macos-arm64-debug -R '^example_roundtrip$' --output-on-failure --no-tests=error
```

Release 将上述 preset 与构建目录中的 `debug` 换为 `release`。

## Windows

在 MSYS2 UCRT64 shell 中，从仓库根目录执行：

```sh
cmake --preset windows-x64-mingw-release
cmake --build --preset windows-x64-mingw-release --target example
ctest --test-dir build/windows-x64-mingw-release -R '^example_roundtrip$' --output-on-failure --no-tests=error
```

CTest 自动把两个库的 DLL 目录加入该测试的 PATH。直接运行时需手动加入：

```sh
export PATH="$PWD/build/windows-x64-mingw-release/outputs/windows-x64-mingw/xlog_encode/bin:$PWD/build/windows-x64-mingw-release/outputs/windows-x64-mingw/xlog_decode/bin:$PATH"
./build/windows-x64-mingw-release/example/example.exe
```

## 构建选项与输出

`example/CMakeLists.txt` 声明 `project(example LANGUAGES CXX)` 与 `example` 构建目标，
使用 C++26，通过公开 C API 调用库，不导入库内部的 Modules。公开 API 仍支持 C99/C++11 消费者。
两个库均启用时，`XLOG_BUILD_EXAMPLES` 默认 `ON`，IDE 中可直接选择 `example` 构建目标。
只启用一个库或两个库均关闭时，新配置默认关闭示例。显式开启示例时必须同时启用
`XLOG_BUILD_ENCODE` 与 `XLOG_BUILD_DECODE`，否则 CMake 配置会给出错误。
官方 preset 显式启用示例；单库 preset 配置或不需要示例时传入 `-DXLOG_BUILD_EXAMPLES=OFF`。
可执行文件位于 `build/<preset>/example/example`（Windows 为 `example.exe`），
不进入库的 `outputs/` SDK 目录，也不参与安装。
`BUILD_TESTING=ON` 时注册 `example_roundtrip` 测试；关闭测试后仍可构建和直接运行示例。
