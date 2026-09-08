# App / Agent 集成设计

## 为什么拆成这些模块

`protocol.py` 只负责帧与数据结构；`Identity` 只负责载入身份和合法认证；`A1Client` 管理一条 asyncio BLE 会话；`LiveRecorder` 将流落地；`USBStorage` 是同步 USB 文件通道；AI 服务在调用方。这使蓝牙、UI 和 AI 不互相绑定，也便于以后移植到 Kotlin/Swift。

每台 A1 由一个宿主进程持有一条连接。手机钉钉、旧控制台、SDK CLI 如果同时抢同一台设备，会影响连接；不要把一次性 `status` CLI 当成能随意并行的跨进程调用。在应用进程中共享同一个 `A1Client`，它会串行化命令/写帧，并为通知提供独立订阅。

## 公开接口

| 模块 | 主要接口 |
| --- | --- |
| 身份 | `Identity.load(path)`；兼容 did/deviceId、corp_id/corpId、device_secret/deviceSecret、serial_number/sn |
| BLE | `scan(timeout)`、`connect(address=...)`、`close()`、`connected` |
| 设备 | `status()`（保留设备原 JSON）、`audio_settings()`、`recording_control(action)`、`set_live_upload(enabled)` |
| 录音 | `recordings(since=0, until=0, limit=100)`、`download(fid, destination, on_progress=...)`、`delete_recording(fid)` |
| 事件 | `async with client.subscribe(commands) as queue`；`await client.next_frame(queue, timeout)` |
| 音频 | `LiveRecorder.feed(frame)` / `close(reason)`；`convert_dtyj`；`export_memos`；`audio.validate_ogg` |
| USB | `with A1USB(identity)`；`info/authenticate/enter_adb`；`USBStorage.start_server/space/list/stat/read_chunks/download/mkdir/upload/delete_file/motor_test` |
| AI 路由 | `ModeRouter.route(transcript, recording_key=...)`，只产任务、不执行 |

蓝牙下载输出原始 DTYJ，不是 WAV；成功后调用 `convert_dtyj` 获得 Ogg/Opus。`on_progress(received_bytes, total_bytes)` 必须快速返回，不能在其中同步请求云服务。

`status` 暂不强行归一化所有字段，避免将未确认的数值冒充统一枚举。展示容量时确认来源单位：USB `space()` 返回 bytes；`GB = bytes / 1e9`，`GiB = bytes / 1024**3`。普通录音索引中的 `duration_hint_seconds` 是 16 位字段，只供提示；准确时长从音频得到。

## 事件与落地

`Frame(kind, command, sequence, payload)` 保留未翻译的协议数据，便于以后扩展。`LiveRecorder.feed` 返回 0 个或多个事件：

- `recording_started`：开始事件。
- `stream_attributes`：保留 `attrs`、`stream_type` 等设备原字段。
- `marker`：明确的 `0x0102` 标记，`ts` 是已研究的相对秒。
- `telemetry` / `telemetry_marker`：`0x000C` 遥测。已知 0x0B/0x01/argument0=0 的标记变体仍保留独立事件，不因为批次延迟而自动归入当前录音。
- `recording_saved`：`path/fid/device_id/recording_key/source/complete/duration_seconds/markers`。
- `recording_empty`：收到了开始/结束但没有音频；不生成假的空录音。

流只在观察到 start、收到明确 stop 且没有检测到序号间隙时标记 `complete=true`；中途接入、掉线、终止监听会保存 `-incomplete.ogg`，不能把它作为完整会议记录自动推送。TOC/CRC 检查不等同于完整解码；需要时安装 `audio` extra 后调用 `validate_ogg`。实时监听默认不每条完整解码，以免阻塞收包。

队列有 256 帧上限。消费者落后到溢出会报错并结束逻辑会话，不悄悄丢音频。磁盘故障或音频格式不认识时停止并保留 `.part`；人工检查后再重新监听。连接超时会关闭旧会话，防止迟到回复被下一条同名命令误用。

## 同步与去重应放在哪里

SDK 对单次下载重传做块去重，不维护一个全局 App 数据库。**不能说安装 SDK 后就再也不会重复同步。** 应用层以 `(device_id, fid)` 为主记录键：

1. 下载前查自己的数据库，完整本地文件已存在就跳过；不同设备必须分开目录。
2. 下载使用 `.part`，只有长度正确且块连续才发布正式文件。
3. `source=live` 和设备正式录音是同一逻辑录音的不同来源，不直接显示成两条，也不把不完整实时片段当成正式文件已备份。
4. 转录/总结/推送分别保存状态。相同 recording_key 的同一任务已经成功时不重复调用；失败可由用户重试。
5. 删除本地备份不等于删除设备源；是否允许重新同步由 App 显式选择。

更换连接、取消录音或拔线可能改变实际结果；不存在只用一个 fid 就跨所有设备安全去重的保证。

## 语音模式和权限

```
按键说话 → 本地 Ogg → 已授权的 ASR → ModeRouter → 对应 Agent 工作区
```

“开发模式，修复首页按钮”只改变宿主的任务路由，不改硬件固件。Router 只识别句首明确模式词；“不要进入开发模式”不会切模式。只说模式词会产出 `mode_changed` 而不是空的开发任务。Router 实例的模式由宿主管理；CLI 每次单独调用不持久保存状态。

录音可能包含他人的声音、引用或误识别，它不是身份认证。不要 `eval` 转录或把文本直接送系统 shell。将任务送到已经选定的项目；删除、对外消息、购买、部署按宿主现有授权规则处理。SDK 不替用户接受第三方服务费用或同意录音上传。

硅基流动转录、DeepSeek 总结、闪念贝壳 MCP 可复用你现有应用的适配器。密钥保存在宿主系统密钥库或私有配置，不能送给 A1 或作为 Skill 内容。长音频分段/并行 ASR 属于 AI 工作层，不在 BLE 收包循环中做；本 SDK 不承诺已经包含此前 Android App 的完整长音频处理流水线。

## 网页 / Android

电脑网页推荐：一个本地后台服务持有 `A1Client`，前端用 HTTP + WebSocket 接收状态/进度/完成事件。USB 同步函数放在线程中运行，不堵住 asyncio 事件循环。局域网暴露需独立访问控制；网页访问令牌与 deviceSecret 分开，不在前端发送 deviceSecret。这个 SDK 未附带一个自动开放局域网的 Web 服务。

Android 原生应用：复用现有已实测的 Kotlin `DingTalkA1Client`，按这里的消息路由/API 形状拆为模块；这版没有把依赖旧 App 的类伪装成可单独安装的 AAR。手机后台接收需要应用自己处理 Android 权限、前台服务和电池管理。若使用 SDK 的电脑服务，Android 也可仅作为 UI，但它就不是离线直连手机方案。

CLI/Skill 路线最短，不必先做 App。Skill 安装：把包内 `skills/dingtalk-a1` 文件夹复制到 Agent 支持的 skills 目录，配置 SDK 的 Python 可执行文件和外部身份文件路径。Skill 没有真实密钥，也不会修改全局 Agent 设置。
