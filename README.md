# DingTalk A1 Local Toolkit

一个面向**自有钉钉 A1 录音卡**的 Windows/Linux 本地互操作工具箱。它演示如何在不运行钉钉 App 的情况下，通过电脑蓝牙完成设备发现、挑战应答鉴权、状态读取、录音索引读取、录音下载，以及把 A1 的 DTYJ/Opus 容器转换成可播放的 Ogg/Opus。

> 这不是“通用破解器”。每台 A1 都有自己的 `deviceSecret`；实测把一台设备的凭据用于另一台设备，蓝牙连接与随机挑战可以成功，但 `connectDevice` 返回 `code: 501`，无法通过鉴权。

## 已实现

- 扫描 BLE 服务 `0000fe3c-0000-1000-8000-00805f9b34fb`
- 使用 `getRandom (0x0008)` 获取随机挑战
- 使用 `connectDevice (0x0133)` 离线鉴权
- 读取设备状态和录音索引
- 只读查询灰度配置与电量命令
- 下载指定录音；设备内录音可在一次确认后删除，未备份时显示不可恢复警告
- 将 `BABA/DTYJ` 固定帧 Opus 容器转换为 `.ogg`
- 对实时 `0x0117` Opus 流做短时、仅元数据探测（不保存音频并自动关闭）
- 常驻接收短按语音备忘录的 `0x0117` Opus 流，自动保存 Ogg、调用可选 ASR，并在网页中播放、下载和删除
- 解析 Android `PreferenceUtils.xml`，生成仅保存在本机的设备配置
- 扫描官方 H5 包中的 JSAPI/ASR 参数契约，不输出源码片段
- 可选的本地/局域网页控制台，使用首次启动随机生成并仅保存在本机的访问令牌
- 网页内直接播放和下载 Ogg，用自己的硅基流动 Key 完成转写、人工校对与 AI 总结
- 支持用标准 HCI/btsnoop 抓包方法复核协议（抓包解析建议使用 Wireshark）

未实现并且有意不提供：恢复出厂、解绑、OTA、固件写入、密钥爆破、批量扫描陌生设备。

## 工作原理

```text
自己的钉钉账号 / A1 页面
          │ 设备列表 RPC 缓存在本地
          ▼
PreferenceUtils.xml ──提取──> .a1-device.json（永不提交）
                                      │
PC 蓝牙 ── getRandom ──> A1          │
PC      <── challenge ─── A1          │
          │                           │
          └─ AES-128-CBC token <──────┘
                    │
                    ▼
             connectDevice 鉴权
                    │
           状态 / 文件索引 / 下载
                    │
              DTYJ ──> Ogg/Opus
```

鉴权 token：

```text
key = ASCII(deviceSecret 的前 16 个字符)
iv  = key
pt  = ASCII(设备返回的 32 字符 random)
token = lowercase_hex(AES-128-CBC(key, iv, pt, no_padding))
```

`deviceSecret` 不是由序列号、DID 或 MAC 地址推导出来的，也没有发现可用公式。它是账号绑定后的设备列表数据中的设备级凭据。

## 快速开始

需要 Python 3.11+、电脑蓝牙，以及一台你有权使用的 A1。

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

### 1. 获取自己设备的配置

最方便的研究环境是一个带 Root 开关的 ARM 兼容 Android 模拟器：安装官方钉钉、登录自己的账号、打开 A1 页面使设备列表加载，然后从模拟器导出：

```text
/data/data/com.alibaba.android.rimet/shared_prefs/PreferenceUtils.xml
```

不同钉钉版本的包名或文件名可能变化。只处理你自己的登录会话。导出的 XML 含账号和设备敏感信息，不要上传。

先查看脱敏后的设备列表：

```powershell
python tools\extract_preferences.py C:\private\PreferenceUtils.xml --list
```

再为指定设备生成本地配置；`DID` 来自你自己的客户端会话或抓包：

