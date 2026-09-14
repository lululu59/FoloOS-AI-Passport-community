# FoloOS AI Passport 社区版

把 AI Passport 变成一台可扩展的像素小系统：设备端提供中文菜单、编程伴侣、番茄专注、单词熊与系统设置；Mac 端桥接本机 Codex，用于读取项目与任务、发送语音指令、显示回答和处理审批。

> [!IMPORTANT]
> 这是非官方社区衍生版本，不是 FoloToy 官方固件，也不由 FoloToy 或 OpenAI 提供支持。刷机有风险，请先阅读[安装指南](INSTALL.md)，并保留官方固件恢复方式。

## 源码与二次开发

**2026-09-14：公开 FoloOS 源码。** 包含现有四个应用、BSP 硬件驱动、Mac / Windows 桥接源码、必要图片与字体资源、音频和主机测试。

- [下载源码包 ZIP](https://github.com/lululu59/FoloOS-AI-Passport-community/raw/refs/heads/main/FoloOS-source-20260914.zip)
- [如何编译、添加自己的小应用](DEVELOPMENT.md)
- [最小 Hello 应用示例](examples/app_hello.c)
- [本次源码发布验证记录](docs/SOURCE_RELEASE_20260914.md)

你可以下载或 Fork 本仓库，修改源码后编译并刷入自己的设备。当前小应用与系统一起编译，尚不支持独立应用包安装。需要 ESP-IDF 5.5.3；新应用按文档追加到菜单，保留已有功能。

此源码包是 2026-09-14 整理的开发快照，不承诺与此前上传的完整固件逐字节对应；之前发布的固件和 Mac 安装器保留原样。

## 已包含

- **编程伴侣**：通过局域网连接 Mac，选择 Codex 项目/任务、语音下达指令、查看状态与审批。
- **番茄专注**：设备端离线计时。
- **单词熊**：100 个词、5 组学习内容，记录进度与错词。
- **系统设置**：亮度、声音、Wi-Fi 配网等设备设置。
- **Mac 图形安装器**：安装/修复后台桥接、配置设备 Wi-Fi、查看连接状态与日志。

## 下载与安装

现有设备固件和 Mac 安装器位于本仓库文件列表：

1. [设备完整固件](https://github.com/lululu59/FoloOS-AI-Passport-community/raw/refs/heads/main/FoloOS-AI-Passport-community-20260825-full.bin)。
2. [Mac 编程伴侣安装器](https://github.com/lululu59/FoloOS-AI-Passport-community/raw/refs/heads/main/FoloOS-Codex-Mac-GUI-Installer-community-20260830-v2.dmg)。
3. [SHA256SUMS.txt](SHA256SUMS.txt)：固件、安装器和源码包的文件完整性校验值。

完整步骤见：[INSTALL.md](INSTALL.md)

## 兼容性与当前边界

- 目标硬件：ESP32-C3、8 MB Flash 的 FoloToy AI Passport。
- 设备只支持 2.4 GHz Wi-Fi；Mac 可以使用同一路由器的 5 GHz，只要双方处于同一局域网。
- Mac 安装器是 Apple Silicon + Intel 通用程序。
- 编程伴侣仍需要 Python 3，以及能够启动 `app-server` 的 Codex CLI；Codex 必须已经登录。
- 当前 DMG 使用 ad-hoc 签名，尚未经过 Apple Developer ID 签名和公证，因此不是“零依赖、双击即用”的正式公开安装器。
- 设备端的番茄专注、单词熊和系统设置可以独立使用；只有编程伴侣需要 Mac 桥接。

## 隐私与安全

- 发布文件不包含发布者的 Wi-Fi 密码、设备配对密钥、Codex 登录信息或个人项目目录。
- Wi-Fi 密码仅在 USB 配网时写入设备，Mac 桥接不保存明文 Wi-Fi 密码。
- 编程伴侣复用用户本机已登录的 Codex，不要求在设备上填写 OpenAI API Key。
- 桥接只应在可信局域网使用，不要把端口 `8765` 暴露到公网。

## 上游与恢复

- [FoloToy AI Passport 官方开发资源](https://github.com/FoloToy/ai-passport)
- [FoloToy 官方文档](https://docs.folotoy.com/zh/docs/web-tool/)
- [浏览器本地固件工具](https://ai-passport.folotoy.cn/tools/web-flasher/)

## 许可证与声明

本仓库保留上游 MIT License 与版权声明；第三方素材和依赖仍遵循各自许可证。详见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。
