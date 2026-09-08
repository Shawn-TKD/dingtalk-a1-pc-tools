# DingTalk A1 SDK · 0.1.0

不改固件，把你自己的 A1 变成 App / Agent 的录音输入设备和 USB 文件设备。

这是独立开发的 Python SDK，不是钉钉官方 SDK。它整理了已有电脑客户端、Android 客户端和 A1 V1.6.88 固件研究；**旧实现实测通过，不等于这个重构包已经逐项重新实机验收**。本次验证范围见 [VALIDATION.md](docs/VALIDATION.md)。

## 能做什么

| 场景 | 接口 | 证据与限制 |
| --- | --- | --- |
| 电脑蓝牙直连、身份认证、状态/电量/存储 | `A1Client.connect/status` | 复用旧实测路径；已有自己的 deviceSecret 后，不需要每次找钉钉云端 |
| 录音索引、分块下载、删除单条录音 | `recordings/download/delete_recording` | 旧实测；下载不主动删除原录音 |
| 常驻接收语音/实时音频，保存 Ogg + 元数据 | `subscribe` + `LiveRecorder` | 旧实测；保留 stream_type，不能把所有实时流都当成备忘录 |
| 录音标记、开始/结束、遥测事件 | 事件订阅 | 保留原始类型/时间；延迟遥测不强行关联当前录音 |
| 远程开始/暂停/恢复/停止普通录音 | `recording_control` | 已恢复固件处理路径；本包需要实机复测 |
| 浏览 `/emmc`、读取本地备忘录聚合文件 | `USBStorage.list/download` | 旧 USB 实测 |
| 存文档/视频、读回、删除个人文件 | `USBStorage.upload/delete_file` | 写入 `/emmc/mindlink`；这是共享 eMMC 上的普通目录，不是独立分区 |
| 固定震动测试 | `USBStorage.motor_test` | USB 旧实测；不是蓝牙任意时长震动 |
| Wi-Fi 热点下载、按已知路径 BLE 原始读取、定时录音、App 发标记 | `client.experimental` | 静态研究接口，尚未在新版 SDK 实机验证 |

**没有实现/不承诺：**刷固件、解绑/重置设备、任意蓝牙文件写入、蓝牙直接显示文本或播放语音、蓝牙任意震动、永久不失效的身份、完整文件系统通过普通录音索引可见。

尤其注意：**USB HID 进入 ADB 模式会停止正常录音并断开 BLE**。退出 SDK 不会自动恢复普通模式；设备重启可作为恢复尝试，但这里没有暗中发重启指令。日常 AI 输入用 BLE，USB 文件管理单独使用。

## 安装

Python 3.11+。在解压目录中建立虚拟环境：

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install ".[all]"
.\.venv\Scripts\a1.exe capabilities
```

macOS / Linux：

```bash
python3 -m venv .venv
.venv/bin/python -m pip install '.[all]'
.venv/bin/a1 capabilities
```

仅蓝牙用 `.[ble]`；USB 用 `.[usb]` 并另行安装 Android platform-tools 的 `adb`；音频完整解码验证用 `.[audio]`。SDK 不捆绑 ADB 或系统驱动。蓝牙需要操作系统原生 BLE 支持和权限；macOS 需给运行它的终端/应用蓝牙权限，Linux 需正常运行 BlueZ。Windows 是旧实现的主要实测平台，macOS/Linux 尚未以本包实机验收。

## 身份文件

**你已有的 `.a1-device.json` 可直接复用，不必重新提取。** 文件放在源码目录之外，通过路径读取即可：

```json
{
  "did": "YOUR_OWN_DID",
  "corp_id": "YOUR_OWN_CORP_ID",
  "device_secret": "YOUR_32_CHARACTER_DEVICE_SECRET_",
  "serial_number": "YOUR_OWN_A1_SN",
  "address": "ADDRESS_FROM_THIS_COMPUTERS_SCAN"
}
```

上面全是占位符；`address` 可省略，扫描时只发现一台 A1 才会自动选择。多台时不再按信号最强盲选。换电脑尤其换 macOS 后请重新 `scan`，新系统的地址标识可能不同。`deviceSecret` 不会在 `Identity` 的打印结果中显示；USB 认证使用与 BLE 相同的客户端级 deviceSecret，**不要再做一次 MD5，也不要把 Flash 的原始字节直接填进来**。

后面的命令假设 `a1` 是虚拟环境里的可执行文件；也可统一写成 `.venv\Scripts\python.exe -m dingtalk_a1`。`--config` / `--address` 位于子命令之前：

```powershell
a1 scan
a1 --config 'C:\private\.a1-device.json' status
a1 --config 'C:\private\.a1-device.json' files
a1 --config 'C:\private\.a1-device.json' download 1700000000 recordings\a1-1700000000.dtyj --ogg recordings\a1-1700000000.ogg --validate
a1 --config 'C:\private\.a1-device.json' listen --output-dir recordings\live --reconnect
```

`listen` 在连接后等待硬件动作，不自动开始录音、不改变删除策略、不调用云端。若连接成功却没有流，先 `settings` 检查，再在需要时显式执行 `live-upload on`。`--reconnect` 在可恢复连接错误后重连；认证被拒、音频格式错误、磁盘写入错误会停止，避免不断重试掩盖问题。

设备录音永久删除只需要一次显式确认，不要求输入长口令：

```powershell
a1 --config 'C:\private\.a1-device.json' delete 1700000000 --yes
```

UI 可以用一个确认弹窗后调用 `delete_recording(fid)`。SDK 不替调用方管理弹窗。

## Python：一个连接同时服务 UI 和语音事件

```python
import asyncio
from dingtalk_a1 import A1Client, Identity

