# Mars xlog 编码调研

调研日期：2026-10-19。用户指定仓库为 `rangerzy/mars` 的 `master`，本次核对的固定提交为
[`6aa5b567afada93936252bac2f271c4b43eccd1f`](https://github.com/rangerzy/mars/commit/6aa5b567afada93936252bac2f271c4b43eccd1f)。
本文区分源码事实和工程建议；encode 尚未开始实现，待 grilling 确认需求。

## 已核对的源码事实

### Mars 的日志 SDK 与编码层

`XloggerAppender` 先调用 `log_formater` 产生带日志元数据的文本，再将文本交给 buffer 编码，最后写文件。
格式化涉及级别、时间、进程/线程、tag、源码位置，并处理换行；这些不属于 xlog 二进制封装本身。
来源：[appender.cc:959–994](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/src/appender.cc#L959-L994)、
[formater.cc:40–168](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/src/formater.cc#L40-L168)。

buffer 存在两条路径：同步 `Write(data, size, out)` 直接产出一个块；异步 `Write(data, size)` 持续积累，
随后 `Flush(out)` 交付一个完整块并清空状态。
来源：[log_base_buffer.cc:259–338](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/src/log_base_buffer.cc#L259-L338)。

### 块结构和模式

现代头为 73 字节：magic 位于 0，序号 uint16 位于 1，起止小时位于 3/4，payload 长度 uint32 位于 5，
64 字节客户端公钥位于 9；payload 后跟一个 `0x00`。上游整数使用本机 `memcpy`，本项目现有协议实现明确使用小端。
来源：[log_crypt.cc:126–205](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/crypt/log_crypt.cc#L126-L205)、
[本地解码模块](../../xlog_decode/src/xlog_decode.cppm)。

| magic | 此分支实际 payload 行为 | 当前本项目 decode |
| --- | --- | --- |
| `0x06`、`0x08` | 同步原文 | 支持 |
| `0x07` | raw DEFLATE 后 TEA | 支持 |
| `0x09` | raw DEFLATE | 支持 |
| `0x0a`、`0x0b` | zstd 配置下的同步原文 | 仅识别结构，解码不支持 |
| `0x0c` | zstd 后 TEA | 仅识别结构，解码不支持 |
| `0x0d` | zstd | 仅识别结构，解码不支持 |

magic 常量名称不能单独判断 payload：`CryptSyncLog` 当前直接复制原文，TEA 分支被注释。
来源：[log_magic_num.h:23–39](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/crypt/log_magic_num.h#L23-L39)、
[log_crypt.cc:208–241](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/crypt/log_crypt.cc#L208-L241)、
[log_zlib_buffer.cc:97–102](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/src/log_zlib_buffer.cc#L97-L102)、
[log_zstd_buffer.cc:91–96](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/src/log_zstd_buffer.cc#L91-L96)。

### 压缩和加密

- zlib 路径使用负 window bits 的 raw DEFLATE，实际初始化使用 `Z_BEST_COMPRESSION`，每次写使用 `Z_SYNC_FLUSH`；
  `Flush` 释放 zlib 状态后封装尾字节，不额外写 `Z_FINISH` 数据。
  来源：[log_zlib_buffer.cc:43–94](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/src/log_zlib_buffer.cc#L43-L94)。
- zstd 路径每次使用 `ZSTD_e_flush`；其结束操作传入空输出 buffer，不能按普通“一次性完整结束的 zstd frame”假定互通。
  来源：[log_zstd_buffer.cc:42–88](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/src/log_zstd_buffer.cc#L42-L88)。
- 接收方公钥为 128 个十六进制字符，表示 secp256k1 的 `X || Y`。每个 `LogCrypt` 实例生成客户端 keypair，
  用接收方公钥与客户端私钥计算 ECDH；共享坐标前 16 字节用于 TEA，客户端公钥进入块头。
  异步 TEA 每 8 字节加密，不足 8 字节的尾部保留，后续写会接续未加密尾部。
  来源：[log_crypt.cc:95–123](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/crypt/log_crypt.cc#L95-L123)、
  [log_crypt.cc:244–278](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/crypt/log_crypt.cc#L244-L278)、
  [log_base_buffer.cc:304–319](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/src/log_base_buffer.cc#L304-L319)。
- `XLogConfig` 的公钥缺省为空；`LogCrypt` 遇到缺失/错误公钥或 keypair/ECDH 失败时保留非加密状态。
  序号和小时由上游内部状态/时钟写入，不是输入明文字节的组成部分。
  来源：[appender.h:52–60](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/appender.h#L52-L60)、
  [log_crypt.cc:57–123](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/crypt/log_crypt.cc#L57-L123)、
  [log_crypt.cc:183–201](https://github.com/rangerzy/mars/blob/6aa5b567afada93936252bac2f271c4b43eccd1f/mars/xlog/crypt/log_crypt.cc#L183-L201)。

## 当前项目的约束

encode 目前只有版本查询，尚无实际编码流程：[公开接口](../../xlog_encode/include/xlog_encode/xlog_encode.h)、
[模块](../../xlog_encode/src/xlog_encode.cppm)。

decode 每块独立初始化解压状态，因此编码产物不能跨 xlog 块延续压缩字典。它接受完整 raw DEFLATE 流和真正完整的 sync-flush 段，
拒绝截断、包装格式、伪 flush 和结束后的额外数据。
来源：[解压实现](../../xlog_decode/src/buffer_codec.cpp)、[边界测试](../../tests/decode_buffer.cpp)。

已有 C ABI、C99/C++11 消费、内外类型独立、类型与函数分文件、同库释放结果、独立 DLL/dylib 分发等约定，
应延续到 encode。
来源：[解码公开类型](../../xlog_decode/include/xlog_decode/xlog_decode_types.h)、
[解码内部类型](../../xlog_decode/src/decode_types.h)、[构建交付](../../cmake/XlogLibrary.cmake)。

## 建议，尚待用户确认

1. 首选纯 buffer 的一次性接口：一次输入任意二进制字节，输出一个完整、可独立解码的 xlog 块；调用方可拼接多个块。
   格式化、时钟、日志线程、mmap、文件轮转不进入 codec。
2. 首版提供原文 `0x08`、DEFLATE `0x09`、DEFLATE + TEA `0x07` 三种模式，先与现有 decode 互通。
   不把 `0x06` 的名字解释成真实加密；若要覆盖 zstd，需要同时设计相应解码扩展。
3. 默认采用与 Mars 一致的 raw DEFLATE / 完整 sync-flush 语义。每个输出块独立压缩；同一块内部是否允许多次 append，
   取决于接口形态的决策。
4. encode 配置应表达接收方公钥，decode 使用对应接收方私钥；默认公钥同样可留空，由头文件/调用配置调整。
   选择加密模式而公钥缺失或错误时，建议明确失败。临时私钥由库随机生成，不作为生产调用参数。
5. 继续采用公开 types/config/API 头和私有 types/方法文件，由 C ABI 适配层转换。
   编解码库保持独立；不要通过相互调用掩盖字节格式问题。
6. 验收除 `decode(encode(input)) == input` 外，还需独立已知向量及参考算法交叉验证，避免编解码两端共同出错。

首轮仅确认接口形态和兼容范围。密钥生命周期、序号/小时来源、空输入和超限拆块策略等，在相关前置选择确定后进入后续轮次。
