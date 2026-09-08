# A1 USB 深度实测：ADB、录音与程序分区

2026-09-07。用户确认未在录音并允许验证更多 USB 功能。本次只做获准的运行模式切换和只读访问，没有刷固件、解绑、删除设备录音或向设备写入测试文件。

## 结论

HID 认证 → ADB 模式 → NuttX shell → USB 文件下载与程序分区读取，已全部在这台 A1 上打通。不需要手机、钉钉 App 或云端 API；但 HID 认证使用了已有合法设备身份和 deviceSecret，并非通用免密访问。

最后复查 HID 仍可用，`work_mode=1`、`bind_status=1`，版本 `V1.6.88-202601291628`。当前保留 ADB 模式，没有自动重启。固件进入该模式会停止录音并断开 BLE，不能假定与正常蓝牙录音同时运行。

## 1. USB 认证

基础 HID 分包格式见 [初轮通信记录](USB-COMMUNICATION.md)。新增工具 `a1_usb_session.py` 从 `--credentials` 指定的本地身份 JSON 读取资料，不硬编码或输出秘密值。

1. 命令 400 查询设备 SN，与本地身份核对。
2. 403 正文为 `corpId`、`did`，返回 `code=200`、32 个 ASCII 字符的随机挑战。
3. 使用已验证 BLE 客户端的同一算法：`device_secret` 字符串前 16 个 ASCII 字节同时作为 AES-128-CBC key 与 IV；加密 32 字节挑战，不加 padding，结果转十六进制。
4. 401 正文为 `did`、`payload`，实测认证返回 `code=200`。
5. 402 正文 `{"work_mode":1}`，返回 `{"status":"success","work_mode":1}`。随后 ADB 从 `offline` 变为 `device`。

```powershell
.\.venv\Scripts\python.exe .\a1_usb_session.py auth --credentials '<已有身份JSON路径>'
# 仅在不录音并允许中断 BLE 时执行：
.\.venv\Scripts\python.exe .\a1_usb_session.py adb --credentials '<已有身份JSON路径>'
```

`dt_hid_adb_thread_entry`（`0x103e2c64`）有停止录音、断开 BLE、启动 `adbd &` 的调用；模式 0 的退出分支涉及 `reboot`，本次尚未验证自动恢复普通模式。不要在录音或文件传输中途切换。

## 2. ADB 只读能力

| 能力 | 实测结果 |
| --- | --- |
| USB shell | `uname -a` 返回 NuttX 12.6.0、BES NuttX EVB、ARM AP |
| 文件目录 | 可列出 `/emmc`、`/emmc/audio`、`/dev` 等 |
| 普通录音列表 | 6 个非零文件名录音，另有 1 个备忘录聚合文件 |
| 普通录音下载 | 6,530,324 字节，ADB 报告约 2.9 MB/s、2.172 秒；仅为本次样本 |
| 备忘录容器下载 | `/emmc/audio/00000000000000`，431,124 字节，约 2.5 MB/s |
| 文件系统空间 | `df -h` 显示 `/emmc` 58G，已用 80M；这是工具取整值 |
| 存储设备容量 | `/dev/mmcsd1` 列出 62,528,684,032 字节，约 62.53 GB / 58.23 GiB；不是精确剩余空间 |
| 程序分区 | AP、APC1、HIFI、USER 四个分区均完整读回 |

```powershell
adb -s '<设备SN>' shell 'uname -a'
adb -s '<设备SN>' shell 'ls -l /emmc/audio'
adb -s '<设备SN>' shell 'df -h'
adb -s '<设备SN>' pull /emmc/audio/00000000000000 '<电脑目标文件>'
```

`free` 只显示约 1.28 MB 的 Umem 堆，不代表整机物理 RAM。Shell 命令列表还包含电量、温度、按键、马达、LED、Wi-Fi 和 Web server 工具；这里只确认它们存在，没有盲目启动测试、开网络端口或写寄存器。

**不是 U 盘或 MTP**：当前是 HID 控制入口加 ADB 文件传输，没有 Windows 盘符。

## 3. 下载 OTA 与实际固件比较

设备和 OTA 版本均为 `V1.6.88-202601291628`。通过 ADB SYNC `RECV` 读回分区，按 OTA 镜像长度逐字节比较：

| 镜像 | 读回分区字节数 | OTA 镜像字节数 | 对应范围比较 |
| --- | ---: | ---: | --- |
| AP | 8,388,608 | 3,892,128 | 仅偏移 0–3 不同，之后全部一致 |
| APC1 | 3,145,728 | 2,753,936 | 仅偏移 0–3 不同，之后全部一致 |
| HIFI | 1,507,328 | 512,664 | 完全一致 |
| USER | 1,048,576 | 133,684 | 完全一致 |

