# xlog_decode

纯内存 xlog 解码动态库，版本 `0.1.0`。公开接口为 C ABI，支持 C99/C++11 消费者；内部使用 GCC 16/C++26 Modules。
输入和输出都是 buffer，不读写文件，不提供 ZIP、CLI 或 MCP 接口。

```c
#include <xlog_decode/xlog_decode.h>

/* data/size 指向宿主程序已经取得的 xlog buffer。 */
int decode(const void *data, size_t size)
{
    xlog_decode_options options = XLOG_DECODE_OPTIONS_INIT;
    xlog_decode_buffer_result output = {0};
    xlog_decode_error error = {0};
    /* 使用配置头中的默认私钥；也可设置 options.private_key_hex 覆盖。 */
    xlog_decode_status status = xlog_decode_bytes(data, size, &options, &output, &error);
    if (status != XLOG_DECODE_OK) {
        /* error.code / error.offset / error.message 描述失败块。 */
        return (int)status;
    }
    /* 使用 output.data[0..output.size)。结果为二进制，不追加 NUL。 */
    xlog_decode_free_buffer(&output);
    return 0;
}
```

## 接口

`xlog_decode_types.h` 单独定义公开枚举、结构体和选项初始化宏，兼容 C99/C++11，不依赖导出宏头。
`xlog_decode.h` 声明公开函数并包含类型头，原有单头 include 用法保持不变。
默认配置仍位于 `xlog_decode_config.h`。内部类型定义在私有 `decode_types.h`，
与公开 C 类型独立，仅由 C ABI 适配层转换，不随 SDK 交付。

| 函数 | 行为 |
| --- | --- |
| `xlog_decode_version()` | 返回借用的版本字符串 `0.1.0`，不要释放 |
| `xlog_decode_is_xlog_buffer(data, size)` | 校验整段块结构，合法返回 1；空/损坏输入返回 0，不解密解压 |
| `xlog_decode_parse_block(data, size, offset, &block, &error)` | 解析指定偏移的单块，返回头字段、payload 视图、偏移和编码长度 |
| `xlog_decode_block(&block, &options, &output, &error)` | 解码单块，返回新的明文 buffer |
| `xlog_decode_bytes(data, size, &options, &output, &error)` | 逐块解码整段并顺序合并结果 |
| `xlog_decode_free_buffer(&output)` | 由本库释放结果并清零，可重复调用，也可传 NULL |

所有带状态的函数成功返回 `XLOG_DECODE_OK`；`error` 可传 NULL，成功会清零已有错误。
失败时输出清零，不交付部分结果；错误包括状态码、失败块相对输入的起始偏移及 NUL 结尾消息。
空输入不是 xlog，整段解码返回 `XLOG_DECODE_INVALID_FORMAT`。
从 buffer 末尾解析返回 `XLOG_DECODE_TRUNCATED_INPUT`，偏移超过末尾返回 `XLOG_DECODE_INVALID_ARGUMENT`。

解析返回的头按值复制，payload 借用输入内存；使用视图期间须保持输入存活、内容和地址不变。
逐块遍历时，在成功后执行 `offset += block.encoded_size`。可不先调用识别函数而直接解析或解码。
`xlog_decode_block` 接受解析得到的完整视图；payload 指针及长度必须有效且匹配头中的长度。
宿主程序负责保留输入；解码结果由库分配，必须用本库的释放函数释放，复用输出变量前先释放旧结果。
不使用全局可变密钥或错误状态；独立输入、输出和配置可并发调用。

## 格式

现代块结构为 73 字节头、payload 和 `0x00` 结束字节。头部包含 magic、序号、起止小时、
小端 payload 长度和 64 字节公钥。解析保留序号与小时原值，不要求序号连续。
结构识别支持 `0x06..0x0d`，与解码支持范围分别判断：

| magic | 解码 |
| --- | --- |
| `0x06`、`0x08` | 原样输出 payload |
| `0x09` | raw DEFLATE 解压 |
| `0x07` | secp256k1 ECDH 派生 TEA 密钥，16 轮小端 TEA 解密，再 raw DEFLATE 解压 |
| `0x0a..0x0d` | 可解析块结构，解码返回 `XLOG_DECODE_UNSUPPORTED_FORMAT` |

旧版 `0x03..0x05` 不在本次兼容范围。raw DEFLATE 接受正常结束流和完整的 `Z_SYNC_FLUSH` 段；
空压缩 payload、截断、非法数据、zlib/gzip 包装和流结束后的额外数据均失败。
空原文块和编码正确的空 DEFLATE 流合法。TEA 不足 8 字节的尾部按 xlog 规则原样保留。

## 默认值与外部覆盖

`xlog_decode_config.h` 内置为本项目新生成的 secp256k1 接收方私钥，
与编码库配置头中的默认公钥配对，未复制参考项目的密钥。
缺少密钥仅在解码加密块时返回 `XLOG_DECODE_KEY_REQUIRED`；识别、解析和其他块不需要密钥。
私钥须为 64 个十六进制字符，标量范围为 `1 <= d < secp256k1_order`，非法密钥或公钥返回
`XLOG_DECODE_INVALID_KEY`，消息不包含密钥内容。

配置头中的默认宏均由 `#ifndef` 保护。可编辑配置头，也可在包含头文件之前覆盖宏：

```c
/* 也可在此定义 XLOG_DECODE_DEFAULT_PRIVATE_KEY_HEX，覆盖默认私钥。 */
#define XLOG_DECODE_DEFAULT_MAX_OUTPUT_SIZE (64u * 1024u * 1024u)
#include <xlog_decode/xlog_decode.h>

/* 此宏在调用方展开，使用修改后的头文件默认值。 */
xlog_decode_options options = XLOG_DECODE_OPTIONS_INIT;
```

也可直接调整 `options.private_key_hex`、`options.max_output_size` 后传入函数。
默认最大输出为 256 MiB，整段解码按合并后的大小计数；上限为 0 表示不限制。
超过上限返回 `XLOG_DECODE_OUTPUT_LIMIT`，不返回部分输出。
使用 `XLOG_DECODE_OPTIONS_INIT` 并重新编译调用方，可在不重编动态库的情况下应用配置头修改。
传入 NULL options 则使用构建动态库时的默认值，修改这些值需重编库。

## 构建与消费

构建依赖 OpenSSL Crypto 3.0+ 和 zlib，链接探针会检查目标架构兼容性。
默认使用静态 Crypto，优先静态 zlib；macOS 可回退系统 zlib。
OpenSSL/zlib 仅为私有构建依赖，静态链接时安装消费者无需其头文件或开发包。
对应许可证随本包 `doc/` 交付。

```cmake
find_package(xlog_decode 0.1 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE xlog::decode)
```

将本包根目录加入 `CMAKE_PREFIX_PATH`。头文件在 `include`，CMake package 在 `cmake`，动态库在
`lib`（Windows DLL 在 `bin`，导入库在 `lib`）。构建本库 target 即生成完整交付目录。
Windows 运行时仍需匹配的 UCRT64 GCC 运行库，macOS 仍需匹配的 GCC 运行库。
若显式选择动态 Crypto/zlib，宿主环境还须提供对应动态库。
