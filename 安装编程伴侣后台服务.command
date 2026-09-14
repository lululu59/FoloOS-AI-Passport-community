#!/bin/zsh

SCRIPT_DIR=${0:A:h}
cd "$SCRIPT_DIR" || exit 1

echo "正在安装 FoloOS 编程伴侣后台服务……"
/usr/bin/python3 tools/install_autostart.py
STATUS=$?

echo
if [[ $STATUS -eq 0 ]]; then
  echo "安装完成。以后不需要手动启动桥接窗口。"
fi
read -k 1 "REPLY?按任意键关闭窗口……"
echo
exit $STATUS