async def main():
    async with A1Client(Identity.load('/private/.a1-device.json')) as a1:
        print(await a1.status())
        for recording in await a1.recordings():
            print(recording['fid'])
        # await a1.recording_control('start')  # 只在想录音时调用

asyncio.run(main())
```

完整常驻示例：[examples/recording_to_agent.py](examples/recording_to_agent.py)。ASR/总结在独立协程或任务队列里处理，不阻塞蓝牙收包。SDK 不内置用户的硅基流动、DeepSeek 或 MCP 密钥，也不自动上传录音。

## USB：目录与个人文件

先确保 A1 没在录音，再明确切换模式：

```powershell
a1 --config 'C:\private\.a1-device.json' usb info
a1 --config 'C:\private\.a1-device.json' usb enter-adb --allow-interrupt
a1 --config 'C:\private\.a1-device.json' usb --adb 'C:\Android\platform-tools\adb.exe' ls /emmc/audio
a1 --config 'C:\private\.a1-device.json' usb space
a1 --config 'C:\private\.a1-device.json' usb mkdir /emmc/mindlink
a1 --config 'C:\private\.a1-device.json' usb push notes.txt /emmc/mindlink/notes.txt
a1 --config 'C:\private\.a1-device.json' usb pull /emmc/mindlink/notes.txt returned-notes.txt
a1 --config 'C:\private\.a1-device.json' usb rm /emmc/mindlink/notes.txt --yes
```

`--adb` 不在 PATH 时，每条 USB 文件命令都要带上。`mkdir` 只需第一次执行；已有目录会明确报同名，不覆盖。

上传按 64 KiB 分块；默认最多 1 GiB / 文件，至少给录音留 1 GiB。上限是客户端参数，不是固件限制，可在 `USBStorage(...)` 构造器修改，但更大文件未经实机验证。默认检查传输确认和长度；加 `--verify` 才做完整读回比较。不会自动更改固件或创建新分区。

离线备忘录不一定出现在普通 BLE 文件索引里；可在 USB 模式导出聚合容器：

```powershell
a1 --config 'C:\private\.a1-device.json' usb pull /emmc/audio/00000000000000 recordings\memo-container.bin
a1 export-memos recordings\memo-container.bin recordings\exported-memos --validate
```

解析失败会保留原容器和 `.part`，报告具体条目失败，不假装全部音频正常。转换不会触发设备的“推送成功后清理备忘录”逻辑。

固件还暴露了一个按已知 `/emmc` 路径读取的实验性 BLE 通道。它没有目录枚举，速度也不及 USB/Wi-Fi，但可用于无需切换 USB 模式的定点读取：

```python
result = await a1.experimental.download_raw_file(
    "/emmc/audio/00000000000000", "recordings/memo-container.bin")
```

该接口逐个验证 CRC 并在失败时保留 `.part`；目前只有固件静态证据，实机验收前不要把它作为唯一备份路线。

## Agent / Skill / App 怎么接

推荐路径：**A1 BLE → 本机 SDK 长连接 → `recording_saved` → ASR → 模式路由 → Agent → 手机/电脑反馈**。

```powershell
a1 route '开发模式，把当前项目首页标题改成记忆面包。'
```

返回标准 JSON 任务，**不直接执行代码**。Python `ModeRouter` 可持有“开发/灵感/调研/复盘/游戏”的当前模式；ASR 可以换成你已有的服务。操作系统权限、项目目录、上传对象和任务确认由 App/Agent 宿主控制。A1 本身不运行大模型。

附带可安装的 [Skill 模板](skills/dingtalk-a1/SKILL.md)。开发 Android、网页服务、AI 路由和同步去重的建议见 [INTEGRATION.md](docs/INTEGRATION.md)。这一版是 Python 开发库 + CLI，并不是新 Android APK/AAR；不要把它当成已经集成到旧 App 的更新。

## 文档

- [API / 集成设计](docs/INTEGRATION.md)
- [协议与研究接口](docs/PROTOCOL.md)
- [测试和实机验证边界](docs/VALIDATION.md)
- [来源和许可](NOTICE.md)

源码为 MIT；不包含官方固件、反编译完整文本、原作者未授权代码、录音或任何真实密钥。`a1 capabilities` 可供其他 Agent 直接查询能力分级。
