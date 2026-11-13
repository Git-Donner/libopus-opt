# Official Arm NEON intrinsic references

Fetched online from: https://raw.githubusercontent.com/ARM-software/acle/main/neon_intrinsics/advsimd.md
Human-facing reference: https://arm-software.github.io/acle/neon_intrinsics/advsimd.html
Downloaded document SHA256: 722dcf758d7a6dbbab54210d55bbce73a6db7691bb4092112375bf096563fd73

The quoted rows below are from the Arm-maintained ACLE intrinsic reference. These are actual online-retrieved rows, not inferred mappings. See each row for architectural availability.

## vld1q_f32 (document line 3857)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vld1q_f32" target="_blank">vld1q_f32</a>(float32_t const *ptr)</code>                                                                                                                                       | `ptr -> Xn`                                                                                                                                 | `LD1 {Vt.4S},[Xn]`              | `Vt.4S -> result`                                                                                                   | `v7/A32/A64`              |

## vst1q_f32 (document line 4336)

| <code>void <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vst1q_f32" target="_blank">vst1q_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32_t *ptr,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t val)</code>                                                          | `val -> Vt.4S`<br>`ptr -> Xn`                                                                                                               | `ST1 {Vt.4S},[Xn]`              |          | `v7/A32/A64`              |

## vld1q_s32 (document line 3841)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vld1q_s32" target="_blank">vld1q_s32</a>(int32_t const *ptr)</code>                                                                                                                                           | `ptr -> Xn`                                                                                                                                 | `LD1 {Vt.4S},[Xn]`              | `Vt.4S -> result`                                                                                                   | `v7/A32/A64`              |

## vst1q_s32 (document line 4320)

| <code>void <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vst1q_s32" target="_blank">vst1q_s32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; int32_t *ptr,<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t val)</code>                                                              | `val -> Vt.4S`<br>`ptr -> Xn`                                                                                                               | `ST1 {Vt.4S},[Xn]`              |          | `v7/A32/A64`              |

## vld1q_u32 (document line 3849)

| <code>uint32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vld1q_u32" target="_blank">vld1q_u32</a>(uint32_t const *ptr)</code>                                                                                                                                         | `ptr -> Xn`                                                                                                                                 | `LD1 {Vt.4S},[Xn]`              | `Vt.4S -> result`                                                                                                   | `v7/A32/A64`              |

## vst1q_u32 (document line 4328)

| <code>void <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vst1q_u32" target="_blank">vst1q_u32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; uint32_t *ptr,<br>&nbsp;&nbsp;&nbsp;&nbsp; uint32x4_t val)</code>                                                            | `val -> Vt.4S`<br>`ptr -> Xn`                                                                                                               | `ST1 {Vt.4S},[Xn]`              |          | `v7/A32/A64`              |

## vdupq_n_f32 (document line 3281)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vdupq_n_f32" target="_blank">vdupq_n_f32</a>(float32_t value)</code>                                                                                    | `value -> rn`                        | `DUP Vd.4S,rn`          | `Vd.4S -> result`  | `v7/A32/A64`              |

## vdupq_n_s32 (document line 3267)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vdupq_n_s32" target="_blank">vdupq_n_s32</a>(int32_t value)</code>                                                                                        | `value -> rn`                        | `DUP Vd.4S,rn`          | `Vd.4S -> result`  | `v7/A32/A64`              |

## vabsq_f32 (document line 899)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vabsq_f32" target="_blank">vabsq_f32</a>(float32x4_t a)</code> | `a -> Vn.4S`           | `FABS Vd.4S,Vn.4S`    | `Vd.4S -> result`  | `v7/A32/A64`              |

## vrecpeq_f32 (document line 1031)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vrecpeq_f32" target="_blank">vrecpeq_f32</a>(float32x4_t a)</code>                                                                         | `a -> Vn.4S`                 | `FRECPE Vd.4S,Vn.4S`       | `Vd.4S -> result` | `v7/A32/A64`              |

## vrecpsq_f32 (document line 1037)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vrecpsq_f32" target="_blank">vrecpsq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b)</code> | `a -> Vn.4S`<br>`b -> Vm.4S` | `FRECPS Vd.4S,Vn.4S,Vm.4S` | `Vd.4S -> result` | `v7/A32/A64`              |

