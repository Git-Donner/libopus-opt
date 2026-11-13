# 实际验证记录

日期：2026-11-13。功能测试通过用户目录内的 AArch64 GCC 14.2 和 QEMU 10.0.13 执行。
工具链来自经 APT 仓库签名/校验和验证的软件包；不修改系统工具链。
QEMU 使用 `-cpu max,sve-max-vq=4`，可请求 16/32/64 字节 VL；无 SVE 测试使用 `-cpu max,sve=off`。
本环境不是用户的原生 ARM 服务器，没有产生可用于判断优化收益的性能数据。

## 已通过

| 验证 | 结果 |
|---|---|
| NEON 文件保留 | 修改前后字节完全相同，SHA-256 记录在 manifest 和 README |
| CMake AArch64 浮点全构建 | 通过；SVE 文件单独使用 scalable SVE flags |
| Autotools autoreconf/configure/make | 通过；`--enable-sve` 探测成功，生成静态库及原有程序 |
| 算子正确性与边界 | 12,352 用例通过；其中 12,348 次直接 SVE 入口检查，另外 4 次异常投影防御检查 |
| 和未改 NEON 对照 | 脉冲向量和返回能量逐字节一致 |
| 原始编译选项风格 | `-O3 -march=armv8.2-a+sve -fno-semantic-interposition -fvisibility=hidden` 下同样通过全部算子及编码证明测试；交叉编译不使用宿主机 `-mtune=native` |
| 实际 CELT 编码调用 | 300 组配对编码帧；SVE256 入口有效调用 11,241 次 |
| 编码/解码一致性 | 两套独立状态的编码器/解码器，所有 packet、解码 PCM、final range 完全一致 |
| 128-bit / 512-bit 回退 | 每种 VL 下各 300 组配对帧通过，actual256=0 |
| 无 SVE 硬件能力回退 | 11,344 算子用例和 300 组配对帧通过，SVE 入口调用为 0 |
| VL 变化 | 同一线程和不同子线程分别切换 16/32/64 字节，结果均与 NEON 一致 |
| 非 presumed-NEON 分派表 | slots 0..2 为 C，3..4 为硬件检查包装器；分派结果与 NEON 一致 |
| UBSan | 对新内核、包装器、NEON 对照及测试程序启用 undefined sanitizer，12,352 用例通过，无诊断；底层整库不声称已全部插桩 |
| 关闭 SVE 配置 | `OPUS_ARM_SVE=OFF` 的编译数据库不包含 SVE 内核、包装器或 SVE 宏 |
| AArch64 原有回归 | API、padding、decode、extensions 四项执行成功；未宣称完整 make check 通过 |
| x86 原有回归 | 原有 CMake 5 项测试全部通过 |
| 固定点构建回归 | 现有固定点构建成功；不启用新浮点 SVE 路径 |

主算子覆盖 N=2..256 的 27 种长度，含非 4/8 倍数和真实 CELT 常见长度；
K 从 0、1 到 1024，覆盖投影阈值、极端脉冲预算防御；
输入包含归一化随机、符号、零、负零、极小值、subnormal、等分数和稀疏向量。
还检查 L1 脉冲数恰好等于 K、符号、返回能量等于脉冲平方和、输入不变。
Linux 的精确 N 外部数组紧贴 PROT_NONE 页，检测尾部越界读写，前部另有写 canary。

实际编码覆盖 mono/stereo，2.5/10/20 ms，48 kHz，64 kbit/s 每通道，complexity=5。
引用实现通过同一测试可执行文件的链接包装器将参考编码器的调用转到原 NEON；
被测编码器调用真实 SVE 函数。包装器及计数只用于证明，不进入性能测量构建。
Autotools 构建的静态库也独立执行了相同两项测试。

## 范围和限制

- 测试观察到与 NEON 完全一致，不构成对所有编译器、fast-math、浮点环境和任意非法输入的形式证明。
- 未测量原生 256-bit SVE CPU 时间。线程绑核、频率、预热、独立重复、用例分布和端到端性能由服务器实测确认。
- ASan 未纳入本次通过项；使用 guard pages 和 UBSan 验证了相关边界与未定义行为。
- 本次交付只启用 Autotools/CMake 的 SVE 接入，Meson 保留原 NEON。
- 无 LTO 的调用计数证明避免了跨文件内联干扰；没有把链接包装器的计时作为性能结果。
- ARM 原有 API 测试中的模拟 malloc 失败组按上游条件 SKIP，不是失败，也未算作已通过覆盖。

更多日志见 validation/。交叉工具链和 QEMU 路径属于本验证环境，服务器编译使用本机 gcc 即可。
