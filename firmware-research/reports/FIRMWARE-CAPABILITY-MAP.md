# DingTalk A1 V1.6.88 固件能力地图

日期：2026-09-07；复核更新：2026-09-08。对象为官方 OTA `V1.6.88-202601291628` 和此前从用户自有设备只读导出的四个程序分区。

参见 [研究复核与纠正](RESEARCH-REVIEW-20260908.md)：反编译质量、命令表正确对应关系、DTIOT 配置边界、秘密派生和 OTA 进度的限制。

本文把自动反编译结果整理为面向客户端和后续固件研究的接口地图。除文中明确标注的实机结果外，其余结论来自静态控制流，尚未向设备发送未知或破坏性命令。

## 1. 运行结构

```text
BLE FE3C service
  -> 协议拆包（8 字节头 + body）
  -> dtiot_ble_recv_data
  -> 鉴权 / 录音 / 文件 / 定时任务 / Wi-Fi / 系统控制
  -> 音频、EMMC、显示、电机和电源管理模块

USB HID 17EF:0101
  -> 400 读取设备信息
  -> 403 获取挑战
  -> 401 使用已绑定身份认证
  -> 402 切换 work_mode
  -> work_mode=1 时启动 adbd，并断开 BLE
```

BLE 和 USB/ADB 是两套入口，但最终调用同一批录音、文件和设备状态模块。USB 模式不是标准 U 盘或 MTP。

## 2. BLE 基础帧

普通协议帧的线格式为：

| 偏移 | 长度 | 含义 |
| ---: | ---: | --- |
| 0 | 1 | magic：`0x13` 请求、`0x31` 响应；设备通知另有 `0x14` |
| 1 | 2 | command，大端 |
| 3 | 1 | message id |
| 4 | 4 | body length，大端 |
| 8 | N | JSON 或二进制 body |

固件接收缓冲区上限为 40,000 字节；普通发送接口拒绝 60,001 字节及以上的 body。BLE 分片仍由 GATT 层继续拆分。

## 3. 完整请求分发表

下表直接来自 `dtiot_ble_recv_data` 的控制流。风险等级是本项目为实机研究添加的标注，不是厂商定义。

| 命令 | 处理路径 | 用途 | 风险 |
| ---: | --- | --- | --- |
| `0x0003` | `dtiot_ble_bind_get_device_info` | 设备信息 | 低 |
| `0x0004` | `dtiot_ble_bind_reset_device` | 重置/解绑相关动作 | 高，未盲试 |
| `0x0006` | `dtiot_ble_bind_get_active_info` | 激活信息 | 低 |
| `0x0007` | `dtiot_ble_bind_active_device` | 激活设备 | 高，可能改变身份状态 |
| `0x0008` | `dtiot_ble_bind_get_random` | 获取鉴权随机数 | 低 |
| `0x0009` | `dtiot_ble_bind_auth` | 鉴权响应 | 低 |
| `0x0100` | `dtiot_ble_bind_on_audio_request` | 录音控制和参数 | 中 |
| `0x0101` | `dtiot_ble_bind_on_voiceprint_request` | 启停一条 BLE 实时音频流 | 中 |
| `0x0102` | `dtiot_ble_bind_on_app_remark` | App/设备录音标记交互 | 低 |
| `0x0110` | `dtiot_ble_bind_get_file_list` | 查询普通录音索引 | 低 |
| `0x0111` | `dtiot_ble_bind_file_sync` | 普通录音同步 | 中 |
| `0x0112` | `dtiot_ble_bind_file_sync_cancle` | 取消同步 | 低 |
| `0x0113` | `dtiot_ble_bind_file_delete` | 删除普通录音 | 高 |
| `0x0114` | `dtiot_ble_bind_file_header_send` | 文件头发送/确认链 | 中 |
| `0x0115` | `dtiot_ble_bind_file_block_send` | 文件块发送/确认链 | 中 |
| `0x011A` | `dtiot_ble_bind_on_schedule_recording` | 定时录音任务 | 中 |
| `0x0120` | `dtiot_ble_bind_on_request_open_ap` | 开启设备 Wi-Fi AP | 中 |
| `0x0121` | `dtiot_ble_bind_on_request_close_ap` | 关闭设备 Wi-Fi AP | 中 |
| `0x0130` | `dtiot_ble_bind_on_get_trans_info` | 传输身份信息 | 敏感 |
| `0x0132` | `audio_status` | 录音、电量、存储和版本状态 | 低 |
| `0x0133` | `dtiot_ble_bind_on_request_connect_device` | 建立逻辑会话 | 低 |
| `0x0134` | `dtiot_ble_bind_on_request_disconnect_device` | 断开逻辑会话 | 低 |
| `0x0135` | `dtiot_ble_bind_query_fw_version` | 固件版本 | 低 |
| `0x0136` | `dtiot_ble_bind_on_request_system_control` | 系统控制 | 混合，见下文 |
| `0x0137` | `dtiot_ble_gray_switch_get_response` | 灰度开关 | 中 |
| `0x014A` | `dt_raw_transfer_sync` | 按路径读取原始文件 | 低（只读） |
| `0x014B` | 设备发出的原始文件块 | 二进制数据帧 | 低（接收） |
| `0x014C` | `dt_raw_transfer_cancel` | 取消原始读取 | 低 |

