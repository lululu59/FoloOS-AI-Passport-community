#!/bin/zsh

set -u

SCRIPT_DIR="${0:A:h}"
TOOLS_DIR="$SCRIPT_DIR/tools"
PLIST="$HOME/Library/LaunchAgents/com.folotoy.foloos-bridge.plist"
SERVICE="gui/$(id -u)/com.folotoy.foloos-bridge"
INSTALLER_VERSION="2026-08-29 v9"
STABLE_WORKSPACE="$HOME/Library/Application Support/FoloOS/codex-workspace"
CODEX_INSTALL_PROXY=""
export PATH="$HOME/.local/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

pause_window() {
    echo
    read -k 1 "?按任意键继续……"
    echo
}

find_codex() {
    CODEX_BROKEN_PATHS=()
    CODEX_FIND_ERROR=""
    local output
    output="$(/usr/bin/python3 "$TOOLS_DIR/install_autostart.py" --find-codex 2>&1)"
    local result=$?
    if [[ $result -eq 0 && -n "$output" ]]; then
        CODEX_BIN="${output##*$'\n'}"
        return 0
    fi
    CODEX_FIND_ERROR="$output"
    return 1
}

macos_proxy_value() {
    local key="$1"
    /usr/sbin/scutil --proxy 2>/dev/null | /usr/bin/awk -F ' : ' -v wanted="$key" '
        {
            name = $1
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", name)
        }
        name == wanted {
            value = $2
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            print value
            exit
        }
    '
}

detect_macos_proxy() {
    local kind enabled host port scheme
    for kind in HTTPS HTTP SOCKS; do
        enabled="$(macos_proxy_value "${kind}Enable")"
        [[ "$enabled" == "1" ]] || continue
        host="$(macos_proxy_value "${kind}Proxy")"
        port="$(macos_proxy_value "${kind}Port")"
        [[ -n "$host" && -n "$port" ]] || continue
        [[ "$host" == *:* && "$host" != \[*\] ]] && host="[$host]"
        scheme="http"
        [[ "$kind" == "SOCKS" ]] && scheme="socks5h"
        echo "${scheme}://${host}:${port}"
        return 0
    done
    return 1
}

