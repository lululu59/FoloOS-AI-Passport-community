#!/usr/bin/env python3
"""Windows stdio transport for the official Codex app-server protocol."""

from __future__ import annotations

import json
import queue
import shutil
import subprocess
import threading
import time
from pathlib import Path
from typing import Any

if __package__:
    from .codex_backend import CodexAppServer, CodexEventRouter, CodexProtocolError
else:
    from codex_backend import CodexAppServer, CodexEventRouter, CodexProtocolError


def find_codex_executable(explicit: str | None = None) -> str:
    if explicit:
        resolved = shutil.which(explicit)
        if resolved:
            return resolved
        candidate = Path(explicit).expanduser()
        if candidate.is_file():
            return str(candidate)
    found = shutil.which("codex") or shutil.which("codex.exe")
    if found:
        return found
    raise CodexProtocolError(
        "没有找到 Codex CLI。请先安装并打开 Windows Codex，再确认 PowerShell 可执行 codex --version"
    )


class WindowsCodexAppServer(CodexAppServer):
    """Own one app-server stdio session; the desktop can still show its task history."""

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        super().__init__(*args, **kwargs)
        self.process: subprocess.Popen[str] | None = None
        self._lines: queue.Queue[str | None] = queue.Queue()
        self._reader: threading.Thread | None = None
        self._stderr_reader: threading.Thread | None = None

    def start(self) -> None:
        if not self.workspace.is_dir():
            raise CodexProtocolError(f"Codex 工作目录不存在：{self.workspace}")
        self.executable = find_codex_executable(self.executable)
        try:
            self.process = subprocess.Popen(
                [self.executable, "app-server", "--listen", "stdio://"],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
        except OSError as error:
            raise CodexProtocolError("Windows 无法启动 Codex app-server") from error

        self.socket = self  # Parent helpers only use this as a connected sentinel.
        self._reader = threading.Thread(target=self._read_stdout, daemon=True)
        self._reader.start()
        self._stderr_reader = threading.Thread(target=self._read_stderr, daemon=True)
        self._stderr_reader.start()
        self._request(
            "initialize",
            {
                "clientInfo": {
                    "name": "folos-coding-companion-windows",
                    "title": "FoloOS 编程伴侣 Windows",
                    "version": "1.0",
                },
                "capabilities": {"experimentalApi": True},
            },
        )
        self._write({"method": "initialized", "params": {}})

    def _read_stdout(self) -> None:
        process = self.process
        if process is None or process.stdout is None:
            return
        for line in process.stdout:
            self._lines.put(line)
        self._lines.put(None)

    def _read_stderr(self) -> None:
        process = self.process
        if process is None or process.stderr is None:
            return
        for line in process.stderr:
            if self.verbose and line.strip():
                print(f"Codex 日志  {line.rstrip()}")

    def _write(self, message: dict[str, Any]) -> None:
        process = self.process
        if process is None or process.stdin is None or process.poll() is not None:
            self.socket = None
            raise CodexProtocolError("Codex app-server 未运行")
        if self.verbose:
            print(f"桥接 → Codex  {message}")
        try:
            process.stdin.write(
                json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n"
            )
            process.stdin.flush()
        except (OSError, BrokenPipeError) as error:
            self.socket = None
            raise CodexProtocolError("Codex app-server 连接已断开") from error

    def _read_one(self, timeout: float) -> bool:
        if self.process is None:
            return False
        try:
            line = self._lines.get(timeout=max(0.0, timeout))
        except queue.Empty:
            return False
        if line is None:
            self.socket = None
            raise CodexProtocolError("Codex app-server 已退出")
        return self._process_wire_message(line.encode("utf-8"))

    def _resume_thread(self, thread_key: str) -> list[dict[str, Any]]:
        thread = self.threads.get(thread_key)
        if thread is None or not thread.get("thread_id"):
            return [{"type": "error", "text": "选择的 Codex 任务已经失效"}]
        self.thread_id = str(thread["thread_id"])
        self.thread_title = str(thread["title"])
        self.router = CodexEventRouter()
        result = self._request("thread/resume", {"threadId": self.thread_id})
        resumed = result.get("thread") if isinstance(result.get("thread"), dict) else result
        if isinstance(resumed, dict):
            self._set_thread_baseline(resumed)
        if self.thread_opener is not None:
            try:
                self.thread_opener(self.thread_id)
            except OSError:
                pass
        return [self._selected_message()]

    def _start_command(self, command: str) -> None:
        if self.thread_id is None:
            raise CodexProtocolError("Codex 任务尚未初始化")
        cwd = str((self.selected_project or {}).get("cwd") or self.workspace)
        result = self._request(
            "turn/start",
            {
                "threadId": self.thread_id,
                "input": [{"type": "text", "text": command}],
                "cwd": cwd,
                "approvalPolicy": "onRequest",
                "sandboxPolicy": {
                    "type": "workspaceWrite",
                    "writableRoots": [cwd],
                    "networkAccess": False,
                },
            },
        )
        turn = result.get("turn") if isinstance(result.get("turn"), dict) else result
        if isinstance(turn, dict):
            self.router.active_turn_id = str(turn.get("id") or "") or None

    def handle_device_event(self, event: dict[str, Any]) -> list[dict[str, Any]]:
        if event.get("type") == "cancel_task" and self.thread_id:
            turn_id = self.router.active_turn_id
            if turn_id:
                self._request(
                    "turn/interrupt",
                    {"threadId": self.thread_id, "turnId": turn_id},
                )
                return [{"type": "task_status", "text": "正在停止 Codex 任务"}]
        return super().handle_device_event(event)

    def poll(self) -> list[dict[str, Any]]:
        if self.process is None or self.process.poll() is not None:
            return []
        while self._read_one(0.0):
            pass
        messages = list(self._device_outbox)
        self._device_outbox.clear()
        return messages

    def close(self) -> None:
        process = self.process
        if process is None:
            return
        self.router.cancel_pending_approval()
        try:
            self._flush_router_rpc()
        except CodexProtocolError:
            pass
        try:
            if process.stdin is not None:
                process.stdin.close()
            process.terminate()
            process.wait(timeout=3)
        except (OSError, subprocess.TimeoutExpired):
            process.kill()
        self.process = None
        self.socket = None
