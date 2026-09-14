#!/bin/zsh

set -eu

ROOT="${0:A:h:h}"
MANAGER="$ROOT/安装与管理编程伴侣.command"

# 只加载代理函数，不进入交互菜单。
source <(/usr/bin/awk '/^ensure_codex\(\)/ { exit } { print }' "$MANAGER")

# 真实 macOS 输出即使没有启用代理，也必须可安全解析。
macos_proxy_value HTTPSEnable >/dev/null

TEST_PROXY_KIND="https"
macos_proxy_value() {
    case "$TEST_PROXY_KIND:$1" in
        https:HTTPSEnable) echo 1 ;;
        https:HTTPSProxy) echo 127.0.0.1 ;;
        https:HTTPSPort) echo 7890 ;;
        socks:HTTPSEnable|socks:HTTPEnable) echo 0 ;;
        socks:SOCKSEnable) echo 1 ;;
        socks:SOCKSProxy) echo 127.0.0.1 ;;
        socks:SOCKSPort) echo 1080 ;;
        *) echo 0 ;;
    esac
}

[[ "$(detect_macos_proxy)" == "http://127.0.0.1:7890" ]]
TEST_PROXY_KIND="socks"
[[ "$(detect_macos_proxy)" == "socks5h://127.0.0.1:1080" ]]

# 官方安装程序还会继续下载二进制，必须继承同一代理。
TEST_DIR="$(mktemp -d -t foloos-proxy-test)"
trap 'rm -rf "$TEST_DIR"' EXIT
export PROXY_TEST_OUTPUT="$TEST_DIR/proxy.txt"
print -r -- 'printf "%s\n" "$HTTPS_PROXY|$https_proxy|$ALL_PROXY|$all_proxy" > "$PROXY_TEST_OUTPUT"' > "$TEST_DIR/install.sh"
CODEX_INSTALL_PROXY="http://127.0.0.1:7890"
run_codex_installer "$TEST_DIR/install.sh" >/dev/null
[[ "$(<"$PROXY_TEST_OUTPUT")" == "http://127.0.0.1:7890|http://127.0.0.1:7890|http://127.0.0.1:7890|http://127.0.0.1:7890" ]]

echo "mac_installer_proxy_ok"
