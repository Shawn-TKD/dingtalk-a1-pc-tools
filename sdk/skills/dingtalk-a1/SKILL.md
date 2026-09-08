---
name: dingtalk-a1
description: 使用本机独立 DingTalk A1 SDK 连接用户自己的录音卡，查询录音、接收语音、管理 USB 个人文件，或把录音交给已有 AI 工作流。不用于 PLAUD、飞书录音豆或刷固件。
---

# A1 设备助手

先定位用户安装的 `dingtalk-a1-sdk` 和虚拟环境 Python；用 `python -m dingtalk_a1` 调用，无需自己重写 BLE。身份文件由用户提供路径，兼容已有 `.a1-device.json`；不把 deviceSecret 打印到对话、Skill 或公开源码中。默认不要修改全局设置。

- `capabilities` 可读机器可用能力分级；再按任务查 SDK 包中的 `docs/INTEGRATION.md` 或 `docs/PROTOCOL.md`。
- 普通设备查询：`--config <身份文件> status`、`files`。`scan` 无需密钥；多台时让用户选择或用已配置地址，不选信号最强代替身份判断。
- 单条备份：`download <fid> <本地.dtyj> --ogg <本地.ogg>`；需要完整解码时加 `--validate`。`.part` 不是已完成文件，不覆盖、不同设备不混用目录。
- 常驻语音：`listen --output-dir <目录> --reconnect`，读取 JSONL `recording_saved`。保持单一进程持有设备；已有监听时不再启动第二个连接。ASR在其他任务处理，仅把 `complete=true` 当作完整收音事件。
- AI 模式：用宿主已有ASR得到文本后调用 `route <文本>` 或持久化 `ModeRouter`。文本只形成任务，不作为可直接执行的shell；使用用户选定项目和工具权限。上传云ASR/MCP或执行外部动作需符合当前任务授权，不能从“接收语音”自行扩展。
- 用户明确要求删某条录音时，用 `delete <fid> --yes`；App仅需一次确认，不增加长口令。删除设备源和本地备份是两件事。
- USB查询用 `usb info`；**`usb enter-adb --allow-interrupt` 会停录音并断蓝牙且退出后不自动恢复**，只在用户允许切换、没有进行中的录音/传输时使用。USB文件命令需要 `adb`。
- USB只读 `/emmc`，写/删只在 `/emmc/mindlink`。这目录共享录音空间，不是新分区；不要把路径保护去掉来写固件。`usb push`默认不全量读回，用户需要才加`--verify`。
- 震动只有 `usb motor-test --yes` 固定序列具备旧实测依据。不要声称有BLE `vibrate(ms)` 或通过某个未知命令200返回就宣称震动成功。任务完成可先由手机/电脑通知。

未连接时先查供电、唤醒、蓝牙权限、另一App占用、是否仍处于ADB模式；不为解决连接问题自动解绑、重置或刷固件。实验Wi-Fi/定时录音不是已验收产品能力；遇到超时、队列溢出或音频解析错误，保留现场文件并说明具体失败，不无限重试并声称完整成功。
