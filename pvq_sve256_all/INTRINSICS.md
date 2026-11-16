# 本次新增的 SVE 指令映射

2026-11-16 重新下载并查阅 ARM 官方 ACLE 文档：
https://raw.githubusercontent.com/ARM-software/acle/main/main/acle.md
在线版本：https://arm-software.github.io/acle/main/acle.html

| 本次转换使用的 intrinsic | 对应指令/用途 |
|---|---|
| `svabs_f32_z` | FABS，绝对值；inactive通道归零 |
| `svcmplt_n_f32`、`svsel_u32`、`svst1_u32` | 原 x<0 符号掩码保存；不将负零视为负数 |
| `svrecpe_f32` | FRECPE，倒数估计 |
| `svrecps_f32` | FRECPS，倒数 Newton 修正步，与原 NEON 修正次序一致 |
| `svcvt_s32_f32_z` | FCVTZS，向零取整，正数等价floor；inactive归零 |
| `svcvt_f32_s32_z` | SCVTF，脉冲整数转float32；inactive归零 |
| `svmla_f32_m` | FMLA，显式融合乘加，匹配GCC正常O3的NEON收缩 |
| `svmul_f32_x` + `svadd_f32_m` | FMUL+FADD，NO_FMA分支的两次舍入 |
| `svext_f32(...,4)` | EXT，提取后四个float32，用于四个逻辑累计通道的第二次更新 |
| `sveor_n_u32_x` | EOR，产生XOR2/XOR1归约排列下标 |
| `svtbl_f32` | TBL，按原 `(0+2)+(1+3)` 顺序重排归约 |
| `svlastb_f32` | LASTB，提取最后一个active通道；用仅lane0有效谓词取第0通道 |
| `svaddv_s32` | SADDV，脉冲计数的整数归约 |
| `svcmpne_n_u32`、`svneg_s32_x`、`svsel_s32` | NEG与谓词选择，恢复脉冲符号 |
| `svld1_*`、`svst1_*`、`svwhilelt_b32_s32` | LD1W/ST1W/WHILELT，只访问有效N个元素 |

`svlasta` 取的是最后active元素的后一个元素，不能误用来提取lane0。
`_x` 的inactive通道结果不指定，不能拿来参与后续整数和浮点归约；本次转换显式用_z或零填充加载处理这些通道。

本次所用全部intrinsic签名均经AArch64 GCC14实际编译确认，反汇编见validation/sve-all-assembly.txt。
浮点乘加收缩选项参考：https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
