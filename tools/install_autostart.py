#!/usr/bin/env python3
"""Install the FoloOS real-Codex bridge as a macOS login service."""

from __future__ import annotations

import os
from pathlib import Path
import plistlib
import shutil
import socket
import subprocess
import sys
import time


LABEL = "com.folotoy.foloos-bridge"
RUNTIME_FILES = (
    "mac_bridge.py",
    "codex_backend.py",
    "mac_speech_helper.m",
    "mac_speech_helper-Info.plist",
)
CODEX_APP_SERVER_START = ("app-server", "daemon", "start")


def login_shell_executable(name: str) -> str | None:
    try:
        result = subprocess.run(
            ["/bin/zsh", "-lic", f"command -v {name} 2>/dev/null || true"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        return result.stdout.strip().splitlines()[-1] or None
    except (IndexError, OSError, subprocess.SubprocessError):
        return None


def launch_agent_path(codex: str, node: str | None = None) -> str:
    home = Path.home()
    node = node or shutil.which("node") or login_shell_executable("node")
    paths = [
        str(Path(codex).parent),
        str(Path(codex).resolve().parent),
        str(Path(node).parent) if node else None,
        str(home / ".local" / "bin"),
        str(home / ".npm-global" / "bin"),
        str(home / ".volta" / "bin"),
        str(home / ".asdf" / "shims"),
        str(home / ".bun" / "bin"),
        str(home / "Library" / "pnpm"),
    ]
    paths.extend(
        str(path)
        for path in sorted(
            (home / ".nvm" / "versions" / "node").glob("*/bin"), reverse=True
        )
    )
    paths.extend(
        [
            "/opt/homebrew/bin",
            "/usr/local/bin",
            "/usr/bin",
            "/bin",
            "/usr/sbin",
            "/sbin",
        ]
    )
    return ":".join(dict.fromkeys(path for path in paths if path))


def app_server_socket_works(timeout: float = 5.0) -> bool:
    socket_path = (
        Path.home()
        / ".codex"
        / "app-server-control"
        / "app-server-control.sock"
    )
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            connection.settimeout(min(1.0, max(0.1, deadline - time.monotonic())))
            connection.connect(str(socket_path))
            return True
        except OSError:
            time.sleep(0.05)
        finally:
            connection.close()
    return False


def codex_app_server_works(candidate: str) -> bool:
    """Require the real daemon command and its local control socket."""
    try:
        result = subprocess.run(
            [candidate, *CODEX_APP_SERVER_START],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0 and app_server_socket_works()


def find_codex() -> str:
    login_shell_codex = login_shell_executable("codex")

    candidates = [
        shutil.which("codex"),
        login_shell_codex,
        str(Path.home() / ".local" / "bin" / "codex"),
        "/opt/homebrew/bin/codex",
        "/usr/local/bin/codex",
        str(Path.home() / ".npm-global" / "bin" / "codex"),
        str(Path.home() / ".volta" / "bin" / "codex"),
        str(Path.home() / ".asdf" / "shims" / "codex"),
        str(Path.home() / ".bun" / "bin" / "codex"),
        str(Path.home() / "Library" / "pnpm" / "codex"),
        str(
            Path.home()
            / ".codex"
            / "packages"
            / "standalone"
            / "current"
            / "bin"
            / "codex"
        ),
        str(
            Path.home()
            / ".codex"
            / "packages"
            / "standalone"
            / "current"
            / "codex"
        ),
    ]
    candidates.extend(
        str(path)
        for path in sorted(
            (Path.home() / ".nvm" / "versions" / "node").glob("*/bin/codex"),
            reverse=True,
        )
    )
    candidates.extend(
        [
            "/Applications/ChatGPT.app/Contents/Resources/codex",
            str(
                Path.home()
                / "Applications"
                / "ChatGPT.app"
                / "Contents"
                / "Resources"
                / "codex"
            ),
            "/Applications/Codex.app/Contents/Resources/codex",
            str(
                Path.home()
                / "Applications"
                / "Codex.app"
                / "Contents"
                / "Resources"
                / "codex"
            ),
        ]
    )
    checked: set[str] = set()
    for candidate in candidates:
        if not candidate or candidate in checked or not Path(candidate).is_file():
            continue
        checked.add(candidate)
        if codex_app_server_works(candidate):
            return candidate
    raise RuntimeError(
        "没有找到能提供 app-server daemon 命令的 Codex CLI。请重新运行"
        "“安装与管理编程伴侣”，按提示安装或修复 Codex。"
    )


def launch_agent_config(
    project_root: Path, python: str, codex: str, runtime_root: Path = None
) -> dict:
    runtime_root = runtime_root or project_root / "tools"
    logs = Path.home() / "Library" / "Logs" / "FoloOS"
    return {
        "Label": LABEL,
        "ProgramArguments": [
            python,
            str(runtime_root / "mac_bridge.py"),
            "--real-codex",
            "--workspace",
            str(project_root),
            "--codex-bin",
            codex,
        ],
        "WorkingDirectory": str(runtime_root),
        "RunAtLoad": True,
        "KeepAlive": True,
        "ThrottleInterval": 10,
        "ProcessType": "Interactive",
        "EnvironmentVariables": {
            "PYTHONUNBUFFERED": "1",
            "PATH": launch_agent_path(codex),
        },
        "StandardOutPath": str(logs / "bridge.log"),
        "StandardErrorPath": str(logs / "bridge-error.log"),
    }


def install(project_root: Path) -> Path:
    project_root = project_root.resolve()
    bridge = project_root / "tools" / "mac_bridge.py"
    if not bridge.exists():
        raise RuntimeError(f"没有找到桥接程序：{bridge}")

    agents = Path.home() / "Library" / "LaunchAgents"
    runtime = (
        Path.home()
        / "Library"
        / "Application Support"
        / "FoloOS"
        / "bridge-runtime"
    )
    workspace = (
        Path.home()
        / "Library"
        / "Application Support"
        / "FoloOS"
        / "codex-workspace"
    )
    logs = Path.home() / "Library" / "Logs" / "FoloOS"
    agents.mkdir(parents=True, exist_ok=True)
    runtime.mkdir(parents=True, exist_ok=True)
    workspace.mkdir(parents=True, exist_ok=True)
    logs.mkdir(parents=True, exist_ok=True)
    for filename in RUNTIME_FILES:
        shutil.copy2(project_root / "tools" / filename, runtime / filename)
    plist_path = agents / f"{LABEL}.plist"
    temporary = plist_path.with_suffix(".plist.tmp")
    with temporary.open("wb") as file:
        plistlib.dump(
            launch_agent_config(
                workspace, sys.executable, find_codex(), runtime
            ),
            file,
            sort_keys=False,
        )
    os.chmod(temporary, 0o600)
    temporary.replace(plist_path)

    domain = f"gui/{os.getuid()}"
    service = f"{domain}/{LABEL}"
    subprocess.run(
        ["/bin/launchctl", "bootout", domain, str(plist_path)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    result = subprocess.run(
        ["/bin/launchctl", "bootstrap", domain, str(plist_path)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or "未知错误"
        raise RuntimeError(f"无法注册后台服务：{detail}")
    subprocess.run(["/bin/launchctl", "enable", service], check=True)
    subprocess.run(["/bin/launchctl", "kickstart", "-k", service], check=True)
    time.sleep(2)
    status = subprocess.run(
        ["/bin/launchctl", "print", service],
        capture_output=True,
        text=True,
        check=False,
    )
    if status.returncode != 0 or "state = running" not in status.stdout:
        error_log = logs / "bridge-error.log"
        detail = "后台进程没有保持运行"
        if error_log.exists():
            lines = [line.strip() for line in error_log.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines() if line.strip()]
            if lines:
                detail = lines[-1]
        raise RuntimeError(f"后台服务启动失败：{detail}")
    return plist_path


def main() -> int:
    try:
        project_root = Path(__file__).resolve().parent.parent
        plist_path = install(project_root)
        print("编程伴侣后台服务已安装并启动。")
        print("以后登录 Mac 会自动连接设备和真实 Codex，无需手动开桥。")
        print(f"服务配置：{plist_path}")
        print(f"日志目录：{Path.home() / 'Library' / 'Logs' / 'FoloOS'}")
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"安装失败：{error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    if sys.argv[1:] == ["--find-codex"]:
        try:
            print(find_codex())
            raise SystemExit(0)
        except (OSError, RuntimeError, subprocess.SubprocessError) as error:
            print(error, file=sys.stderr)
            raise SystemExit(1)
    raise SystemExit(main())