AP/APC1 的 OTA 开头为 `FF FF FF FF`，设备为 `1C EC 57 BE`，小端数值 `0xBE57EC1C`。

固件函数 `0x10231e6c` 的日志名称是 `subsys_check_boot_struct`，它将启动头首字段与位于 `0x10231ec4` 的常数 `0xBE57EC1C` 比较，通过后返回代码入口；另一处 `0x1019798c` 同样引用该常数，并检查 AP 启动映射 `0x30190000`。因此差异可确定为启动结构识别标记；本次没有实测 OTA 在哪一步写入此标记。

结论：**同一版本，四个对应程序镜像内容一致，仅 AP/APC1 启动标记不同；不是所有 Flash 与 ZIP 原封不动相同。** 分区剩余空间、工厂配置、引导信息与 eMMC 录音不属于上述比较。未导出 `/dev/factory`、整个 eMMC 或整片 Flash。

```powershell
.\.venv\Scripts\python.exe .\a1_usb_readback.py --adb '<adb.exe路径>' --serial '<设备SN>' --output '.\usb-readback-20260907'
```

脚本前置条件：输出目录已有普通 ADB 下载的 `00000000000000.memo`。先将自实现 SYNC 读回文件与该文件直接比较，一致后才读四个已知大小的程序节点。脚本仅实现 `RECV`，没有 `SEND`、擦除或烧写功能。

## 4. 备忘录容器与音频

容器头 8 字节：4 字节大端条目数，随后 `5A 10 20 50`（标记、16 kHz 输入采样率、32 kbps 名义码率、80 字节包长）。

每条头部 12 字节：`5A`（1 字节）、时间戳（6 字节大端）、整数时长（2 字节大端）、音频长度（3 字节大端），后接固定 80 字节 Opus 包。此次 13 条记录全部边界与 431,124 字节总长吻合。

`a1_usb_audio_export.py` 只处理电脑上已下载文件，复用旧项目 `dtyj_to_ogg.py` 的 DTYJ 解析、Ogg CRC 和页面函数。保留压缩包、不重新编码；先本地 PyAV 解码计算时间轴，再完整解码生成的 Ogg 验证。

- 普通录音：77,741 个包，1,554.82 秒（25 分 54.82 秒），完整解码成功。
- 13 条备忘录中 12 条完整解码生成 Ogg。第 6、7 条各只有 20 ms，不能算有效的完整语音内容。
- 第 9 条虽完整解码，但开头帧长异常，保留警告；解码通过不保证原始声音无损。
- 第 11 条第 2 个包解码失败，未生成标称有效的 Ogg。未静默丢包或修改原文件；数据仍在备份容器和设备上。
- 头部整数时长与实际解码时长不总相同，报告同时保存两者。异常原因尚需区分原始写入异常与格式变体，不能直接断言硬件损坏。
- 没有云转录、上传、发送同步完成确认或清理缓存。固件同步成功事件有删除备忘录调用，本次特意未触发。

```powershell
.\.venv\Scripts\python.exe .\a1_usb_audio_export.py --input '.\usb-readback-20260907' --output '<新的音频输出目录>' --helpers '<旧项目dtyj_to_ogg.py路径>'
```

## 5. 文件位置及异常处理

- 最终音频：`usb-readback-20260907/audio-validated/`，内含 `audio-export-result.json`。
- 原始录音、备忘录容器及四个程序分区：`usb-readback-20260907/`。
- 认证、信息和比较记录：本目录 `usb-*-result.json`。
- 设备读回资料和身份结果均被 `.gitignore` 排除，没有上传 GitHub。

标准 `adb pull /dev/ap` 在电脑端跳过 NuttX 特殊文件；`exec-out` 被关闭，`shell -T` 提示只支持 PTY，普通 `shell cat` 二进制又与原文件不一致。因此不用 shell 导出固件，改用本机既有 ADB server 的 SYNC `RECV`，验证字节一致后成功读出程序节点；没有更换 USB 驱动或更改设备文件类型。

音频第一轮在第 11 条异常包处停止。改为逐条验证并记录失败，继续处理其余条目；最终目录为 `audio-validated/`，没有把失败结果当作成功文件。

参考：[AOSP ADB SYNC 协议 RECV / DATA / DONE](https://android.googlesource.com/platform/system/core/+/android10-release/adb/SYNC.TXT)。其余结论来自本地固件反编译和本次设备实测。

## 下一步

1. 在允许重启的条件下验证退出 ADB、恢复 BLE，以及冷启动后重复认证。
2. 从固件恢复马达/LED/按键工具的参数和副作用，再进行明确、短时的硬件反馈测试。
3. 把 USB 只读下载接入现有管理页面；自动同步需避免触发设备删除备忘录的确认路径。
4. 继续检查升级验签与恢复机制。读到程序分区不等于能安全刷入修改固件，更不构成已有可恢复刷机方案。
