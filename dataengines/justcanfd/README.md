# JustCanFd

JustCanFd 是用于解析 AxDr CAN-FD-native 应用协议的 VOFA+ 数据引擎。

当前插件只负责 USB framing、PLOT RESPONSE、FAST_DATA 和 NORMAL_DATA 的解析。AxDr 参数 ID 与 FAST Plot Scale 不再手写在 `justcanfd.cpp` 中，而由 AxDr_L_Motor 的 `Parameter/parameter.yaml` 生成 `axdr_plot_meta.generated.h`。

协议说明见 [AxDr_Protocol.md](AxDr_Protocol.md)，测试与回归步骤见 [TESTING.md](TESTING.md)，VOFA+ 协议帮助配置位于 `../generated/justcanfd.json`。

## USB 承载

AxDr 的规范 wire 表示是标准 CAN FD frame；USB/UDP 只透明封装完整 CAN FD frame：

```text
magic[4] = {'A', 'X', 'D', 'R'}
uint16_t can_id          # little endian
uint8_t canfd_len        # 0..8,12,16,20,24,32,48,64
uint8_t data[canfd_len]  # 包含 CAN FD 零填充
```

11-bit CAN ID 使用 `Message Type + Node ID`。Node 1 的数据 ID：

- `0x0401`：NORMAL_DATA，float32；
- `0x0601`：FAST_DATA，int16 定点；
- `0x0081`：RESPONSE。

FAST_DATA 因 CAN FD padding 不能再从 `canfd_len` 反推通道数。插件必须先收到匹配 `Config_ID` 的成功 FAST `PLOT_CONFIG RESPONSE`，取得 `Channel_Count + Var_ID[]` 后才能解析对应 FAST_DATA。Plot Scale 可用时将 int16 raw 恢复为物理值；若配置已知但本地缺少某个 scale，则保持 raw int16 count。

## 同步 AxDr 参数元数据

当 AxDr_L_Motor 的 `Parameter/parameter.yaml` 修改了 `id` 或 `plot_scale` 后，在 Vodka 仓库执行：

```bash
python3 dataengines/justcanfd/sync_axdr_parameters.py
```

默认假设两个仓库位于同一父目录：

```text
<workspace>/Vodka
<workspace>/AxDr_L_Motor
```

也可以显式指定：

```bash
python3 dataengines/justcanfd/sync_axdr_parameters.py \
    --source /path/to/AxDr_L_Motor/Parameter/parameter.yaml
```

或设置 `AXDR_L_MOTOR_ROOT`。提交前可检查生成文件是否同步：

```bash
python3 dataengines/justcanfd/sync_axdr_parameters.py --check
```

生成文件 `axdr_plot_meta.generated.h` 应提交到仓库，使 JustCanFd 的正常构建不依赖另一个仓库在线或存在于本机。

## 编译

该插件目前仍使用 Qt 5 / qmake 工程；本次协议同步不要求迁移 Qt 6 或 CMake。使用与目标 VOFA+ 插件 ABI 匹配的 Qt qmake：

```bash
cd dataengines/justcanfd
mkdir -p build
cd build
qmake ../justcanfd.pro CONFIG+=release
make -j"$(nproc)"
```

不要依赖 README 中固定的个人 Qt 安装路径。

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

测试发送器会把有效应用字段向上对齐到标准 CAN FD data-field length，并用 `0x00` 填充尾部。FAST 测试会先发送一帧 `PLOT_CONFIG RESPONSE`。默认 CH0/CH1 为 `PARAM_ADC_IA/PARAM_ADC_IB`，Plot Scale 均为 `0.001`，因此默认 ±10000 raw count 在 VOFA+ 中显示约 ±10 A。
