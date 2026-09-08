# DingTalk A1 BLE 震动路径复核

日期：2026-09-08。对象：官方 `V1.6.88-202601291628` 的 `nuttx_ap.bin`。本次只做静态分析，没有向设备发送绑定、解绑、重启、未知 BLE 命令或刷写固件。

## 结论

现有原厂固件中，**确实存在“BLE 流程间接触发震动”**：首次激活/绑定把状态改成 `2` 后，经绑定状态回调执行约 300 ms 的一次震动。但普通的 BLE 物理连接、`0x0133` 逻辑认证连接和已知系统控制命令中，没有找到可安全重复调用的“震动”接口。

因此不能把首次绑定接口当成 Agent 的震动 API：它同时写入激活、身份和认证材料，可能改变绑定状态。当前 stock firmware 上最准确的能力边界是：

- USB HID 只负责 400/403/401/402 信息、认证和工作模式；
- 进入 ADB 后，NuttX shell 的 `lnv_motor_test` 能执行固定测试序列；
- BLE 没有已确认的独立 `vibrate(duration)` 命令；
- 要获得“Agent 完成后让 A1 震一下”，最干净的路线仍是给 BLE 系统控制增加一个经过认证的新 key，而不是滥用绑定命令。

## USB 震动是怎么发现的

它不是 HID command 400–403 之外的第 5 条控制命令。发现链路是：

1. 在 AP 镜像字符串和 NuttX builtin app 表中找到 `lnv_motor_test`；
2. 修正 app 表的字段边界后，确定入口为 `0x1030bc2c`；
3. HID 403 取挑战、401 用已有 owner identity 认证、402 切 `work_mode=1`；
4. 固件启动 `adbd`，通过 USB ADB shell 执行 `lnv_motor_test`；
5. 用户在实机上确认感受到震动。

`0x1030bc2c` 不解析用户输入时长。它调用底层 GPIO/pulse 包装和等待函数形成固定测试序列，所以它不能被准确命名为 USB `vibrate(ms)`。

## 电机实现

高层震动函数 `0x103c7d84` 接受毫秒时长：

```text
duration <= 0  -> 只记录无震动日志
duration > 0   -> GPIO on -> wait(duration * 1000 us) -> GPIO off
```

对应日志为 `Starting vibration for %d ms` 和 `Vibration completed`。直接调用者在当前静态图中只有：

| 调用者 | 行为 |
| --- | --- |
| `0x103c7dc0` | 一次性 500 ms 包装，初始化时有 guard |
| `0x103c7df0` | 固定 300 ms 包装 |
| `0x103d896c` `dt_button` | 按键/关机状态机中的 200 ms、1000 ms 提示 |

反向调用图继续向上只有启动、供电/状态处理、按键、绑定状态回调和 HID 工作模式线程，没有普通 BLE 命令处理函数直接抵达该函数。

## BLE 绑定为什么会震动

`dt_ble_bind_init` (`0x103dc320`) 调用 `0x103dc2c0` 初始化 device module。其 literal `0x103dc31c` 的值是 Thumb 指针 `0x103db4c5`，即将 `0x103db4c4` 注册为外部绑定状态 callback。

`0x103db4c4(status)` 中：

- `status == 2`：记录 `device binded`，若之前不是 2，则调用 `0x103c7df0`（300 ms）并更新 UI；
- `status == 0`：记录 unbound 并更新状态；
- 最后同步 HID 可见的绑定状态。

BLE `0x0007` 的 `dtiot_ble_bind_active_device` (`0x103cae14`) 不只是发通知。它解析并写入多个激活/身份字段，然后通过 device module：

```text
vtable + 0x98 -> set bind status(2)
vtable + 0x10 -> dtiot_device_notify_bind_status()
              -> registered callback 0x103db4c4
              -> 300 ms vibration on transition
```

`0x0004` reset 则进入恢复、清身份和重启路径。这两条都不能用于普通触觉反馈。

## 已知 BLE 分发表复核

对 `dtiot_ble_recv_data` (`0x103ce7b8`) 的 31 个直接被调函数构建调用图，向下最多搜索 15 层；普通录音、语音流、文件、标记、Wi-Fi、定时录音、状态和逻辑连接处理函数均没有直接到达：

- `0x103c7d84` 高层可变时长震动；
- `0x103c7dc0` / `0x103c7df0` 固定包装；
- `0x103e9370` / `0x103e938c` GPIO on/off；
- `0x103e93a8` / `0x103e93d4` 测试 pulse 包装。

唯一已确认的 BLE 间接路径是上述 device module 的绑定状态回调，因函数指针调用而不会出现在普通 direct-call BFS 中。

系统控制 `0x0136` 的解析层要求当前连接 DID 通过检查；下层 `dtiot_ble_system_control_proc` (`0x103cd510`) 只接受：

- key 1–5：系统动作、清录音、OTA文件/事件和日志开关；
- key 10001：读 `battery_percent`；
- 其他 key：返回 `code=0x198`。

没有隐藏的 `10002` 震动分支。`0x0133` 只建立已经绑定设备的逻辑会话并返回 capability，不通知绑定状态，所以重连不应被描述成震动入口。

## 为什么“USB 能震动”不等于“BLE 一定有命令”

USB/ADB shell 是固件工程师的本地诊断入口，可以直接运行 builtin app；BLE 是产品协议，只暴露命令分发表允许的能力。二者最终运行在同一颗主控上，当然都能调用内部电机函数，但入口权限和路由不同。

硬件能力、内部函数、外部协议接口是三层：

```text
电机硬件存在
  -> 内部 GPIO / vibrate(ms) 存在
    -> shell 测试入口存在（已实测）
    -> 首次绑定业务回调存在（静态确认）
    -> 独立 BLE 震动命令不存在（当前固件未找到）
```

## 最小固件补丁方案（尚未生成或刷写）

若继续走固件路线，建议在已认证的 `0x0136` 下增加 `key=10002`，而不是新增整个 GATT service：

```json
{"did":"...","key":10002,"val":200}
```

设计约束：

1. 只在 `dtiot_ble_bind_connect_did_check()` 通过后受理；现有 `0x0136` 已有这层验证。
2. 将 `val` 限制为 50–1000 ms，并加最短调用间隔，防止持续震动和耗电。
3. BLE handler 只把任务投递到 worker 后立即响应；不要在 BLE 接收线程直接调用同步的 `0x103c7d84`，否则会阻塞 `duration` 毫秒，影响音频和 ACK。
4. 忙于录音同步、低电量或关机流程时决定拒绝/排队策略；返回码必须能区分 queued/busy/invalid。
5. 先做 RAM 调试或可恢复加载验证，再考虑持久 OTA；不能把“应用镜像能改”当成“签名和回滚已经解决”。

客户端可将它封装为 `vibrate(duration_ms)`，但只有实际刷入、断电重启、BLE长录音并发和恢复测试通过后，才能从 SDK 的 `experimental` 移到稳定 API。

## 下一步证据

在写补丁前优先完成：

1. 只读备份完整 `/dev/bes_flash` 和 boot/OTA 元数据；
2. 确认 Bootloader 对修改 AP 镜像的签名/校验/回滚行为；
3. 找可恢复的调试加载或硬件救砖路径；
4. 在反编译工程中为 `0x103c7d84`、`0x103db4c4`、`0x103cf884`、`0x103cf7dc` 和 `0x103cd510` 建立确定符号和类型；
5. 在不刷实机的镜像副本中设计 code cave、重定位和前后字节校验，再单独审查。

在这些条件完成之前，不应向当前唯一设备刷入试验补丁。
