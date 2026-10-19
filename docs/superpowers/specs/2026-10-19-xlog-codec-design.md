# xlog_codec 初始化设计

状态：需求共识与本文均已获用户确认。日期：2026-10-19。

本文记录初始化方案。后续用户已确认简化构建入口，当前行为见文末修订及根 README。

## 目标与边界

建立一个 xlog 编解码工程，验证现代 C++ 构建、内部模块组织和动态库分发方式，为后续真实算法提供基础。本次只提供版本查询方法、调用示例和必要的构建验证，不引入真实 xlog 编解码、加密或压缩实现。

用户确认的要求优先于参考项目的现有结构。参考项目 `/Users/yongzhang/Project/ObsTest/decode_xlog` 提供 CMake、GCC 16 和 C++26 静态反射的基线；它目前没有命名 Modules，也没有可复用的动态库安装布局，因此这两部分由本工程建立。

## 工程组织

根项目名称为 `xlog_codec`，负责公共 CMake 配置、平台检查、测试入口和 GitHub 工作流。根目录不承载业务实现。

```text
xlog_codec/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
├── xlog_encode/
│   ├── CMakeLists.txt
│   ├── include/xlog_encode/
│   ├── src/
│   ├── examples/
│   └── doc/
├── xlog_decode/
│   ├── CMakeLists.txt
│   ├── include/xlog_decode/
│   ├── src/
│   ├── examples/
│   └── doc/
├── tests/
├── docs/
└── .github/workflows/
```

两个子目录、子项目、动态库 target 的名称分别保持为 `xlog_encode` 和 `xlog_decode`。统一从根目录执行 CMake 配置，支持按 target 单独构建，按同名安装组件单独安装。两个库互不依赖，不增加共享业务库。

## 工具链与平台

- 最低 CMake 版本为 4.2.2，使用 Ninja 单配置生成器，最低 Ninja 版本为 1.11。
- 构建库本身要求 GNU GCC 16 或更新版本，C++ 标准设置为 26，要求标准可用，关闭语言扩展，并仅对 C++ 源码启用 `-freflection`。
- 配置阶段检查所用编译器、平台和最小静态反射表达式，遇到不满足要求的环境时给出明确错误，不静默降级语言版本。
- 首批支持 `macos-arm64` 和 `windows-x64-mingw`。macOS 在当前 ARM64 本机验证；Windows 使用 MSYS2 UCRT64 的 MinGW GCC 16 工具链并由 GitHub Actions 验证。
- Debug、Release 使用独立的 `CMAKE_BINARY_DIR`，例如 `build/macos-arm64-debug` 和 `build/macos-arm64-release`；产物目录不再增加配置层。
- 本机验证使用已有的 GCC 16.2、CLion CMake 4.3.1 和 Ninja 1.13.2。默认 PATH 的 CMake 3.30.5 不作为有效基线。个人机器上的绝对路径只进入本地说明或用户配置，不作为其他开发者必须具有的项目配置。
- 不继承参考工程因预编译依赖产生的 macOS 27.0 最低部署限制。本次记录实际验证环境；更早系统的兼容性需要对应环境验证后再承诺。

MSYS2 UCRT64 仍属于已确认的 MinGW/GCC 路线。其选择属于实现细节，不增加 MSVC 支持承诺。Linux 和其他架构后续另行扩展。

## Modules 与动态库边界

每个子项目有私有命名模块，分别使用 `xlog_encode.core` 和 `xlog_decode.core`。CMake 以 `FILE_SET CXX_MODULES` 管理模块依赖，模块文件集为 PRIVATE。模块接口声明可供本库内部适配层导入的实体，不向库使用者发布模块源文件、BMI、扫描结果或编译中间文件。

标准库通过全局模块片段中的头文件使用，不强制 `import std`。两者属于不同的工具链能力，本阶段仅要求项目自身的命名 Modules。

每个库具有独立的普通 C++ 适配源文件：包含公开 `.h`、导入本库私有模块，并实现 C ABI。公开头文件兼容 C 和 C++，具有 include guard、C++ 下的 `extern "C"` 和平台导出宏。默认隐藏内部符号，只导出公开 API。

消费者通过普通头文件和动态库链接，无需开启 C++ Modules、C++26 或静态反射。目标运行环境仍需满足动态库自身的系统和 GCC 运行库依赖；初始化阶段不静态捆绑 GCC 运行库，也不复制参考项目的全局静态链接选项。

## 最小公开 API

项目初始版本为 `0.1.0`。

```c
/* include/xlog_encode/xlog_encode.h */
const char *xlog_encode_version(void);

/* include/xlog_decode/xlog_decode.h */
const char *xlog_decode_version(void);
```

实际声明附带各自的导出宏。两个方法均返回内容为 `0.1.0` 的非空、以 NUL 结尾的字符串；指针由库持有，在库保持加载期间有效，调用者不得修改或释放。方法没有参数，不传递 STL 类型、异常或跨库分配的对象。

每个库在私有模块内放置最小版本元数据及真实的 C++26 静态反射表达式，用编译期断言验证版本元数据结构。该表达式必须参与编译，不能仅以编译器标志替代特性使用。版本值由项目版本统一驱动。

