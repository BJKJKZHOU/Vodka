# AxDr Protocol V1 草案

> 状态：Draft  
> 目标：USB CDC 与 CAN FD 共用同一套应用层控制、变量访问和波形协议。

## 1. 基础规则

- 多字节字段：Little Endian。
- `float`：IEEE-754 float32。
- CAN FD 使用 11-bit Standard ID。
- USB CDC 只增加流式 framing，不改变应用层 Payload。
- 变量表是静态协议规范，不在连接时由固件动态上传。

## 2. 应用消息

```c
typedef struct
{
    uint16_t Id;      /* lower 11 bits */
    uint8_t  Len;     /* 0..64 */
    uint8_t  Data[64];
} AxDr_Msg_T;
```

## 3. 11-bit CAN ID

```text
bit10..6  Message Type
bit5..0   Node ID

CanId = (MsgType << 6) | NodeId
```

- Node 0：Broadcast。
- Node 1~63：Device。
- Broadcast 不返回 ACK。

| MsgType | 名称 | 方向 |
|---:|---|---|
| `0x00` | FAULT | Motor -> Master |
| `0x01` | CONTROL | Master -> Motor |
| `0x02` | RESPONSE | Motor -> Master |
| `0x03` | VARIABLE | Master -> Motor |
| `0x04` | PLOT | Master -> Motor |
| `0x05` | SYSTEM | 双向 |
| `0x10` | NORMAL_DATA | Motor -> Master |
| `0x18` | FAST_DATA | Motor -> Master |

Node 1 示例：

```text
CONTROL     0x041
RESPONSE    0x081
VARIABLE    0x0C1
PLOT        0x101
NORMAL_DATA 0x401
FAST_DATA   0x601
```

## 4. Transaction / Response

Master 发起的 CONTROL / VARIABLE / PLOT / SYSTEM 单播请求带 `uint8_t Txn`，范围 1~255。

RESPONSE：

```text
Byte0     Txn
Byte1     ReqMsgType
Byte2     ReqOp
Byte3     Status
Byte4...  Response Data
```

Status：

```text
0x00 OK
0x01 ERR_OP
0x02 ERR_LENGTH
0x03 ERR_VAR_ID
0x04 ERR_READ_ONLY
0x05 ERR_VALUE
0x06 ERR_STATE
0x07 ERR_CONFIG
0x08 ERR_BANDWIDTH
0x09 ERR_NOT_SUPPORTED
```

## 5. CONTROL

```text
Byte0     Txn
Byte1     Cmd
Byte2...  Argument
```

| Cmd | 名称 | Argument |
|---:|---|---|
| `0x01` | ENABLE | — |
| `0x02` | RUN | — |
| `0x03` | STOP | — |
| `0x04` | DISABLE | — |
| `0x05` | SET_MODE | `uint8 Mode` |
| `0x06` | SET_TORQUE | `float32 N*m` |
| `0x07` | SET_SPEED | `float32 rad/s` |
| `0x08` | SET_POSITION | `int32 Turn + float32 Theta(rad)` |

Mode：0 Torque，1 Speed，2 Position。

## 6. VARIABLE

变量类型由静态 `Var_ID` 表唯一确定，所以 READ/WRITE 不重复发送 Type。

```text
READ:
Byte0 Txn
Byte1 0x01
Byte2~3 Var_ID

WRITE:
Byte0 Txn
Byte1 0x02
Byte2~3 Var_ID
Byte4... Value

SAVE:
Byte0 Txn
Byte1 0x03
```

`SAVE` 保存当前支持持久化的参数集合；具体 NVM 实现不属于协议层。

## 7. PLOT

固定：

```text
FAST   20 kHz
NORMAL 1 kHz
```

V1 不支持任意采样率。

Operation：

```text
0x01 CONFIG
0x02 START
0x03 STOP
```

### 7.1 PLOT_CONFIG Request

```text
Byte0      Txn
Byte1      CONFIG
Byte2      Group       (0 FAST / 1 NORMAL)
Byte3      Config_ID
Byte4      Channel_Count
Byte5~6    CH0 Var_ID
Byte7~8    CH1 Var_ID
...
```

```text
Len = 5 + Channel_Count * 2
FAST_MAX_CH   = 8
NORMAL_MAX_CH = 15
```

