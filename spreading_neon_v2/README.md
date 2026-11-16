# spreading_decision NEON V2：两个独立优化变体

**[下载 V2 增量更新 ZIP](https://github.com/Git-Donner/libopus-opt/raw/refs/heads/main/spreading_neon_v2/opus-spreading-neon-v2.zip)**

本包以已应用的第一版 spreading NEON 优化为基础：
https://github.com/Git-Donner/libopus-opt/tree/6b79fb04c31d810112789fa5423afa473a25acf7/spreading_neon_float

`files/` 按源码相对路径提供本次全部新增/修改文件，不重复附带未修改的 V1 文件。
原 PVQ NEON/SVE 和 spreading 历史状态更新逻辑均保持不动。

**这是一轮待目标 CPU 实测的优化候选，没有声称已经获得原生 ARM 加速。**
四种组合已完成相同的功能验证，请分别测试，避免只测组合版后无法判断哪个改动有效。

## 两项改动

1. **精确幅值阈值**：对标准15种N，用单精度整数舍入模型找出原两次乘法判定的精确边界，
   在向量主循环中将 `(x*x)*N` 换成 `abs(x)` 与边界比较，消除两条依赖的FMUL，新增一条FABS。
   不使用未经修正的sqrt近似，也不修改计数和状态语义。
2. **向量内归约和判决**：N为4的倍数时，用配对加法组成 `[count0,count1,count2,0]`，
   在NEON内完成半数比较并求和，减少向标量寄存器搬运三份计数。
   需要高频统计时单独取出count0+count1。非4倍数沿用原归约及标量尾部。

精确阈值只保存 `.25` 的边界，其余两个由指数位精确缩放得到。
为直接按N索引，使用177项uint32表（708字节），15项有效，其他项为零哨兵；不做线性搜索。
不支持的N自动回到原两次乘法路径。未加入循环展开、提前终止或SVE。

## 如何选择四个变体

这两个开关是**写在编译器 CFLAGS 中的宏**，不是 configure 参数或CMake缓存选项：

| 名称 | CUTOFFS | VECTOR_FINISH | CFLAGS末尾追加内容 |
|---|---:|---:|---|
| 00 对照 | 0 | 0 | `-DOPUS_SPREAD_NEON_CUTOFFS=0 -DOPUS_SPREAD_NEON_VECTOR_FINISH=0` |
| 10 仅精确阈值 | 1 | 0 | `-DOPUS_SPREAD_NEON_CUTOFFS=1 -DOPUS_SPREAD_NEON_VECTOR_FINISH=0` |
| 01 仅向量判决 | 0 | 1 | `-DOPUS_SPREAD_NEON_CUTOFFS=0 -DOPUS_SPREAD_NEON_VECTOR_FINISH=1` |
| 11 两项组合 | 1 | 1 | `-DOPUS_SPREAD_NEON_CUTOFFS=1 -DOPUS_SPREAD_NEON_VECTOR_FINISH=1` |

不写宏时默认11。00关闭两个新增算法，但不承诺编译器生成的机器码与V1逐字节相同；
严格比较原V1时还应保留V1源码/构建目录。
改变宏后必须干净重编译或使用独立构建目录，不能只修改环境变量再运行一个不重建对象的make。

## 应用与编译

保留第一版源码作对照。原源码已配置时，建议覆盖前先 `make distclean`，然后在完整源码根目录：

```sh
patch --dry-run -p1 < /解压目录/spreading-neon-v2.patch
patch -p1 < /解压目录/spreading-neon-v2.patch

autoreconf -isf
```

或者按相对路径合入 `files/`。本次改了头文件分发列表、Makefile.am和CMake测试注册，
请重新生成/配置，不要把 files/ 单独当作完整Opus库。

例如构建10（只启用精确阈值）：

```sh
CC=gcc \
CFLAGS="-O3 -march=armv8.2-a+sve -mtune=native -fno-semantic-interposition -fvisibility=hidden -DOPUS_SPREAD_NEON_CUTOFFS=1 -DOPUS_SPREAD_NEON_VECTOR_FINISH=0" \
./configure --disable-shared --enable-static --disable-fixed-point --disable-sve

make -j8
make check -j8
```

其余三种组合只替换末尾两个宏，其他条件一致。上例 `--disable-sve` 管理的是之前的PVQ SVE；
若你保留它，请在所有版本统一改成 `--enable-sve`。本次统计内核始终使用128-bit NEON。
全局 `-march=...+sve` 仍允许编译器在其它地方生成SVE，不能据此声称整库可运行在无SVE机器上。

CMake同样把两个宏放进 `-DCMAKE_C_FLAGS="..."`，重新运行 `cmake -S ... -B ...` 后再build。
源码已有原来的两个spreading测试；本次新增test_spreading_cutoffs。
静态Linux/ARM NEON测试构建可运行：

```sh
ctest --test-dir /你的CMake构建目录 \
  -R 'test_(unit_spreading_neon|encoder_spreading_neon|spreading_cutoffs)$' --output-on-failure
```

## Autotools下新增边界测试

在源码内构建目录执行（源码外构建需调整源码路径，保留-I.指向构建config.h）：

```sh
gcc -O2 -frounding-math -ffp-contract=off -DHAVE_CONFIG_H -I. -Iinclude -Icelt \
  celt/tests/test_spreading_cutoffs.c celt/tests/test_spreading_reference.c \
  .libs/libopus.a -lm -o test_spreading_cutoffs
./test_spreading_cutoffs
```

V1的单位测试和真实编码调用证明也应重新链接到各自新静态库，编译命令沿用V1说明。
上游make check不会自动执行本次手动新增测试。调用证明使用无LTO构建，计数包装器不进入性能二进制。

## 浮点环境与回退

精确阈值路径目前用于AArch64 GCC/Clang、未启用 `__FAST_MATH__` 的配置。
每次完整统计扫描读取一次FPCR：非round-to-nearest或开启浮点异常陷阱时，自动执行原乘法路径。
ARM32及其他未满足编译条件的平台同样回退；本次实际验证是AArch64 GCC14。

FZ/DN等对这些远离subnormal的分类边界不改变输出；NaN/Inf和正负零做了额外比较测试。
**精确性承诺针对分类、聚合、编码结果和历史状态，不包括FPSR粘滞异常标志轨迹。**
去掉乘法后可能少产生inexact/underflow/overflow标志；开启异常陷阱时已回退以保留原运算。
除整体fast-math外，其他人为放宽浮点语义的选项也不属于已验证配置；需要原精确语义时不要使用它们。

## 如何测量收益

- 保存应用spreading补丁前的原始基线、V1，以及00/10/01/11独立构建出的静态库和测试二进制。最终保留方案应对原始基线有端到端收益。
- 完全相同的编译器、全局选项、PVQ/SVE开关、CELT编码配置、输入、CPU核、预热和重复次数。
- 测试框架必须重新链接对应静态库。test_1的启动器内嵌worker，仅替换磁盘上的.a不会更新旧启动器。
- 优先比较CELT encode的耗时和cycles/frame；查看 `spreading_statistics_neon`，并与外层 `spreading_decision` 合计。
- 分支失败率不是这轮的唯一指标。外部perf覆盖整个worker时还会包含预热和计时之外的验证解码。
- 若10有效而01无效，保留10；反之保留01；若均无效，回退对照。没有按向量宽度推算的加速承诺。

实际验证范围和原始日志见 [VALIDATION.md](VALIDATION.md) 与 validation/。
