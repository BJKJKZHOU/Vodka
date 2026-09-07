# JustCanFd 测试记录

## 历史实测环境

以下结果来自 2026-08-17 的历史实测，不能代表当前分支已经重新完成实机/VOFA+ 回归。

| 项目 | 值 |
| --- | --- |
| 测试日期 | 2026-08-17 |
| 操作系统 | Ubuntu 26.04 LTS，x86_64 |
| VOFA+ | 1.4.4 |
| Qt | 5.14.2，qmake 3.1 |
| 编译器 | GCC 15.2.0 |
| 数据接口 | UDP `127.0.0.1:1347` |
| 测试代码基线 | `a5a7f6c` |

历史测试曾验证 Release 编译、NORMAL 数据解析和 FAST 多采样点解析。2026-09 参数协议及 CAN FD canonical wire 更新后，需要按下述步骤重新回归。

## 当前回归前检查

先同步并检查 AxDr 参数元数据：

```bash
python3 dataengines/justcanfd/sync_axdr_parameters.py
python3 dataengines/justcanfd/sync_axdr_parameters.py --check
```

如果两个仓库不在同一父目录，使用 `--source` 或 `AXDR_L_MOTOR_ROOT` 指定 AxDr_L_Motor。

然后编译插件：

```bash
cd dataengines/justcanfd
mkdir -p build
cd build
qmake ../justcanfd.pro CONFIG+=release
make -j"$(nproc)"
```

## CAN FD wire 检查

USB/UDP wrapper 中的 `len` 现在表示完整 CAN FD data-field length，只允许：

```text
0..8, 12, 16, 20, 24, 32, 48, 64
```

有效应用字段不足该长度时，尾部必须为 `0x00`。测试发送器会自动按此规则对齐并填充。

## 普通数据回归

在 VOFA+ 中选择 `JustCanFd` 数据引擎和 UDP 数据接口，将本地端口设置为 `1347` 并打开连接，然后执行：

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode normal --count 150 --channels 2
```

当前 Node ID 为 1，因此 NORMAL_DATA CAN ID 为 `0x0401`。预期生成两个 `float32` 通道，默认幅值为 ±1。

建议再测试会触发 CAN FD padding 的通道数，例如 6 通道：

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode normal --count 50 --channels 6
```

NORMAL 有效字段为 `4 + 6 * 4 = 28` bytes，实际 CAN FD data-field length 应为 32，最后 4 bytes 为零填充。

## 快速数据回归

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode fast --count 150 --samples-per-packet 8 --channels 2
```

当前 FAST_DATA CAN ID 为 `0x0601`。测试脚本会先发送 Node 1 的 `PLOT_CONFIG RESPONSE` (`0x0081`)，默认配置：

```text
CH0 = PARAM_ADC_IA (0x0001), scale = 0.001
CH1 = PARAM_ADC_IB (0x0002), scale = 0.001
```

随后发送的 FAST_DATA 有效字段为：

```text
uint16 Seq
uint8  Config_ID
uint8  Sample_Count
int16  Sample[Sample_Count][Channel_Count]
```

2 通道 × 8 点时，有效字段长度为：

```text
4 + 8 * 2 * 2 = 36 bytes
```

实际 CAN FD data-field length 为 48 bytes，最后 12 bytes 必须为 0。插件必须使用之前的 FAST `PLOT_CONFIG RESPONSE` 得到 `Channel_Count=2`，不能再通过 48-byte wire length 反推通道数。

默认 raw 幅值约 ±10000，因此 VOFA+ 应显示约 ±10 A。150 包、每包 8 点时，应得到 1200 组双通道采样。

## FAST 元数据回归

建议至少再用 8 通道运行一次：

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode fast --count 50 --samples-per-packet 3 --channels 8
```

当前测试变量顺序为：

```text
0x0001 PARAM_ADC_IA       scale 0.001
0x0002 PARAM_ADC_IB       scale 0.001
0x0011 PARAM_RUN_IQ       scale 0.001
0x0010 PARAM_RUN_ID       scale 0.001
0x0012 PARAM_RUN_UD       scale 0.001
0x0013 PARAM_RUN_UQ       scale 0.001
0x0014 PARAM_RUN_THETA_E  scale 0.0002
0x0021 PARAM_OBS_WE       scale 0.1
```

重点确认最后两个通道没有再按旧版 `Theta_m/Wm_Ref` 的 ID/比例解释。

## 边界测试

| 编号 | 测试内容 | 预期结果 |
| --- | --- | --- |
| EDGE-01 | CAN ID 大于 `0x07FF` | 标记为无效数据 |
| EDGE-02 | 数据包长度小于 wrapper 声明长度 | 不越界；等待后续完整数据 |
| EDGE-03 | wrapper length 为 9/17/36 等非 CAN FD 合法长度 | 拒绝 |
| EDGE-04 | 合法 CAN FD length 但 padding 非零 | 对应 PLOT/FAST/NORMAL 帧不解析 |
| EDGE-05 | FAST 未收到匹配 Config_ID 的配置响应 | 不解析 FAST_DATA |
| EDGE-06 | FAST Config_ID 与当前配置不一致 | 不解析 FAST_DATA |
| EDGE-07 | 配置已知但某 Var_ID 缺少本地 Plot Scale | 按已知通道数输出 raw int16 count，不套错误 scale |
| EDGE-08 | 64 字节最大 CAN FD data field | 正常解析且无越界 |

## 多 sample 的 Hex 显示

当前实现只让一个 FAST packet 的第一个 sample 持有原始数据范围，其余 sample 使用空 raw range。因此历史版本中“一个 packet 在 VOFA+ Hex 区按 Sample_Count 重复显示”的现象已经在代码层规避。

本文件在重新完成当前分支的 VOFA+ 回归前，不把上述 2026-09 项目标记为 PASS。