## vrsqrteq_f32 (document line 1050)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vrsqrteq_f32" target="_blank">vrsqrteq_f32</a>(float32x4_t a)</code>                                                                         | `a -> Vn.4S`                 | `FRSQRTE Vd.4S,Vn.4S`       | `Vd.4S -> result` | `v7/A32/A64`              |

## vrsqrtsq_f32 (document line 1056)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vrsqrtsq_f32" target="_blank">vrsqrtsq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b)</code> | `a -> Vn.4S`<br>`b -> Vm.4S` | `FRSQRTS Vd.4S,Vn.4S,Vm.4S` | `Vd.4S -> result` | `v7/A32/A64`              |

## vcvtq_s32_f32 (document line 2061)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vcvtq_s32_f32" target="_blank">vcvtq_s32_f32</a>(float32x4_t a)</code>                                                                                     | `a -> Vn.4S`                   | `FCVTZS Vd.4S,Vn.4S`    | `Vd.4S -> result` | `v7/A32/A64`              |

## vcvtq_f32_s32 (document line 2153)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vcvtq_f32_s32" target="_blank">vcvtq_f32_s32</a>(int32x4_t a)</code>                                                                                     | `a -> Vn.4S`                   | `SCVTF Vd.4S,Vn.4S`     | `Vd.4S -> result` | `v7/A32/A64`              |

## vcgtq_f32 (document line 1469)

| <code>uint32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vcgtq_f32" target="_blank">vcgtq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b)</code> | `a -> Vn.4S`<br>`b -> Vm.4S`   | `FCMGT Vd.4S,Vn.4S,Vm.4S`   | `Vd.4S -> result`  | `v7/A32/A64`              |

## vcltq_f32 (document line 1513)

| <code>uint32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vcltq_f32" target="_blank">vcltq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b)</code> | `a -> Vn.4S`<br>`b -> Vm.4S`   | `FCMGT Vd.4S,Vm.4S,Vn.4S`   | `Vd.4S -> result`  | `v7/A32/A64`              |

## vcltq_s32 (document line 1505)

| <code>uint32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vcltq_s32" target="_blank">vcltq_s32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t b)</code>     | `a -> Vn.4S`<br>`b -> Vm.4S`   | `CMGT Vd.4S,Vm.4S,Vn.4S`    | `Vd.4S -> result`  | `v7/A32/A64`              |

## vceqq_f32 (document line 1315)

| <code>uint32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vceqq_f32" target="_blank">vceqq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b)</code> | `a -> Vn.4S`<br>`b -> Vm.4S`   | `FCMEQ Vd.4S,Vn.4S,Vm.4S`   | `Vd.4S -> result`  | `v7/A32/A64`              |

## vbslq_s32 (document line 3140)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vbslq_s32" target="_blank">vbslq_s32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; uint32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t b,<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t c)</code>          | `a -> Vd.16B`<br>`b -> Vn.16B`<br>`c -> Vm.16B` | `BSL Vd.16B,Vn.16B,Vm.16B` | `Vd.16B -> result` | `v7/A32/A64`              |

## vbslq_f32 (document line 3154)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vbslq_f32" target="_blank">vbslq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; uint32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t c)</code>    | `a -> Vd.16B`<br>`b -> Vn.16B`<br>`c -> Vm.16B` | `BSL Vd.16B,Vn.16B,Vm.16B` | `Vd.16B -> result` | `v7/A32/A64`              |

## vmaxq_f32 (document line 955)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vmaxq_f32" target="_blank">vmaxq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b)</code> | `a -> Vn.4S`<br>`b -> Vm.4S`   | `FMAX Vd.4S,Vn.4S,Vm.4S`    | `Vd.4S -> result`  | `v7/A32/A64`              |

## vmaxq_s32 (document line 947)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vmaxq_s32" target="_blank">vmaxq_s32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t b)</code>       | `a -> Vn.4S`<br>`b -> Vm.4S`   | `SMAX Vd.4S,Vn.4S,Vm.4S`    | `Vd.4S -> result`  | `v7/A32/A64`              |

