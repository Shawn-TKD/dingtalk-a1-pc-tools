# A1 USB 通信：已实机验证读取设备信息

后续更新：已在用户许可下完成认证、进入 ADB、读取录音和四个程序分区。详见 [USB 深度实测](USB-VALIDATION-20260907.md)。下文保留最初仅测试命令 400 时的范围记录，不代表最新进度。

验证日期：2026-09-07。用户已将 A1 用 USB 连接到本台 Windows 电脑。

## 实测结果

- Windows 枚举 `VID_17EF / PID_0101`，接口 0 为 ADB Interface，接口 1 为厂商自定义 HID。
- HID Usage Page `0xff00`，Usage `1`，Report ID `1`；输入、输出各 127 字节有效负载，Report ID 在 HIDAPI 缓冲区中额外占 1 字节。
- 从 HID 发送命令 400，成功收到两片应答，拼接得到 173 字节 JSON。
- 返回版本 `V1.6.88-202601291628`、电量 100%、`bind_status=1`、`work_mode=0`，并返回当前设备 SN 和 Wi-Fi MAC。
- 使用不同 request ID 连续复查也成功，仍保持普通模式。

此次成功通信没有使用手机、BLE、钉钉 App、云 API 或 deviceSecret。**这只证明设备信息查询无需认证，不代表全部 USB 操作都无需认证。**

详细设备标识和原始应答保存在本地 `usb-info-result.json`，该文件被 `.gitignore` 的 `*-result.json` 规则排除。没有修改固件、绑定关系、录音或设备工作模式。

## 可重复运行的工具

脚本：`a1_usb_info.py`。它只实现描述符读取和命令 400，未提供切换模式、删除、解绑、录音控制或刷写操作。

在本目录执行：

```powershell
.\.venv\Scripts\python.exe .\a1_usb_info.py info
```

只查看 HID 描述符：

```powershell
.\.venv\Scripts\python.exe .\a1_usb_info.py describe
```

若同时插入多台 A1，使用 `--serial <设备SN>` 明确选择。虚拟环境已安装 `hidapi==0.15.0`，无需替换现有 Windows 驱动。

## 已验证的报文格式

以下偏移以去除 HID Report ID 后的协议负载起点计算：

| 偏移 | 长度 | 含义 |
| --- | ---: | --- |
| 0 | 1 | 请求 magic 为 `0x13`，应答为 `0x31` |
| 1 | 2 | 命令编号，小端 uint16 |
| 3 | 1 | 请求 ID，应答沿用相同值 |
| 4 | 4 | 正文长度，小端 uint32，不包含 8 字节包头 |
| 8 | 变长 | UTF-8 JSON 正文；查询设备信息时可无正文 |

命令 400、ID 1、空正文的协议数据是：

```text
13 90 01 01 00 00 00 00
```

使用 HIDAPI 写入时，在它前面加 `01`（Report ID），并把协议数据补零至 127 字节，得到总共 128 字节的输出报告。读取时每个报告都先去掉首字节 Report ID，再按协议包头声明的正文长度重组，不能把第二片误当成新包，也不能把末尾填充零加入 JSON。

架构上将协议包与 HID 传输分开：`HEADER` 定义小端协议字段，`read_info()` 负责写报告、按 ID/命令核回应答并拼接分片；设备枚举只匹配 A1 的 VID/PID 和厂商 Usage。没有把私有密钥硬编码到工具中。

固件依据：`dt_hid_process_packet_fragment`（`0x103e2970`）、`dt_hid_send_packet_fragments`（`0x103e2db0`）、底层发送包装（`0x103e914c`）、命令分发主循环（`0x103e34e8`）。HID Report ID 和长度同时由本机读取的描述符确认。

## 从“通信”到“读取录音”还差什么

目前实机只验证了普通模式下的设备信息查询，**尚未通过 USB 列出或下载录音文件，也没有验证能否访问全部存储**。

静态反编译表明下一条路径为：

1. 用已有合法设备身份恢复 HID 认证流程（401；随机材料相关入口为 403）。
2. 认证通过且设备已绑定后，发送 402，将 JSON 中的 `work_mode` 设为 1。
3. 确认 ADB 服务可用，再测试只读目录访问和文件导出功能。

第 2 步会触发工作模式变化。固件 ADB 线程中有停止录音、断开 BLE、启动 `adbd &` 的代码，退出路径还可能重启设备。因此本次没有自动执行，应在用户停止录音并确认可以中断 BLE 后再验证。

这不是把 A1 变成 U 盘；HID 是控制入口，ADB 是否支持所需的 shell、文件列表与传输操作仍需分别实测。

## 本次运行问题

最初 Windows PnP 读取和 pip 下载被执行环境权限限制阻止。通过获准的提权执行完成只读 PnP 枚举和项目虚拟环境内的库安装后，HID 打开及查询可以正常运行；没有因此安装系统驱动或改动系统蓝牙配置。

## 通信库参考

- [HIDAPI 官方 API：Report ID 与读写约定](https://libusb.info/hidapi/group__API.html)
- [Python HIDAPI 包官方仓库](https://github.com/trezor/cython-hidapi)
