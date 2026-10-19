# xlog_encode

纯内存 xlog 编码动态库，版本 `0.1.0`。公开接口为 C ABI，支持 C99/C++11 消费者；内部使用 GCC 16/C++26 Modules。
一次输入 buffer，输出一个完整、可独立解码的 xlog 块。输入可包含任意二进制字节，不添加日志前缀、
换行或时间戳；不读写文件，不提供 ZIP、CLI 或 MCP 接口。

以下代码可由 C99 或 C++11 编译：

```c
#include <xlog_encode/xlog_encode.h>

/* data/size 指向宿主程序已经取得的任意二进制 buffer。 */
int encode(const void *data, size_t size)
{
    xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
    xlog_encode_buffer_result output = {NULL, 0};
    xlog_encode_error error;
    /* 默认生成 0x07 加密块，模式与接收方公钥来自配置头。 */
    xlog_encode_status status = xlog_encode_bytes(data, size, &options, &output, &error);
    if (status != XLOG_ENCODE_OK) {
        /* error.code / error.message 描述失败原因。 */
        return (int)status;
    }
    /* 使用 output.data[0..output.size) 中的完整 xlog 块。 */
    xlog_encode_free_buffer(&output);
    return 0;
}
```

## 接口与所有权

`xlog_encode_config.h` 保存可覆盖的默认值；`xlog_encode_types.h` 单独定义公开枚举、结构体及选项初始化宏，
兼容 C99/C++11，不依赖导出宏头。`xlog_encode.h` 声明公开函数并包含类型头。
私有 `encode_types.h` 定义内部类型，算法方法位于 `buffer_codec.h/.cpp`，块封装位于 `xlog_encode.core`。
公开与私有类型相互独立，仅由 C ABI 适配层显式转换，内部源码不随 SDK 交付。

| 函数 | 行为 |
| --- | --- |
| `xlog_encode_version()` | 返回借用的版本字符串 `0.1.0`，库加载期间有效，不得修改或释放 |
| `xlog_encode_bytes(data, size, &options, &output, &error)` | 将任意二进制输入编码成一个完整块；options 可传 NULL |
| `xlog_encode_free_buffer(&output)` | 由本库释放结果并清零，可重复调用，也可传 NULL |

`data` 仅在 `size == 0` 时可为 NULL。空输入合法，仍生成完整块，解码后为空 buffer。
输入由调用方持有，调用结束后可以复用。结果由库分配，必须使用 `xlog_encode_free_buffer()` 释放，
不得使用宿主的 `free` 或 `delete`；复用输出变量前先释放旧结果。

成功返回 `XLOG_ENCODE_OK`。`output` 必须有效；`error` 可传 NULL，每次调用会重置已有错误。
失败清空输出，不返回部分块；错误包含状态码和 256 字节数组中的 NUL 结尾消息，异常不会穿越 C ABI。
常见错误包括参数或小时无效的 `XLOG_ENCODE_INVALID_ARGUMENT`、未知模式的 `XLOG_ENCODE_UNSUPPORTED_MODE`、
输入过大的 `XLOG_ENCODE_INPUT_LIMIT`、输出过大的 `XLOG_ENCODE_OUTPUT_LIMIT`，以及压缩、加密和内存分配错误。

## 格式与模式

现代块由 73 字节头、payload 和 `0x00` 结束字节组成，固定开销为 74 字节。
头部包含 magic、序号、起止小时、小端 payload 长度和 64 字节公钥字段。
每次调用只生成一个块，不自动分块或截断；多个结果可由宿主按顺序拼接后交给整段解码接口。

| mode | magic | payload |
| --- | --- | --- |
| `XLOG_ENCODE_PLAIN` | `0x08` | 原样保留输入字节 |
| `XLOG_ENCODE_DEFLATE` | `0x09` | raw DEFLATE，生成完整的 `Z_SYNC_FLUSH` 段，不加 zlib/gzip 包装 |
| `XLOG_ENCODE_DEFLATE_TEA`（默认） | `0x07` | 先按同样方式压缩，再以 secp256k1 ECDH 派生密钥执行 16 轮小端 TEA 加密 |

序号 `seq` 为 `uint16_t`，默认 0。`begin_hour` 和 `end_hour` 默认 0，仅接受 `0..23`；
不要求结束小时大于等于开始小时，可表示跨午夜。库不替调用方生成这些元数据。

## 公钥与加密

