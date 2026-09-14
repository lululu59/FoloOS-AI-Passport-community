# FoloOS Codex Windows 安装包

这是 Windows 10/11 版电脑桥接包，不含设备固件，不会修改或覆盖设备上的编程伴侣、番茄专注、单词熊和系统设置。

## 需要先准备

1. 已登录的 Windows Codex 应用，且 PowerShell 输入 `codex --version` 能返回版本号。
2. Python 3.10 或更高版本（推荐 3.11/3.12）。安装 Python 时勾选加入 PATH。
3. 已刷入 FoloOS 分享版固件的 AI Passport。
4. 电脑和设备连接同一个 2.4GHz 局域网。代理软件不改变 Wi-Fi 名称，设备填写路由器或手机热点本身的名称和密码。
5. Windows 设置中已安装中文语音识别/语音包。英文单词发音建议同时安装英文（美国）语音。

官方说明：

- Windows Codex：[OpenAI Windows 文档](https://learn.chatgpt.com/docs/windows/windows-app)
- Codex App Server：[OpenAI App Server 文档](https://learn.chatgpt.com/docs/app-server)

## 首次安装

1. 解压 ZIP，不要在压缩包预览窗口里直接运行。
2. 双击唯一的入口 `安装与管理编程伴侣.bat`。
3. 选择 `1. 一键安装或修复`。
4. 安装程序会检查 Codex CLI、登录状态、Python、设备 Wi-Fi 和后台桥接。

Codex 账号和登录信息不能随分享包分发。若电脑没有可正常运行的 Codex CLI，安装程序会打开 OpenAI 官方安装说明；安装并登录后，再重新选择“一键安装或修复”。

安装后，桥接会在 Windows 登录时自动后台启动。以后通常不需要再手动打开。

## 换 Wi-Fi

设备已预存的 Wi-Fi 可以在设备“系统设置 → Wi-Fi”中选择。若要加入全新的网络：

1. 用 USB 数据线连接设备。
2. 关闭占用串口的 Chrome 刷机页。
3. 再次打开 `安装与管理编程伴侣.bat`，选择 2，输入新网络。

## 日常使用

1. 打开 Windows Codex 并保持登录。
2. 打开设备“编程伴侣”，选择项目和任务。
3. 按一次 OK 开始录音，再按一次结束；在设备确认文字后发送。
4. 任务进度、审批、最终回答和语音播报会回到设备。

Windows 版使用 OpenAI 官方 `codex app-server` 标准输入输出协议。任务完成后会进入同一 Codex 本地任务历史；任务正在由桥接执行时，Codex 桌面端可能提示任务正在其他位置运行，这是官方会话所有权边界，不是设备故障。

## 数据和隐私

- 最后一次录音：`%LOCALAPPDATA%\FoloOS\recordings\last-command.wav`，下一次录音覆盖它，不累计保存。
- 配对信息：`%LOCALAPPDATA%\FoloOS\bridge.json`。
- 单词熊数据：`%LOCALAPPDATA%\FoloOS\WordBear`。
- 桥接日志：`%LOCALAPPDATA%\FoloOS\CodexBridge\bridge.log` 和 `bridge-error.log`。
- 回答语音按需生成并分片传输，临时 WAV 会自动删除，不长期占用设备和电脑空间。

## 常见问题

### 提示找不到 codex

重新打开安装与管理程序并选择“一键安装或修复”。若 Codex 缺失或安装不完整，程序会给出官方安装入口。本包不会携带你的账号、密钥或登录状态。

### 语音识别提示没有匹配的语言

进入“Windows 设置 → 时间和语言 → 语言和区域”，给中文安装“语音”，然后重启 Windows。

### 设备连接不上

确认电脑与设备同一局域网、设备使用 2.4GHz、访客网络没有开启设备隔离。换网络后重新运行配网脚本。

### 查看运行状态

打开安装与管理程序，选择 5 查看日志。如果修改过 Codex 或 Python 安装，重新选择“一键安装或修复”。

## 取消后台启动

打开安装与管理程序，选择 4。它只停止桥接并移除自动启动，不删除设备 Wi-Fi 配置和单词熊学习记录。
