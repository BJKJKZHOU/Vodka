# JustCanFd 测试记录

## 测试环境

| 项目 | 值 |
| --- | --- |
| 测试日期 | 2026-08-17 |
| 操作系统 | Ubuntu 26.04 LTS，x86_64 |
| VOFA+ | 1.4.4 |
| Qt | 5.14.2，qmake 3.1 |
| 编译器 | GCC 15.2.0 |
| 数据接口 | UDP `127.0.0.1:1347` |
| 测试代码基线 | `a5a7f6c` |

## 测试结果

| 编号 | 测试内容 | 输入 | 预期结果 | 实际结果 | 结论 |
| --- | --- | --- | --- | --- | --- |
| BUILD-01 | Release 编译 | Qt 5.14.2，`CONFIG+=release` | 生成动态库 | 生成 `libjustcanfd.so.1.0.0`，29072 字节 | 通过 |
| NORMAL-01 | 普通数据解析 | CAN ID `0x0400`，负载 12 字节，2 个 `float32` 通道 | 输出连续的正弦、余弦数据 | VOFA+ 显示 `I0`、`I1`，波形与数值正常 | 通过 |
| FAST-01 | 快速数据及多采样点解析 | CAN ID `0x0600`，150 包，每包 8 点 × 2 个 `int16_t` 通道 | 解析 1200 组采样 | 波形幅值约 ±10000，通道顺序正确 | 通过 |
| FAST-02 | 快速数据精确值断言 | 单包 3 点：`(100,-100)`、`(200,-200)`、`(300,-300)` | 生成 3 个双通道 Frame | 三组输出值与输入完全一致 | 通过 |
| EDGE-01 | 非法 CAN ID | CAN ID 大于 `0x07FF` | 标记为无效数据 | 尚未执行 | 待测试 |
| EDGE-02 | 不完整数据包 | 帧长度小于声明长度 | 保留数据并等待后续字节 | 尚未执行 | 待测试 |
| EDGE-03 | 最大负载 | 64 字节负载 | 正常解析且无越界 | 尚未执行 | 待测试 |

构建过程中 GCC 对 Qt 5.14.2 的 `qfutureinterface.h` 输出 C++20 兼容性警告，项目源码没有编译错误，最终退出码为 `0`。

## 普通数据回归

先在 VOFA+ 中选择 `JustCanFd` 数据引擎和 UDP 数据接口，将本地端口设置为 `1347` 并打开连接，然后执行：

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode normal --count 150 --channels 2
```

预期生成两个 `float32` 通道，默认幅值为 ±1。

## 快速数据回归

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode fast --count 150 --samples-per-packet 8 --channels 2
```

每个 UDP 包的结构为：

```text
AXDR + CAN ID 0x0600 + LEN 0x24 + 3 字节元数据
     + sample_count 0x08 + 8 × 2 个 int16_t
```

预期发送 150 个 43 字节数据包，并解析得到 1200 组双通道采样，默认幅值为 ±10000。

## 快速数据精确值断言

使用 Qt `QPluginLoader` 加载编译后的插件，向 `ProcessingDatas()` 输入一个包含 3 个采样点的 `0x0600` 数据包，结果如下：

```text
sample 0: I0=100, I1=-100
sample 1: I0=200, I1=-200
sample 2: I0=300, I1=-300
PASS: one 0x0600 packet produced 3 samples x 2 channels
```

## 已知现象

快速数据包包含多个采样点时，VOFA+ 十六进制接收区会按采样点数量重复显示同一个原始数据包。例如 `sample_count = 8` 时显示 8 次。

这是因为当前实现为每个采样点生成一个 `Frame`，并让这些 `Frame` 共用同一个原始数据范围。该现象不影响通道数量、采样顺序和波形数据。
