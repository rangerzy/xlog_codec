# xlog_codec

xlog 编解码工程骨架。根 CMake 项目管理两个互不依赖的动态库：

| 子目录 / 项目 / 库 | 私有模块 | C ABI | CMake target |
| --- | --- | --- | --- |
| `xlog_encode` | `xlog_encode.core` | `xlog_encode_version()` | `xlog::encode` |
| `xlog_decode` | `xlog_decode.core` | `xlog_decode_version()` | `xlog::decode` |

当前版本 `0.1.0`，仅有版本查询、静态反射示例和头文件调用测试，尚未实现真实 xlog 编解码。

## 工具链

- CMake **4.2.2+**，Ninja **1.11+**（单配置）。
- GNU GCC **16+**，C++26、`<meta>` 静态反射、`-freflection`。
- macOS ARM64 或 Windows x64 MinGW/GCC **MSYS2 UCRT64**。

构建使用 CMake `FILE_SET CXX_MODULES` 跟踪私有模块，不要求 `import std`。
默认 AppleClang、MSVC、Linux、Ninja Multi-Config 不在本阶段支持范围。
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

## Windows 构建

在 MSYS2 **UCRT64** shell 安装工具链，然后进入本仓库：

```sh
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
cmake --preset windows-x64-mingw-release
cmake --build --preset windows-x64-mingw-release
ctest --test-dir build/windows-x64-mingw-release --output-on-failure --no-tests=error
```

若 MSYS2 更新要求关闭终端，先完成更新再继续安装。GCC 必须达到 16；项目会在配置时检查反射能力。
Debug 使用 `windows-x64-mingw-debug`。Windows CI 将运行同一 Release 构建与 CTest。

## 输出与独立安装

普通构建直接生成动态库及完整交付目录：

```text
<CMAKE_BINARY_DIR>/outputs/<平台>/<子项目名称>/
├── include/<子项目名称>/   # API 与导出宏 .h
├── lib/                  # dylib / Windows 导入库
│   └── cmake/<子项目名称>/
├── bin/                  # Windows DLL
└── doc/                  # 本库 API 说明
```

平台标签为 `macos-arm64`、`windows-x64-mingw`。Debug/Release 使用不同构建目录，输出中不再增加配置层。
单独构建任意一个库，同样得到该库的完整交付目录：

```sh
cmake --build build/macos-arm64-debug --target xlog_encode
cmake --build build/macos-arm64-debug --target xlog_decode
```

构建自动同步公开头文件、导出宏头、文档和 CMake 引用配置；仅修改文档不会重新链接库，删除输出副本后再次构建会补回文件。
安装使用相同文件清单。先构建所需库，再安装到自选目录：

```sh
cmake --install build/macos-arm64-debug --component xlog_encode --prefix /absolute/path/encode-sdk
cmake --install build/macos-arm64-debug --component xlog_decode --prefix /absolute/path/decode-sdk
```

每个交付目录可单独移动。模块源、BMI、对象文件留在构建树，不作为分发内容。

子项目的 `CMakeLists.txt` 直接声明动态库与私有模块；`cmake/XlogLibrary.cmake` 集中库属性、文件同步及安装规则。
产品构建目标只有 `xlog_encode`、`xlog_decode`，调用示例由下面的代码和 C/C++ 测试提供。

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
#include <stdio.h>
int main(void) {
    puts(xlog_encode_version());
    return 0;
}
```

配置消费者时传入 `-DCMAKE_PREFIX_PATH=/absolute/path/encode-sdk`。
解码库使用 `find_package(xlog_decode CONFIG REQUIRED)`、`xlog::decode` 和
`#include <xlog_decode/xlog_decode.h>`，调用 `xlog_decode_version()`。
返回字符串由库持有，库加载期间有效，不得修改或释放。

macOS 库使用 `@rpath` install name，外部 CMake target 提供对应链接位置。
Windows 运行时将本包 `bin` 加入 PATH；同时需要匹配的 GCC 运行库。
本工程不静态捆绑 GCC 运行库，也不承诺未经验证的旧版 macOS 兼容性。

CTest 验证 C/C++11 调用、交付目录移动后的消费、单库构建、头文件和文档增量同步，以及 build/install 文件清单和内容一致性。
macOS 还检查实际加载的 dylib 路径。

## 持续集成

推送 `main`、向 `main` 提交 PR 或手动触发时，GitHub Actions 使用
`windows-2025` 与 MSYS2 UCRT64 运行 Windows Release 配置、构建、产物布局检查和完整 CTest。
工作流记录实际 GCC/CMake/Ninja 版本；未达到工具链最低要求时配置会失败。
可在仓库 Actions 页面查看运行结果。
