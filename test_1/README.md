# libopus 六场景性能基准

本框架分别测量 **SILK / CELT / HYBRID × 编码 / 解码** 六个场景的基本 API 处理耗时。默认是 48 kHz、单声道、20 ms、complexity=5；SILK 16 kbps、CELT 64 kbps、HYBRID 32 kbps。使用 CBR、关闭 DTX/FEC，不模拟网络丢包或真实音频系统。

HYBRID 是 SILK 低频与 CELT 高频的组合，其编码和解码各自必须实际进入两种实现。为了研究特定模式的算子，框架显式固定模式，不测自动路由选择策略。

## 构建与运行

需要 Linux、Python 3.9+、C 编译器、支持 `--wrap` 的 GNU 兼容链接器，以及已编译的静态 libopus。框架自身不下载依赖、不重新编译/修改音频库。生成的 `opus_perf_bench` 是包含两个原生 C 程序的 Python zipapp，可像旧程序一样复制保存；Python 不在音频 API 的计时区内。`ldd` 不能用于检查这个启动包；库身份在结果的 build_metadata 中。

以下路径沿用你的布局。先确认 CPU 33 在当前进程允许的 CPU 集合中：

```sh
cd /home/zl_00986855/workspace/libopus/test_1
taskset -pc $$
CPU_ID=33

# Autotools 库：../opus-github-01/.libs/libopus.a 和 config.h 已生成
make OPUS_DIR=../opus-github-01
mkdir -p binary_tmp
cp opus_perf_bench binary_tmp/github-01
```

CMake 外部构建则明确指定源目录和构建目录：

```sh
make OPUS_DIR=.. OPUS_BUILD_DIR=/path/to/opus-build
```

这里使用 `GNUmakefile`，避免 libopus 根目录 `.gitignore` 的 `Makefile` 规则漏掉框架构建文件。每次 `make` 会检查所选库和源码指纹，切换 OPUS_DIR 或库内容变化会重新链接，不需要依赖手工 `make clean`。

**第一次先用基线版本生成固定解码语料，之后所有候选版本复用它：**

```sh
binary_tmp/github-01 -c configs/six_scenarios.json --cpu "$CPU_ID" \
  --fixtures-dir fixtures/basic --prepare-fixtures

binary_tmp/github-01 -c configs/six_scenarios.json --cpu "$CPU_ID" \
  --fixtures-dir fixtures/basic --lib-name github-01 -C -o output/github-01
```

`configs/module_coverage.json` 也已更新为同一份六场景配置，原来的命令可以继续使用这个配置路径。`--cpu` 必填；可额外使用 taskset，但框架会自行检查允许集合、绑定并验证实际亲和性。CPU 不可用、绑定失败或执行后亲和性变化都会使该次实验失败。

解码语料带有完整参数、包边界、内容校验和基线构建信息。候选程序不会默默用自己的编码器重新生成语料；缺失或损坏就失败。修改帧数、预热帧数、输入种子、声道或其他码流参数后，应使用基线程序显式准备新的语料。相同参数的编码/解码用例共用同一个语料身份。

## 实际路径证明

每个用例首先运行独立的 `verify_worker`：

- 每个测量包都检查 TOC 模式、实际带宽、声道和时长，要求全部符合配置。
- 通过链接器包装真实的 `silk_Encode`、`silk_Decode`、`celt_encode_with_ec`、`celt_decode_with_ec` 和 `celt_decode_with_ec_dred` 入口统计调用，不能仅用配置推测模式。
- SILK 用例要求对应 SILK 调用数至少等于测量帧数，且没有 CELT 调用；CELT 反之；HYBRID 要求两者均至少等于测量帧数。
- 编码验证区不允许解码调用，解码验证区不允许编码调用。预热调用不计入覆盖计数。

验证通过后，使用 **同一个静态库的无包装 worker** 测性能，核对其输入、包和输出哈希与验证程序一致。插桩计时不会被当作性能结果。

这是针对编解码实现入口的覆盖证明，并不意味着内部每一个 SIMD 或数学算子都被覆盖。未来优化某个更深层算子时，可以在 `src/verify_calls.c` 中增加对应探针。

模式固定使用此源码版本的内部 CTL `11002`；wrapper ABI 使用所选构建的实际头文件和编译定义。库不兼容、符号不存在、LTO 导致包装失效等情况会明确失败，不能绕过覆盖验证继续声称结果有效。若研究 LTO，先确认探针仍可覆盖所选归档；否则需为其增加适合该构建的独立验证机制。

## 修改配置

配置是严格的 JSON v2。一个用例示例：

