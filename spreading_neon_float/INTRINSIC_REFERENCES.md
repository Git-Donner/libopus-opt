# NEON 指令依据

参考 ARM 官方 Advanced SIMD intrinsics 文档：
https://arm-software.github.io/acle/neon_intrinsics/advsimd.html

本次核对的函数及语义：

| Intrinsic | 指令/作用 |
|---|---|
| `vld1q_f32` | 四个float32加载；只在j+3<N时使用 |
| `vmulq_f32` | FMUL Vd.4S；依次计算x*x，再乘N |
| `vcltq_f32` | FCMGT（交换操作数）；严格小于，结果为0或UINT32_MAX |
| `vsubq_u32` | SUB Vd.4S；无符号计数器减去全1掩码，相当于加1 |
| `vaddvq_u32` | AArch64 ADDV；频带结束时归约整数计数 |
| `vpadd_u32`、`vget_low/high_u32` | ARM32分支中的配对归约；本次实际运行验证为AArch64 |

没有浮点水平求和，也没有新增FMA；最终累计是精确的整数计数。
尾部不用补零向量比较，因为补入的零会错误地满足所有三个阈值。
基线ISA构建的实际反汇编保存在validation/neon-assembly.txt。
