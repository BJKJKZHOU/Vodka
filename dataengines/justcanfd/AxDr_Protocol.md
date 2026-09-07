# AxDr Protocol（JustCanFd 当前快照）

> 本文用于记录 JustCanFd 当前需要解析的 AxDr_L_Motor 协议部分。  
> 参数 ID、类型、访问权限和 Plot Scale 的单一事实源是 AxDr_L_Motor `Parameter/parameter.yaml`；不要在本文或插件源码中另行维护一份参数字典。

## 1. 基础规则

- 多字节字段：Little Endian。
- `float`：IEEE-754 float32。
- **标准 CAN FD frame 是 AxDr 的规范 wire 表示。**
- CAN FD 使用 11-bit Standard ID，Node ID 当前为 1。
- CAN FD Data Field Length 只能是 `0..8, 12, 16, 20, 24, 32, 48, 64`。
- 应用字段不足对应 CAN FD 长度时，尾部使用 `0x00` padding。
- USB/UDP 不重新定义应用长度，只透明封装完整 CAN FD frame。

JustCanFd 使用的 USB/UDP 流式承载：

```text
Byte0..3  'A' 'X' 'D' 'R'
Byte4..5  uint16 CanId
Byte6     uint8 CanFdDataLength
Byte7..   CanFdData[CanFdDataLength]
```

`CanFdDataLength` 必须是合法 CAN FD data-field length；USB/UDP 中的数据字节必须与真实 CAN FD frame 完全一致，包括零填充。

## 2. 11-bit CAN ID

```text
bit10..6  Message Type
bit5..0   Node ID

CanId = (MsgType << 6) | NodeId
```

当前 AxDr_L_Motor 定义：

| MsgType | 名称 | Node 1 CAN ID |
|---:|---|---:|
| `0x02` | RESPONSE | `0x0081` |
| `0x04` | PLOT | `0x0101` |
| `0x07` | PARAMETER | `0x01C1` |
| `0x08` | EVENT | `0x0201` |
| `0x10` | NORMAL_DATA | `0x0401` |
| `0x18` | FAST_DATA | `0x0601` |

旧草案中的 CONTROL / VARIABLE / SYSTEM 消息已经不是当前接口。Motor Mode、Target 和普通参数统一通过 PARAMETER 访问；Enable / Run / Stop / Disable、Identification 等动作也使用 Parameter Dictionary 中的 `ACTION_*` 对象触发。

## 3. RESPONSE

公共响应有效字段：

```text
Byte0     Txn
Byte1     ReqMsgType
Byte2     ReqOp
Byte3     Status
Byte4...  Response Data
```

有效字段长度向上对齐到合法 CAN FD data-field length，剩余字节必须为 `0x00`。

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

## 4. PARAMETER

Message Type：`0x07`。

Operation：

```text
0x01 READ
0x02 WRITE
```

参数类型：

```text
0 U8
1 I8
2 F32
3 I32
4 U32
5 POSITION
6 ACTION
```

`POSITION` wire ABI 固定为 8 bytes：

```text
int32 Turn
float Theta
```

### 4.1 READ Request

有效字段：

```text
Byte0     Txn
Byte1     READ
Byte2..3  Parameter_ID
```

成功 RESPONSE Data：

```text
Byte4..5  Parameter_ID
Byte6     Type
Byte7..   Value
```

### 4.2 WRITE Request

普通值有效字段：

```text
Byte0     Txn
Byte1     WRITE
Byte2..3  Parameter_ID
Byte4     Type
Byte5..   Value
```

Action：

```text
Byte0     Txn
Byte1     WRITE
Byte2..3  Action_ID
Byte4     ACTION (6)
```

例如 F32 WRITE 有效字段为 9 bytes，因此实际 CAN FD data-field length 为 12，Byte9..11 必须填 0。

Action 不携带 Value。写请求由 Motor Thread 执行；成功接收请求并不等同于长动作已经完成。

WRITE RESPONSE Data 回显：

```text
Byte4..5  Parameter_ID / Action_ID
```

具体对象 ID、类型和访问规则必须以 AxDr_L_Motor `Parameter/parameter.yaml` 为准。

## 5. EVENT

Message Type：`0x08`。

当前与 Action 相关的完成事件有效字段：

