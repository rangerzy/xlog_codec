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
