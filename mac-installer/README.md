# FoloOS 编程伴侣 Mac 图形安装器

这是面向 Apple Silicon 和 Intel Mac 的统一图形安装入口。它复用已经在真实 Mac 与 AI Passport 上验证的 v9 桥接，不重写设备协议。

## 用户操作

1. 打开 DMG，把 `FoloOS 编程伴侣.app` 拖入“应用程序”。
2. 首次是内测未公证版：在 Finder 中右键 App，选择“打开”。
3. 先点击“安装 / 修复桥接”。这一步不要求插设备。
4. 只有需要配置或更换设备 Wi-Fi 时，才插 USB 并点击“配置设备 Wi-Fi…”。

安装器会显示四个独立状态：可用 Codex CLI、后台桥接、设备 Wi-Fi 和设备实际连接。不再用“桥接进程已启动”代替“设备已认证连接”。

## 当前内测版边界

- App 本身是 Apple Silicon + Intel 通用原生二进制，界面使用系统 AppKit，不依赖 Swift 运行时。
- 当前内测 DMG 是 ad-hoc 签名，未使用 Apple Developer ID 公证。公开分发前还需开发者证书签名与 notarization。
- 桥接仍需 Python 3。安装器会先检测；缺少时会给出明确错误，不会虚假显示成功。真正“零依赖”公开版需在 App 内再嵌入独立运行时。
- Codex 必须已登录，且 CLI 能真正启动 `app-server daemon` 并建立本地 socket。

## 开发构建

在仓库根目录执行：

```bash
./mac-installer/build.sh
```

输出 DMG 和 SHA-256 位于 `releases/community-20260825/`。
