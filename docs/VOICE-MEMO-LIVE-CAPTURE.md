# A1 语音备忘录实时接收与转录

本文记录一条已在自有 DingTalk A1 上实机验证的路径：电脑保持 BLE 鉴权连接，用户在 A1 上短按录音后，电脑直接接收设备主动推送的 Opus 音频，不需要打开钉钉 App。

## 最终数据流

```text
A1 短按录音
  -> 0x0100 start（包含 fid）
  -> 0x0116 音频属性（编码、采样率、stream_type）
  -> 多个 0x0117 二进制音频块
  -> 0x0100 stop
  -> 拆分 84 字节 Opus packet
  -> 封装 Ogg/Opus
  -> 原子保存 recordings/memo-<fid>.ogg
  -> SiliconFlow ASR
  -> recordings/memo-<fid>.json
  -> 网页轮询后显示、播放、下载和删除
```

监听器是被动的：它不会替用户开启麦克风，不会把内容发送到钉钉会话，也不会修改或删除 A1 内的文件。只有用户在实体设备上触发录音后，A1 才会推送音频。

## BLE 帧与 Opus 数据

完成 `getRandom (0x0008)` 与 `connectDevice (0x0133)` 鉴权后，监听器直接消费通知队列。

一次实测会话的事件顺序如下：

1. `0x0100` JSON，`action=start`，并包含本次录音的 `fid`。
2. `0x0116` JSON，包含 `attrs`、`stream_type` 等属性。
3. 连续的 `0x0117` 二进制帧。
4. `0x0100` JSON，`action=stop`。

当前样本中，`0x0117` 的关键字段采用大端序：

| 偏移 | 长度 | 含义 |
|---:|---:|---|
| 4 | 4 | `fid` |
| 16 | 4 | block sequence |
| 20 | 4 | 音频区长度 |
| 28 | N | Opus 音频区 |

音频区通常是一个 84 字节 Opus packet，也观察到单个推送批量携带多个 84 字节 packet。长度为零的 `0x0117` 是边界/控制哨兵，不应当作为损坏音频报错。

`attrs` 的实测形式类似：

```text
audio@opus@16000@84@1@16@32000@4
```

监听器把 Opus packet 按到达顺序写进标准 Ogg 页面。Ogg Opus 的 granule position 使用 48 kHz 时钟，每个 20 ms packet 增加 960；`OpusHead` 中的 input sample rate 保存设备属性中的原始输入采样率。

文件先写入 `.tmp`，收到停止事件并完成最后一个 Ogg EOS 页面后再原子重命名。因此网页不会读取到半成品。

## 转录

监听器使用 SiliconFlow 的标准语音转文字接口：

```text
POST https://api.siliconflow.cn/v1/audio/transcriptions
model=FunAudioLLM/SenseVoiceSmall
file=<刚生成的 Ogg 文件>
```

API Key 只从环境变量 `SILICONFLOW_API_KEY` 或本机 `.siliconflow-api-key` 读取。该文件已加入 `.gitignore`；不要把真实 Key 写入代码、文档、命令示例或提交历史。

转录结果与非敏感技术元数据保存为同名 JSON：

```json
{
  "kind": "voice_memo",
  "fid": 1700000000,
  "duration_seconds": 6.96,
  "packet_count": 348,
  "sample_rate": 16000,
  "stream_type": 1,
  "transcription": "示例转录文本",
  "transcription_error": null
}
```

音频保存与转录相互独立。即使网络、余额、API Key 或模型服务发生错误，Ogg 仍会保留，错误只记录在 JSON 中，可以稍后重试。

SiliconFlow 官方接口文档：<https://docs.siliconflow.cn/cn/api-reference/audio/create-audio-transcriptions>

## 启动

先安装项目依赖并准备只保存在本机的 `.a1-device.json`。在仓库根目录运行：

```powershell
$env:SILICONFLOW_API_KEY = "YOUR_API_KEY"
python tools\a1_memo_capture.py --config .a1-device.json --output-dir recordings
```

出现 `READY` 后即可在 A1 上短按录音。程序会持续监听，多次录音无需重启。按 `Ctrl+C` 停止监听。

另开一个终端启动网页控制台：

```powershell
python console\server.py --config .a1-device.json --output-dir recordings
```

网页每 1.5 秒重新读取本地状态；新 Ogg 完成原子重命名后会自动出现，并支持播放、下载以及一次确认后删除本地 Ogg 与转录 JSON。

## 已验证结果

2026-08-17 在一台自有 A1 上连续验证了两次物理短按录音：

| 样本 | `stream_type` | Opus packets | 时长 | Ogg 解码 | ASR |
|---|---:|---:|---:|---|---|
| A | 1 | 348 | 6.96 秒 | 348 帧 / 334080 samples | 成功 |
| B | 0 | 204 | 4.08 秒 | 204 帧 / 195840 samples | 成功 |

两个 Ogg 均由 FFmpeg/PyAV 的 Opus 解码器完整读取。这里的 `stream_type=0/1` 是原始实测值；两个样本不足以把它们永久定义为“长录音/语音备忘录”。因此实现保留原值，而不依赖该字段丢弃音频。

## 长录音与“标记”

长录音和短按语音备忘录是两条不同的持久化路径：

- 长录音通常进入 A1 文件索引，使用文件下载命令取得 DTYJ，再转换为 Ogg。
- 短按语音备忘录可在连接期间通过 `0x0117` 实时取得；它未必出现在普通文件索引中。

实测长录音按下“标记”时，BLE 上出现独立机器事件，而不是新的音频编码。样本中事件携带相对录音开始约 4 秒的时间值。标记不会改变 Opus packet，也不会自动把文字嵌入 DTYJ；已有样本的 `extr` chunk 为空。

因此正确处理方式是：

1. 转录完整长录音。
2. 保存标记事件的 `fid` 与相对秒数。
3. 如果 ASR 返回时间戳，则将标记映射到对应词句；若只返回纯文本，页面只能显示“约第 N 秒有标记”，不能精确定位某个字。

不要根据“有无标记”使用不同的 ASR 算法。标记是时间轴元数据，音频转录方式相同。

已有长录音可以直接运行：

```powershell
python tools\transcribe_audio.py recordings\a1-1700000000.ogg
```

若已从 BLE 事件确认标记位置，可在生成的 `a1-<fid>.json` 中保留：

```json
{
  "markers": [
    {"relative_seconds": 4, "source": "ble_machine_event"}
  ]
}
```

## 当前限制

- 多个进程不能可靠地同时独占同一台 A1 的 BLE 连接。常驻监听期间，按需扫描、文件下载或设备删除可能需要暂时停止监听器。
- 进程被强制结束时，尚未收到 stop 的当前录音还不会生成最终 Ogg；后续可增加滚动临时恢复日志。
- ASR 当前保存纯文本。若需要把长录音标记对齐到句子，需要换用能返回分段时间戳的接口或本地模型。
- 这套流程只应用于用户拥有或获授权测试的设备和录音。