```powershell
python tools\extract_preferences.py C:\private\PreferenceUtils.xml `
  --serial YOUR_A1_SERIAL `
  --did YOUR_DID `
  --write-config .a1-device.json
```

更详细的、从零开始的研究路线见 [docs/RESEARCH-METHODOLOGY.md](docs/RESEARCH-METHODOLOGY.md)。

### 2. 扫描和认证

```powershell
python tools\scan_a1.py
python tools\a1_auth_test.py --config .a1-device.json --inspect
```

认证工具默认只读取状态和索引，不会删除或修改录音。

### 3. 验证实时流（可选）

先让 A1 处于正在录音状态，再运行：

```powershell
python tools\a1_live_stream_probe.py --config .a1-device.json --seconds 3
```

它只统计推送帧数、84 字节 Opus 单元、TOC 和长度，不把音频写入磁盘；
设备可能在一次推送中批量携带多个 Opus 单元。实时流会在
`finally` 中关闭；脚本还要求关闭响应为 `code:200`。最长允许探测 15 秒。

### 4. 下载并转换一条录音

从索引结果选择属于自己的 `fid`：

```powershell
python tools\a1_download.py --config .a1-device.json `
  --fid 1700000000 --output recordings\a1-1700000000.dtyj

python tools\dtyj_to_ogg.py recordings\a1-1700000000.dtyj `
  recordings\a1-1700000000.ogg
```

### 4.1 常驻接收语音备忘录

```powershell
$env:SILICONFLOW_API_KEY = "YOUR_API_KEY"
python tools\a1_memo_capture.py --config .a1-device.json --output-dir recordings
```

出现 `READY` 后，在 A1 上短按录音即可。结束后会生成 `memo-<fid>.ogg` 和本地 JSON 元数据；网页控制台会自动显示新录音及转录文本。完整协议、Ogg 封装、标记事件与限制见 [语音备忘录实时接收与转录](docs/VOICE-MEMO-LIVE-CAPTURE.md)。

已有的长录音转换成 Ogg 后，可以使用同一个 ASR 接口转录：

```powershell
python tools\transcribe_audio.py recordings\a1-1700000000.ogg
```

结果写入 `recordings\a1-1700000000.json`，网页会显示转录文本。转换器会从
DTYJ 的 `0x80/0xA0/0xC0` 标记帧（包括 `0x80010000` 扩展形式）提取相对秒数，
写入 JSON 的 `markers` 数组；网页在波形上显示黄色标记，并可点击跳转。标记是
时间轴元数据，不改变音频转录方式。

下载器拒绝覆盖已有文件。

### 5. 本地网页控制台

Windows 上完成依赖安装和前端构建后，可以直接双击仓库根目录的
`start-a1-console.cmd`。首次启动会随机生成 `.a1-console-token`，后续重启复用同一
令牌，并在启动窗口显示电脑和手机访问地址。该文件已被 Git 忽略；删除它再重启即可
轮换令牌。手机与电脑必须处在同一个可信局域网。

也可以手动启动：

```powershell
cd console
pnpm install
pnpm build
cd ..
python console\server.py --config .a1-device.json --open
```

打开网页后：

1. 点击“读取设备”获取 A1 文件列表；选择录音后可下载到本机并在线播放。
2. 在右上角设置中填写自己的硅基流动 API Key。Key 默认只保存在当前 Python
   进程内存，网页和状态接口不会把它读回；重启服务后需要重新填写。
3. 点击“开始转写”，校对文本后保存，再点击“生成总结”。转写使用
   `FunAudioLLM/SenseVoiceSmall`，总结默认使用 `Qwen/Qwen3-8B`。
4. 打开右侧“语音备忘录”监听，然后在 A1 上短按录制。收到的 Opus 流会在本机
   封装为 Ogg；配置了 Key 时自动转写，完成后立即进入录音列表。页面会分别显示
   “BLE 鉴权”“实时推流”“音频接收”：前两项变绿表示监听已经就绪，短按 A1
   开始录音后第三项才会进入接收状态，并显示时长和 Opus 包数量。

语音备忘录监听会独占同一条 BLE 连接。网页读取索引、下载或删除设备文件时会暂停
监听，操作结束后自动恢复；暂停窗口内不要发起新的短语音。监听器鉴权后会主动设置
`upload_stream=1`，停止监听或正常退出时再恢复为 `upload_stream=0`。

手动启动默认只监听 `127.0.0.1:8765`。局域网模式必须提供至少 24 字符的随机令牌：

```powershell
python console\server.py --config .a1-device.json --host 0.0.0.0 `
  --access-token YOUR_RANDOM_TOKEN --open
```

