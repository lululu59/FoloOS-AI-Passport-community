# 安装指南

## 1. 安装前准备

- 一台 FoloToy AI Passport。
- 一根支持数据传输的 USB 线。
- Chrome 浏览器。
- 如需使用编程伴侣：Apple Silicon 或 Intel Mac、Python 3、已安装并登录的 Codex CLI。

安装 Codex CLI 可参考 [OpenAI 官方说明](https://help.openai.com/en/articles/11096431)。安装后先在终端运行一次 `codex` 并完成登录。

> [!WARNING]
> 写入 `full.bin` 会覆盖设备当前固件。不要在已有重要配置或未经备份的设备上直接操作。需要恢复时，请使用 FoloToy 官方固件和官方刷机说明。

## 2. 校验下载文件

在终端进入下载目录后执行：

```bash
shasum -a 256 FoloOS-AI-Passport-community-20260825-full.bin
shasum -a 256 FoloOS-Codex-Mac-GUI-Installer-community-20260830-v2.dmg
```

输出应与 [SHA256SUMS.txt](SHA256SUMS.txt) 一致。

## 3. 刷入设备完整固件

1. 用 USB 数据线连接 AI Passport。
2. 在 Chrome 打开 [浏览器本地固件工具](https://ai-passport.folotoy.cn/tools/web-flasher/)。
3. 点击“连接设备”，选择名称包含 `USB JTAG/serial debug unit` 的串口。
4. 只选择 `FoloOS-AI-Passport-community-20260825-full.bin`。
5. 写入地址必须是 `0x0`。
6. 点击“开始写入”，等待校验完成和设备重启。

不要同时选择多个 `.bin`，也不要把完整固件写到 `0x8000` 或 `0x20000`。

## 4. 安装 Mac 编程伴侣

1. 打开 `FoloOS-Codex-Mac-GUI-Installer-community-20260830-v2.dmg`。
2. 把 `FoloOS 编程伴侣.app` 拖入“应用程序”。
3. 因为当前版本未公证，首次打开请在 Finder 中右键 App，选择“打开”，再确认一次。
4. 点击“安装 / 修复桥接”。安装桥接时不需要插设备。
5. 确认界面显示可用 Codex CLI，并且后台桥接已安装运行。

如果安装器提示缺少 Python 3，请先安装 Xcode Command Line Tools 或 Python 3；如果提示 Codex CLI 不可用，需安装能启动 `app-server` 的独立 Codex CLI，仅安装桌面 App 不一定满足桥接要求。

## 5. 配置设备 Wi-Fi

1. 先在 Chrome 刷机工具中断开设备串口。
2. 用 USB 数据线连接设备。
3. 在 Mac 安装器中点击“配置设备 Wi-Fi…”。
4. 输入 2.4 GHz Wi-Fi 名称和密码。
5. 配置完成后拔掉 USB，确认安装器显示“设备已通过 Wi-Fi 认证”。

Mac 可以连接同一路由器的 5 GHz Wi-Fi，但设备与 Mac 必须在同一局域网。不要使用开启了 AP 隔离、客户端隔离或访客隔离的网络。

## 6. 开始使用

1. 打开设备上的“编程伴侣”。
2. 等待项目列表同步。
3. 选择项目与任务，按设备提示录音、确认发送、查看回答或处理审批。

后台桥接安装后会随 Mac 登录自动启动，通常不需要每次手动打开安装器。

## 常见问题

### 设备显示未连接

- 确认设备和 Mac 在同一局域网。
- 确认路由器没有开启网络隔离。
- 在安装器中分别检查“后台桥接”和“设备实际连接”；桥接进程运行不等于设备已经认证连接。
- 打开日志目录：`~/Library/Logs/FoloOS/`。

### 找不到串口或配网无响应

- 退出或断开 Chrome 刷机工具。
- 关闭占用串口的终端监视器或其他刷机程序。
- 更换支持数据传输的 USB 线。

### Codex 项目为空或桥接启动失败

- 确认 Codex CLI 已登录并可以正常运行。
- 安装器需要的不是仅能显示版本号的包装程序，而是能启动 `app-server` 的 CLI。
- 点击安装器中的“安装 / 修复桥接”，然后重新检测。

### macOS 阻止打开

当前 DMG 没有 Apple Developer ID 签名和公证。请只从本仓库 Release 下载、核对 SHA-256，然后在 Finder 中右键 App 选择“打开”。不要从来源不明的转载包安装。
