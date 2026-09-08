# DingTalk A1 V1.6.88 RPTUN / OpenAMP 核间 ABI

本页把 AP 端三条伴随核通道拆成三层：BES `rmt_ipc`、板级
`rptun_ops_s` 适配器、NuttX/OpenAMP RPMsg。它们是不同层次，不能用一个
函数名混为一谈。

## 先给结论

- `0x1024e6c8` 是 BES `rmt_ipc_open`，不是 `rptun_initialize`。它配置
  mailbox IRQ、消息槽和发送队列。
- AP 中的 NuttX `rptun_initialize` 是 `0x10260c44`。其分配的私有对象
  为 `0x220` 字节，并创建 `/dev/rptun/<remote-name>` 和 `rptun-<name>`
  线程。
- 厂商 fork 的 `rptun_ops_s` 在公开 NuttX 12.6 布局前多一个
  `local CPU name` 槽位。原厂三张 AP 侧 ops 表的第一项都是
  `NULL`；`+0x04` 才是 remote CPU name。
- 每个 RPMsg 资源记录固定 `0xd0` 字节，`fw_rsc_config.reserved[0]`
  的偏移 `+0x94` 被 BES 适配器用作 `resource_id`。
- 三条 AP 侧通道都是 master、都不 autostart；伴随核由产品启动链
  先拉起，RPTUN 的 `start` 因此是 no-op。
- 每侧的 vring 参数是 8 个 descriptor、8 字节对齐、512 字节
  RPMsg buffer，所需本地 arena 恰好 `0x10e0` 字节。

## AP 侧三条通道

| 伴随核 | RPMsg remote name | BES transport | `rmt_ipc` core/channel | resource id | 本地 arena | ops 表 | 私有状态 |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: |
| HiFi4 DSP | `audio` | `M55C0_2_DSPC0` | `2 / 0` | 3 | `0x201e057c` | `0x10486488` | `0x201d16f4` |
| Bluetooth host | `bth` | `M55C0_2_BTHC0` | `3 / 0` | 1 | `0x23c00000` | `0x104864c0` | `0x201d1750` |
| M55 core 1 | `apc1` | `M55C0_2_M55C1` | `1 / 0` | 0 | `0x201e1664` | `0x104864f8` | `0x201d17ac` |

BES `rmt_ipc` 的三张底层配置表分别在 `0x104958b4`、
`0x1049597c`、`0x10495850`。对产品实际使用的 channel 0，还可恢复：

| 伴随核 | peer/local config pointer | channel 数 | channel 0 message slots | RX / TX IRQ | wake-lock flag |
| --- | --- | ---: | ---: | --- | --- |
| DSPC0 | `0x20bbffa4` / `0x20bbffa0` | 2 | 5 | 76 / 79 | false |
| BTHC0 | `0x22ffffe4` / `0x22ffffe0` | 2 | 8 | 96 / 94 | true |
| M55C1 | `0x21dbffa4` / `0x21dbffa0` | 2 | 5 | 88 / 91 | false |

第一组 IRQ 已确认为 RX（对端 `SEND_IND`），第二组为 TX
完成（对端 `RECV_DONE`）。证据不只是寄存器位：`rmt_ipc_open`
分别把两个入口挂到这两组 NVIC 线，RX 入口遍历 peer 的发送
链表并调用本地接收回调，TX 入口释放本地发送槽并推进待发
链表。BTHC0 的 channel 0 比另两条通道多 3 个消息槽，也是其
底层通道不能直接复用 M55/DSP 布局的证据。

A1 这一版 `HAL_RMT_IPC_CFG_T` 在资源和数组字段之后放了 9 个
函数指针，顺序已闭环为：

```text
irq_init
peer_tx_irq_set
local_tx_irq_clear
rx_done
irq_active
rx_irq_suspend
rx_irq_resume
rx_irq_entry
tx_irq_entry
```

其中 `peer_tx_irq_set` 在首条发送消息发布后敲响对端；
`local_tx_irq_clear` 在处理 `RECV_DONE` 时清本地 TX IRQ；
`rx_irq_suspend/resume` 用于未完整消费消息时的流控。这些名称
同时与公开的较早 BES `hal_rmt_ipc` 源码中的同类配置项相符，
但兼容实现仍以 A1 V1.6.88 自身的反编译行为为准。

三张 ops 表每张都是 14 个 32-bit 槽：

