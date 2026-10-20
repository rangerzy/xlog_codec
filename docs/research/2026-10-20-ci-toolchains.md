# 三平台 Release CI 工具链调研

核对日期：2026-10-20。本文记录官方文档和包元数据；下面的安装片段是 CI 实施建议，不代表已经完成远程构建。真实验证结果应以对应 GitHub Actions run、测试日志和产物为准。

## 推荐选择

使用 `macos-15`、`ubuntu-24.04`、`windows-2025`。前两者用 Homebrew 的 GCC 16 bottle；Windows 用 MSYS2 UCRT64。这样不需要从源码构建 GCC。GitHub 官方将 `macos-15` 列为 ARM64，`windows-2025`、`ubuntu-24.04` 列为 x64；`macos-15-arm64` 不是此表中的普通 runner 标签。[GitHub runner 标签](https://github.com/actions/runner-images/blob/main/README.md#available-images)、[GitHub runner 规格](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job)

GCC 16 的官方变更记录明确将 P2996R13 Reflection 的开关列为 `-std=c++26 -freflection`。安装后仍应运行项目已有的 `<meta>` / reflection 检测，不能只检查编译器版本。[GCC 16 变更](https://gcc.gnu.org/gcc-16/changes.html#cxx)

## 已核实的软件包

| 环境 | 软件包事实 | 官方来源 |
| --- | --- | --- |
| Homebrew | `gcc` 为 16.2.0；存在 `arm64_sequoia` 和 `x86_64_linux` bottle，程序后缀为 `-16`。macOS bottle 要求 CLT。 | [gcc formula](https://github.com/Homebrew/homebrew-core/blob/master/Formula/g/gcc.rb#L7-L35)、[程序后缀](https://github.com/Homebrew/homebrew-core/blob/master/Formula/g/gcc.rb#L77-L95) |
| macOS runner | `macos-15` 的软件清单包含 Homebrew、Xcode CLT、CMake 4.4.3、Ninja 1.13.2；预装 GCC 只列到 15，仍需安装 GCC 16。 | [macOS 15 ARM64 清单](https://github.com/actions/runner-images/blob/main/images/macos/macos-15-arm64-Readme.md) |
| Linux runner | Ubuntu 24.04 已预装 Homebrew，但未加入 PATH，官方给出 `/home/linuxbrew/.linuxbrew/bin/brew shellenv`；系统默认 CMake 3.31.6 不满足项目 4.2.2 要求。 | [Ubuntu 24.04 清单](https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Readme.md#package-management)、[工具清单](https://github.com/actions/runner-images/blob/main/images/ubuntu/Ubuntu2404-Readme.md#tools) |
| Homebrew 构建工具 | `cmake` 为 4.4.4，`ninja` 为 1.13.2，均有 Apple Silicon 和 Linux x86_64 bottle。 | [cmake](https://formulae.brew.sh/formula/cmake)、[ninja](https://formulae.brew.sh/formula/ninja) |
| Homebrew 依赖 | `openssl@3` 为 OpenSSL 3.5.9，使用 `--libdir=lib`；`zlib` 为 1.3.2。公式均在 macOS 15 ARM64 和 Linux x86_64 提供 bottle。 | [OpenSSL formula](https://github.com/Homebrew/homebrew-core/blob/master/Formula/o/openssl%403.rb#L3-L20)、[OpenSSL 路径](https://github.com/Homebrew/homebrew-core/blob/master/Formula/o/openssl%403.rb#L48-L56)、[zlib formula](https://github.com/Homebrew/homebrew-core/blob/master/Formula/z/zlib.rb#L3-L26) |
| MSYS2 UCRT64 | GCC 16.2.0-6、CMake 4.4.4-3、Ninja 1.13.2-1；依赖必须从同一个 `ucrt64` 仓库选取。 | [gcc](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-gcc)、[cmake](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-cmake)、[ninja](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-ninja) |
| MSYS2 静态依赖 | OpenSSL 3.6.5-1 的文件清单同时包含 `/ucrt64/lib/libcrypto.a` 和 `libcrypto.dll.a`；zlib 同时包含 `libz.a` 和 `libz.dll.a`。需要选择前者。 | [OpenSSL 文件清单](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-openssl)、[zlib 文件清单](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-zlib) |

Homebrew 会自动下载与系统匹配的 bottle；更改安装选项、强制源码构建或不匹配的 prefix 会影响 bottle 使用。[Homebrew bottle 文档](https://docs.brew.sh/Bottles)

OpenSSL 的 Unix 安装模板分别生成并安装静态库与共享库；zlib 的安装规则明确复制 `libz.a`。CI 应检查实际安装的 `.a` 文件存在，并将绝对路径传给 CMake。[OpenSSL 安装模板](https://github.com/openssl/openssl/blob/openssl-3.5.9/Configurations/unix-Makefile.tmpl#L153-L176)、[zlib 安装规则](https://github.com/madler/zlib/blob/v1.3.2/Makefile.in#L291-L324)

## 建议安装片段

macOS 使用 runner 中已有的 Homebrew；Linux 先启用官方清单中的 Homebrew 路径。`brew update` 后安装并核验实际版本，再显式选择静态依赖：

```bash
# 仅 Linux 需要这一行。
eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"
brew update
brew install gcc cmake ninja openssl@3 zlib
gcc-16 --version
g++-16 --version
cmake --version
ninja --version
openssl_prefix="$(brew --prefix openssl@3)"
zlib_prefix="$(brew --prefix zlib)"
test -f "$openssl_prefix/lib/libcrypto.a"
test -f "$zlib_prefix/lib/libz.a"
cmake --preset "$preset" \
  -DCMAKE_C_COMPILER="$(command -v gcc-16)" \
  -DCMAKE_CXX_COMPILER="$(command -v g++-16)" \
  -DOPENSSL_USE_STATIC_LIBS=ON \
  -DOPENSSL_ROOT_DIR="$openssl_prefix" \
  -DOPENSSL_CRYPTO_LIBRARY="$openssl_prefix/lib/libcrypto.a" \
  -DOPENSSL_INCLUDE_DIR="$openssl_prefix/include" \
  -DZLIB_USE_STATIC_LIBS=ON \
  -DZLIB_LIBRARY="$zlib_prefix/lib/libz.a" \
  -DZLIB_INCLUDE_DIR="$zlib_prefix/include"
```

`preset` 应由 workflow 设置为 `macos-arm64-release` 或 `linux-x64-release`。Linux preset 默认指定无后缀 `gcc`/`g++`，因此这里覆盖为 Homebrew 的 `gcc-16`/`g++-16`。[项目 presets](../../CMakePresets.json)

MSYS2 在 UCRT64 shell 安装以下同前缀包；随后 CMake 可显式选取 `/ucrt64/lib/libcrypto.a` 和 `/ucrt64/lib/libz.a`：

```bash
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-openssl \
  mingw-w64-ucrt-x86_64-zlib
```

这些命令的包名来自上表对应 MSYS2 官方页面；GitHub Actions 的完整 MSYS2 初始化与构建步骤由 workflow 负责。

## Fedora 44 备选路径

Fedora 44 官方包页列出 GCC 16.2.1-2.fc44、CMake 4.3.0-1.fc44、Ninja 1.13.2-2.fc44，满足工具版本要求。[GCC](https://packages.fedoraproject.org/pkgs/gcc/gcc/)、[CMake](https://packages.fedoraproject.org/pkgs/cmake/cmake/)、[Ninja](https://packages.fedoraproject.org/pkgs/ninja-build/ninja-build/)

zlib 静态包名为 `zlib-ng-compat-static`，提供 `zlib-static`、`/usr/lib64/libz.a` 并依赖匹配的 devel 包；其 changelog 明确记录静态兼容库启用 `-fPIC`。[Fedora 44 静态 zlib 包](https://packages.fedoraproject.org/pkgs/zlib-ng/zlib-ng-compat-static/fedora-44.html)

`openssl-static` 官方页只列 Fedora 35 / OpenSSL 1.1.1，没有 Fedora 44 记录，所以不能据此编写 `dnf install openssl-static` 的新版本 CI。若使用 Fedora 44，可保留 RPM GCC/CMake/Ninja/zlib，并从官方 OpenSSL 源码构建静态 Crypto；建议 `no-shared no-module no-pinshared no-tests`、`-fPIC` 和固定 `--libdir=lib`。这些是上游支持的配置选项，其中 `no-pinshared` 适用于把静态 Crypto 链入可卸载的共享库。[Fedora openssl-static](https://packages.fedoraproject.org/pkgs/openssl/openssl-static/)、[OpenSSL 构建选项](https://github.com/openssl/openssl/blob/openssl-3.5.9/INSTALL.md#no-module)、[no-pinshared](https://github.com/openssl/openssl/blob/openssl-3.5.9/INSTALL.md#no-pinshared)、[no-shared](https://github.com/openssl/openssl/blob/openssl-3.5.9/INSTALL.md#no-shared)

## 远程验证范围

软件源和 runner 元数据只证明安装来源可用。CI 仍需实际完成 Release 配置、构建、CTest、安装、SDK 消费者构建/运行和 ZIP 文件清单检查。共享库的动态依赖还应在 runner 上用 `otool -L`、`readelf -d` 或 MinGW `objdump -p` 检查，确认没有动态 Crypto/zlib 依赖；GCC 运行库按既有分发策略由宿主提供。此段是验证建议，不是已完成的验证记录。
