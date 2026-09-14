#!/bin/zsh

set -u

PLIST="$HOME/Library/LaunchAgents/com.folotoy.foloos-bridge.plist"
SERVICE="gui/$(id -u)/com.folotoy.foloos-bridge"
LOG_DIR="$HOME/Library/Logs/FoloOS"
exit_code=0

echo "正在启动 FoloOS 编程伴侣桥接……"
echo

if [[ ! -f "$PLIST" ]]; then
    echo "还没有安装后台桥接服务。"
    echo "请先双击“安装与管理编程伴侣.command”，选择 1。"
    exit_code=1
else
    /bin/launchctl bootstrap "gui/$(id -u)" "$PLIST" >/dev/null 2>&1 || true
    if /bin/launchctl kickstart -k "$SERVICE" >/dev/null 2>&1; then
        sleep 2
        service_state="$(/bin/launchctl print "$SERVICE" 2>/dev/null || true)"
        if [[ "$service_state" == *"state = running"* ]]; then
            echo "编程伴侣后台桥接正在运行。"
            echo "设备真正连入后，设备端才会显示已连接。"
            echo "后台日志：$LOG_DIR"
        else
            echo "后台服务已注册，但进程没有保持运行。"
            if [[ -f "$LOG_DIR/bridge-error.log" ]]; then
                echo "最近错误："
                tail -n 8 "$LOG_DIR/bridge-error.log"
            fi
            echo "请打开“安装与管理编程伴侣.command”，选择 4 查看错误日志。"
            exit_code=1
        fi
    else
        echo "后台桥接启动失败。"
        echo "请重新运行一键安装或修复。"
        exit_code=1
    fi
fi

echo
read -k 1 "?按任意键关闭窗口……"
echo
exit $exit_code
