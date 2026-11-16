# spreading_decision：128-bit NEON 优化包

**[下载 ZIP](https://github.com/Git-Donner/libopus-opt/raw/refs/heads/main/spreading_neon_float/opus-spreading-neon-float.zip)**

`files/` 内按完整源码的相对路径保存全部新增、修改文件。先核对版本并备份，再合入。
本包基于你此前已经应用 PVQ NEON/SVE 改动的源码；不会再次提供或替换未修改的 PVQ 文件。
`manifest.json` 记录每个文件修改前后的 SHA-256，增量补丁以此次修改前的工作区快照为基准，
仓库根目录的提交本身并不等于已经合入此前全部交付包。

## 实现范围

- 新增 `celt/arm/bands_neon_intr.c`：每次处理四个 float32，三个整数向量累计阈值计数，频带结束时归约；不足四个样本用标量安全处理。
- `(x*x)*N` 的运算次序、三个严格小于比较、整数计数和每声道每频带的除法次序保持原样。
- `update_hf=0` 时省去不会被使用的高频除法和累加。
- 完整频带/声道扫描只做一次架构分派；历史平均、迟滞和最终判决共用原来的标量逻辑。
- `spreading_decision` 的内部接口增加末尾 `arch` 参数，编码器调用点传入已有 `st->arch`。
- ARM presumed-NEON 直接调用新内核；RTCD 选择 C 或 NEON；x86 和固定点继续走 C。
- 未实现 SVE、提前终止、声道重排、大幅展开或近似除法。原 PVQ 优化保持不动。

这里的高频条件仍是 `i>nbEBands-4`，标准模式实际覆盖最后三个频带；原归一化分母也保留。
提前返回仍不修改任何历史状态。没有高频贡献但 `update_hf=1` 时，仍执行历史衰减和 tapset 更新。

## 文件布局与应用

`files/celt/...` 对应源码中的 `celt/...`；`files/CMakeLists.txt` 等对应源码根目录的同名文件。
新增测试及独立的原实现参考代码也保留在 `files/celt/tests/`。
说明、补丁、日志是交付材料，不需要复制进编解码库。

在尚未覆盖源码之前，如果原目录已配置编译过，先执行 `make distclean`，然后应用补丁：

```sh
cd /你的/完整Opus源码目录
patch --dry-run -p1 < /解压目录/spreading-neon.patch
patch -p1 < /解压目录/spreading-neon.patch
```

也可逐文件合入 `files/`，但要核对本地改动，避免覆盖自己的修改。
不能只替换静态库中的一个对象：内部函数签名和调用点都改变，必须重新编译库。

## Autotools 编译

本次更改了源文件列表、头文件列表和 Makefile.am，先重新生成构建文件：

```sh
autoreconf -isf
```

这不会运行 autogen.sh 的模型下载步骤。需要本机的 autoconf、automake、libtool。
下面示例关闭的是之前 PVQ 的 SVE 路径，以便单独评估本次 NEON 改动：

```sh
CC=gcc \
CFLAGS="-O3 -march=armv8.2-a+sve -mtune=native -fno-semantic-interposition -fvisibility=hidden" \
./configure --disable-shared --enable-static --disable-fixed-point --disable-sve

make -j8
make check -j8
```

如果希望继续启用之前的 PVQ SVE，把上例改为 `--enable-sve`；基线和优化版必须保持同一设置。
本次 NEON spreading 不要求启用 SVE，也没有新增 configure 开关。
`-march=armv8.2-a+sve` 是你原有的全局选项；若构建可在无 SVE 的 AArch64 机器运行的库，
请在两个版本中一致地使用基线 ISA，例如 `-march=armv8-a`。

`make check` 运行上游测试套件。新增两项测试在 Autotools 下需要单独编译和执行：

```sh
gcc -O2 -DHAVE_CONFIG_H -I. -Iinclude -Icelt \
  celt/tests/test_unit_spreading_neon.c celt/tests/test_spreading_reference.c \
  .libs/libopus.a -lm -o test_unit_spreading_neon
./test_unit_spreading_neon

gcc -O2 -DHAVE_CONFIG_H -I. -Iinclude -Icelt \
  celt/tests/test_encoder_spreading_neon.c celt/tests/test_spreading_reference.c \
  .libs/libopus.a -Wl,--wrap=spreading_statistics_neon -lm \
  -o test_encoder_spreading_neon
./test_encoder_spreading_neon
```

以上按源码内构建写出；源码外构建时保留 `-I.` 指向构建目录，将源码和 include 参数改成源码绝对路径。
不要在包含 OPUS_BUILD 的 config.h 之外再次手工添加 `-DOPUS_BUILD`。
验证程序必须重新链接新静态库，不要直接使用以前静态链接的旧二进制。

## CMake 编译

源列表有变化，请重新配置，再构建，不要只运行一次旧缓存下的 build：

```sh
cmake -S . -B build-spreading \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_C_FLAGS="-O3 -march=armv8.2-a+sve -mtune=native -fno-semantic-interposition -fvisibility=hidden" \
  -DOPUS_FIXED_POINT=OFF -DOPUS_BUILD_SHARED_LIBRARY=OFF \
  -DOPUS_ARM_SVE=OFF -DOPUS_BUILD_TESTING=ON
cmake --build build-spreading -j8
ctest --test-dir build-spreading -R 'test_(unit|encoder)_spreading_neon' --output-on-failure
```

本次的静态 Linux/ARM NEON CMake 测试构建自动加入这两项测试。
同理，`OPUS_ARM_SVE` 只控制之前的 PVQ SVE；两套性能构建必须一致。
新增源文件挂在现有 `CELT_SOURCES_ARM_NEON_INTR` 组中，沿用已有 NEON 编译开关；Meson 同样读取此源列表，
但本次没有完成 Meson 构建验证。

## 验证与性能测量

详见 [VALIDATION.md](VALIDATION.md)。已通过 150,016 组直接验证、300 组真实编码配对测试、
RTCD 分派和 UBSan。原生 ARM 性能没有在本环境测量，不提供加速比。

实际收益重点看 CELT 编码长块。HYBRID、SILK、解码，以及被复杂度/短块等条件绕过本函数的情况，
不能假设会获得本次收益。调用证明会统计真正进入扫描内核的次数和处理的系数数量。

公平对比时保留应用补丁前后的两个源码/构建目录，只改变本次补丁；使用相同编译器、CFLAGS、
PVQ/SVE 开关、输入和测试配置。测试框架重新链接各自静态库后，在同一 CPU 核、相同预热和重复设置下测量。
除 cycles 和时间外可观察 instructions、branches、branch-misses，但以整体编码耗时为最终依据。

链接包装器及调用计数只用于验证，不进入性能二进制。调用证明使用无 LTO 的验证构建，
避免跨文件内联绕开 --wrap；性能构建如使用 LTO，两个版本必须保持一致。