未知命令当前会收到 `code=200`，但这不表示执行了动作。

## 4. 录音控制 `0x0100`

请求体通过 `action` 选择动作：

- `start`：开始普通录音；
- `stop`：停止；
- `pause`：暂停；
- `resume`：继续；
- `get`：读取当前参数；
- `set`：用 `params` 数组写参数。

`set` 的每项是 `{"key":"...","val":number}`。固件确认支持：

| key | 内部编号 | 值 |
| --- | ---: | --- |
| `upload_stream` | 2 | 1 开、2 关 |
| `delete_after_upload` | 3 | 0/1 |
| `mode` | 5 | 0..5 |
| `aes` | 6 | 0/1，非 1 归零 |
| `incognitomode` | 7 | 0/1 |
| `force_sync_incognito` | 8 | 0/1，非 1 归零 |
| `aikey_option` | 9 | 只接受 1000 或 1001 |
| `stream_record` | 10 | 0/1，非 1 归零 |

对应持久化键包括：

- `persist.dt.aud.mode`
- `persist.dt.aud.aes`
- `persist.dt.aud.incognitomode`
- `persist.dt.aud.incognito_flag`
- `persist.dt.aud.force_sync`
- `persist.dt.aud.key_option`
- `persist.dt.aud.stream_rec`
- `persist.dt.aud.del_after_upload`

这说明电脑或 Android 客户端可以远程开始、暂停、恢复和停止录音，并配置是否同时推送实时流。`start` 不是语音备忘录按钮事件，而是普通录音控制。

## 5. AI 键、录音键和实时输入

`dt_button` 的状态机显示两组硬件事件：

- AI 键按下、松开、单击、双击、长按；
- 录音键按下、松开、单击、长按、超长按。

AI 键长按的默认路径：

1. BLE 逻辑会话已连接：发送 HEAD，启动 chat stream；
2. BLE 未连接：创建或追加 `/emmc/audio/00000000000000`，录制本地语音备忘录；
3. 松开：停止对应的 chat stream 或 voice memo。

`persist.dt.aud.key_option` 只允许 `1000`、`1001`。`1000` 是固件日志中的 original mode；`1001` 会改变长按时开始/停止录音的时机。这个开关可通过 `0x0100 set` 修改，不需要刷固件，但仍应先实机确认交互再暴露给普通用户。

电脑端的“开发模式、灵感模式、调研模式、复盘模式”无需写进固件：保持同一条 chat stream，在 Agent 收到完整语音后按客户端当前模式路由即可。硬件仍是一键按住说话、松手提交。

## 6. 标记 `0x0102`

