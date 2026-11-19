# CELT 编码循环 Demo

用法：`./demo_source input.pcm [loops]`。`loops` 是正整数，默认 1。

输入保持为此前的 48 kHz、单声道、S16LE、无文件头 PCM；程序一次读入前 1100 帧（22 秒、2,112,000 字节），更长文件的其余部分不使用。现有 mixed PCM 可直接复用。

每轮编码全部 1100 帧，不安排预热，不区分 warmup/test。总帧数 = loops × 1100。所有轮次使用同一个编码器状态，不在轮次之间 reset；每轮重新从内存中 PCM 第一帧开始。循环边界代表音频片段拼接，不是多次独立测试。

编码参数保持不变：RESTRICTED_CELT，48 kHz，单声道，960 samples/frame，64 kbit/s，complexity 5，FULLBAND，CBR，DTX/FEC 关闭，signal hint AUTO。

文件读取、PCM 转换、参数检查及编码器创建只做一次。外层循环每轮只增加循环控制，内层只推进帧指针、调用 opus_encode 并检查错误。没有解码、计时、哈希或逐帧文件 I/O。loops 参数检查在编码之前执行。

## 编译

```bash
ROOT=/home/zl_00986855/workspace/opus

gcc -O2 -g -std=c99 -Wall -Wextra \
  -I"$ROOT/opus-source/include" celt_encode_demo.c \
  "$ROOT/opus-source/.libs/libopus.a" -lm -o demo_source

gcc -O2 -g -std=c99 -Wall -Wextra \
  -I"$ROOT/opus-branch/include" celt_encode_demo.c \
  "$ROOT/opus-branch/.libs/libopus.a" -lm -o demo_branch
```

库本身无需因本次 Demo 修改而重新配置；使用原有编译参数和已有 libopus.a。若修改了库，则先重建库并重新链接两个 Demo。头文件必须支持 OPUS_APPLICATION_RESTRICTED_CELT。

## 使用

```bash
CPU_ID=66
PCM=input_pcm/celt_encode_mixed_48k_mono_22s.pcm
LOOPS=1000

taskset -c "$CPU_ID" ./demo_source "$PCM" "$LOOPS"

"$DEVKIT" tuner top-down -L 1 taskset -c "$CPU_ID" \
  ./demo_source "$PCM" "$LOOPS"

"$DEVKIT" tuner top-down -L 1 taskset -c "$CPU_ID" \
  ./demo_branch "$PCM" "$LOOPS"
```

LOOPS=1000 的预期成功输出：

```text
Encoded 1100000 CELT frames (1000 loops x 1100 frames; no warmup).
```

这是 22,000 秒音频的编码量，不代表程序实际运行 22,000 秒。可以先跑 100 轮，根据实际运行时长增大 loops。公平比较时两个版本固定同一 CPU、PCM 和 loops，分别运行。

Top-down 是整个目标进程的统计，仍包含启动、一次性读入/转换和初始化；循环增大后这些固定成本所占比例减小。没有增加 sleep 或声称做到仅 API 的 PMU 统计。延长运行针对短进程采集问题；如果仍出现 no such process，应继续排查 DevKit，不能据此保证一定解决。

## 验证

validation.json 记录云环境 x86_64 GCC 原生验证，不代表 ARM/DevKit 实测。浮点和定点分别编译；验证默认轮次、显式 1/3 轮、100 轮长运行及非法参数/输入；验证专用链接包装器检查 3300 帧的 PCM、编码参数、CELT 包类型和无解码调用，并将三轮包内容/长度/final_range 与连续 3300 帧参考实现逐包比较。验证包装器不进入交付 Demo。