```text
Byte0     0x03  ACTION_COMPLETE
Byte1     Txn
Byte2..3  Action_ID
Byte4     Status
```

长时间动作通过这个事件报告最终完成状态。其他 Event 同样按有效字段长度向上对齐到 CAN FD 合法长度并零填充。

## 6. PLOT

Message Type：`0x04`。

Operation：

```text
0x01 CONFIG
0x02 START
0x03 STOP
```

Group：

```text
0 FAST
1 NORMAL
```

### 6.1 CONFIG Request

有效字段：

```text
Byte0      Txn
Byte1      CONFIG
Byte2      Group
Byte3      Config_ID
Byte4      Channel_Count
Byte5..6   CH0 Parameter_ID
Byte7..8   CH1 Parameter_ID
...
```

```text
Used = 5 + Channel_Count * 2
FAST_MAX_CH   = 8
NORMAL_MAX_CH = 15
```

`Used` 向上对齐到合法 CAN FD data-field length，尾部 padding 为 0。

正在运行的 Group 不能直接重新 CONFIG；使用：

```text
STOP -> CONFIG -> START
```

### 6.2 CONFIG Response

成功时 RESPONSE 有效字段：

```text
Byte0      Txn
Byte1      ReqMsgType = PLOT
Byte2      ReqOp      = CONFIG
Byte3      Status = OK
Byte4      Group
Byte5      Config_ID
Byte6      Channel_Count
Byte7..8   CH0 Parameter_ID
Byte9..10  CH1 Parameter_ID
...
```

```text
Used = 7 + Channel_Count * 2
```

FAST 的物理量缩放由 `parameter.yaml` 中各对象的 `plot_scale` 定义。JustCanFd 使用由该文件生成的 `axdr_plot_meta.generated.h` 查表，不再在 `justcanfd.cpp` 手写 ID/Scale。

### 6.3 START / STOP

有效字段：

```text
Byte0 Txn
Byte1 START / STOP
Byte2 Group_Mask

bit0 FAST
bit1 NORMAL
```

## 7. FAST_DATA

CAN ID（Node 1）：`0x0601`。

有效字段：

```text
Byte0..1  uint16 Seq
Byte2     Config_ID
Byte3     Sample_Count
Byte4..   int16 Sample[Sample_Count][Channel_Count]
```

```text
Used = 4 + Sample_Count * Channel_Count * 2
```

实际 CAN FD data-field length 是 `Used` 向上对齐后的合法长度，尾部全部为 0。

因为 CAN FD padding 会使 `Len` 大于 `Used`，**不能再通过 `(Len - 4) / (Sample_Count * 2)` 反推 Channel_Count**。JustCanFd 必须先收到匹配 `Config_ID` 的成功 FAST `PLOT_CONFIG RESPONSE`，从该响应取得 `Channel_Count` 与通道顺序，再解析 FAST_DATA。

每个 int16 raw 值按对应参数的 `plot_scale` 恢复：

```text
physical = raw * plot_scale
```

若 Config_ID 未知或不匹配，JustCanFd 不解析该 FAST_DATA；若配置已知但本地缺少某个 Plot Scale，则仍可按已知通道数读取 raw int16 count，但不应用物理量缩放。

## 8. NORMAL_DATA

CAN ID（Node 1）：`0x0401`。

有效字段：

```text
Byte0..1  uint16 Seq
Byte2     Config_ID
Byte3     Channel_Count
Byte4..   float32 Channel[Channel_Count]
```

```text
Used = 4 + Channel_Count * 4
```

`Channel_Count` 在帧内，因此 JustCanFd 可直接得到有效字段长度；实际 CAN FD data-field length 必须等于 `Used` 向上对齐后的合法长度，尾部 padding 必须为 0。

NORMAL_DATA 已经是 float32 物理值，不需要 Plot Scale。

## 9. 参数元数据同步

Vodka 中执行：

```bash
python3 dataengines/justcanfd/sync_axdr_parameters.py
python3 dataengines/justcanfd/sync_axdr_parameters.py --check
```

生成的 `axdr_plot_meta.generated.h` 应随源码提交，但它不是新的事实源；源仍然是 AxDr_L_Motor `Parameter/parameter.yaml`。
