# 验证记录（2026-11-16）

功能验证使用已有的 AArch64 GCC 14.2、QEMU 10.0.13，工具链位于用户目录。
QEMU 时间不是性能数据。本次未在用户的原生 ARM 服务器上测量加速比。

| 检查 | 结果 |
|---|---|
| 精确统计与状态 | 150,016 用例通过，比较 C/NEON/独立参考的聚合统计及实际分派/冻结原函数的返回值和全部历史状态 |
| 内存边界 | 2,344 组 guard-page 输入序列，无额外可读 SIMD 填充，所有输入不变 |
| 实际 CELT 编码 | 300 组配对帧；所有 packet、解码 PCM、编码器/解码器 final range 一致 |
| 调用证明 | NEON 和原统计路径各实际调用296次，各处理165,384个系数，非仅编进静态库 |
| 构建配置 | CMake 的基线 AArch64/无 PVQ SVE 构建，以及原有 `-O3 -march=armv8.2-a+sve -fno-semantic-interposition -fvisibility=hidden` 构建，新增两项测试均通过 |
| Autotools | 独立验证副本的 autoreconf、configure、静态构建及两项新测试通过；`--disable-sve`，归档无 SVE256 符号 |
| RTCD | 非 presumed-NEON 模式表项0..2为C、3..4为NEON；掩码、每次一次扫描、早退不调用扫描且状态不变均通过 |
| UBSan | 当前 bands.c、新 NEON 文件和单位测试/参考代码插桩后，150,016 用例通过，无诊断，不声称底层整库全部插桩 |
| x86 回归 | 原有5项 CMake 测试通过 |
| 固定点 | 现有固定点构建通过，使用C统计路径 |
| 代码审查 | 公共历史状态更新尾部与冻结基线逐字节相同，唯一生产调用点已传入st->arch，未发现边界/分派问题 |

单位测试覆盖 N=1..209、不同起始偏移、单/双声道、标准 M=1/2/4/8、所有21种end、
两种update_hf、32种输入分布和不同权重/初始状态，并连续检查状态演进。
含相邻阈值浮点、奇偶N的半数边界、零/负零、极小值、NaN/Inf及大值稳健性测试。
标准与自定义模拟模式用于单函数覆盖；真实编码测试另外通过公开Opus API执行。

原完整 spreading_decision 被冻结为独立参考，仅改函数名；没有调用新的共享统计或状态尾部。
单位测试还直接调用 C、NEON 和独立参考扫描器，比较 sum/nb_bands/hf_sum。
在 presumed-NEON 构建中，arch=0也不会强制切换到C；完整C分派的验证另由非presumed RTCD测试完成。

真实编码覆盖48kHz、mono/stereo、2.5/10/20ms、每通道64kbit/s、complexity=5。
两套独立编码器中，测试包装器分别选择真实NEON扫描或冻结的原扫描，后续状态逻辑共用；
单位测试则独立比较完整的原函数，以免编码测试的共享尾部掩盖问题。

反汇编确认统计核心为128-bit NEON FMUL/FCMGT/SUB，频带末尾为整数ADDV。
基线ISA构建的新扫描器中没有SVE向量寄存器指令。
本次未实现提前终止、声道重排、无分支判决或SVE统计，便于隔离首版收益。

未宣称完整ARM上游make check、ASan、ARM32、Meson均已验证。
原有构建中的Silk decode_indices警告和Automake模式规则扩展警告不是本次NEON扫描器的诊断。
详见validation/中的原始日志、命令和反汇编。
