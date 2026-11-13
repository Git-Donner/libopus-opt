# 浮点 PVQ 的 256-bit SVE 增量实现

本包以你已应用并验证的 NEON 版本为基础，优化 `op_pvq_search` 的贪心候选扫描。
原 `celt/arm/vq_neon_intr.c` 未改动，SHA-256 为：
`689522b99cf2aa1a4bdb55137b7e9f9a2447ef3446da78873766ef24c8976565`。

## 包含什么

- `files/`：所有新增和修改文件的完整内容，保留源码相对路径。
- `pvq-sve256.patch`：相对于当前 NEON 版本的增量补丁。不是相对于原始未打 NEON 补丁的 Opus。
- `manifest.json`：修改前/后 SHA-256、参考提交及必需的 NEON 基础文件哈希。
- `ALGORITHM.zh-CN.md`：算法、并列分数处理、边界和运行时分派说明。
- `VALIDATION.zh-CN.md`：实际测试结果、范围和限制。
- `INTRINSIC_REFERENCES.md`：查阅的 ARM ACLE 和 Linux 官方资料。
- `validation/`：测试日志、实际编译命令、反汇编，以及额外的 RTCD 分派测试源码。
- `SHA256SUMS`：本包文件的校验和。

新增核心文件为 `celt/arm/vq_sve256.c` 和 `celt/arm/vq_sve_dispatch.c`。
`celt/arm/vq_arm.h` 增加声明与选择宏，`arm_celt_map.c` 更新非 presumed-NEON 的分派表。
`configure.ac`、`Makefile.am`、`celt_sources.mk` 接入 Autotools；
`CMakeLists.txt`、`cmake/OpusSources.cmake` 接入 CMake。
两个新增测试源码在 `celt/tests/`。本次接入并验证 Autotools 和 CMake；Meson 继续使用原 NEON 路径。

## 应用

必须在已应用上一版 NEON 补丁的、匹配版本源码上操作。先备份源码。

```sh
cd /你的/opus源码目录
patch --dry-run -p1 < /解压目录/pvq-sve256.patch
patch -p1 < /解压目录/pvq-sve256.patch
```

如果 dry-run 报上下文不匹配，先核对版本和本地改动。`files/` 可供逐文件对照，
不要直接覆盖不匹配版本的整套构建文件。测试源码只用于验证，不进入库的性能计时路径。

## 按原来的 Autotools 方式编译

如果源码已经配置过，先执行 `make distclean`。由于修改了 `configure.ac` 和
`Makefile.am`，必须重新生成构建文件：

```sh
# 需要本机安装 autoconf、automake、libtool，以及正常的 C 编译工具。
autoreconf -isf
```

这是本次实际验证过的生成方法，不调用 `autogen.sh` 的模型下载步骤。
以下配置不启用 DNN 特性；不要将“跳过模型下载”推广到启用 DNN 的构建。
然后使用你原来的优化选项：

```sh
CC=gcc \
CFLAGS="-O3 -march=armv8.2-a+sve -mtune=native -fno-semantic-interposition -fvisibility=hidden" \
./configure --disable-shared --enable-static --disable-fixed-point --enable-sve

make -j8
make check -j8

nm .libs/libopus.a | grep op_pvq_search
```

`--enable-sve` 要求探测成功，否则 configure 报错，避免以为启用实际却没编进去。
省略此选项时默认自动探测。`--disable-sve` 恢复原 NEON 分派，不编译 SVE 内核及其包装器。
重编译纯 NEON 对照库时用相同的编译器、CFLAGS 和其它 configure 参数，仅将
`--enable-sve` 改为 `--disable-sve`，并使用独立、干净的构建目录和静态库。

构建系统只给 `vq_sve256.c` 追加：
`-march=armv8-a+sve -msve-vector-bits=scalable`。这足以生成所需指令，保留你的 `-O3` 和
`-mtune` 等选项。不要手工改成 `-msve-vector-bits=256`：实际线程 VL 的回退检查必须保留。
实现只在实际 VL=32 字节且 N>=8 时使用八路候选扫描，其他情况回退现有 NEON。
仅需 SVE，不要求 SVE2。