标记能力不是音频文件名上的简单标志：

- 开关持久化在 `persist.dt.remark`；
- 设备在录音流中形成 `ts`、`fid`、`type`；
- 设备调用 `dtiot_ble_send_request(0x0102, ...)` 主动发给客户端；
- JSON 字段为字符串形式的 `ts`、字符串形式的 `fid` 和数值 `type`；
- App 回传的 `0x0102` 至少处理 `type=2`；
- 连续标记有约 3 秒节流，过密会忽略。

因此客户端可以把标记保存成时间轴事件，用于“重点片段”“待办”“问题”“代码修改点”，转录后再把标记附近的文本单独交给 Agent。

## 7. 定时录音 `0x011A`

支持两个动作：

```json
{"action":"get"}
```

以及：

```json
{
  "action": "set",
  "current": 1780000000,
  "params": [
    {"start": 1780000100, "end": 1780003700, "sid": 1}
  ]
}
```

静态控制流确认：

- 最多保存 20 个区间；超出会截断；
- `start`、`end`、`sid` 为每项字段；
- 无 `params` 或空数组会清空任务；
- `current` 可用于校准设备时间；
- 只接受合法且尚未结束的区间；
- 数据持久化到 `/emmc/schedule/schedule.dat`。

这可以直接形成“会议前自动开录”“访谈时间段自动录音”，不必让 Agent 常驻保持按钮状态。

## 8. 原始文件读取 `0x014A/0x014B`

`0x014A` 请求体：

```json
{"path":"/emmc/audio/00000000000000","offset":0}
```

已恢复的行为：

- `path` 必填，固件本地缓冲区允许最多约 63 字节；
- `offset` 可选，支持断点续传；
- 直接以 `rb` 打开传入路径；本处理函数没有 `/emmc/audio` 白名单；
- 响应 JSON 包含 `code`、`path`、`size`、`offset`、`block_size`、`total_blocks`、`remain_blocks`；
- 默认文件数据块为 8000 字节；
- 正在传输时发起新请求会先取消旧请求。

随后设备用 `0x014B` 发送二进制 body：

```text
sequence_be32 | data_length_be32 | data | crc_be32
```

客户端对每块回 `code=200` 后继续；`code=0x193` 会让设备重发当前块。固件最多重试 3 次。

这条通道比普通录音索引更通用，适合：

- 读取语音备忘录聚合容器；
- 读取 `/emmc/schedule/schedule.dat`；
- 在 BLE 模式下读取已知路径文件；
- 做可恢复的后台同步。

它目前只证明“按路径读取”，没有发现对应的任意路径 BLE 写入命令。写入个人文件仍应使用已验证的 USB/ADB 通道。

## 9. Wi-Fi AP、HTTP 下载与 TCP OTA

`0x0120` 会在已经通过 DID 检查的 BLE 会话上创建临时热点，并返回：

```json
{
  "ssid": "DingTalkA1_XXXXXX",
  "passwd": "12345678",
  "ip": "192.168.1.1",
  "port": 0,
  "url": "http://192.168.1.1/audio/",
  "code": 200
}
```

其中：

- SSID 后缀为 6 个大写字母，前 2 个随机，后 4 个优先取设备 SN；
- 密码为每次生成的 8 位随机数字；
- 获取接口 IP 失败时回退到 `192.168.1.1`；
- 建热点后会提高 CPU 频率、阻止休眠，并取消正在进行的 BLE 文件同步；
- `0x0121` 关闭热点并恢复 CPU/休眠状态。

请求中的 `type` 决定热点上启动的服务：

### `type=0`：录音 HTTP 下载

