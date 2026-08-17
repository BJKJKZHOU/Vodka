# JustCanFd

JustCanFd 是用于解析 AxDr CAN-FD-native 应用协议的 VOFA+ 数据引擎。

完整协议见 [AxDr_Protocol.md](AxDr_Protocol.md)，测试记录见 [TESTING.md](TESTING.md)，VOFA+ 协议帮助配置位于 `../generated/justcanfd.json`。

## USB 承载

```text
magic[4] = {'A', 'X', 'D', 'R'}
uint16_t can_id  # 小端
uint8_t len
uint8_t payload[len]
```

11-bit CAN ID 使用 `Message Type + Node ID`。Node 1 的典型数据 ID：

- `0x0401`：NORMAL_DATA，1 kHz，float32；
- `0x0601`：FAST_DATA，20 kHz 采样，int16 定点；
- `0x0081`：RESPONSE。

FAST 在收到成功的 `PLOT_CONFIG RESPONSE` 后，根据回显的 `Config_ID + Var_ID[]` 使用协议静态表中的 Plot Scale 将 int16 原始值恢复为物理值。若尚未收到对应配置，插件仍输出原始 int16 count，便于诊断。

## Qt 路径

```text
/home/zhouheng/Qt5.14.2/5.14.2/gcc_64/bin/qmake
```

## 编译

```bash
cd /home/zhouheng/GitHub_Pro/Vodka/dataengines/justcanfd
mkdir -p build
cd build
make distclean 2>/dev/null || true
/home/zhouheng/Qt5.14.2/5.14.2/gcc_64/bin/qmake \
    ../justcanfd.pro CONFIG+=release
make -j"$(nproc)"
```

## UDP 测试

VOFA+ 选择 `JustCanFd` 和 UDP，监听 `127.0.0.1:1347`。

NORMAL：

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode normal --count 150
```

FAST：

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode fast --count 150 --samples-per-packet 8 --channels 2
```

FAST 测试会先发送一帧 `PLOT_CONFIG RESPONSE`，默认 CH0/CH1 为 `Ia/Ib`，因此默认 ±10000 raw count 在 VOFA 中显示约 ±10 A。
