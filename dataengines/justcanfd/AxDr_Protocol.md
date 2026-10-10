# AxDr 通信协议（当前固件）

本文以 [AxDr_L_Motor](https://github.com/BJKJKZHOU/AxDr_L_Motor)
`main` 的 `Comm/Protocol.*`、`Comm/Plot.c` 和
`Parameter/parameter.yaml` 为依据。多字节值采用小端，
浮点数为 IEEE-754 float32。

JustCanFd 是 VOFA+ **接收数据引擎**，不主动发送控制指令，也不直接解析
SocketCAN 的原生 CAN 帧。本文描述的是 USB CDC/UDP 字节流中的 AXDR 封装。

## USB 字节流和 CAN FD 长度

```text
"AXDR" (4 字节) | CAN ID (uint16 LE) | Len (uint8) | Payload[Len]
```

11-bit CAN ID = `(MsgType << 6) | NodeId`；当前固件 NodeId 为 1。
Len 是 CAN FD 的 **真实数据域长度**：0～8、12、16、20、24、32、48、64。
有效负载不足该长度时固件填充零。解码必须检查合法 DLC 和补零，
不可用 DLC 直接推断 FAST 通道数。

| MsgType | 名称 | Node 1 CAN ID |
| --- | --- | --- |
| 0x02 | RESPONSE | 0x081 |
| 0x04 | PLOT | 0x101 |
| 0x07 | PARAMETER | 0x1C1 |
| 0x08 | EVENT | 0x201 |
| 0x10 | NORMAL_DATA | 0x401 |
| 0x18 | FAST_DATA | 0x601 |

## RESPONSE

`Byte0 Txn, Byte1 ReqMsgType, Byte2 ReqOp, Byte3 Status, Byte4... Data`。

Status：0 OK、1 ERR_OP、2 ERR_LENGTH、3 ERR_VAR_ID、
4 ERR_READ_ONLY、5 ERR_VALUE、6 ERR_STATE、7 ERR_CONFIG、
8 ERR_BANDWIDTH、9 ERR_NOT_SUPPORTED。

## PARAMETER（0x07）

- 读请求：`Txn, 0x01, Id_L, Id_H`；成功回复附带 `Id(u16),Type(u8),Value`
- 写请求：`Txn, 0x02, Id_L, Id_H, Type, Value...`；写回复附带 `Id(u16)`
- Type：0 U8、1 I8、2 F32、3 I32、4 U32、5 Position（int32 Turn + float32 Theta）、6 Action（无 Value）

电机操作也使用参数写 Action，而不是旧版 CONTROL/VALUE 命令：
Enable `0x1001`、Run `0x1002`、Stop `0x1003`、
Disable `0x1004`、Rs/Ls `0x1101`、Flux `0x1102`、
J/B `0x1105`、Protection Clear `0x1201`、Save `0x1202`。
其他参数 ID、类型与范围以固件 `Parameter/parameter.yaml` 为准。

普通 Action RESPONSE 表示请求执行的处理结果，不代表电机已物理停稳；
只有部分有限异步操作另发送完成事件。不能对所有 Action 等待完成事件。

## EVENT（0x08）

Event `0x01`：随后 16 字节依次是 Report、Warning、Error、Trip 四组
uint32 位图；按 DLC 补零。Event `0x03`：随后是 `Txn(u8), ActionId(u16),
Status(u8)`，用于特定异步动作完成。JustCanFd 不将事件转换为曲线。

## PLOT（0x04）

| Op | 含义 |
| --- | --- |
| 0x01 | CONFIG |
| 0x02 | START |
| 0x03 | STOP |
| 0x04 | CAPS |

CONFIG 请求：`Txn, 1, Group, Config_ID, Count, uint16 VarID[Count]`。
成功 RESPONSE：`Txn, 4, 1, 0, Group, Config_ID, Count,
uint16 VarID[Count]`，按 CAN FD DLC 补零。

Group 0=FAST（最多 8 通道），1=NORMAL（最多 15 通道）。
运行中允许 CONFIG，固件在采样边界完成切换；旧配置的在途数据可能
在新 RESPONSE 之后到达。因此每次换通道都应使用新的 Config_ID。
同一 Config_ID 立刻复用为不同通道时，旧包和新包无法区分。

START、STOP 请求：`Txn, Op, Group_Mask`，bit0 FAST、bit1 NORMAL。
这是 Plot 数据采集启停，**不是电机 Run/Stop**。

CAPS 请求：`Txn, 4, Start_Index`。成功 RESPONSE 的 Byte4 起依次为
`Start_Index(u8), Next_Index(u8), Count(u8), Fast_Max_CH(u8),
Normal_Max_CH(u8), Fast_Block_Samples(u8), Fast_Rate_Hz(u32),
Normal_Rate_Hz(u32)`，后面重复 `Id(u16), Modes(u8), Fast_Scale(f32)`
共 Count 次；每页最多 6 项，Next=0xFF 表示结束。

CAPS 是固件实际支持的波形能力。JustCanFd 被动接收，不主动请求 CAPS；
其已知比例映射对应固件当前 Plot 表。

### FAST_DATA（0x18）

```text
uint16 Seq | uint8 Config_ID | uint8 Sample_Count
int16[Sample_Count][Channel_Count] (sample-major) | 0 填充
```

**FAST 不携带 Channel_Count**，应从之前成功 CONFIG RESPONSE 的回显
得知通道顺序和通道数，再以 `physical = raw * Fast_Scale` 转换。
没有对应配置时不能根据 DLC 猜通道数。未知的通道比例保留原始 int16 count。

### NORMAL_DATA（0x10）

```text
uint16 Seq | uint8 Config_ID | uint8 Channel_Count
float32[Channel_Count] | 0 填充
```

直接输出 float32。FAST 采样率为 20 kHz、NORMAL 为 1 kHz
（可用 CAPS 查询）。一个 FAST 帧可能包含多组样本，Seq 每包递增。

## FAST Scale 来源

固件 `Parameter/Plot.generated.inc` 与 `Comm/Plot.c`
共同提供真实 Plot 能力。常用值：

| ID | 信号 | Scale |
| --- | --- | --- |
| 0x0001/02/03 | Ia/Ib/Ic | 0.001 |
| 0x0010/11/12/13 | Id/Iq/Ud/Uq | 0.001 |
| 0x0014 | 电角度 Theta_e | 0.0002 |
| 0x0015/16 | Ualpha/Ubeta | 0.001 |
| 0x0020 | Observer Theta | 0.0002 |
| 0x0021 | Observer We | 0.1 |
| 0x0022/23/24/25 | ESO Theta/Wm/Td/Error | 0.0002/0.1/0.001/0.0002 |
| 0x0510 | Iq Ref | 0.001 |
| 0x0B05/06/07 | 注入、Id Ref、Id PI 输出 | 0.001 |

旧版 `0x0020/0x0021` 已不再是电流参考。
机械速度 `0x0502` 与 `0x0511` 为 NORMAL-only。

## VOFA 二进制发送示例

配置 Node 1 FAST 的 Ia/Ib，Config_ID=1：

```text
41 58 44 52 01 01 0C 01 01 00 01 02 01 00 02 00 00 00 00
```

启动 FAST：

```text
41 58 44 52 01 01 03 02 02 01
```

这些是 **二进制十六进制字节**，不是字符 "4"、"1" 等组成的 ASCII 文本。
