# DingTalk A1 Local Toolkit

一个面向**自有钉钉 A1 录音卡**的 Windows/Linux 本地互操作工具箱。它演示如何在不运行钉钉 App 的情况下，通过电脑蓝牙完成设备发现、挑战应答鉴权、状态读取、录音索引读取、录音下载，以及把 A1 的 DTYJ/Opus 容器转换成可播放的 Ogg/Opus。

> 这不是“通用破解器”。每台 A1 都有自己的 `deviceSecret`；实测把一台设备的凭据用于另一台设备，蓝牙连接与随机挑战可以成功，但 `connectDevice` 返回 `code: 501`，无法通过鉴权。

## 已实现

- 扫描 BLE 服务 `0000fe3c-0000-1000-8000-00805f9b34fb`
- 使用 `getRandom (0x0008)` 获取随机挑战
- 使用 `connectDevice (0x0133)` 离线鉴权
- 读取设备状态和录音索引
- 只读下载指定录音，不删除设备文件
- 将 `BABA/DTYJ` 固定帧 Opus 容器转换为 `.ogg`
- 解析 Android `PreferenceUtils.xml`，生成仅保存在本机的设备配置
- 可选的本地网页控制台
- 支持用标准 HCI/btsnoop 抓包方法复核协议（抓包解析建议使用 Wireshark）

未实现并且有意不提供：恢复出厂、删除录音、解绑、OTA、固件写入、密钥爆破、批量扫描陌生设备。

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

### 3. 下载并转换一条录音

从索引结果选择属于自己的 `fid`：

```powershell
python tools\a1_download.py --config .a1-device.json `
  --fid 1700000000 --output recordings\a1-1700000000.dtyj

python tools\dtyj_to_ogg.py recordings\a1-1700000000.dtyj `
  recordings\a1-1700000000.ogg
```

下载器拒绝覆盖已有文件。

### 4. 本地网页控制台

```powershell
cd console
pnpm install
pnpm build
cd ..
python console\server.py --config .a1-device.json --open
```

默认只监听 `127.0.0.1:8765`。不要通过端口转发或 `0.0.0.0` 暴露到局域网/互联网。

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

## 资料和致谢

研究起点与协议线索来自 [AwHsR15/dingtalk-a1-reverse](https://github.com/AwHsR15/dingtalk-a1-reverse) 的公开分支。参见 [NOTICE.md](NOTICE.md)。

代码采用 [MIT License](LICENSE)。仅限合法的互操作、备份、可访问性与安全研究用途。