## 构建与分发布局

每个子项目的分发根严格为：

```text
${CMAKE_BINARY_DIR}/outputs/<平台>/<子项目名称>/
├── include/
│   └── <子项目名称>/
│       ├── <子项目名称>.h
│       └── 导出宏头文件
├── lib/
│   ├── 动态库或 Windows 导入库
│   └── cmake/<子项目名称>/
├── bin/
│   └── Windows DLL 或示例程序
└── doc/
    └── 本库 API 和使用说明
```

macOS `.dylib` 放入 `lib/`；Windows DLL 放入 `bin/`、导入库放入 `lib/`。库使用正常平台命名和版本规则。对象文件和 BMI 等中间产物留在普通 CMake 构建树。

公开头文件、生成的导出头、库文件、CMake package 和文档均须出现在各自分发根中。构建流程提供依赖本库的打包目标，以本库安装组件和该分发根为 prefix，完成标准安装式布局。各安装规则使用相对 `include`、`lib`、`bin`、`doc` 的路径，不能把平台和子项目名硬编码到安装目的地中。

每个分发根可以单独移动或复制；移动后仍能通过 `find_package(xlog_encode CONFIG REQUIRED)` 或 `find_package(xlog_decode CONFIG REQUIRED)` 使用。两个导出的 CMake target 分别为 `xlog::encode` 和 `xlog::decode`。其使用需求仅包含普通头文件与平台链接信息，不向消费者传播 GCC 16、C++26 或模块扫描要求。

标准 `cmake --install` 允许调用者用 `--component xlog_encode` 或 `--component xlog_decode` 选择库，用 `--prefix` 选择安装位置。执行默认项目构建后，文档所述的分发目标或其默认依赖须生成上述完整输出目录。

## 验证与验收

验证集中在本次交付的动态库边界和分发约定：

1. 先以公开头文件和版本 API 的调用场景定义冒烟验证，确认缺少库实现时无法通过，再实现最小 API。
2. 本机完成全新配置、构建和 CTest，实际编译命名模块及反射表达式，分别调用两个动态库并检查版本字符串。
3. C 与普通 C++ 的调用示例验证公开头文件兼容性；至少一个独立消费者仅使用已安装 CMake package，且不启用 C++26 或 Modules。
4. 将两个分发根复制到新的位置，再分别配置、构建并运行消费者，验证头文件、导出 target 和动态库路径可重定位。
5. 单独构建、打包或安装一个子项目时，不依赖另一个子项目的已构建产物。
6. 检查输出目录和导出符号，确认公开 API 可用，BMI 和内部源码没有成为分发依赖。
7. GitHub Actions 在 Windows UCRT64 GCC 16 环境完成配置、构建、CTest 和安装消费者验证。首次推送后检查远程工作流结果，不能用仅提交工作流替代 Windows 验证结果。

测试采用 CTest 和普通 C/C++ 消费者，不引入额外测试框架或产品依赖。README 提供工具链、构建、测试、输出、安装和消费示例，两个子项目分别包含简短 API 文档。

## Git 与 GitHub

本地仓库使用 `main`。GitHub 目标为账号 `rangerzy` 下的私有仓库 `xlog_codec`，远程使用 HTTPS，提交身份沿用 `/Users/yongzhang/Documents/project/rangerzy.github.io` 已使用的 Git 配置。

已有凭据仅通过现有认证机制使用，不复制到项目。构建目录、用户本机 CMake 配置和临时凭据不提交。完成本地验证、独立代码审阅并修复必要问题后推送；若同名远程已存在，先确认其内容与历史，再决定安全接入方式，不覆盖远程历史。

## 依据

- 本地参考：`/Users/yongzhang/Project/ObsTest/decode_xlog/CMakeLists.txt`、`cmake/Platform.cmake`。
- GitHub 配置参考：`/Users/yongzhang/Documents/project/rangerzy.github.io`。
- [CMake 4.3 Modules 文档](https://cmake.org/cmake/help/v4.3/manual/cmake-cxxmodules.7.html)：项目命名模块和 `import std` 的支持边界。
- [GCC 16 变更说明](https://gcc.gnu.org/gcc-16/changes.html)：C++26 静态反射支持。
- [MSYS2 环境说明](https://www.msys2.org/docs/environments/)：UCRT64 工具链选择。

## 后续修订：直接构建动态库即交付完整目录

保留同名的两个 SHARED target，移除额外的 `package_*` 和独立示例程序目标。
子项目直接声明库和 Modules，公共 CMake 文件统一配置属性、复制清单和安装规则。
公开头文件、导出宏头、文档、可重定位 Config/ConfigVersion 文件作为库的生成文件依赖：
单库构建时自动同步，修改文档无需重链，删除交付副本后再次构建会补回。
build 与 install 使用相同文件清单和相同 Config，不再需要单独的打包构建命令。
调用示例保留在 README 及 C/C++ 测试中；CMake Config 直接声明 `xlog::encode/decode` imported target。