API 和音频均校验令牌，令牌不会写入配置或请求日志。不要进行路由器端口转发，
也不要把带令牌的完整链接发给不受信任的人。

网页把删除分成两个互不混淆的动作：选择“删除本机”只移除电脑上的 DTYJ、OGG、
转写、总结和标记，A1 原件仍然保留；选择“删除设备”只删除 A1 原件，已有本地备份
仍列为“仅本地”并支持播放。两种操作都会先显示说明明确的确认弹窗；尚未备份的设备
原件删除后无法恢复。确认删除设备原件后，弹窗会立即关闭，对应录音显示“正在删除”；
收到 A1 的 `0x0113` 成功回执后才从列表移除，失败或超时则保留原条目并显示原因。
`0x0113` 的字段契约来自官方客户端静态分析，并已在一台自有 A1 上确认返回
`code:200` 且索引减少一条。

### 6. 核对官方 H5 接口契约（可选）

对自己从客户端研究环境导出的 H5 目录、单个 JS 或 tar 包运行：

```powershell
python tools\h5_contract_scan.py C:\private\a1-h5-package.tar --json
```

扫描器只报告 JSAPI/参数名称、出现次数、文件名和 URL 主机，不输出代码片段、
URL 路径或查询参数。H5 包本身不应提交到仓库。

## 固定上游研究分支

仓库把作者的 `findings/h5-and-processing-architecture` 分支作为固定提交的
Git 子模块保留，方便逐项复核，同时维持清晰的许可边界：

```powershell
git clone --recurse-submodules https://github.com/Shawn-TKD/dingtalk-a1-pc-tools.git
```

已经普通克隆过的仓库可运行 `git submodule update --init`。工具运行不依赖该
子模块。具体提交、证据等级和未验证项目见
[docs/VALIDATION-MATRIX.md](docs/VALIDATION-MATRIX.md)。

## 验证与测试

```powershell
python -m unittest discover -s tests -v
python -m compileall -q tools console\server.py
```

前端：

```powershell
cd console
pnpm install
pnpm build
```

## 研究边界和局限

- Windows 可能为同一 BLE 设备显示缓存或随机地址，MAC 不能稳定代表物理身份。
- `code: 501` 表示认证阶段被拒绝；它不等同于蓝牙连接失败。
- 当前下载路径是 BLE 文件传输。A1 还暴露了开启 Wi‑Fi AP 的命令，但本项目未实现或声称验证其完整传输协议。
- App 更新或固件更新可能改变路径、字段和命令行为。
- HCI 日志可能包含账号、设备标识和音频内容，公开前必须脱敏。
- 实时流探针会短暂修改 `upload_stream` 状态，但会在所有退出路径尝试恢复为关闭；
  若系统在进程级别强制终止，重新运行一次探针或官方客户端可再次关闭。

## 资料和致谢

研究起点与协议线索来自 [AwHsR15/dingtalk-a1-reverse](https://github.com/AwHsR15/dingtalk-a1-reverse) 的公开分支。参见 [NOTICE.md](NOTICE.md)。

代码采用 [MIT License](LICENSE)。仅限合法的互操作、备份、可访问性与安全研究用途。
