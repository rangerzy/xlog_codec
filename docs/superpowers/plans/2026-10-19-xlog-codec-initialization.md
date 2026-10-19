# xlog_codec Initialization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 初始化两个可独立分发、通过普通头文件调用的 xlog 动态库，并验证后推送至私有 GitHub 仓库。

**Architecture:** 根 CMake 项目管理 `xlog_encode`、`xlog_decode`，两库互不依赖。每库内部命名 Modules 实现版本查询和静态反射示例，普通 C++ 适配源文件提供 C ABI；component install 生成各自完整且可重定位的分发目录。

**Tech Stack:** CMake ≥4.2.2、Ninja ≥1.11、GNU GCC ≥16、C++26 静态反射、CTest、Windows MSYS2 UCRT64、GitHub Actions。

**Spec:** [已确认的设计](../specs/2026-10-19-xlog-codec-design.md)。规格优先于本计划；实施中出现技术矛盾时先记录并修订方案，不能静默放宽验收。

## Global Constraints

- 根项目名称为 `xlog_codec`；两个子目录、子项目、动态库 target 的名称分别保持为 `xlog_encode` 和 `xlog_decode`。
- 最低 CMake 版本为 4.2.2，使用 Ninja 单配置生成器，最低 Ninja 版本为 1.11。
- 构建库本身要求 GNU GCC 16 或更新版本，C++ 标准设置为 26，要求标准可用，关闭语言扩展，并仅对 C++ 源码启用 `-freflection`。
- 首批支持 `macos-arm64` 和 `windows-x64-mingw`；Windows 使用 MSYS2 UCRT64 的 MinGW GCC 16 工具链并由 GitHub Actions 验证。
- 每个子项目的分发根严格为 `${CMAKE_BINARY_DIR}/outputs/<平台>/<子项目名称>/`，含 `include/`、`lib/`、`bin/`、`doc/`。
- Debug、Release 使用独立的 `CMAKE_BINARY_DIR`；产物目录不再增加配置层。
- 每个子项目有私有命名模块，分别使用 `xlog_encode.core` 和 `xlog_decode.core`；模块文件集为 PRIVATE，不强制 `import std`。
- 项目初始版本为 `0.1.0`；本次只提供版本查询方法、调用示例和必要的构建验证，不引入真实 xlog 编解码、加密或压缩实现。
- 公开 C ABI、`.h`、平台导出宏；消费者无需开启 C++ Modules、C++26 或静态反射；不静态捆绑 GCC 运行库。
- GitHub 目标为账号 `rangerzy` 下的私有仓库 `xlog_codec`；本地和远程使用 `main`、HTTPS，沿用参考项目提交身份，不覆盖已有远程历史。

## Review Focus

1. 错误工具链或不支持的生成器：配置应明确报错，不能降级为不含 Modules/反射的构建。（Task 1）
2. Release 下断言被关闭、C ABI 或 C++ 头兼容性失效：版本验证应以显式失败码运行，且 C/C++ 调用都能链接。（Task 1）
3. 单库打包或仅修改公开头、文档后再次构建：输出应完整更新，无需构建另一个库或触发本库重新链接。（Task 2）
4. 包根目录移动至含空格的新路径：CMake 导入位置和运行时加载路径应指向新包，无需源目录、BMI 或 C++26。（Task 2）
5. Windows DLL 与导入库分离：DLL 在 `bin`、导入库在 `lib`，独立消费者设置正确的运行时搜索路径后可执行。（Task 3）

## 执行约定

本计划是一次工程初始化，三个任务按顺序执行；两个库的同类修改作为一组完成。当前目录为新建仓库，直接在本目录实施，无需为已有代码提供额外隔离。

本机命令中的 `XLOG_CMAKE` 指 `/Applications/CLion.app/Contents/bin/cmake/mac/aarch64/bin/cmake`，`XLOG_CTEST` 指同目录 `ctest`；执行前检查版本。项目共享配置不写入此类个人绝对路径。配置名称固定为 `macos-arm64-debug`、`macos-arm64-release`、`windows-x64-mingw-debug`、`windows-x64-mingw-release`，构建目录为 `build/<配置名称>`。

### Task 1: 两个动态库与 C/C++ 调用验证

**Files:**
- Create: `CMakeLists.txt`、`CMakePresets.json`、`.gitignore`。
- Create: `cmake/XlogToolchain.cmake`、`cmake/XlogLibrary.cmake`、`cmake/VersionConfig.h.in`。
- Create, for each `p` in `xlog_encode`, `xlog_decode`: `${p}/CMakeLists.txt`、`${p}/include/${p}/${p}.h`、`${p}/src/${p}.cppm`、`${p}/src/${p}_api.cpp`、`${p}/examples/version.c`。
- Test: `tests/CMakeLists.txt`、`tests/version_c.c`、`tests/version_cpp.cpp`。

