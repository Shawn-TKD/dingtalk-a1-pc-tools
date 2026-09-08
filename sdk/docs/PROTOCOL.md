# 协议速查与实验边界

依据：已有 owner-tested Python / Kotlin 客户端、2026-09-07 USB 实测、2026-09-08 固件复核。研究资料所在本机目录为 `a1-firmware-lab`；SDK 包不分发官方固件或全部反编译产物。

## BLE

服务 `0000fe3c-0000-1000-8000-00805f9b34fb`，写 `...fe1c...`，通知 `...fe1b...`。

帧头：`kind:u8 | command:u16be | message_id:u8 | length:u32be`；kind 0x13 请求，0x31 响应，0x14 通知。正文 JSON 或二进制。固件收到不支持命令仍可能回 code=200，不能据此宣称功能有效。

客户端先 `0x0008 {did,corp_id}` 获取 32 字节 ASCII 挑战，再 `0x0133 {did,corp_id,model,sdk_ver,timestamp,token}`。`token = hex(AES-128-CBC(key, IV=key, challenge))`，`key = ASCII(deviceSecret[:16])`，无 padding。这里 deviceSecret 指此前从客户端取得的 32 字符串，不是 DTIOT 原始 R。不要二次派生。

| 命令 | 用途 / 正文 |
| --- | --- |
| 0x0132 | 状态，`{did}` |
| 0x0110 | 普通录音索引，`{did,s_fid:"0",e_fid:"0",recently:100}`；响应 u16be code、u16be count，再 count 个 8 字节记录（flag:u16、fid:u32、raw:u16） |
| 0x0111 | 下载，`{did,fid:"...",offset:0,progress:65537}` |
| 0x0114 | 设备推文件属性，回 kind=0x31、原 message_id、`{code:200}` |
| 0x0115 | 块：保留2、fid4、保留2、number4、length4、data、保留4、CRC-32/BZIP2；整数大端。按原 message_id ACK |
| 0x0112 / 0x0113 | 取消同步 / 删除一个普通录音；fid 用字符串 |
| 0x0100 | action=start/stop/pause/resume/get/set；set 使用 params:[{key,val}] |
| 0x0116 | 实时流属性；保留 attrs / stream_type |
| 0x0117 | 实时流：fid 在4..8，block seq在16..20，音频字节数在20..24，28开始是84字节定长 Opus 单元 |
| 0x0102 | 标记 {fid,ts,type}；ts相对秒，保留原type |
| 0x000C | 20字节记录的批量遥测；不可把所有事件都当标记 |
| 0x014A | 原始只读同步，`{did,path,offset}`；响应含 size/offset/block_size=8000/块计数 |
| 0x014B | 原始块：sequence4、length4、data、CRC-32/BZIP2；回200，CRC错回0x0193 |
| 0x014C | 取消原始同步 |

`upload_stream` 值 1/2 分别开/关。`delete_after_upload`、`aes`、`incognitomode`、`aikey_option` 等会改变配置，本包不在连接时自动修改。特别不能为了收音而先清除隐私模式、解绑或把录音自动删除打开。

## USB

VID:PID `17ef:0101`，vendor usage page 0xff00，usage 1。Report ID=1，每个报告127字节有效载荷（补零），总报告128字节。

USB 协议帧头是 **小端** `<BHBI`，与 BLE 不同。400查信息，403 `{corpId,did}` 取挑战，401 `{did,payload}` 认证，402 `{work_mode:1}` 开 ADB。`corpId` 在 HID 中采用驼峰字段，不能简单照搬 BLE JSON。

ADB 必须先完成本设备身份认证和模式切换；本包不会尝试枚举他人身份。文件数据用 adb server 的 SYNC `LIST/STAT/RECV/SEND`，不走终端 PTY，避免二进制换行被改。host服务长度是4位十六进制，SYNC长度是u32le，DATA最多65536字节。`lnv_motor_test` 是进入 ADB 后执行的 NuttX shell builtin，不是 HID command 400–403 中的“USB震动指令”。

此 SDK 文件入口只公开 `/emmc`，写/删限制 `/emmc/mindlink`。没有暴露 `/dev`、分区刷写、原始 Flash 写入或任意 shell。`motor_test` 只调用已知的 `lnv_motor_test`，执行固定序列，不接受时长；固件中找到电机函数不等于存在 BLE 对外震动命令。

## 本地音频

DTYJ/BABA：开头 BABA、u32le总长（不含前8字节）、DTYJ。fmt区包含采样率和每条记录尺寸；data区的记录前4字节是**不透明标志**，已见0x20、0xA0、0xC0、0x1A0，不能把高位当损坏。余下字节保持为 Opus 包。

离线备忘录文件 `/emmc/audio/00000000000000`：8字节总头（4字节be数量、5A、输入采样率kHz、码率kbps、包长）；每条12字节头（5A、6字节be时间戳、2字节be整数秒、3字节be音频长度），随后定长 Opus 包。设备整数时长不一定准确；也存在原文件条目不能完整解码的真实样本。

Ogg 采用 [RFC 6716 的 TOC/包时长规则](https://www.rfc-editor.org/rfc/rfc6716.html#section-3.1) 计算48kHz时间轴，不再假设每个包必为20ms。Ogg CRC 是容器必需校验；未额外给录音新增文件哈希。`validate=True` 还会本地完整解码核对，但它不修复原始坏包。

## 实验接口

```python
ap = await client.experimental.open_wifi()  # type=0；可能取消BLE同步、阻止休眠
# 由用户/宿主系统连接 ap['ssid']，密码 ap['passwd']；不要公开这些字段
from dingtalk_a1.experimental import wifi_download
# 在独立线程执行，ip取实际返回值；路径先从设备确认，不能猜文件名
result = await asyncio.to_thread(wifi_download, ap['ip'], '/emmc/audio/已知文件名', 'recording.dtyj')
await client.experimental.close_wifi()
```

HTTP根目录映射 `/emmc`，GET已知路径，不提供目录列表或任意上传。URI最多约63字节。不要把 `type=1` 的 OTA 5922 端口当文件传输服务，本 SDK 不开放该类型。

定时录音：`schedules()`读；`replace_schedules([{start,end,sid}])`整组替换，时间为Unix秒，最多20条，空数组清空。`send_marker(fid,seconds)`发送type=2。上述实验均来自静态控制流，**需要你自己的固件上验证动作本身**，不能只看返回200。

原始 BLE 文件读取 `0x014A/0x014B/0x014C` 已实现为 `experimental.download_raw_file()`：只允许已知 `/emmc` 路径，8000字节分块，逐块 CRC-32/BZIP2，支持与 `.part` 长度严格一致的 offset 续传。固件的 CRC 失败重发路径会在每次发送时清零其重试计数，可能导致坏链路无限重发；SDK 本地最多接受三次连续 CRC 失败并取消连接。这仍是静态恢复接口，需要实机验收；目录枚举和唯一备份优先使用已验证的 USB 路线。