## vextq_f32 (document line 3514)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vextq_f32" target="_blank">vextq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b,<br>&nbsp;&nbsp;&nbsp;&nbsp; const int n)</code>    | `a -> Vn.16B`<br>`b -> Vm.16B`<br>`0 <= n <= 3`  | `EXT Vd.16B,Vn.16B,Vm.16B,#(n<<2)` | `Vd.16B -> result` | `v7/A32/A64`              |

## vextq_s32 (document line 3500)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vextq_s32" target="_blank">vextq_s32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t b,<br>&nbsp;&nbsp;&nbsp;&nbsp; const int n)</code>          | `a -> Vn.16B`<br>`b -> Vm.16B`<br>`0 <= n <= 3`  | `EXT Vd.16B,Vn.16B,Vm.16B,#(n<<2)` | `Vd.16B -> result` | `v7/A32/A64`              |

## vrev64q_f32 (document line 3541)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vrev64q_f32" target="_blank">vrev64q_f32</a>(float32x4_t vec)</code>   | `vec -> Vn.4S`         | `REV64 Vd.4S,Vn.4S`   | `Vd.4S -> result`  | `v7/A32/A64`              |

## vrev64q_s32 (document line 3533)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vrev64q_s32" target="_blank">vrev64q_s32</a>(int32x4_t vec)</code>       | `vec -> Vn.4S`         | `REV64 Vd.4S,Vn.4S`   | `Vd.4S -> result`  | `v7/A32/A64`              |

## vaddq_f32 (document line 227)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vaddq_f32" target="_blank">vaddq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b)</code> | `a -> Vn.4S`<br>`b -> Vm.4S`   | `FADD Vd.4S,Vn.4S,Vm.4S`   | `Vd.4S -> result`  | `v7/A32/A64`              |

## vmulq_f32 (document line 387)

| <code>float32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vmulq_f32" target="_blank">vmulq_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t b)</code>        | `a -> Vn.4S`<br>`b -> Vm.4S`   | `FMUL Vd.4S,Vn.4S,Vm.4S`   | `Vd.4S -> result`  | `v7/A32/A64`              |

## veorq_s32 (document line 3030)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/veorq_s32" target="_blank">veorq_s32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t b)</code>    | `a -> Vn.16B`<br>`b -> Vm.16B` | `EOR Vd.16B,Vn.16B,Vm.16B` | `Vd.16B -> result` | `v7/A32/A64`              |

## vandq_s32 (document line 2988)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vandq_s32" target="_blank">vandq_s32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t b)</code>    | `a -> Vn.16B`<br>`b -> Vm.16B` | `AND Vd.16B,Vn.16B,Vm.16B` | `Vd.16B -> result` | `v7/A32/A64`              |

## vaddq_s32 (document line 215)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vaddq_s32" target="_blank">vaddq_s32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t a,<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t b)</code>       | `a -> Vn.4S`<br>`b -> Vm.4S`   | `ADD Vd.4S,Vn.4S,Vm.4S`    | `Vd.4S -> result`  | `v7/A32/A64`              |

## vreinterpretq_s32_u32 (document line 2470)

| <code>int32x4_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vreinterpretq_s32_u32" target="_blank">vreinterpretq_s32_u32</a>(uint32x4_t a)</code>     | `a -> Vd.4S`           | `NOP`                 | `Vd.4S -> result`  | `v7/A32/A64`              |

## vgetq_lane_f32 (document line 3488)

| <code>float32_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vgetq_lane_f32" target="_blank">vgetq_lane_f32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; float32x4_t v,<br>&nbsp;&nbsp;&nbsp;&nbsp; const int lane)</code>      | `0<=lane<=3`<br>`v -> Vn.4S`         | `DUP Sd,Vn.S[lane]`   | `Sd -> result` | `v7/A32/A64`              |

## vgetq_lane_s32 (document line 3482)

| <code>int32_t <a href="https://developer.arm.com/architectures/instruction-sets/intrinsics/vgetq_lane_s32" target="_blank">vgetq_lane_s32</a>(<br>&nbsp;&nbsp;&nbsp;&nbsp; int32x4_t v,<br>&nbsp;&nbsp;&nbsp;&nbsp; const int lane)</code>          | `0<=lane<=3`<br>`v -> Vn.4S`         | `SMOV Rd,Vn.S[lane]`  | `Rd -> result` | `v7/A32/A64`              |