`xlog_encode_config.h` 内置为本项目新生成的 secp256k1 接收方公钥，与解码库配置头中的默认私钥配对，
未复制参考项目的密钥。公钥恰好为 128 个十六进制字符，格式为 `X||Y`，不带 `04` 前缀，
且必须是有效的非无穷远曲线点。仅选择 `XLOG_ENCODE_DEFLATE_TEA` 时检查公钥；
原文与普通 DEFLATE 模式不需要密钥。可通过 `XLOG_ENCODE_DEFAULT_PUBLIC_KEY_HEX` 宏或
`options.public_key_hex` 字段覆盖默认公钥。

公钥缺失返回 `XLOG_ENCODE_KEY_REQUIRED`，十六进制或曲线点无效返回 `XLOG_ENCODE_INVALID_KEY`；
安全随机数或 ECDH 失败返回 `XLOG_ENCODE_CRYPTO_ERROR`，加密失败不会降级为明文。
每次加密由 OpenSSL 安全随机生成临时 secp256k1 私钥，头中存放对应的 64 字节临时公钥。
ECDH 共享 X 坐标的前 16 字节作为 TEA 密钥；每 8 字节独立加密，不足 8 字节的尾部原样保留，不增加 padding。
临时私钥、共享值与 TEA 密钥在退出时清除，公开 API 不允许调用方注入临时私钥。

## 默认值与输出限制

`XLOG_ENCODE_DEFAULT_MODE` 默认是 `XLOG_ENCODE_DEFLATE_TEA`（`0x07`）。
配置头中的默认宏均由 `#ifndef` 保护。可编辑配置头，也可在包含头文件之前覆盖宏：

```c
#define XLOG_ENCODE_DEFAULT_MODE XLOG_ENCODE_PLAIN
#define XLOG_ENCODE_DEFAULT_MAX_OUTPUT_SIZE (64u * 1024u * 1024u)
#include <xlog_encode/xlog_encode.h>

/* 宏在调用方展开，使用此处覆盖后的默认值。 */
xlog_encode_options options = XLOG_ENCODE_OPTIONS_INIT;
```

配置宏还包括 `XLOG_ENCODE_DEFAULT_PUBLIC_KEY_HEX`、`XLOG_ENCODE_DEFAULT_SEQ`、
`XLOG_ENCODE_DEFAULT_BEGIN_HOUR` 和 `XLOG_ENCODE_DEFAULT_END_HOUR`。
也可直接修改初始化后的 options 字段再传入函数。使用 `XLOG_ENCODE_OPTIONS_INIT` 并重新编译调用方，
即可应用配置头宏覆盖，无需重编动态库；传入 NULL options 使用构建动态库时的默认值，修改这些值需重编库。

`max_output_size` 默认 256 MiB，按完整输出块计数，包含 73 字节头、payload 和 1 字节结束标记。
值为 0 时关闭此配置上限，但仍受 wire 格式和地址空间限制：单次输入及 payload 长度分别不得超过
`UINT32_MAX`。上限不足以容纳完整块时返回 `XLOG_ENCODE_OUTPUT_LIMIT`，不交付部分结果。

## 构建与消费

根项目的 `XLOG_BUILD_ENCODE` 与 `XLOG_BUILD_DECODE` 独立控制两个库，默认都为 ON。
仅构建本库可使用 `-DXLOG_BUILD_DECODE=OFF`；任一库启用都需 OpenSSL Crypto 3.0+ 和 zlib，
两个开关都为 OFF 时跳过依赖发现与链接检查。链接探针会检查依赖与目标架构是否匹配。
默认使用静态 Crypto，优先静态 zlib；macOS 可回退系统 zlib。

OpenSSL/zlib 只作为 PRIVATE 依赖，编码库不链接解码库。静态链接时，安装消费者无需其头文件或开发包。
对应许可证 [OpenSSL-LICENSE.txt](OpenSSL-LICENSE.txt) 与 [ZLIB-LICENSE.txt](ZLIB-LICENSE.txt) 随本包 `doc/` 交付。

```cmake
find_package(xlog_encode 0.1 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE xlog::encode)
```

将本包根目录加入 `CMAKE_PREFIX_PATH`。头文件在 `include`，CMake package 在
`cmake`，动态库在 `lib`（Windows DLL 在 `bin`，导入库在 `lib`）。构建本库 target 即生成可单独移动的完整交付目录。

Windows 运行时需使本包 `bin` 和匹配的 UCRT64 GCC 运行库可被系统找到；macOS 仍需匹配的 GCC 运行库。
若显式选择动态 Crypto/zlib，宿主环境还须提供对应动态库。工程不复制 Windows 依赖运行库，也不静态捆绑 GCC 运行库。
