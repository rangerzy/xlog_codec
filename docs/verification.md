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

## Buffer 解码（2026-10-19）

本次新增纯内存的 xlog 识别、单块解析、单块及整段解码，以及同库释放接口。
默认密钥在公开配置头中留空；测试仅使用公开标量 1/2 和独立已知向量，未读取或复制参考默认密钥。

- 环境：macOS 27.0.1 ARM64，GCC 16.2.0，CMake 4.3.1。
- 先观察到全部新增公开接口缺失的预期链接失败，再实现生产代码。
- 测试发现空压缩 payload 被错误接受，修正后压缩块要求实际 DEFLATE 流；空明文块仍合法。
- 最终 Debug 构建成功、完整 CTest **10/10**；Release 构建成功、完整 CTest **10/10**。
- 覆盖完整结构识别、所有截断边界、零拷贝视图、四种解码、TEA/ECDH 已知向量、非法密钥/公钥、
  raw DEFLATE 正常结束/完整 flush/伪结束与尾随数据、错误偏移、整体失败和输出限制。
- 配置头宏覆盖经真实 C99/C++11 调用验证；搬迁后的消费者使用自己的宏密钥完成加密样本解码。
- 两库单独交付、文档与配置头同步、删除恢复、build/install 文件及哈希一致性验证全部通过。
- 本机从[官方 OpenSSL 3.5.9 源码](https://github.com/openssl/openssl/releases/tag/openssl-3.5.9)
  构建 ARM64 静态 Crypto，校验源码 SHA-256 为
  `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`。
  未复制参考预编译库；本机构建依赖位置仅保存在未提交的构建 cache。
- `otool` 确认解码库仅新增系统 zlib 依赖，无 OpenSSL 动态依赖；`nm` 确认 Crypto 符号未暴露为公开 ABI。
- 独立审阅的唯一重要发现已处理：OpenSSL 最低要求统一为 **3.0+**，与随包 Apache 2.0 许可证对应。
  其他解码算法、C ABI、预算及所有权检查无重要发现。
- Windows UCRT64 CI 已添加依赖安装与静态依赖检查；本轮尚未实际运行新的 Windows CI，
  上文历史成功记录不能作为本次解码实现的 Windows 验收结果。

## 解码类型与方法拆分（2026-10-19）

按用户确认，将公开 C 类型移动到 `xlog_decode_types.h`，公开函数仍由 `xlog_decode.h` 声明。
私有 `decode_types.h` 独立定义内部 C++ 类型；codec/命名模块不依赖公开头，C ABI 适配层显式转换字段和错误状态。

- 基线完整 Debug CTest **10/10** 通过；引用新增类型头的 C 消费者先观察到预期缺失头文件错误。
- 拆分前后逐字比较：1 个公开枚举、5 个公开结构体、初始化宏和 6 个公开函数声明全部相同。
- 使用旧头预先编译的 C/C++11 消费者直接加载新动态库，真实解码与错误测试通过；公开导出符号列表相同。
- 私有类型头和 codec 在未提供公开头路径的条件下编译通过；公开类型头独立供 C99/C++11 使用，无需导出宏头。
- 新增非法 C 块视图检查，覆盖空 payload 指针、长度不匹配和极大长度；在拆分前 Release 库及新库上结果一致。
- 构建/安装 SDK 只包含 API、公开类型、配置和导出宏 4 个公开头，未交付内部类型或 codec 头。
- 完整验证类型头增量同步、删除恢复、两个库独立构建、搬迁消费与 build/install 文件和内容一致性。
- 最终 macOS ARM64 Debug、Release 均构建无警告，完整 CTest 分别 **10/10** 通过。
- 独立审阅未发现本轮拆分引入的重要问题。Windows 布局检查已包含新增类型头，本轮未实际运行 Windows CI。

## Buffer 编码（2026-10-19）

按用户确认实现一次输入 buffer、输出一个完整 xlog 块，支持 `0x08` 原文、`0x09` raw DEFLATE
和 `0x07` secp256k1 ECDH / 16 轮小端 TEA。输入与输出都是内存 buffer，不涉及文件、CLI 或 MCP。
公开配置、类型和方法分别成头文件；私有 C++ 类型独立定义，C ABI 层显式转换。
默认接收方公钥留空，未读取或复制参考项目的默认密钥。

- 本机环境沿用 macOS ARM64 / GCC 16.2.0 / CMake 4.3.1；修改生产实现前的完整 Debug 基线 **10/10** 通过。
- 新接口声明后、实现前，真实 C99 调用观察到缺少 `xlog_encode_bytes` 和 `xlog_encode_free_buffer` 的预期链接错误。
- 最终 Debug、Release 均构建无警告，完整 CTest 分别 **13/13** 通过。
- 固定原文帧和 `abc` 的独立 raw DEFLATE 字节向量验证头部、小端字段、长度及结束标记；
  独立 zlib oracle 在 flush 后追加终结空块，要求完整消费并还原输入。
- 独立 TEA oracle 验证 16 轮小端运算及不足 8 字节的尾部；公开测试标量 1/2 验证加密互通，
  非 1 接收方确保派生过程实际使用 ECDH，而非直接使用临时公钥 X。
- 所有模式覆盖空输入、任意二进制、65535/65536/65537 和 131071/131072/131073 字节边界；
  还验证拼接多块、重复调用、序号和跨午夜小时、非法参数、公钥长度/hex/曲线点、完整输出上限、
  超 `UINT32_MAX` 输入在读取前拒绝、失败无部分结果及重复释放。
- 三个公开头分别通过 C99/C++11 严格独立编译；私有 codec 在未提供公开头路径时编译通过。
- 两库 encode-only / decode-only 的单独交付、配置及类型头增量同步、缺失副本恢复、私有头不交付、
  build/install 文件清单和 SHA-256 一致，以及搬迁后 C99/C++11 的真实编码/解码调用全部通过。
- 迁移编码消费者首次失败，经预处理 include trace 定位为本机 `/usr/local/include` 旧版头文件优先于
  imported target 的 `-isystem` 路径。测试消费者设置 `CMAKE_NO_SYSTEM_FROM_IMPORTED=ON` 后完整重跑通过；
  产品 API 和库包头文件无需修改。
- 两库共用 GLOBAL 依赖发现，各自 PRIVATE 链接 Crypto/zlib，互不链接；双 OFF 时不发现这两项依赖。
  `otool` 确认无动态 Crypto 或另一 codec 依赖，保留系统 zlib 和 GCC 动态运行库。
  `nm` / dyld exports 确认编码接口为 version/bytes/free，未暴露 Crypto 或内部 codec 符号；
  GCC 自带的四个 weak helper 导出与解码库一致。
- 独立审阅未发现重要实现问题，并使用独立 Python secp256k1 运算、非平凡接收方标量、TEA 解密及 zlib，
  对 0..400000 字节的八组输入交叉验证通过。
- Windows CI 已同时检查两 DLL 的公开头/许可证、静态 Crypto/zlib 和互不依赖；工作流 YAML 与 shell
  语法检查通过。本轮未实际运行 Windows CI，上文历史成功记录不代表本次编码实现已在 Windows 验收。

## 运行时密钥加密往返示例（2026-10-19）

用户要求自行生成公私钥，实际编码加密压缩、解码解密解压后比较，并将示例放在两库同级。
新增根目录 `example/roundtrip.c`（C99）及独立的 CMake、运行说明。

- 每次运行使用 OpenSSL 3 EVP 生成新的 secp256k1 接收方密钥；将公钥 X/Y 分别补足 32 字节，
  私钥标量补足 32 字节，转换为公开 API 所需的 128/64 字符 hex。
- 显式选择 `XLOG_ENCODE_DEFLATE_TEA`，检查输出确为单个完整 `0x07` 块及指定元数据，再解码并比较长度与 `memcmp`。
  比较使用普通条件判断，Release 下同样执行。
- 本机实际运行结果：空输入 `0 -> 79 -> 0`，UTF-8 文本 `55 -> 134 -> 55`，
  二进制 `256 -> 340 -> 256`，较大 buffer `262161 -> 1420 -> 262161`（输入 -> 完整 xlog -> 解码字节数）。
  四组逐字节一致，程序退出码 0。
- 密钥只保存在内存，不打印或保存；私钥临时数组、BN 和 hex 退出时清除。
  两库默认密钥配置仍为空，没有修改产品库的 API 或编解码实现。
- `XLOG_BUILD_EXAMPLES` 默认 OFF；ON 时要求两库都启用。可执行文件位于构建目录 `example/`，
  不进入 SDK、不安装。启用测试时注册 `example_roundtrip`，Windows 测试配置两个 DLL 的 PATH。
- C99 严格语法检查（`-Wall -Wextra -Wpedantic -Werror`）通过；Debug、Release 示例均构建无警告。
- 完整 Debug、Release CTest 各 **14/14** 通过，包含实际随机生成密钥的示例和既有库包验证。
- 独立审阅未发现需修改的问题。Windows CI 已启用示例并通过工作流语法检查，本轮未在 Windows 实际运行。

## C++ 示例与默认构建目标修正（2026-10-19）

根据用户后续要求，当前同级示例使用 `example/roundtrip.cpp`，
`example/CMakeLists.txt` 声明 `project(example LANGUAGES CXX)` 和名为 `example` 的可执行目标。

- 使用严格 C++11、`std::array` / `std::vector` 和 RAII 管理密钥及两个库的输出，退出和异常路径均释放资源、清除私钥。
  不依赖库内部 C++26 Modules；示例目标关闭模块扫描。
- 两个库均启用时，示例默认 ON；官方 presets 也显式启用，常规配置即可显示 `example` 目标。
  新的单库或无库配置默认关闭示例，独立交付回归显式设置 `XLOG_BUILD_EXAMPLES=OFF`。
- 复现目标不可见：CLion 的 `cmake-build-relwithdebinfo-gcc16` 缓存示例为 OFF，Ninja target 清单没有示例。
  已在该现有目录重新配置为 ON，并将其旧 Crypto 架构缓存调整为已验证的 ARM64 静态依赖。
  没有把本机依赖路径写入公共 preset。
- 已实际执行 CLion RelWithDebInfo 的 `--target example` 构建，退出码 0；Ninja 清单中可见 `example`。
  程序位于 `cmake-build-relwithdebinfo-gcc16/example/example`，四组数据均逐字节一致。
- 已删除两个空的 `xlog_encode/examples/`、`xlog_decode/examples/` 目录及 C 示例源文件；
  清除两个预设构建目录内已知的旧示例二进制。
- `roundtrip.cpp` 通过 C++11 严格语法检查（`-Wall -Wextra -Wpedantic -Werror`）；
  Debug、Release、CLion RelWithDebInfo 均成功构建新目标。
- 最终完整 Debug、Release CTest 各 **14/14** 通过，包含新的 C++ 示例和两个库的独立交付回归。
- 独立审阅未发现重要问题。Windows CI 的示例产物路径已改为 `example/example.exe`，本轮未实际运行 Windows。

## 默认密钥与 C++26 简例（2026-10-19）

用户后续明确要求将生成的公私钥直接保存为头文件默认值，并删除复杂示例，重新生成简洁 C++26 示例。
此要求取代前文历史实现中的“默认密钥留空”和“示例运行时生成密钥”。

- 本次使用系统安全随机数生成全新 secp256k1 接收方密钥对，未复制参考项目密钥。
  公钥 128 个 hex 字符写入 `xlog_encode_config.h`，私钥 64 个 hex 字符写入 `xlog_decode_config.h`；
  保留 `#ifndef` 保护，调用方仍可通过宏或 options 覆盖。
- 已从私钥重新推导并比对完整公钥，独立审阅也验证密钥对匹配。
  公钥字节的 SHA-256 为 `4d88399d92493bb532ba3305c970f8f24ef27ff5e54355198bcbd33aa2dc6d9f`。
- 删除旧 `example/roundtrip.cpp`，新建 `example/main.cpp`，仅保留一个 `main()`。
  默认公钥初始化选项后显式选择 `0x07`；解码传 NULL options，使用已重编动态库内的默认私钥。
  完成编码、解码、长度与字节比较，并在成功和错误路径调用各库释放接口。
- `example` 目标使用 C++26，只链接两个库，不直接依赖 OpenSSL，不包含密钥生成方法。
  库的默认编码模式仍为 `0x09`；公开 C ABI 和 C99/C++11 消费能力保持不变。
- 已实际运行 CLion RelWithDebInfo 示例，退出码 0：`input=35, xlog=95, decoded=35, match=true`。
- 测试改为检查配置头默认值，并新增默认公钥编码 / NULL 或初始化选项私钥解码的空、二进制往返；
  继续覆盖显式 NULL/空密钥错误、无效密钥和调用方宏覆盖。
- Debug、Release、CLion RelWithDebInfo 均已成功构建；独立审阅未发现重要实现问题。
  最终完整 Debug、Release CTest 各 **14/14** 通过，包含默认密钥往返、外部宏覆盖、缺失密钥错误和独立库包验证。
  Windows 本轮未实际运行。

## 默认加密模式与无 options 示例（2026-10-19）

用户继续要求默认编码模式也使用 `0x07`，示例直接调用两个库的默认配置。

- `XLOG_ENCODE_DEFAULT_MODE` 改为 `XLOG_ENCODE_DEFLATE_TEA`，对应 wire magic `0x07`；现有默认密钥对未改变。
- `example/main.cpp` 已移除 options 变量及单独设置模式的代码；encode、decode 均传入 nullptr，
  使用各自库编译自配置头的默认值。保留了用户在示例中加入的显示代码。
- CLion RelWithDebInfo 已重编并实际运行：`input=35, xlog=95, decoded=35, match=true`。
- 默认模式测试改为验证 `0x07` 的结构、元数据和长度；加密结果每次使用新临时密钥，不比较两次密文是否相等。
  `0x09` 的独立 raw DEFLATE 已知向量继续通过显式模式验证。
- 互通测试覆盖 encode/decode 使用 nullptr 或初始化选项的四种组合，包含空和二进制输入，
  校验默认 `0x07` 块并比较还原字节。
- 三份现行 README 已同步默认模式及无需 options 的调用方式；前文默认 `0x09` 的描述属于历史行为。
- 最终 macOS Debug、Release 完整 CTest 各 **14/14** 通过；CLion 示例运行通过。Windows 本轮未实际运行。

## 示例内容输出（2026-10-19）

- `example/main.cpp` 仍只有一个 `main()`，直接打印原文、完整编码 buffer 的两位十六进制字节和解码内容。
  解码内容按 buffer 长度输出，不依赖 NUL 结尾；保留长度与逐字节比较结果。
- Debug、Release、CLion RelWithDebInfo 的 `example` 构建均通过；Debug、Release 的 `example_roundtrip` 各 **1/1** 通过。
- 实际输出校验：原文与解码内容均为 35 字节且一致；95 字节编码结果的十六进制输出无遗漏，
  重组后 magic 为 `0x07`、结束标记为 `0x00`，payload 长度与完整帧大小匹配，最终 `match=true`。

## 128 KiB 日志示例（2026-10-20）

- 参考 [zlib 官方示例](https://www.zlib.net/zlib_how.html) 在内存允许时使用 128 KiB 或 256 KiB 处理缓冲区的建议，
  将输入改为 128 KiB（131072 字节）的多条日志记录。该建议针对处理效率，不代表最小输入长度或压缩率保证。
- 仍仅有一个 `main()`，在其中生成请求序号、耗时等内容变化的日志；保留默认配置调用和完整原文、hex、解码打印。
  汇总增加完整编码大小与原文大小的百分比。
- 实测 `input=131072, xlog=7033, decoded=131072, encoded/input=5.37%, match=true`，
  完整 xlog 比原文减少 124039 字节。单独重组打印的 hex，验证全部 7033 字节、magic、payload 长度和结束标记；
  同时按长度提取全部原文与解码输出，逐字节相同。
- Debug、Release、CLion RelWithDebInfo 的 `example` 均构建通过；Debug、Release 的 `example_roundtrip` 各 **1/1** 通过。

## 三平台 Release 分发（2026-10-20）

- 128 KiB 示例修改已通过提交 `530ef41` 推送至 main；对应 [Windows CI](https://github.com/rangerzy/xlog_codec/actions/runs/39353683456)
  已成功完成 Release 构建、静态依赖检查和全部 **14/14** CTest。
- 新增 Linux x64 平台与 Debug/Release presets；Linux 包使用版本化 SONAME 和 `$ORIGIN` 安装 RPATH。
- 新工作流为 macOS ARM64、Windows UCRT64 x64、Linux x64 分别构建两个库的 Release SDK，
  在 main 推送或 main 手动触发时上传三个平台 ZIP；PR 构建执行相同验证。
- 本机从仅含项目源文件的临时副本全新配置 macOS ARM64 Release，构建及 **14/14** CTest 通过。
  `PackageRelease.cmake` 生成 ZIP 后重新解压，校验精确文件清单和库别名，两个独立 SDK 的 C/C++11
  调用程序均编译、运行通过。工作目录中已有的 `* 2.*` 副本未改动或加入提交。
- ZIP 只包含两个独立 SDK 的公开头、动态库、CMake package、API 文档及许可证；版本取自构建 cache。
  新三平台远程运行结果与产物以 [Release SDKs 工作流](https://github.com/rangerzy/xlog_codec/actions/workflows/build.yml) 为准。
