# JustCanFd

JustCanFd 是用于解析 AxDr CAN FD 数据的 VOFA+ 数据引擎，支持普通数据和快速数据两种消息类型。

协议帮助配置位于 `../generated/justcanfd.json`，详细测试记录见 [TESTING.md](TESTING.md)。

## 数据格式

USB 数据包使用以下帧头：

```text
magic[4] = {'A', 'X', 'D', 'R'}
uint16_t can_id  # 小端
uint8_t len
uint8_t payload[len]
```

消息类型由 `(can_id >> 6) & 0x1F` 得到：

- `0x10`：普通数据，CAN ID 示例为 `0x0400`，负载包含小端 `float32` 通道数据。
- `0x18`：快速数据，CAN ID 示例为 `0x0600`，负载包含按采样点优先排列的小端 `int16_t` 数据。

## Qt 路径

本项目使用 Qt 5.14.2，qmake 路径为：

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

编译生成的动态库位于 `build` 目录。

## 安装

在仓库根目录执行：

```bash
sudo install -o nobody -g nogroup -m 0644 \
    dataengines/justcanfd/build/libjustcanfd.so.1.0.0 \
    '/opt/vofa+/plugins/dataengines/libjustcanfd.so'

sudo install -o nobody -g nogroup -m 0644 \
    dataengines/generated/justcanfd.json \
    '/opt/vofa+/plugins/dataengines/justcanfd.json'
```

安装后完全退出并重新启动 VOFA+。

## UDP 快速测试

在 VOFA+ 中选择 `JustCanFd` 数据引擎和 UDP 数据接口，将本地端口设置为 `1347`，然后打开连接。

普通数据测试：

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode normal --count 150
```

快速数据与多采样点测试：

```bash
python3 dataengines/justcanfd/udp_test_sender.py \
    --mode fast --count 150 --samples-per-packet 8 --channels 2
```
