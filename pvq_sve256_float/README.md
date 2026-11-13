# 浮点 PVQ：256-bit SVE 优化交付包

在已有 NEON 版本上新增独立 SVE256 路径，原 NEON 实现保持不变。

- **[下载完整 ZIP 包](https://github.com/Git-Donner/libopus-opt/raw/refs/heads/main/pvq_sve256_float/opus-pvq-sve256-float.zip)**
- [使用与编译说明](README.zh-CN.md)
- [全部新增及修改文件的完整源码](files/)
- [相对于 NEON 版本的增量补丁](pvq-sve256.patch)
- [算法和运行时分派说明](ALGORITHM.zh-CN.md)
- [测试结果和验证范围](VALIDATION.zh-CN.md)
- [官方指令资料](INTRINSIC_REFERENCES.md)
- [编译命令及测试日志](validation/)

请先在已应用上一版 NEON 补丁的源码上 dry-run 检查，再应用本增量补丁。
详细步骤见使用说明。此目录是交付材料，仓库根目录的生产源码未在本次发布中替换。

已在 AArch64 QEMU 环境完成 12,352 组算子验证、300 组真实 CELT 编码配对验证、
VL/无 SVE 回退、UBSan，以及 Autotools/CMake 构建检查。
这些结果不代表原生 ARM 服务器性能收益，性能需在目标 CPU 上实测。

ZIP SHA-256：`5eefeeba94f096083edd8cf2aa67a70e1d25149b8d10d1d8fda63b216408bbf0`