运行中的 Group 不直接 CONFIG，使用：

```text
STOP -> CONFIG -> START
```

### 7.2 PLOT_CONFIG Response

配置成功时必须回显实际接受的配置：

```text
Byte0      Txn
Byte1      ReqMsgType = PLOT
Byte2      ReqOp      = CONFIG
Byte3      Status     = OK
Byte4      Group
Byte5      Config_ID
Byte6      Channel_Count
Byte7~8    CH0 Var_ID
Byte9~10   CH1 Var_ID
...
```

```text
Len = 7 + Channel_Count * 2
```

失败时只返回公共 RESPONSE Header 和错误码，不回显变量列表。

回显不是动态变量表。它只用于确认当前 Config_ID 与通道顺序；上位机/VOFA 插件根据同一份静态 Var_ID 表确定名称、单位和 FAST Plot Scale。

### 7.3 START / STOP

```text
Byte0 Txn
Byte1 START/STOP
Byte2 Group_Mask

bit0 FAST
bit1 NORMAL
```

## 8. FAST_DATA

- 采样率：20 kHz。
- 数据：int16 定点。
- 控制内部仍保持 float 精度。

Header：

```text
Byte0~1  uint16 Seq
Byte2    Config_ID
Byte3    Sample_Count
```

随后按 Sample Major 排列：

```text
Sample0: CH0 CH1 ...
Sample1: CH0 CH1 ...
...
```

每个通道为 `int16_t`。

物理值：

```text
Physical_Value = Raw * Plot_Scale
```

3 通道时：

```text
4B Header + 10 * (3 * 2B) = 64B
```

即一帧 CAN FD 可装 10 个三通道采样点。

## 9. NORMAL_DATA

- 采样率：1 kHz。
- Plot 数据统一 float32。

```text
Byte0~1  uint16 Seq
Byte2    Config_ID
Byte3    Channel_Count
Byte4... float32 CH0, CH1, ...
```

最大 15 通道：

```text
4 + 15 * 4 = 64B
```

变量真实类型只影响 VAR_READ/VAR_WRITE；进入 NORMAL Plot 后统一转换为 float32。

## 10. Buffer

20 kHz ISR 不执行 USB/CAN Send：

```text
20 kHz ISR -> Fast Ping-Pong Buffer -> Comm Thread -> FAST_DATA
1 kHz Tick -> Normal Snapshot Buffer -> Comm Thread -> NORMAL_DATA
```

采样与 Transport 发送解耦。Transport 卡顿最多影响遥测，不允许拖慢电流环。

## 11. USB CDC Transport

USB 是连续字节流，V1 使用：

```text
Byte0~3  "AXDR"
Byte4~5  uint16 CanId
Byte6    uint8 Len
Byte7... Data[Len]
```

约束：

```text
CanId <= 0x7FF
Len <= 64
```

USB Transport 不额外增加应用层 CRC；USB 与 CAN FD 均依赖各自链路层 CRC。一个 CDC Write 可以聚合多个完整 AXDR Frame。

## 12. 静态 Variable ID 表

Access：RO / RW / RW-D（仅 SERVO_DISABLED 可写）。  
Plot：F=FAST，N=NORMAL。

