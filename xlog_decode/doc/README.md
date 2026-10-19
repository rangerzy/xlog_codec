# xlog_decode

使用 C++26 命名 Modules 实现的 xlog 解码动态库骨架，当前版本 `0.1.0`。
本阶段仅提供版本查询，尚未实现真实 xlog 解码。

```c
#include <xlog_decode/xlog_decode.h>
const char *version = xlog_decode_version();
```

返回 `0.1.0`，字符串由库持有，在库保持加载期间有效，不得修改或释放。
API 为 C ABI，头文件兼容 C 与 C++。内部 Modules 与反射不需要消费者参与编译。

```cmake
find_package(xlog_decode 0.1 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE xlog::decode)
```

将本包根目录加入 `CMAKE_PREFIX_PATH`。头文件在 `include`，CMake package 在
`cmake`，动态库在 `lib`（Windows DLL 在 `bin`，导入库在 `lib`）。
直接构建 `xlog_decode` 即可得到以上完整目录；头文件调用示例见上方代码。

Windows 运行时需使 `bin` 和匹配的 UCRT64 GCC 运行库可被系统找到。
macOS 需要匹配的 GCC 运行库；包不静态捆绑 GCC 运行库。
