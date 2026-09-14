#!/bin/zsh

SCRIPT_DIR=${0:A:h}
cd "$SCRIPT_DIR" || exit 1

FIRMWARE="$SCRIPT_DIR/build/FoloToy-AI-Passport.bin"
AGENT="$HOME/Library/LaunchAgents/com.folotoy.foloos-bridge.plist"
echo "FoloOS 无线固件升级"
echo "请保持设备开机，并确认 Mac 与设备在同一局域网。"
echo
if [[ -f "$AGENT" ]]; then
  echo "正在暂时停止编程伴侣后台服务……"
  /bin/launchctl bootout "gui/$UID" "$AGENT" >/dev/null 2>&1
fi
python3 tools/mac_bridge.py --ota "$FIRMWARE"
STATUS=$?

echo "正在恢复编程伴侣后台服务……"
/usr/bin/python3 tools/install_autostart.py
if [[ $? -ne 0 && $STATUS -eq 0 ]]; then
  STATUS=1
fi

echo
if [[ $STATUS -ne 0 ]]; then
  echo "升级失败，设备仍会保留当前可启动固件。"
fi
read -k 1 "REPLY?按任意键关闭窗口……"
echo
exit $STATUS
