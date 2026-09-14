#!/bin/zsh

SCRIPT_DIR=${0:A:h}
cd "$SCRIPT_DIR" || exit 1

echo "AI Passport 无线桥首次配置"
echo "请先插入 USB，并确认 Chrome 刷机工具已经断开设备。"
echo "Wi-Fi 名称和密码只会在这个本机窗口输入。"
echo
python3 tools/mac_bridge.py --setup-wifi
STATUS=$?

echo
if [[ $STATUS -ne 0 ]]; then
  echo "配置失败，请查看上面的提示。"
fi
read -k 1 "REPLY?按任意键关闭窗口……"
echo
exit $STATUS