```text
+0x00 get_local_cpuname  (原厂表中均为 NULL)
+0x04 get_cpuname
+0x08 get_firmware
+0x0c get_addrenv
+0x10 get_resource
+0x14 is_autostart
+0x18 is_master
+0x1c config
+0x20 start
+0x24 stop
+0x28 notify
+0x2c register_callback
+0x30 reset
+0x34 panic
```

这一布局由两类证据交叉确认：三张连续函数指针表各为
`0x38` 字节；NuttX 通用层 `0x10260c44` 从 ops `+0x04` 取 remote
name，而 `0x102601dc` 从 `+0x00` 取 local name。公开 12.6 源码与其余
13 个槽位的调用顺序一致。

## 资源表是怎样被找到的

`rmt_ipc_get_rsc_table` 调用链为 `0x1024ee00 -> 0x1024ee40`，先按
core/channel 取得底层通道，再等待 peer 打开，最后返回 peer 公告的
resource-table 起止地址。`0x101c5e0a` 按以下约束查找对应记录：

1. 起始地址不能为空；
2. 总长度至少 `0xd0` 且必须是 `0xd0` 的整数倍；
3. 每次前进 `0xd0`，比较记录 `+0x94` 的 32-bit `resource_id`。

`0xd0` 与 NuttX/OpenAMP 的 `struct rptun_rsc_s` 在该 ABI 下的对齐尺寸
一致；`+0x64/+0x78` 分别是两个 vring DA，`+0x68/+0x7c` 是
align，`+0x6c/+0x80` 是 num，`+0x8c/+0x90` 是两个方向的 buffer
size。

APC1 镜像提供了反向交叉证据：

- APC1 的 `M55C0-M55C1` 底层配置表在 `0x10c225c4`；
- 它公告的资源范围是 `0x23c0b000..0x23c0b0d0`，恰好一条记录；
- APC1 端 RPMsg remote name 是 `ap`，它是 non-master 且 autostart；
- APC1 的本地 arena 是 `0x200f3724`，下一个状态对象从
  `0x200f4804` 开始，差值正好是 `0x10e0`。

因此 M55C0/M55C1 的共享资源表地址已确认。DSPC0 和 BTHC0
的表由 peer 在运行时公告，只有 AP 端本地 arena 地址已确认，暂不
把未知 peer 表地址写死。

## arena 大小

V1.6.88 的 `0x101c5e74` 使用与 OpenAMP `vring_size()` 等价的布局：

```text
descriptor+available = align_up(18 * 8 + 6, 8) = 152
used                 = 8 * 8 + 6             = 70
RPMsg buffers         = 8 * 512               = 4096
arena                 = align_up(152 + 70 + 4096, 8)
                      = 4320 = 0x10e0
```

AP 中 DSP arena 的结束 `0x201e165c` 紧接 BTH 通道状态；M55 arena
从 `0x201e1664` 开始。这个无缝边界是算式的第二个静态验证。

## 启动与通知职责

AP 产品启动链先调用 `0x10232354` / `0x1022c4a8` / `0x1023388c`
校验或搬运伴随核镜像，再打开 BES `rmt_ipc`，最后由 NuttX RPTUN
建立 virtqueue/RPMsg。所以兼容 AP 不能只创建一张 OpenAMP 表，它还必须：

- 保持原伴随核所需的 image descriptor 和启动顺序；
- 实现 core 1/2/3、channel 0 的 mailbox IRQ 和 cache/barrier 语义；
- 把 RPTUN `notify(vqid)` 映射成 BES 12-byte 消息槽的发送操作；
- 在 peer 可见的共享资源表中交换两侧 vring DA，不得把 AP
  虚拟地址当作伴随核虚拟地址。

## 还没有闭环的部分

- DSPC0 和 BTHC0 peer 公告的 resource-table 实际地址；
- RX/TX IRQ 方向已确认；仍需恢复 NVIC 优先级、doorbell
  寄存器正式位名和 cache 一致性范围；
- `0x01020100` 版本/就绪哨兵值在 peer 中的写入时机；
- BTH 核镜像的存储位置和可重建边界；
- 原厂 TrustZone/MPU 为 `0x23c0xxxx` 共享区设置的属性。

`compat-src/a1/companion_core` 只固化上述已证明的清单、arena 计算和
resource-id 查找，不触碰寄存器、不启动伴随核、不写共享内存。