- 启动 NuttX 的只读 HTTP/1.0 服务；初始化参数 `0x5000` 对应网络字节序端口 80；
- 只接受 `GET`，支持 HTTP/1.0 和 HTTP/1.1 请求行；
- 请求 URI 最长约 63 字节；非零 `Content-Length` 会被拒绝；
- Web 根目录直接映射到 `/emmc`，即 URL `/audio/<name>` 对应 `/emmc/audio/<name>`；
- 不提供目录列表，客户端必须先从 BLE 文件列表得到文件名；
- 可识别 html/css/txt/json/js/png/gif/jpeg/mp3 等 MIME，其他文件返回 `application/octet-stream`；
- HTTP 层没有另外发现 token 或签名校验，访问控制主要依赖 BLE 鉴权后才获得的随机热点密码。

因此 BLE 适合发现、鉴权和列目录，Wi-Fi HTTP 适合真正搬运大录音。返回 JSON 的 `port=0` 不是监听在 0 端口；URL 省略端口且服务器按 80 初始化。

### `type=1`：OTA TCP 上传

- 监听 TCP `0x1722`，即 5922 端口；
- TCP 仍复用 8 字节 BLE 协议帧头；
- 只接受 `0x0114` 文件头和 `0x0115` 文件块；
- 文件头必须包含 `size`、`verify_code`、`attrs`、`version`，且 `attrs` 必须为 `ota@bin`；
- 文件块包含保留字段、CRC、序号、长度和数据，固件逐块校验后写入 `/emmc/ota.zip`；
- 其他命令返回 `{"code":8}`。

所以 5922 端口不是任意文件上传接口，也不是录音下载加速接口。

## 10. 系统控制 `0x0136`

请求体使用 `key` 和可选 `val`。目前分支只接受 `1..5` 和 `10001`：

| key | 静态动作 | 建议 |
| ---: | --- | --- |
| 1 | 保存报告并进入系统级重启/关机路径 | 不盲试 |
| 2 | 清空 `/emmc/audio` 并执行后续系统动作 | 破坏性，禁止盲试 |
| 3 | 删除 `/emmc/ota.zip` | 不需要测试 |
| 4 | 向 OTA 状态机发送事件 9 | 不需要测试 |
| 5 | 设置 `persist.dt.logrecord`；`val=1` 时启动日志线程 | 可研究，但会写配置 |
| 10001 | 返回 `battery_percent` | 低风险 |

现有分支没有调用电机函数，故不能把 `0x0136` 直接描述为“震动命令”。

## 11. 震动能力与缺口

固件存在明确的电机函数：

- 核心包装支持毫秒时长；
- 已见固定 300 ms、500 ms；
- 按键关机提醒会重复 200 ms；
- 最终关机前会振动 1000 ms；
- USB/ADB 的 `lnv_motor_test` 已由实机确认能让设备震动。

2026-09-08 核对命令表：`lnv_motor_test` 的实际处理函数为 `0x1030bc2c`，是固定电机测试序列，不解析用户指定时长；不能把它直接封装成“任意毫秒短震”的产品接口。

静态调用图中，核心电机函数的直接调用者只有两个固定时长包装和按键状态机；已知 BLE 命令处理函数没有直接调用它。因此“Agent 完成后让 A1 震动”目前有两条现实路径：

1. USB/ADB 常驻：调用已验证的马达测试入口，但设备会离开正常 BLE 录音模式；
2. 固件补丁：为 `0x0136` 增加一个新 key，例如 `10002`，把限幅后的 `val` 交给异步电机任务。

第二条在确认启动验签和恢复链之前不能刷入实机。

## 12. 修改固件前的启动链缺口

主系统接收 OTA 时先计算 `/emmc/ota.zip` 的 MD5 十六进制字符串，再按 `hash = hash * 33 + byte`（32 位溢出）计算 32 字节字符串的 `verify_code`。这只是传输完整性检查，不是密码学签名验证。通过后，应用确认文件存在并同步文件系统，然后调用 `boardctl(BOARDIOC_RESET, 4)` 重启进入升级路径。

包内 `ota.sh` 通过 `setprop ota.flash.writing 1` 触发 C 侧刷写，并用 `/emmc/ota_progress` 观察进度。完整的 ZIP 验签、分区刷写和失败恢复路径尚未在现有输出中定位；推测需要继续分析独立升级环境，不能仅依据字符串缺失断言代码一定不在四个镜像中。

