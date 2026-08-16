# A1 BLE protocol notes

本文只记录已经通过自有设备验证、且本仓库代码实际使用的部分。

## GATT

| 用途 | UUID |
|---|---|
| 主服务 | `0000fe3c-0000-1000-8000-00805f9b34fb` |
| 主机写命令 | `0000fe1c-0000-1000-8000-00805f9b34fb` |
| 设备通知 | `0000fe1b-0000-1000-8000-00805f9b34fb` |

## 帧格式

```text
offset  size  meaning
0       1     frame type: 0x13 request, 0x31 response/ack
1       2     command, unsigned big-endian
3       1     sequence
4       4     payload length, unsigned big-endian
8       N     UTF-8 JSON or command-specific binary payload
```

超过一次 ATT 负载的应用层帧会继续分片。接收端必须按 8 字节头部声明的总长度重组，而不能把每个 ATT notification 当成新命令。

## 本项目使用的命令

| command | 名称 | 用途 |
|---|---|---|
| `0x0008` | getRandom | 获取 32 字符挑战值 |
| `0x0100` | audio record option | 已识别，默认工具不发送 |
| `0x0110` | getFileList | 获取录音索引 |
| `0x0111` | request file | 请求一条录音 |
| `0x0114` | file attributes | 文件属性/确认 |
| `0x0115` | file block | 文件数据/确认 |
| `0x0132` | sync device status | 电量、存储、版本等 |
| `0x0133` | connectDevice | 带 token 的鉴权 |
| `0x0134` | disconnectDevice | 结束会话 |

危险命令（例如 reset、delete、OTA）不在客户端接口中暴露。

## 鉴权

1. 客户端发送 `0x0008`，包含自己的 `did` 与 `corp_id`。
2. A1 返回 `{"random":"<32 ASCII chars>"}`。
3. 客户端用设备自己的 `deviceSecret` 计算 token。
4. 客户端发送 `0x0133`，包含 `did`、`corp_id`、`model`、`sdk_ver`、Unix 时间戳和 token。
5. `code == 200` 才能继续读取状态或录音。

算法见 README。只有 `deviceSecret` 的前 16 个 ASCII 字符进入 AES key/IV；不能先做十六进制解码。

## 文件索引与容器

已观察到的索引响应开头是大端状态码和记录数，随后每条记录占 8 字节。`fid` 表现为录音开始时刻的 Unix 时间戳。

下载得到的容器使用 `BABA`/`DTYJ` 标记，内部为固定大小记录，每条记录包含 4 字节前缀和一个 Opus 包。`tools/dtyj_to_ogg.py` 为它生成标准 `OpusHead`、`OpusTags` 和 Ogg 页。