download_codex_installer() {
    local installer="$1"
    local proxy=""
    local env_proxy="${HTTPS_PROXY:-${https_proxy:-${ALL_PROXY:-${all_proxy:-}}}}"
    local curl_args=(-fL --show-error --connect-timeout 10 --max-time 60)
    CODEX_INSTALL_PROXY=""

    if [[ -n "$env_proxy" ]]; then
        CODEX_INSTALL_PROXY="$env_proxy"
        echo "检测到终端代理，正在通过代理下载……"
        /usr/bin/curl "${curl_args[@]}" \
            https://chatgpt.com/codex/install.sh -o "$installer" && return 0
    else
        proxy="$(detect_macos_proxy || true)"
        if [[ -n "$proxy" ]]; then
            CODEX_INSTALL_PROXY="$proxy"
            echo "检测到 macOS 系统代理，正在通过代理下载……"
            /usr/bin/curl "${curl_args[@]}" --proxy "$proxy" \
                https://chatgpt.com/codex/install.sh -o "$installer" && return 0
        else
            echo "未检测到系统代理，正在直接下载……"
            /usr/bin/curl "${curl_args[@]}" \
                https://chatgpt.com/codex/install.sh -o "$installer" && return 0
        fi
    fi

    echo
    echo "终端仍无法连接 chatgpt.com。浏览器代理插件不会自动提供给终端。"
    read "proxy?请输入本机代理地址（例如 http://127.0.0.1:7890），直接回车取消："
    [[ -n "${proxy:-}" ]] || return 1
    [[ "$proxy" == *://* ]] || proxy="http://$proxy"
    CODEX_INSTALL_PROXY="$proxy"
    echo "正在使用手动代理重试……"
    /usr/bin/curl "${curl_args[@]}" --proxy "$proxy" \
        https://chatgpt.com/codex/install.sh -o "$installer"
}

run_codex_installer() {
    local installer="$1"
    if [[ -z "$CODEX_INSTALL_PROXY" ]]; then
        /bin/sh "$installer"
        return $?
    fi
    echo "官方安装程序将继续使用同一代理下载 Codex……"
    HTTPS_PROXY="$CODEX_INSTALL_PROXY" \
    https_proxy="$CODEX_INSTALL_PROXY" \
    ALL_PROXY="$CODEX_INSTALL_PROXY" \
    all_proxy="$CODEX_INSTALL_PROXY" \
        /bin/sh "$installer"
}

ensure_codex() {
    if find_codex; then
        echo "已找到可用于桥接的 Codex CLI：$CODEX_BIN"
        return 0
    fi
    echo "没有找到可正常运行的 Codex CLI。"
    [[ -n "${CODEX_FIND_ERROR:-}" ]] && echo "$CODEX_FIND_ERROR"
    if (( ${#CODEX_BROKEN_PATHS[@]} > 0 )); then
        echo "发现了 Codex 文件，但它们不能提供桥接需要的 app-server daemon 命令："
        printf '  %s\n' "${CODEX_BROKEN_PATHS[@]}"
        echo "如果其中是 ChatGPT.app 内的 codex，说明桌面应用已安装，但独立 Codex CLI 还不完整。"
    fi
    echo "这通常是首次安装，或旧 Codex 安装不完整。"
    read "answer?是否现在使用 OpenAI 官方安装程序安装/修复？[Y/n] "
    [[ "${answer:-Y}" == [Nn]* ]] && return 1
    local installer="$(mktemp -t foloos-codex-install.XXXXXX)"
    echo "正在下载 OpenAI 官方安装程序，最多等待 60 秒……"
    if ! download_codex_installer "$installer"; then
        rm -f "$installer"
        echo "Codex 官方安装程序下载失败或超时。"
        echo "请检查代理软件的本机 HTTP/SOCKS 端口，或先按官方说明安装 Codex CLI："
        echo "https://learn.chatgpt.com/docs/codex/cli"
        return 1
    fi
    run_codex_installer "$installer"
    local install_code=$?
    rm -f "$installer"
    if [[ $install_code -ne 0 ]] || ! find_codex; then
        echo "Codex 安装没有完成，或安装后 app-server 仍不可用。"
        echo "请按官方说明修复，并确认 'codex app-server daemon start' 能启动本地共享服务。"
        return 1
    fi
    return 0
}

ensure_login() {
    "$CODEX_BIN" login status >/dev/null 2>&1 && return 0
    echo "Codex 还没有登录，即将打开登录流程。"
    "$CODEX_BIN" login
}

stop_service() {
    [[ -f "$PLIST" ]] || return 0
    /bin/launchctl bootout "gui/$(id -u)" "$PLIST" >/dev/null 2>&1 || true
}

restart_service() {
    [[ -f "$PLIST" ]] || return 0
    /bin/launchctl bootstrap "gui/$(id -u)" "$PLIST" >/dev/null 2>&1 || true
    /bin/launchctl kickstart -k "$SERVICE" >/dev/null 2>&1 || true
}

configure_wifi() {
    stop_service
    echo "请插好 USB 数据线，并先让 Chrome 刷机页断开设备。"
    /usr/bin/python3 "$TOOLS_DIR/mac_bridge.py" --setup-wifi
    local setup_code=$?
    restart_service
    return $setup_code
}

install_all() {
    if ! ensure_codex; then
        echo "未安装 Codex，桥接安装已取消。"
        return 1
    fi
    ensure_login || return 1
    /usr/bin/python3 "$TOOLS_DIR/install_autostart.py" || return 1
    echo
    echo "安装已完成：后台桥接会随 Mac 登录自动启动。"
    echo "设备 Wi-Fi 改为独立操作；需要时返回主菜单选择 2。"
    echo "需要手动重启时，双击包里的“启动编程伴侣桥接.command”。"
}

run_diagnostics() {
    if ! ensure_codex; then
        echo "没有可正常运行的 Codex CLI。"
        return 1
    fi
    stop_service
    mkdir -p "$STABLE_WORKSPACE"
    echo "前台诊断已启动；检查结束后按 Control-C。"
    /usr/bin/python3 "$TOOLS_DIR/mac_bridge.py" \
        --real-codex --workspace "$STABLE_WORKSPACE" --codex-bin "$CODEX_BIN"
    restart_service
}

while true; do
    clear
    echo "FoloOS 编程伴侣安装与管理 $INSTALLER_VERSION"
    echo "========================"
    echo "1. 一键安装或修复（推荐）"
    echo "2. 配置或更换设备 Wi-Fi"
    echo "3. 前台启动并查看诊断"
    echo "4. 查看后台日志"
    echo "0. 退出"
    echo
    read "choice?请选择："
    case "$choice" in
        1) install_all; pause_window ;;
        2) configure_wifi; pause_window ;;
        3) run_diagnostics; pause_window ;;
        4)
            echo "普通日志：$HOME/Library/Logs/FoloOS/bridge.log"
            echo "错误日志：$HOME/Library/Logs/FoloOS/bridge-error.log"
            tail -n 60 "$HOME/Library/Logs/FoloOS/bridge-error.log" 2>/dev/null || true
            pause_window
            ;;
        0) exit 0 ;;
        *) echo "请输入 0 到 4。"; pause_window ;;
    esac
done