**Interfaces:**
- Consumes: 已存在的 GCC 16、CMake、Ninja；不增加产品依赖。
- Produces: `const char *xlog_encode_version(void)`、`const char *xlog_decode_version(void)`，各返回库持有的 NUL 结尾 `0.1.0` 字符串；调用者不修改、不释放。
- Produces: root `project(xlog_codec VERSION 0.1.0 LANGUAGES C CXX)`，同名子项目、同名 SHARED targets；私有模块 `xlog_encode.core`、`xlog_decode.core`。
- Produces: `xlog_add_library(project_name, export_name)` CMake helper，调用参数分别为 `(xlog_encode, encode)`、`(xlog_decode, decode)`；模块、反射及版本配置仅为 PRIVATE 使用需求。
- Produces: 公开导出宏 `XLOG_ENCODE_EXPORT`、`XLOG_DECODE_EXPORT`；导出宏头分别为 `xlog_encode/xlog_encode_export.h`、`xlog_decode/xlog_decode_export.h`。

- [x] **Step 1:** 写版本调用测试与最小头文件、目标声明；C 和 C++ 各测试两个库。每次调用都显式验证非空和 `strcmp(value, "0.1.0") == 0`，使用非零返回码报告失败，不依赖 `assert`。
- [x] **Step 2:** 配置 `macos-arm64-debug` 并构建测试，记录缺少 `xlog_encode_version`、`xlog_decode_version` 定义导致的预期链接失败。若失败原因是工具链或拼写错误，先修正测试环境。
- [x] **Step 3:** 实现工具链检查、四个 presets 和公共目标 helper。检查 GNU GCC ≥16、Ninja 单配置和允许的平台；以 `<meta>` 反射表达式验证编译器能力。忽略 `build/`、`CMakeUserPresets.json`、`.idea/`，保留已有 IDE 文件。
- [x] **Step 4:** 为两库实现私有模块、最小版本元数据反射和编译期断言，并以普通 `.cpp` 实现公开 C ABI。版本值来自 root 版本配置。默认隐藏符号，公开函数有导出宏，设置平台库名与版本。
- [x] **Step 5:** 添加两个 C 调用示例，输出版本；例程仅通过公开头文件调用。C++ 测试使用普通 C++11，不导入模块。将最终动态库、Windows 导入库和示例分别放入约定的 `lib/`、`bin/`。
- [x] **Step 6:** 执行 `"$XLOG_CMAKE" --build --preset macos-arm64-debug`，然后 `"$XLOG_CTEST" --test-dir build/macos-arm64-debug --output-on-failure --no-tests=error`；四个 API 测试全部通过。以默认 AppleClang 配置另一个临时构建目录，预期得到清楚的 GNU GCC 16 要求错误；使用 Ninja Multi-Config 时预期明确拒绝。
- [x] **Step 7:** 对照 API 与模块边界检查 diff，执行 `git diff --check`，提交 `feat: initialize modular xlog shared libraries`，仅暂存本任务文件。

### Task 2: 独立分发与重定位验收

**Files:**
- Create: `cmake/XlogPackage.cmake`、`cmake/LibraryConfig.cmake.in`。
- Modify: 两个子项目 `CMakeLists.txt`、`tests/CMakeLists.txt`。
- Create: `tests/RunPackageConsumer.cmake`、`tests/consumer/CMakeLists.txt`。
- Create: `xlog_encode/doc/README.md`、`xlog_decode/doc/README.md`、root `README.md`。

**Interfaces:**
- Consumes: Task 1 的库、头文件、导出宏、C/C++ 版本测试源文件及示例。
- Produces: `xlog_add_package(project_name, export_name)`、同名安装组件、`package_xlog_encode` 和 `package_xlog_decode` 构建 targets；默认构建包含完整分发。
- Produces: 相对 `include`、`lib`、`bin`、`doc` 的安装规则，`lib/cmake/<project>` 下的 Config/Version/Targets 文件；导出 `xlog::encode`、`xlog::decode`。
- Produces: 两个独立 CTest 测试 `package_xlog_encode`、`package_xlog_decode`；各测试移动包根后配置并运行 C 和 C++11 消费者。

