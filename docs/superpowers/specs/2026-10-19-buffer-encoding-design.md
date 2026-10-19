# Buffer 编码设计

用户已确认继续实现：一次输入 buffer，输出一个完整 xlog 块；首版仅生成 0x08、0x09、0x07；内外类型独立，结构体与方法分文件。
参考源码与依据见 [Mars 编码调研](../../research/2026-10-19-mars-xlog-encoding.md)。

## 行为

- 输入是任意二进制字节，不添加日志文本前缀、换行、时间戳，不处理文件、线程、ZIP、CLI 或 MCP。
- `xlog_encode_bytes(data, size, options, output, error)` 生成一个现代块；`xlog_encode_free_buffer(output)` 同库释放并清零。
- 0x08 原样封装；0x09 以 raw DEFLATE / 完整 Z_SYNC_FLUSH 段封装；0x07 先按同样方式压缩，再 ECDH/TEA 加密。
- 块为 73 字节小端头、payload、0x00。序号和起止小时取调用方 options，默认 0；小时只接受 0..23，不额外约束先后顺序。
- 空 buffer 合法，NULL 仅在 size=0 时合法；空输入仍生成完整块，解码结果为空。
- 单次输入上限 UINT32_MAX，payload 同样不超过 UINT32_MAX；超限明确失败，不自动分块或截断。
- 默认模式为 DEFLATE。最大输出默认 256 MiB，计入头和结束字节；0 表示只受 wire/地址空间约束。
- 失败返回状态及固定大小消息、清空输出，不交付部分块；异常不得穿越 C ABI。

## 密钥

加密输入为接收方 secp256k1 公钥：128 个十六进制字符，X||Y，不带04前缀。默认公钥留空。
只有明确选择 0x07 时检查密钥；空公钥为 KEY_REQUIRED，非法十六进制/曲线点为 INVALID_KEY，随机或ECDH失败为 CRYPTO_ERROR，绝不降级明文。
每次加密调用由 OpenSSL 安全随机生成一个临时私钥，头中写对应64字节临时公钥；ECDH共享 X 坐标前16字节作为TEA密钥。
TEA为16轮、小端、每8字节独立加密；不足8字节尾部原样保留，不增加padding。临时私钥、共享值和TEA密钥在退出时清除。
默认值放在公开配置头并允许宏覆盖和调用 options 覆盖；NULL options 使用库编译时默认值。
不读取或复制参考默认/生产密钥，不允许在生产API中注入临时私钥。

## 结构

公开 `xlog_encode_config.h`、`xlog_encode_types.h` 与 `xlog_encode.h` 分别保存默认值、C数据类型与函数声明。
私有 `encode_types.h`、`buffer_codec.h/.cpp`、`xlog_encode.core` 分别承载内部类型、算法方法和块封装；C ABI适配层显式转换类型。
维持 C99/C++11 消费、GCC16/C++26 私有 Modules、macOS ARM64 与 Windows UCRT64。
OpenSSL Crypto 3.0+ 与zlib为 PRIVATE依赖，优先静态链接；两库互不链接，各自交付完整头文件、文档、许可证和可移动安装包。

## 验证

先验证新API缺失的预期失败，再实现。独立检查原文完整字节及DEFLATE已知样本；加密验证临时公钥、128位TEA派生和已知原文压缩数据。
对三种模式执行现有decoder互通，覆盖空、二进制、短尾、超过64KiB、大块、重复调用、多个结果拼接、错误密钥、公钥点、输入/输出上限及错误原子性。
保持现有decode测试，增加encode独立C/C++11安装消费者、类型头独立消费、元数据同步、缺失恢复、build/install一致性。
执行完整Debug/Release构建、CTest、符号/依赖检查及独立审阅；Windows实际执行结果单独报告。
