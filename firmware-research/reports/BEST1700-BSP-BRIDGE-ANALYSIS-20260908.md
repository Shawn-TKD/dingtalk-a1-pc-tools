# A1 V1.6.88 与 openvela BEST1700 BSP 的桥接分析

## 结论

`open-vela/vendor_bes` 的 `dev-ai-contest-2026` 分支提供了与 A1 AP
同平台、同 Flash 基址的 BEST1700/BES2800BP 库模式 BSP。它不是 A1
原厂产品源码，但已经可以作为兼容实现的芯片 HAL、启动和链接参考。

更关键的是：从公开 AP 静态库提取的函数，在屏蔽 ELF 链接重定位后，
有 **139 个函数符号唯一匹配 A1 V1.6.88 AP 镜像**。其中包括 RMT IPC、
CMU、IOMUX、USB device controller、按键、SD/MMC 和 NOR Flash 函数。
这证明两者不只是使用同一种 Cortex-M55，而是共享了相当大一部分同世代
BES 平台实现。

## 来源与可复现性

- 上游：`https://github.com/open-vela/vendor_bes`
- 分支：`dev-ai-contest-2026`
- 本次固定提交：`36465377d944a575ce8a0c7ba45259f5c6c61ea8`
- 本地只读参考：`reference-cache/openvela-vendor-bes`
- 提取对象：`reference-cache/best1700-objects`
- 匹配结果：`analysis/best1700-object-matches.tsv`
- 匹配工具：`match-elf-objects.py`

匹配工具不靠字符串或地址猜测。它读取 ELF 符号与 relocation，屏蔽链接器
必然改写的 32-bit 调用/地址位置，再对剩余机器码做唯一匹配。默认拒绝小于
12 字节、最长稳定片段小于 8 字节和多重命中的函数。因此该表适合用来恢复
原函数名；它不证明未命中的函数不同，未命中还可能来自配置、优化和版本差异。

复现命令：

```powershell
$objects = Get-ChildItem .\reference-cache\best1700-objects -Filter *.o |
  ForEach-Object FullName
.\.venv\Scripts\python.exe .\match-elf-objects.py \
  .\unpacked-V1.6.88\nuttx_ap.bin $objects \
  --base 0x10190000 --output .\analysis\best1700-object-matches.tsv
```

## 同一平台的硬证据

公开 linker script 与 A1 静态恢复结果同时给出：

- chip/platform：`CONFIG_ARCH_CHIP_BES1700=y`，实际芯片 BES2800BP；
- NOR 总容量：`0x01000000`，即 16 MiB；
- AP Flash origin：`0x10000000 + 0x190000 = 0x10190000`；
- AON mailbox：`0x23c00000`；
- RPTUN AP 端为 master；
- eMMC、GPIO IRQ、AP↔APC1 RPMsg 均在公开配置中启用。

A1 镜像字符串还保留了 `boards/best1700_ep/.../configs/ap` 和
`CHIP=best1700` 构建路径。公开 linker script 的 AP 起点与 A1 入口镜像
起点逐字一致，比仅凭芯片型号更强。

## 当前精确匹配覆盖

| 公开对象 | A1 中唯一匹配函数数 | 对兼容实现的价值 |
| --- | ---: | --- |
| `hal_cmu_best1700.o` | 49 | 时钟、复位、伴随核启动/唤醒 |
| `hal_iomux_best1700.o` | 27 | 引脚复用、数字麦克风、显示和调试口 |
| `hal_sdmmc.o` | 17 | eMMC/SD 协议与卡状态 |
| `hal_usb.o` | 13 | USB device controller、端点和中断 |
| `hal_key.o` | 7 | GPIO 按键映射与中断使能 |
| `hal_norflash.o` | 7 | NOR 初始化、时钟和挂起策略 |
| `hal_m55c0_2_bthc0_best1700.o` | 6 | AP↔BTH mailbox |
| `hal_m55c0_2_dspc0_best1700.o` | 5 | AP↔DSP mailbox |
| `hal_m55c0_2_m55c1_best1700.o` | 5 | AP↔APC1 mailbox |
| `hal_rmt_ipc_common.o` | 2 | IPC busy/resource-table 路径 |
| `hal_rmt_ipc_best1700.o` | 1 | core/channel 配置选择器 |

同一地址存在弱符号/别名时，表中可能出现两个符号名，例如
`boot_loader_entry_hook` 与 `hal_cmu_boot_setup_sp_ram`。139 是匹配的
symbol 行数，不应误报成 139 个互不重叠的地址。

对关键路径已经得到的正式命名包括：

- `0x10240764 hal_rmt_ipc_get_cfg`
- `0x1024e43c _rmt_ipc_tx_busy`
- `0x1024ee40 hal_rmt_ipc_get_rsc_table`
- `0x10256e28 hal_usb_wakeup`
- `0x10256ec0 hal_usb_reset_all_transfers`
- `0x10256ef4 hal_usb_send_ep0`
- `0x1025703c hal_usb_recv_ep0`
- `0x1025843c hal_usb_close`
- `0x1024c11c hal_gpiokey_enable_irq`

三组 M55C1/DSPC0/BTHC0 mailbox 函数的机器码也与 A1 对应地址匹配。
这把之前依靠寄存器行为和调用图恢复的回调语义，提升为同芯片目标文件的
直接佐证。

## 可以复用什么

公开包提供 AP/APC1 的 `libbesboard_*`、`libbeschip_*`、
`libnx_bestbsp_*`，以及 linker script、defconfig、programmer 和 OTA boot
二进制。合理的兼容路线是：

1. 自己维护 A1 产品层：BLE 协议、录音状态机、文件同步、按钮策略、
   震动/显示策略和身份边界；
2. 在合法、匹配的构建环境中，把产品层绑定到 BEST1700 BSP API；
3. 保留原厂 APC1/HiFi/USER 伴随镜像，先做 no-flash 链接和开发板运行；
4. 恢复 A1 特有 pinmux、PMIC、eMMC、麦克风、显示与马达板级配置；
5. 只有完成整机备份、BootROM 恢复与签名/启动标记验证后，才考虑 A1 实机刷写。

这些库不能直接让当前 host 版 `compat-src` 变成可刷固件。公开板是
`aos_evb`，A1 原厂板是 `dtiot_2800hp`；二者芯片层高度一致，但板级
引脚、电源、外设和产品启动序列不同。

## 许可边界

该公开仓库根 `LICENSE` 明确写的是 BES 保留权利/授权使用，并非可任意
再分发的 Apache 源码包；静态库还附带第三方 notices。当前工程只保存本地
研究缓存、符号/匹配事实和独立编写的兼容源码，不把 BES 二进制库并入 SDK
发布物，也不从其他泄露源码复制实现。若未来发布可链接的 target port，
使用者应自行取得并接受对应 BSP 授权。

## 仍未解决

- 公开包没有 A1 `dtiot_2800hp` 板级源码；
- 未证明公开库与 A1 对所有 ABI 都完全相同；
- USB HID 产品协议在 A1 业务层，公开 `hal_usb.o` 只解决控制器 HAL；
- BLE host 主要运行在 BTH companion core，公开 AP 库不能代替完整 BT 固件；
- 仍缺 BootROM 恢复入口、验签/启动提交、故障回滚的实机闭环；
- 当前没有生成或刷写任何 A1 镜像。

所以这次发现显著降低了“从零重建芯片 BSP”的工作量，但还没有把产品变成
安全可刷状态。