- [ ] **Step 1:** 编写 package consumer 验证驱动：在含空格的临时目录复制一个包根，设置 `CMAKE_PREFIX_PATH`，以 `find_package(<project> CONFIG REQUIRED)` 查找对应 imported target，分别编译、运行 C/C++11 版本调用测试。验证 imported library 和 include 路径位于新包根，源代码及 BMI 不作为消费输入。
- [ ] **Step 2:** 对 Task 1 产物运行该驱动，记录缺少 Config 或完整安装文件时的预期失败。
- [ ] **Step 3:** 实现每库独立 export set、Config/Version 文件、安装组件和 package target。package target 依赖本库及其示例，每次执行 component install 到 `${CMAKE_BINARY_DIR}/outputs/<平台>/<子项目名称>`；安装不依赖另一个库。使用 `configure_package_config_file` 和相对安装目的地支持重定位。
- [ ] **Step 4:** 完成 root README 与两库 API 文档，写明工具链、presets、构建测试命令、输出目录、独立安装和 `find_package` 示例，以及运行库依赖。确认公共头、导出头、文档和 CMake package 均进入输出，内部模块和编译中间产物没有进入分发。
- [ ] **Step 5:** 全新配置一个构建目录，只执行 `--target package_xlog_encode`，检查 decode 动态库未构建；再用另一个全新目录对 decode 重复。移动各包并执行消费者验证，均应成功。
- [ ] **Step 6:** 用显式临时安装 prefix 验证两个 `--component`；在不改库实现的情况下修改并还原一次公开头注释或文档，重新执行 package target，检查输出对应内容更新。使用 `nm` 检查两个未改名 C API 符号已导出、内部模块实体未泄漏；用 `otool -L` 检查 macOS install name 和依赖，确认消费者实际加载移动后的 dylib。
- [ ] **Step 7:** 构建并运行 Debug 的全部 CTest，再配置、构建和运行 `macos-arm64-release` 的全部 CTest；所有测试通过后，检查 diff 并提交 `build: add relocatable per-library packages`。

### Task 3: Windows CI、独立审阅与 GitHub 推送

**Files:**
- Create: `.github/workflows/build.yml`。
- Modify: `README.md` 中的 CI 与验证说明。
- Record: 本计划的任务状态与实际验证结果；临时工具和凭据不进入仓库。

**Interfaces:**
- Consumes: 前两任务的 presets、CTest、分发目录及已确认 GitHub 目标。
- Produces: Windows UCRT64 GCC 16 Release 构建和包消费验证；HTTPS `origin`；私有 `rangerzy/xlog_codec` 的 `main` 与本地提交一致。

- [ ] **Step 1:** 写一个 `windows-2025` Release job，使用 `msys2/setup-msys2@v2`、`msystem: UCRT64`、`update: true`，安装 `mingw-w64-ucrt-x86_64-gcc`、`mingw-w64-ucrt-x86_64-cmake`、`mingw-w64-ucrt-x86_64-ninja`；后续步骤用 `msys2 {0}` shell。
- [ ] **Step 2:** 工作流记录编译器/CMake/Ninja/包版本，运行 Windows Release preset、默认构建和 `ctest --output-on-failure --no-tests=error`；复用 Task 2 的独立包消费者验证。运行消费者时将对应包 `bin/` 放入 PATH，检查 DLL 与导入库分别存在于约定位置。
- [ ] **Step 3:** 在本机检查工作流结构及项目完整 diff，完成所有本地验收；交给独立审阅者核对已批准设计、运行证据和代码。修复必要问题，重跑受影响检查；不把审阅意见直接当作未经验证的修改指令。
- [ ] **Step 4:** 检查 GitHub 同名仓库是否存在；复用现有 HTTPS 认证，不展示凭据。不存在则创建私有 `rangerzy/xlog_codec`；已存在则核对可见性和历史，非空且历史不一致时保留远程，不执行强推。
- [ ] **Step 5:** 提交经验证的代码和文档，配置 HTTPS `origin` 并执行 `git push -u origin main`。此创建和推送已由原任务授权，无需再次请求发布批准；若认证机制缺少所需权限，则在本地工作完整可审阅后说明具体缺失。
- [ ] **Step 6:** 等待首次 GitHub Windows 工作流完成，检查实际结果。失败则读取日志、修复、运行相关本地验证并推送，直到通过或遇到确需外部输入的阻塞；不能将工作流文件存在记作 Windows 验收通过。
- [ ] **Step 7:** 最后核对远程可见性为 private、本地 `main` 与 `origin/main` 提交一致、工作区无遗漏的产品变更；报告仓库链接、本机和 Windows 验证结果，以及仍需环境支持的限制。

## 计划自查与执行交接

已逐项对照设计：API/Modules/工具链归 Task 1；输出布局、独立构建安装和重定位归 Task 2；Windows 实际运行及 GitHub 交付归 Task 3。Review Focus 的五项均有对应验收步骤。所有函数名、项目名、平台标签和版本值保持一致；不实现真实算法，不引入新产品依赖。

执行方法：用户已确认 Native。采用本会话实施并在完成后独立审阅。原推荐理由：三个任务共享相同的 CMake 与安装接口，两个库仅有同类小实现；本会话连续实施后再独立审阅，能减少交接成本。也可选择 Subagent-driven，按任务实施并逐任务独立审阅。