如果希望整库可在无 SVE 的 AArch64 机器运行，全局 CFLAGS 应使用基线 ISA，
例如 `-O3 -march=armv8-a -mtune=generic`，由单文件选项启用 SVE 内核。
全局 `-march=armv8.2-a+sve` 会允许编译器在库的其他位置也生成 SVE；
本包装器的硬件回退不能改变全局编译选项对整库的要求。

## 在 Autotools 编译后运行两个新增测试

以下命令在源码内构建目录执行；采用源码外构建时，将源码参数和 include 路径换为源码的绝对路径，保留 `-I.` 指向构建目录的 config.h。
测试程序自身不需要 `-march=...+sve`。

```sh
# 精确 N 长度边界、脉冲/能量、NEON 一致性、线程 VL 变化。
gcc -O2 -DHAVE_CONFIG_H -I. -Iinclude -Icelt \
  celt/tests/test_unit_pvq_sve256.c .libs/libopus.a \
  -lm -pthread -o test_unit_pvq_sve256
./test_unit_pvq_sve256 32

# 真实编码器调用证明；比较两套独立编码器和解码器的全部 packet、PCM、final range。
gcc -O2 -Iinclude celt/tests/test_encoder_dispatch_sve256.c \
  .libs/libopus.a -Wl,--wrap=op_pvq_search_sve256 -lm \
  -o test_encoder_dispatch_sve256
./test_encoder_dispatch_sve256 32
```

第二个程序成功时应有 `HWCAP_SVE=1 VL=32` 且 `actual256` 大于 0。
测试参数 32 的单位是字节；它会为测试线程请求 256-bit VL。
返回 77 或 `SKIP` 表示相应测试未执行，不能作为通过证明。
调用证明使用 GNU 链接器 `--wrap`，请用无 LTO 的验证构建，避免跨文件内联绕开包装器；
正常性能测试链接未经包装的静态库。

## CMake 方式

```sh
cmake -S . -B build-sve256 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_C_FLAGS="-O3 -march=armv8.2-a+sve -mtune=native -fno-semantic-interposition -fvisibility=hidden" \
  -DOPUS_FIXED_POINT=OFF -DOPUS_BUILD_SHARED_LIBRARY=OFF \
  -DOPUS_ARM_SVE=ON -DOPUS_BUILD_TESTING=ON
cmake --build build-sve256 -j8
ctest --test-dir build-sve256 -R 'test_(unit_pvq|encoder_dispatch)_sve256' --output-on-failure
```

CMake 默认尝试启用 SVE；务必确认配置输出有 `OPUS_COMPILER_SUPPORT_SVE256 - Success`，
并检查归档符号。`-DOPUS_ARM_SVE=OFF` 关闭新增实现；不满足平台或编译器条件时不会启用。
CMake 的静态测试构建会自动加入这两个测试。

## 性能测试注意点

沿用你的框架，分别链接干净构建出的纯 NEON 与 SVE 静态库，使用相同编译选项、
输入、用例、CPU 核、预热和重复次数。仅 CELT 编码及 HYBRID 中的 CELT 编码部分可能受益。
解码和 SILK 场景用于确认没有意外回退或回归。

系统默认 VL 可用 `cat /proc/sys/abi/sve_default_vector_length` 查看，32 表示 256-bit。
VL 是每线程状态，默认值不等于所有已有线程的实际 VL；本实现每次进入 SVE 函数用 CNTW 检查。
测试程序会主动设置自己的 VL，但不会替另一个性能测试进程设置 VL。

本包完成的是功能验证和构建验证。QEMU 时间不能用作服务器性能数据；
没有宣称端到端加速比，也不保证所有 N、K 下 SVE 都比 NEON 快。