```json
{
  "schema_version": 2,
  "name": "silk-experiment",
  "repeats": 7,
  "seed": 42,
  "test_cases": [
    {
      "name": "silk-encode",
      "mode": "silk",
      "operation": "encode",
      "sample_rate": 48000,
      "channels": 1,
      "frame_size": 960,
      "bitrate": 16000,
      "complexity": 5,
      "bandwidth": "WIDEBAND",
      "signal": "mixed",
      "frames": 1000,
      "warmup": 100
    }
  ]
}
```

- `mode`：silk、celt、hybrid；`operation`：encode、decode。兼容字段别名 module、iterations 和操作名 encode_only/decode_only，但不再混合 encode_decode。
- `frames`：每次独立运行的测量帧数；`warmup`：连续前缀的预热帧数；`repeats`：重新创建编解码状态的独立进程运行次数。
- 支持普通的 10/20 ms 帧、1/2 声道。WIDEBAND 至少需16 kHz，SUPERWIDEBAND 至少24 kHz，FULLBAND 需48 kHz。SILK 用 WIDEBAND，HYBRID 用 SUPERWIDEBAND/FULLBAND，且至少16 kbps。
- `signal`：mixed、sine、noise、impulse。输入在计时前生成，状态连续、种子确定、双声道有区别。默认 mixed 是适度的多频+噪声+缓慢包络合成输入，适合可复现的基础对比，不声称代表全部自然语音或音乐。
- 未知字段、重复键/用例名、布尔值代替整数、非法参数、零样本、截断 JSON 都会报错，不能静默退回默认值。
- 任意增删用例，也可 `--case silk-encode` 单独运行，或重复 `--case` 选择多项；`--repeats N` 临时覆盖重复次数。

此前的宽泛实验配置保留在 `configs/legacy/` 供迁移参考，不支持直接执行。旧的手写 JSON 解析器、混合计时引擎以及硬编码个人路径的批量/统计脚本已经退役，避免混用两种结果口径。

## 结果与科学比较

默认终端只显示一行一个场景的表格：路径验证、每次运行平均耗时的中位数/均值、跨运行均值区间、帧 P95、运行间 CV 和处理速度。JSON 保留完整配置、真实调用计数、原始纳秒样本、各次运行、语料身份以及静态库/worker 哈希与构建信息；`-C` 额外输出简表 CSV。

主指标单位是 **微秒/帧**，只计算 opus_encode 或 opus_decode API 的时钟差；不包含 PCM 生成、文件 I/O、创建/销毁、输出哈希和正确性检查。`total_ns=sum(samples_ns)`。`loop_ns` 和 `thread_cpu_ns` 含测试程序检查开销，仅是诊断数据，不能当作纯音频库 CPU 时间。吞吐倍数=音频时长/测得处理时间，不是系统端到端延迟。

失败、缺失或不足的样本不生成有效聚合；文件写入失败返回非零；每次输出唯一 run-id 目录，避免秒级文件名覆盖。少于3次运行不提供单程序均值区间；短于10 ms的测量或CV超过5%会警示，应增加 frames 或排查环境。区间是对当前重复实验的估计，不保证 CPU 独占、频率固定或无系统中断。

比较两个版本应采用固定语料和平衡 AB/BA 调度：

```sh
# 各自 make + cp 保存后，使用同一配置、CPU、fixtures/basic
python3 scripts/compare.py \
  --baseline binary_tmp/github-01 --candidate binary_tmp/github-02 \
  -c configs/six_scenarios.json --cpu "$CPU_ID" \
  --fixtures-dir fixtures/basic --pairs 10 -o output/compare
```

该脚本在每一对实验中相邻执行 A/B，交替先后顺序并打乱用例次序；逐次检查配置和工作量身份，拒绝失败数据。报告配对几何加速比和95% bootstrap 区间；少于5对不提供区间，区间跨1时显示 inconclusive。所有原始结果会保留。

默认还要求 A/B 重建音频哈希一致，防止损坏输出被当成提速。跨浮点/定点或有意近似优化可能合法地产生不同样本；完成独立正确性/质量验证后，才使用 `--allow-output-differences`，结果仍会明确记录差异。

解释百分比时：`speedup=A/B`；吞吐提升为 `(A/B-1)*100%`；耗时下降为 `(1-B/A)*100%`。1.10倍加速对应耗时降低约9.09%，不是10%。

进行算子代码对比时，固定编译器、编译选项、数值模式、hardening 和输入；编译选项优化另做一组实验。先做 A/A 重复以了解噪声，再看 A/B。绑定核心不会排除其他进程、SMT 同核线程、频率变化和 NUMA 影响，仍需选择空闲、稳定的机器；不要同时运行构建或其他基准。

## 回归检查

```sh
make OPUS_DIR=../opus-github-01 check
```

检查六类真实算子调用、纯解码无编码、配置/CPU/语料错误拒绝、复制程序可运行、结果不覆盖和统计口径。可另外对 native worker 使用 ASan/UBSan；带 sanitizer 的程序只做正确性验证，不作为性能结果。
