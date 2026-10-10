# JustCanFd

JustCanFd 是 VOFA+ 的 AxDr **接收数据引擎**，根据当前
[AxDr_L_Motor](https://github.com/BJKJKZHOU/AxDr_L_Motor)
的 CAN FD 格式解析 FAST / NORMAL 波形。

- 输入：USB CDC/UDP 字节流中的 `AXDR + uint16 CAN ID + uint8 DLC + Payload`
- 输出：NORMAL float32，以及根据配置回显正确换算的 FAST int16
- FAST 必须先收到 PLOT_CONFIG 成功 RESPONSE；配置未知时不推断通道数量
- 未知 Scale 的通道保持原始 int16 计数；不同 Config_ID 的在途旧数据仍可解码
- 支持多个连续帧与跨接收块的部分帧，并校验 CAN FD DLC 补零

格式见 [AxDr_Protocol.md](AxDr_Protocol.md)，历史与回归测试见
[TESTING.md](TESTING.md)。插件不是通用 SocketCAN 驱动，也不自动执行
Enable、Run、Stop 或 Save；如需发送这些命令，请使用 VOFA 二进制发送或外部工具。

## 固件通信 ID（Node 1）

| 类型 | CAN ID |
| --- | --- |
| RESPONSE | `0x081` |
| PLOT 请求 | `0x101` |
| PARAMETER 请求 | `0x1C1` |
| NORMAL_DATA | `0x401` |
| FAST_DATA | `0x601` |

运行中更改 FAST 通道时应更换 Config_ID；否则无法通过数据包分辨旧、新配置。

## Qt 5 编译

```bash
cd dataengines/justcanfd
mkdir -p build && cd build
qmake ../justcanfd.pro CONFIG+=release
make -j"$(nproc)"
```

这里的 qmake 需要来自与 VOFA+ 插件 ABI 兼容的 Qt 5。
也可以替换成 Qt 5 qmake 的完整路径。

## UDP 模拟

在 VOFA 选择 JustCanFd 数据引擎，UDP 监听 `127.0.0.1:1347`：

```bash
python3 dataengines/justcanfd/udp_test_sender.py --mode normal --count 150
python3 dataengines/justcanfd/udp_test_sender.py --mode fast --count 150 --channels 2 --samples-per-packet 8
```

FAST 测试先发送正确补零的 CONFIG 成功回包，再发采样。
默认 Ia/Ib 的 10000 raw count 换算后为 10 A。
发送器可以用 `--channels`、`--node`、`--config-id` 修改模拟条件。

## C++ 回归

```bash
cd dataengines/justcanfd
mkdir -p tests/build && cd tests/build
qmake ../tests.pro && make -j"$(nproc)"
./test_justcanfd
```

Qt 5 开发环境是构建及测试的前置条件。
