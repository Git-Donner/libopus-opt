# PVQ SVE256：剩余 NEON 运算转换更新

**[下载本次更新 ZIP](https://github.com/Git-Donner/libopus-opt/raw/refs/heads/main/pvq_sve256_all/opus-pvq-sve256-all-update.zip)**

本次只修改 **`celt/arm/vq_sve256.c`**。完整代码在
[`files/celt/arm/vq_sve256.c`](files/celt/arm/vq_sve256.c)。
没有再次提供未改变的 `.h`、构建文件、分派器、NEON 实现或测试源码。

前提是已经应用上一版 SVE256 包：
https://github.com/Git-Donner/libopus-opt/tree/4a7e0bf2355088db632cd532e15a18586cc5685f/pvq_sve256_float

## 这次改了什么

- 移除 `arm_neon.h`、NEON 向量类型及全部 NEON intrinsics。
- 用 SVE 实现输入绝对值与符号保存、初始投影、倒数估计、浮点/整数转换和最后的符号恢复。
- 初始化和投影每次读写八个元素，以谓词处理精确 N 长度的尾部，本地数组也不再要求 N+3 填充。
- 浮点累计保留四个逻辑通道，以两个有序更新消费八个元素；`pvq_sum4_f32` 通过 SVE TBL、FADD 和 LASTB 保留原求和顺序。
- 脉冲总数用 SVE 整数加法和 ADDV 归约。
- 原有八路贪心评分及并列分数规则保留。

256-bit 主分支的向量运算全部采用 SVE。标量状态更新仍使用标量计算。
无 SVE、线程 VL 不为 256-bit、N<8 时继续回退原 NEON；原 NEON 文件没有修改。
这次没有扩大 SVE 的平台/长度适用范围。

## 替换和重新编译

将本目录 `files/celt/arm/vq_sve256.c` 替换到你已应用上一版 SVE 的完整源码的同名路径，
或者在源码根目录使用增量补丁：

```sh
patch --dry-run -p1 < /本更新目录/pvq-sve256-all.patch
patch -p1 < /本更新目录/pvq-sve256-all.patch
```

如果已有 SVE 构建采用你之前的 GCC `-O3` 选项，可以直接：

```sh
make -j8
make check -j8
```

本次未修改 `configure.ac`/`Makefile.am`，不需要因这次更新重新运行 autoreconf。
原 `--enable-sve` 配置及其它选项继续使用。若此前没有启用 SVE，应先按上一包说明配置。
CMake 用户同样只需重新构建。修改的文件必须在完整源码内编译，不能单独把 files/ 当作源码库。

## 乘加收缩选项：请保留一致的参考配置

GCC 14 的普通 `-O3` 会将原 NEON 投影中的 `vadd(vmul)` 合并为 FMLA。
SVE 的两个独立 ACLE 乘、加 intrinsic 不会自动发生同样合并，因此本次默认显式使用 `svmla`。
这与本次验证的 GCC `-O3` NEON 参考一致；你原来的普通 GCC `-O3` 选项无需增加参数。

如果你用 **`-ffp-contract=off`** 编译 NEON 参考，请给这份 SVE 实现同时增加：

```sh
-DOPUS_PVQ_SVE_NO_FMA
```

例如重新配置时 CFLAGS 中同时包含：

```sh
CFLAGS="-O3 -march=armv8.2-a+sve -mtune=native -fno-semantic-interposition -fvisibility=hidden -ffp-contract=off -DOPUS_PVQ_SVE_NO_FMA"
```

该开关只使投影累计采用分别舍入的 SVE FMUL+FADD；不影响硬件分派。
未优化的调试构建或其它编译器也可能具有不同的收缩策略；需要与参考库匹配。
本次已验证普通 GCC `-O3` 默认分支、以及 `-ffp-contract=off` 配合此开关的分支。
不要将“实测一致”推广为所有编译器/优化选项下均逐字节一致。

## 验证

12,352 组算子用例、300 组真实 CELT 编码配对测试、128/512-bit 和无 SVE 回退、
UBSan 均已通过，详见 [VALIDATION.md](VALIDATION.md)。测试使用上一包未修改的两个测试源码。
重新运行时务必重新链接验证程序到新静态库，避免运行上次静态链接的旧二进制。
其完整编译命令继续使用上一包 README。

验证环境为 AArch64 GCC/QEMU；本次没有测量原生 ARM 服务器的性能收益。
全部换成 SVE 不保证端到端更快，请用相同参数、同一 CPU 核比较旧混合版和本版。
