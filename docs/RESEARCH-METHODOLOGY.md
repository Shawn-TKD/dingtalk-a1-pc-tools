# 从零复现 A1 本地互操作的方法论

## 1. 先定义安全边界

只研究自己拥有或明确获授权的 A1 和账号；先做被动观察，再做只读命令。协议研究和自动验证阶段不猜测密钥，不试陌生设备，不触发 reset、delete、unbind、OTA 或固件写入。产品控制台中的单文件删除另有“当前设备索引 + 输入完整确认码”门槛；没有备份时使用 `NOBACKUP` 强确认，不属于自动验证步骤。

## 2. 识别真实通信链路

USB 枚举只能证明设备暴露了哪些接口，不能证明官方 App 使用 USB。通过正常 App 操作和 Android 蓝牙 HCI 日志，可以确认 A1 的主要控制/文件通道是 BLE GATT。USB HID/伪 ADB 接口可以记录为后续研究对象，但不应成为第一条复现路线。

## 3. 无 Root 被动抓取 BLE HCI

在 Android 开发者选项开启“启用蓝牙 HCI 信息收集日志”，完成两轮最小操作：连接、开始/停止录音、刷新列表、下载一条新录音。之后通过 `adb bugreport` 导出 btsnoop 日志。

建议保留两轮独立样本，因为需要区分：

- 稳定的服务 UUID、特征 UUID 和命令号；
- 每次变化的 challenge、timestamp、sequence；
- 与单条录音对应的 `fid` 和文件块；
- ATT 分片与完整应用层帧。

使用 Wireshark/tshark 按 ATT handle、方向和时间线做离线解析；再按照 [PROTOCOL.md](PROTOCOL.md) 的 8 字节应用层头部重组跨 notification 的帧。抓包仅用于自己的会话，并始终视为敏感文件。

## 4. 只读静态分析用于“解释”，不是替代抓包

对自己合法取得的客户端安装包做只读分析，优先搜索抓包里已经看到的字符串、UUID、JSON 字段与命令号，例如 `deviceSecret`、`connectDevice`、`getRandom`、`corp_id`、`sdk_ver`。这样可以把混淆后的代码范围缩小到协议桥接层。

关键思路是交叉验证：抓包告诉你“线上实际发生了什么”，静态分析解释“字段从哪里来、怎样计算”。任何单一路径都容易误判。

本仓库不再分发官方 APK、反编译产物或厂商 native 库。

## 5. 区分账号标识、客户端标识和设备凭据

- `deviceSecret`：设备级认证凭据，由绑定后的设备列表数据返回；每台设备不同。
- `deviceId` / `sn`：设备身份字段，用于选择设备，不能推出 secret。
- `did`：客户端/会话侧标识之一，参与 JSON 请求，但不是 secret 的导出公式。
- `corpId`：账号/组织上下文，参与请求，同样不能推出 secret。
- `random`：A1 每次给出的挑战值。
- `token`：用 secret 和 random 临时计算出来的应答，不是长期凭据。

关系是“输入到协议”，不是 `secret = f(sn, did, corpId)`。目前没有证据支持通过公开标识暴力推导 secret。

## 6. 为什么模拟器 Root 足够

Root 的目的不是让模拟器通过蓝牙连接 A1，而是读取**自己的钉钉会话已经从云端得到并缓存的设备列表**。因此模拟器只需：

1. 开启 Root；
2. 安装官方钉钉并登录自己的账号；
3. 打开 A1 页面，让设备列表加载；
4. 导出应用私有目录中的 `PreferenceUtils.xml`；
5. 在电脑本地解析 `device_list_id`。

真正和 A1 建立 BLE 连接的是 Windows 客户端。物理 Android 手机不一定要 Root，也不需要让模拟器具备 BLE 直通。

## 7. 离线验证后再连接实物

如果抓包里有一组属于自己设备的 `(random, token)`，先用候选 secret 离线计算并比较。只有完全匹配后才进行 BLE 鉴权测试。

实物测试顺序：

1. scan；
2. getRandom；
3. connectDevice；
4. sync status；
5. get file list；
6. 下载明确选择的一条录音；
7. 本地转换并播放验证。

每一步失败都先停下分析返回码，避免用重复写命令“碰运气”。

## 8. 跨设备验证的结论

在只开机第二台 A1 的情况下，用第一台的完整认证配置重复测试两次：BLE 连接和随机挑战成功，`connectDevice` 均返回 `501`。这说明“看得见/连得上 GATT”与“通过设备认证”是两回事，也支持 `deviceSecret` 是一机一密的判断。

若要支持多台设备，应为每台设备保存独立配置，并在连接前让用户明确选择序列号；不要把一个 secret 当作账号级万能密钥。

## 9. 发布研究结果时的脱敏清单

- 删除真实 `deviceSecret`、DID、corpId、SN、deviceId 和 MAC；
- 不提交 Preference XML、bugreport、btsnoop、APK、DEX、SO；
- 不提交录音或可还原语音的 Opus 数据；
- 示例 token/challenge 必须是人工生成或明确标注不可用；
- 检查 Git 历史，而不只是当前文件；
- 清楚标注未验证推断和已在实物上复现的结论。
