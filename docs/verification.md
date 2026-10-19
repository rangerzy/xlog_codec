# 初始化验证记录

日期：2026-10-19。项目版本：0.1.0。

## 本机验证

环境：macOS 27.0.1 ARM64、Homebrew GNU GCC 16.2.0、CLion CMake 4.3.1、Ninja 1.13.2。

| 验证项 | 结果 |
| --- | --- |
| `macos-arm64-debug` 全新配置和构建 | 通过，实际编译命名 Modules 与 `<meta>` 静态反射 |
| Debug CTest | 6/6 通过 |
| `macos-arm64-release` 全新配置和构建 | 通过，构建无警告 |
| Release CTest | 6/6 通过 |
| 单独构建 `package_xlog_encode` / `package_xlog_decode` | 分别通过；没有构建另一库的产物 |
| 分别指定 `--component` 和独立 `--prefix` 安装 | 通过 |
| 两个包复制到含空格的新路径后，由 C/C++11 消费者使用 | 通过；检查导入位置与实际 dylib 加载位置 |
| 只启用 `LANGUAGES C` 的 AppleClang 消费者 | 两库均链接运行成功 |
| 仅修改文档再运行 package target | 输出更新，动态库没有重新链接 |
| `nm -gU` 导出检查 | 各库仅导出对应的未修饰 C API |
| 默认 AppleClang、Ninja Multi-Config 配置 | 均按要求明确拒绝 |

API 验证先观察到缺少两个版本方法定义的预期链接失败，再实现库并通过测试。
包消费验证先观察到缺少 package Config 的预期失败，再实现组件安装和导出规则。
CTest 中的版本检查使用显式失败码，Release 下也有效。

## GitHub 与 Windows

私有仓库：[rangerzy/xlog_codec](https://github.com/rangerzy/xlog_codec)，HTTPS 远程、`main` 分支。

首次 Windows 工作流：[Build and test](https://github.com/rangerzy/xlog_codec/actions/runs/39214819579)。
状态：**成功**。被验证的代码提交为 `173f6d3ecbc777b419dd1381ea46c8d146b16f21`；
后续验收记录提交只修改 Markdown，不改变已验证的实现或工作流。

实际环境为 Windows x64 UCRT64、GCC/G++ 16.2.0、CMake 4.4.4、Ninja 1.13.2。
配置、库与安装包构建、DLL/导入库布局检查全部通过，CTest **6/6** 通过。
现有 HTTPS 认证完成创建和推送，仓库可见性已确认 private。

## 独立审阅与范围取舍

完整实现已通过一次独立审阅，Critical / Important / Minor 均无发现。
审阅时考虑后暂缓的行为，处理如下：

1. **GitHub 和 Windows 外部验收**：在审阅后实际完成了创建、推送和 Windows CI，未用工作流文件存在代替实际结果。若遗漏，则无法支撑已推送或 Windows 已验证的声明。
2. **合并安装前缀**：按已确认的独立库包根交付；将两库同时安装到同一个 prefix 会使 `doc/README.md` 相互覆盖。需要合并 SDK 时应另行调整文档命名。
3. **平台范围**：当前范围为 macOS ARM64 与 Windows x64 UCRT64 GCC；其他 MinGW 发行版、Linux、MSVC、其他架构和更早 macOS 均未作兼容性承诺，直接使用可能不兼容。
4. **GCC 运行库**：按已确认范围保留动态运行库依赖，不静态捆绑。目标机器缺少匹配运行库时可能出现加载失败，使用者应准备相应运行环境。

## 构建与 install 一致性修正

代码提交：`a8440e904f2691d7eea22c276c2b92c7fdaf1772`。

保留 `xlog_encode`、`xlog_decode` 两个同名 SHARED target，移除 `package_*`、独立示例程序目标和单独的 package helper。
子项目直接声明库与私有 Modules，公共配置统一管理库属性、文件复制依赖和安装规则。
同一份头文件、文档与可重定位 Config/ConfigVersion 用于 build 和 install。
调用示例由 README 和 C/C++ 测试提供。

回归测试先观察到单独构建缺少公开头文件的失败，再验证修正后的行为：

- 全新 Debug、Release 配置与构建无警告，CTest 各 **8/8** 通过。
- 单独构建各库得到完整交付目录，未生成另一个库的交付文件。
- 仅修改文档时同步副本，不重新链接动态库；头文件更新也会同步。
- 删除公开头、导出头、文档或 CMake 配置副本后，再次构建会补回。
- build 与独立 component install 的文件清单和每个文件的 SHA-256 一致。
- 安装目录移动后，普通 C/C++11 消费者仍能链接运行。
- 既有 macOS Debug/Release 预设已重新构建；旧示例程序和旧 Targets 文件残留在本次迁移时清理，CMake 不添加自动清理任意历史文件的逻辑。
- 独立审阅未发现 Critical / Important / Minor 问题。

Windows 验证：[run 39218534706](https://github.com/rangerzy/xlog_codec/actions/runs/39218534706) **成功**。
实际工具链为 GCC/G++ 16.2.0、CMake 4.4.4、Ninja 1.13.2；DLL/导入库布局检查通过，CTest **8/8** 通过，包含两库的 build/install 回归。
本记录的后续提交仅修改 Markdown，不改变已验证的实现。

## CMake package 目录调整

按用户确认，将每个库的 Config/ConfigVersion 从 `lib/cmake/<子项目>/` 移至交付根目录下的 `cmake/`，与 `include/`、`lib/`、`bin/`、`doc/` 同级。
同步配置生成时的安装位置、构建复制清单和安装规则，消费者仍只传库包根目录作为 `CMAKE_PREFIX_PATH`。

- 全新 Debug、Release 配置与构建均无警告，CTest 各 **8/8** 通过。
- 测试覆盖单库完整交付、增量同步、缺失文件补回、build/install 文件清单及 SHA-256 一致性，以及含空格新路径下的 C/C++11 消费。
- 生成 Config 的 `PACKAGE_PREFIX_DIR` 正确指向 `cmake/` 的上一级。
- 现有 macOS Debug/Release 预设及 CLion RelWithDebInfo 构建目录已重新构建，旧位置的已知 Config 文件已清理；未增加额外目标或自动清理逻辑。
- Windows CI 增加新位置下两个 Config 文件的布局检查。
