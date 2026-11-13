# 查阅资料和指令对应

实际重新下载并查阅了 ARM 官方 ACLE 文档（2026-11-13）：
- https://arm-software.github.io/acle/main/acle.html
- https://raw.githubusercontent.com/ARM-software/acle/main/main/acle.md
- Intrinsics 查询入口：https://developer.arm.com/architectures/instruction-sets/intrinsics/

实现使用 ACLE 的 C SVE intrinsics。下表的指令名对应 ACLE 的 SVE instruction/intrinsic 表；
具体生成代码可见 `validation/kernel-assembly.txt`，所有签名均已由 AArch64 GCC 14.2 实际编译验证。
编译器可折叠常量和指令，并非每次 intrinsic 调用都会独立生成一条指令。

| 实际使用的 intrinsic | 指令/语义 | 本实现用途 |
|---|---|---|
| `svcntw` | CNTW，当前 VL 的 32-bit 元素数 | 每线程 VL 必须等于 8 |
| `svptrue_b32` | PTRUE | 所有 float32 通道谓词 |
| `svwhilelt_b32_s32` | WHILELT，按有符号 32-bit 比较形成谓词 | 尾部及前四通道 |
| `svld1_f32` | LD1W，受谓词控制的加载 | 不跨越 N 的有效范围 |
| `svdup_n_f32`, `svdup_n_s32` | DUP/MOV 广播 | 初值、xy/yy、哨兵 |
| `svindex_s32` | INDEX | 候选下标 0..7 |
| `svadd_f32_x` | FADD | 分子和分母 |
| `svadd_n_s32_x` | ADD | 下标加 8 |
| `svmul_f32_x` | FMUL | 估计修正和分数 |
| `svrsqrte_f32` | FRSQRTE，倒平方根估计 | 对应 NEON `vrsqrteq_f32` |
| `svrsqrts_f32` | FRSQRTS，倒平方根 Newton 修正步 | 对应 NEON `vrsqrtsq_f32` |
| `svcmpgt_f32` | 浮点严格大于比较 | 保留各通道第一个最大值 |
| `svcmpeq_n_f32` | 浮点等于比较 | 找到全局最高分通道 |
| `svcmplt_n_s32` | 有符号整数小于比较 | 去除 INT_MAX 哨兵 |
| `svsel_f32`, `svsel_s32` | SEL，按谓词选通道 | best/pos、无效分母和哨兵选择 |
| `svmaxv_f32` | FMAXV | 分数最大值归约 |
| `svext_s32(...,4)` | EXT，拼接提取，偏移 4 个 int32 元素 | 八通道旋转四位；反汇编为偏移 16 字节 |
| `svmin_s32_x` | SMIN | 合并模 4 同组的最早位置 |
| `svmaxv_s32` | SMAXV | 四组并列胜者位置取最大 |

`_x` 表示谓词未激活的结果通道可以是不指定值，不能误当作归零。
本实现仅在受 active 谓词限制的比较中使用这些分数；不受谓词控制的 FRSQRTE/FRSQRTS
所需分母则显式将无效通道设置为 1，并在整个向量上进行修正乘法。

Linux 官方 SVE userspace ABI：
- https://www.kernel.org/doc/html/latest/arch/arm64/sve.html
- https://raw.githubusercontent.com/torvalds/linux/master/Documentation/arch/arm64/sve.rst

该文档明确：HWCAP_SVE 报告用户态 SVE 支持；VL 是 per-thread 状态；
PR_SVE_SET_VL/PR_SVE_GET_VL 的长度单位为字节。硬件能力缓存与每线程 VL 检查因此分开。

编译选项参考（scalable VL，避免编译器将 CNTW 折叠成常量 8）：
https://gcc.gnu.org/onlinedocs/gcc/AArch64-Options.html

保留的 NEON 部分沿用上一版的 ARM 官方 Advanced SIMD 引用：
https://arm-software.github.io/acle/neon_intrinsics/advsimd.html