| Var ID | Name | Type | Access | Plot | Unit | FAST Scale |
|---:|---|---|---|---|---|---:|
| `0x0001` | Ia | FLOAT32 | RO | F/N | A | 0.001 |
| `0x0002` | Ib | FLOAT32 | RO | F/N | A | 0.001 |
| `0x0003` | Ic | FLOAT32 | RO | F/N | A | 0.001 |
| `0x0004` | Vbus | FLOAT32 | RO | N | V | — |
| `0x0010` | Id | FLOAT32 | RO | F/N | A | 0.001 |
| `0x0011` | Iq | FLOAT32 | RO | F/N | A | 0.001 |
| `0x0012` | Ud | FLOAT32 | RO | F/N | V | 0.001 |
| `0x0013` | Uq | FLOAT32 | RO | F/N | V | 0.001 |
| `0x0014` | Theta_e | FLOAT32 | RO | F/N | rad | 0.0002 |
| `0x0020` | Id_Ref | FLOAT32 | RO | F/N | A | 0.001 |
| `0x0021` | Iq_Ref | FLOAT32 | RO | F/N | A | 0.001 |
| `0x0100` | Turn | INT32 | RO | N | turn | — |
| `0x0101` | Theta_m | FLOAT32 | RO | F/N | rad | 0.0002 |
| `0x0102` | Wm | FLOAT32 | RO | F/N | rad/s | 0.1 |
| `0x0110` | Wm_Ref | FLOAT32 | RO | F/N | rad/s | 0.1 |
| `0x0111` | Pos_Ref_Turn | INT32 | RO | N | turn | — |
| `0x0112` | Pos_Ref_Theta | FLOAT32 | RO | N | rad | — |
| `0x0200` | Servo_State | UINT8 | RO | N | — | — |
| `0x0201` | Ctrl_Mode | UINT8 | RO | N | — | — |
| `0x0210` | Te_Target | FLOAT32 | RO | N | N*m | — |
| `0x0211` | Wm_Target | FLOAT32 | RO | N | rad/s | — |
| `0x0212` | Pos_Target_Turn | INT32 | RO | N | turn | — |
| `0x0213` | Pos_Target_Theta | FLOAT32 | RO | N | rad | — |
| `0x1000` | Id_Kp | FLOAT32 | RW | — | V/A | — |
| `0x1001` | Id_Ki | FLOAT32 | RW | — | V/(A*s) | — |
| `0x1010` | Iq_Kp | FLOAT32 | RW | — | V/A | — |
| `0x1011` | Iq_Ki | FLOAT32 | RW | — | V/(A*s) | — |
| `0x1100` | Speed_Kp | FLOAT32 | RW | — | A/(rad/s) | — |
| `0x1101` | Speed_Ki | FLOAT32 | RW | — | A/rad | — |
| `0x1200` | Position_Kp | FLOAT32 | RW | — | (rad/s)/rad | — |
| `0x2000` | User_I_Max | FLOAT32 | RW | N | A | — |
| `0x2001` | User_Te_Max | FLOAT32 | RW | N | N*m | — |
| `0x2002` | User_Wm_Max | FLOAT32 | RW | N | rad/s | — |
| `0x2010` | Motor_I_Max | FLOAT32 | RO | N | A | — |
| `0x2011` | Motor_Te_Max | FLOAT32 | RO | N | N*m | — |
| `0x2012` | Motor_Wm_Max | FLOAT32 | RO | N | rad/s | — |
| `0x2100` | Motor_Pp | UINT8 | RW-D | — | pair | — |
| `0x2101` | Motor_Rs | FLOAT32 | RW-D | — | Ohm | — |
| `0x2102` | Motor_Ld | FLOAT32 | RW-D | — | H | — |
| `0x2103` | Motor_Lq | FLOAT32 | RW-D | — | H | — |
| `0x2104` | Motor_Flux | FLOAT32 | RW-D | — | Wb | — |
| `0x2105` | Motor_J | FLOAT32 | RW-D | — | kg*m^2 | — |
| `0x2106` | Motor_B | FLOAT32 | RW-D | — | N*m/(rad/s) | — |
| `0x2200` | Enc_Dir | INT8 | RW-D | — | — | — |
| `0x2201` | Theta_Off | FLOAT32 | RW-D | — | rad | — |

FAST 白名单：Ia/Ib/Ic、Id/Iq、Id_Ref/Iq_Ref、Ud/Uq、Theta_e/Theta_m、Wm/Wm_Ref。

## 13. SYSTEM / FAULT

SYSTEM V1 至少保留 GET_INFO，用于返回 Protocol Version、Firmware Version、Node ID、Transport Mask。

FAULT 保留 FAULT_READ / FAULT_CLEAR，精确 Payload 在故障体系实现时冻结。

## 14. V1 明确不做

```text
运行时变量表上传
任意 MCU 地址读写
对象树 / RPC
JSON 设备自描述
任意采样率
ISO-TP
完整 CANopen / CiA402
文件传输 / 固件升级
通信鉴权
USB 断线自动停机
```

## 15. 实现边界

- CONTROL 调用 Motor 已有公开接口，不维护第二套状态机。
- Target 使用 CONTROL 写入，变量表中的 Target 仅用于只读监控。
- FAST 定点化只发生在 Plot Buffer，不改变控制内部 float 精度。
- 已分配的 MsgType / Cmd / Var_ID 不应复用；删除后保留为 Reserved。
