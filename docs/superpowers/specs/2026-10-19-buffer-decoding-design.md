# Buffer 解码设计

用户已确认：仅处理输入和输出 buffer；提供 xlog 识别、块解析、字节解码；支持参考项目实际实现的四种块；默认密钥先留空；损坏立即失败；识别检查整段结构。用户已要求继续实现。

## 范围与接口

- 保留 C ABI、C/C++11 消费者、GCC 16/C++26 私有 Modules、macOS ARM64 与 Windows UCRT64。
- 不添加文件读写、ZIP 容器、CLI、入口程序或 MCP。不修改编码库的业务能力。
- `xlog_decode_is_xlog_buffer(data, size)`：整段结构校验，不解密解压；空输入返回 0。
- `xlog_decode_parse_block(data, size, offset, block, error)`：从指定偏移解析一个块，返回头、借用的 payload、块偏移和编码长度；失败清空结果。
- `xlog_decode_block(block, options, output, error)`：解码一个已解析的块。
- `xlog_decode_bytes(data, size, options, output, error)`：逐块解码并按顺序合并；空输入为格式错误。
- `xlog_decode_free_buffer(output)`：同库释放并清零，允许重复释放已清零结果及传 NULL。
- 结果为任意二进制字节，不追加 NUL，不保证文本编码。调用方释放旧结果后方可复用输出参数。

## 类型边界与文件组织（用户已确认修订）

公开 `xlog_decode_types.h` 只放 C 枚举、结构体和选项初始化宏，`xlog_decode.h` 包含该头并仅声明公开函数。
公开函数、状态码数值、结构体字段与顺序、原有单头 include 用法均保持不变。
私有 `src/decode_types.h` 独立定义 C++ 状态、错误、块头、借用视图、选项和字节/密钥类型，
不包含公开头，也不使用公开类型的别名。内部 codec 与命名模块只使用私有类型；
`xlog_decode_api.cpp` 显式转换字段和状态，先验证 C 指针/长度再构造内部 span。
方法声明仍在 `buffer_codec.h`，实现在 `.cpp`/`.cppm`，类型定义与方法分文件。
所有内部头均不分发，公开类型头与原配置头一同同步到构建与安装 SDK。

## 格式与错误

现代块为 73 字节小端头、指定长度 payload 和一个 0x00 结束字节。头字段为 magic、seq、begin_hour、end_hour、payload_length、64 字节公钥。结构解析识别 0x06..0x0d；解码仅支持 0x06/0x08 原样、0x09 raw DEFLATE、0x07 secp256k1 ECDH + 16 轮小端 TEA + raw DEFLATE。其余现代类型结构可识别，解码返回不支持。

接受正常结束的 raw DEFLATE 与完整 Z_SYNC_FLUSH 段；拒绝空压缩 payload、截断、非法压缩和流结束后附带数据。空明文块仍然有效。TEA 每 8 字节独立处理，不足 8 字节的尾部原样保留。ECDH 使用 X||Y 公钥，取共享点 X 大端前 16 字节。

发生任何错误都不交付部分输出；错误使用状态码、块偏移和固定大小消息传递；异常不得穿过 C ABI。不存在全局可变错误状态。识别和解析不需要密钥。

## 配置与内存

公开 `xlog_decode_config.h` 保存可覆盖宏：默认密钥为空、默认最大输出 256 MiB。`XLOG_DECODE_OPTIONS_INIT` 在调用方使用头文件默认值；显式 options 可覆盖密钥和输出上限，0 上限表示不限制。NULL options 使用构建库时的默认值。修改交付头文件后重新编译调用方并传入初始化的 options 即可使用新默认值；NULL options 的默认值需重编库。

密钥只接受空值或 64 个十六进制字符；加密块缺少密钥返回 KEY_REQUIRED，非法标量或公钥返回 INVALID_KEY。任何参考项目默认密钥都不读取、不复制。测试只使用公开私钥标量 1 和独立已知样本。

块头按值复制，payload 为零拷贝视图，原输入必须保持存活且地址、内容不变。解码输出由库分配和释放。支持输出上限以阻止无界解压和整段合并。

## 构建与验证

OpenSSL Crypto 3.0+ 与 zlib 为 PRIVATE 依赖，优先静态链接；macOS 系统 zlib 可动态链接。使用可配置的系统开发包，不复制参考预编译归档。Windows 保持 UCRT64。新增配置头进入构建交付和安装清单。静态依赖许可证随包交付，OpenSSL 版本范围与其 Apache 2.0 声明保持一致。

验证公开 C/C++ API 的识别、逐块视图、四种解码、已知加密样本、缺省/错误密钥、截断、尾随垃圾、损坏压缩、输出上限、内存释放与错误偏移；保留现有完整 CTest、安装消费、移动包和单库构建验证。
