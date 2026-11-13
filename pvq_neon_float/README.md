# 浮点 PVQ 搜索的 128-bit ARM NEON 移植

基于 Git-Donner/libopus-opt 提交 `05c87e25f7a7cda44650a370f1997748c2ababa5` 的源码树。下载 [ZIP代码包](opus-pvq-neon-float.zip) 和 [SHA256校验文件](opus-pvq-neon-float.zip.sha256)，解压后可阅读说明及复制完整源码。

完整文件在 `files/`，只包含本次 PVQ 改动；不包含此前测试框架的修改。另提供 `pvq-neon.patch` 和各文件 SHA256 的 `manifest.json`。

## 文件清单

必须应用的实现/集成文件：

1. 新增 `celt/arm/vq_neon_intr.c`：浮点 `op_pvq_search_neon` 完整实现。
2. 新增 `celt/arm/vq_arm.h`：声明及 NEON 直接/运行时分派宏。
3. 修改 `celt/vq.h`：引入 ARM 浮点分派头。
4. 修改 `celt/arm/arm_celt_map.c`：ARM RTCD 表。
5. 修改 `celt_sources.mk`：加入 NEON 源文件组。
6. 修改 `celt_headers.mk`：加入 ARM 头文件。

可选验证文件：`celt/tests/test_unit_pvq_neon.c`。它是独立的函数正确性/边界测试，不是性能基准，也没有自动加入原有 `make check`/CTest。

`validation/test_encoder_dispatch.c` 是额外的实际编码调用证明程序，使用链接包装统计新函数是否被调用。

Autotools、CMake、Meson 均读取共享 `.mk` 源文件组，因此无需分别改写三套构建文件。没有修改标量 C、x86 SSE2 或定点算法。

## 算法与数值口径

继承 SSE2 的四路 SIMD 搜索策略：保存符号、取绝对值；脉冲多时按 L1 和投影初始化；剩余脉冲逐次以 `(xy+X[j])/sqrt(yy+y[j]+1)` 评分；选中后更新累积相关量、能量和双倍脉冲向量；最后恢复符号。

重要细节：

- 水平和保持 `(lane0+lane2)+(lane1+lane3)` 的 SSE 顺序。
- lane 内严格 `>`，保留该 lane 首次获胜的位置；全局同分时选最大的已保存 lane 获胜索引。它不同于标量 C 的优先最小索引，也不是简单地取全局最大索引。
- NEON `vrecpeq_f32`/`vrsqrteq_f32` 原始估计与 SSE 不等价，各追加一次 Newton 修正。没有使用 AArch64 专有的向量除法/平方根，内核本身可用于 A32 NEON。
- `_X` 复制到本地再处理，与 SSE 一致；不修改调用者输入。
- 本地数组填充 N+3；外部 `iy` 只读写 N 个元素，尾部评分显式屏蔽无效 lane，不依赖外部数组多分配3个元素。
- 投影总脉冲数若异常超过 K，重置初始化并走保守路径，确保脉冲预算。正常 CELT 有限归一化输入是主要支持域。
- `-0` 使用 `<0` 判符号，与 SSE 一致。NaN 等非法输入下不保证与 SSE MAXPS 的特殊语义相同。

这是近似浮点搜索移植，不保证与标量或 SSE 脉冲向量/码流/重建音频位精确一致。编译器的 FMA contraction 也可能影响边界舍入。算法一致性和音质仍需在目标服务器上用你的素材评估。`compare.py --allow-output-differences` 仅允许采集跨版本性能，不代替质量验证。

本函数仅在编码脉冲搜索中执行，直接目标是 CELT 编码和 HYBRID 编码中的 CELT 部分；不能预期它直接提升解码性能。

## 应用修改

推荐先在目标仓库执行：

```sh
git apply --check /path/to/pvq-neon.patch
git apply /path/to/pvq-neon.patch
```

若本地已有其他算子优化导致补丁冲突，应手工合并。`files/` 提供每个文件的完整修改后版本，方便逐文件比较；不要在不核对本地自定义修改时直接覆盖现有文件。

## 在 AArch64 服务器重新构建

建议本次先使用常规优化级别，不启用 LTO 或 fast-math。若与基线比较，两边使用同一构建系统和选项。

### CMake

在源码根目录执行，`build-pvq-neon` 应为独立的新构建目录：

```sh
cmake -S . -B build-pvq-neon \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="-ffp-contract=off" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DOPUS_BUILD_SHARED_LIBRARY=OFF \
  -DOPUS_FIXED_POINT=OFF \
  -DOPUS_BUILD_TESTING=ON \
  -DOPUS_BUILD_PROGRAMS=ON
cmake --build build-pvq-neon --parallel 8
ctest --test-dir build-pvq-neon --output-on-failure --parallel 8
```

`-ffp-contract=off` 用于先验证不发生乘加融合的版本，不能用来声称与 SSE 位精确。你最终要测的服务器编译选项可以随后分别验证；基线和优化版应保持一致。

### Autotools

修改了 `.mk` 源文件清单，须重新生成构建系统再配置。源码准备完整、有 Autoconf/Automake/Libtool 时，在根目录执行：

```sh
autoreconf -isf
env -u CFLAGS -u CPPFLAGS -u LDFLAGS CC=gcc \
  ./configure --disable-shared --enable-static --disable-fixed-point
make -j8
make check -j8
```

`autoreconf -isf` 是仓库 `autogen.sh` 最后的构建生成步骤，本身不下载模型。若源码仍缺少构建所需的模型/生成文件，先完成原仓库的源码准备流程；本补丁不修改下载或校验机制。Autotools 此处使用自己的默认参数，不与上面 CMake Release 混为同一基线。

## 单独运行 PVQ 测试（原生 ARM，Autotools 构建）

完成 `./configure` 和 `make` 后，在源码根目录：

```sh
gcc -O2 -std=gnu99 -DHAVE_CONFIG_H \
  -I. -Iinclude -Icelt -Icelt/arm -Isilk -Idnn \
  celt/tests/test_unit_pvq_neon.c .libs/libopus.a -lm \
  -o test_unit_pvq_neon
./test_unit_pvq_neon
```

这里通过该构建生成的 config.h 读取相同宏定义。AArch32 还需采用与库一致的 NEON/浮点 ABI 编译参数。CMake 将部分宏通过命令行定义，若测试 CMake 产物，应从 `compile_commands.json` 的 Opus 编译命令复用同一组 `-D`/`-I`；不要只依赖其简化的 config.h。若测试输出 SKIP/退出77，则没有实际验证浮点 NEON。

实际编码调用证明（AArch64、Autotools）：

```sh
gcc -O2 -Iinclude validation/test_encoder_dispatch.c \
  -Wl,--wrap=op_pvq_search_neon .libs/libopus.a -lm \
  -o test_encoder_dispatch
./test_encoder_dispatch
```

输出必须有正数 `NEON PVQ calls`，同时帧数和编码/解码 final range 校验通过。

## 已完成验证

详见 `VALIDATION.md`。QEMU 验证的是 ARM 指令执行、正确性和分派，不是目标机器的性能。真实速度、不同优化参数及素材下的质量需由服务器测试确认。

## 指令资料

已在线读取 Arm 官方 ACLE Advanced SIMD Intrinsics Reference，具体35个 intrinsic 的官方表格摘录、来源 URL 和下载 SHA256 见 `INTRINSIC_REFERENCES.md`。人类可读页面：https://arm-software.github.io/acle/neon_intrinsics/advsimd.html 。