脚本在进度卡住时会自行增加进度，并在超时且进度至少为 95 时写入 100。因此单看进度或 SUCCESS 日志不能确认实际刷写成功。

当前已知：

- ZIP 带 JAR 风格 RSA 2048 / SHA-1 签名和每文件 SHA-1；签名证书 SHA-256 指纹为 `8ad127abae8285b582ea36745f220ab8fe397ffb3b068df19ca22d122c7b3b86`；
- 证书 Subject 与 Android 测试证书相似，但序列号、有效期和公钥均不是 AOSP 公开 testkey，不能直接用公开测试私钥重签；
- 包内另有四个镜像的 MD5 清单；
- 四个 OTA 镜像不是整颗 Flash；
- 设备暴露 `/dev/bes_flash` 16 MiB、`/dev/ota`、`/dev/ota_b`、`/dev/ota_flag`、`/dev/ota_info`、`/dev/factory` 等分区；
- 尚未只读备份完整 `/dev/bes_flash`，所以 Boot ROM 之后的引导/验签逻辑仍缺失。

在取得完整 Flash、定位引导器、确认验签和可恢复刷写方式之前，不应改包刷机。即使四个应用分区可读写，也不代表失败后一定能由 USB 恢复。

## 13. 产品开发优先级

### 无需改固件即可做

1. A1 长连接 Agent：AI 键长按产生 chat stream，松手提交；
2. 客户端工作模式：开发、灵感、调研、复盘等只改变 Agent 路由；
3. 远程启停、暂停和恢复普通录音；
4. BLE 原始文件读取和断点续传；
5. 语音备忘录容器自动同步、拆分和转码；
6. 标记时间轴与转录文本对齐；
7. 最多 20 条定时录音任务；
8. Wi-Fi AP 快速同步；
9. 电量、存储、固件、当前录音状态面板。

### 需要继续验证

1. `0x014A` 在实机上的目录和权限边界；
2. Wi-Fi HTTP 实机下载路径、断线行为和吞吐；
3. `aikey_option=1001` 的完整用户交互；
4. 标记 ACK 与设备显示/状态的对应关系；
5. 语音备忘录成功 ACK 后的删除时机；
6. 多中心 BLE 限制和 Windows 连接缓存问题。

### 尚无已验证外部接口的扩展候选

以下能力可能需要固件补丁；是否存在可复用的诊断或间接入口仍需继续验证：

1. 独立 BLE 震动反馈；
2. 新的屏幕图标或 Agent 状态提示；
3. 任意文件 BLE 写入；
4. 自定义按键组合；
5. 在设备端保存模式编号，而不是只在客户端维护。

## 14. 关键证据入口

- `decompiled/focus/103ce7b8_dtiot_ble_recv_data.c`
- `decompiled/focus/103cc040_dtiot_ble_bind_on_audio_request.c`
- `decompiled/focus/103d896c_dt_button.c`
- `decompiled/focus/103d809c_FUN_103d809c.c`
- `decompiled/focus/103cdc30_dtiot_ble_bind_on_schedule_recording.c`
- `decompiled/focus/103e4bd0_dt_raw_transfer_sync.c`
- `decompiled/focus/103ca73c_dtiot_ble_raw_file_block_send.c`
- `decompiled/focus/103cd510_dtiot_ble_system_control_proc.c`
- `decompiled/focus/103ea1d4_dtiot_hal_os_do_upgrade.c`
- `decompiled/focus/103cd098_dtiot_ble_bind_on_request_open_ap.c`
- `decompiled/focus/102eade4_FUN_102eade4.c`
- `decompiled/focus/102eb128_FUN_102eb128.c`
- `decompiled/focus/103e5286_FUN_103e5286.c`
- `decompiled/focus/103e6e4c_dt_tcp_handle_file_header_send.c`
- `decompiled/focus/103e7088_dt_tcp_handle_file_block_send.c`
