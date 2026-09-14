#!/usr/bin/env python3
"""Small Codex app-server client for the FoloOS hardware bridge."""

from __future__ import annotations

import base64
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import socket
import struct
import subprocess
import time
from typing import Any, Callable
import unicodedata
import uuid


DEVICE_STATUS_CHARS = 54
DEVICE_RESULT_CHARS = 900
DEVICE_APPROVAL_QUESTION_CHARS = 34
DEVICE_APPROVAL_DETAIL_CHARS = 82
DEVICE_CATALOG_TITLE_CHARS = 22
DEVICE_CATALOG_DETAIL_CHARS = 28
CATALOG_LIMIT = 8
CATALOG_PAGE_SIZE = CATALOG_LIMIT - 1
DEFAULT_APP_SERVER_SOCKET = (
    Path.home() / ".codex" / "app-server-control" / "app-server-control.sock"
)
DEFAULT_DESKTOP_IPC_SOCKET = Path.home() / ".codex" / "ipc" / "ipc.sock"
DEFAULT_DESKTOP_STATE_PATH = Path.home() / ".codex" / ".codex-global-state.json"
DEVICE_SYMBOLS = set("、。！？：％，；“”‘’（）【】《》—…·「」『』〈〉￥")
MAX_DESKTOP_IPC_FRAME = 256 * 1024 * 1024


class CodexProtocolError(RuntimeError):
    pass


class DesktopCodexIpc:
    """Send a turn through the Codex Desktop task owner without taking its lock."""

    def __init__(
        self,
        socket_path: Path = DEFAULT_DESKTOP_IPC_SOCKET,
        *,
        timeout: float = 8.0,
    ) -> None:
        self.socket_path = socket_path.expanduser()
        self.timeout = timeout
        self.client_id = "uninitialized"
        self.socket: socket.socket | None = None

    def send_message(self, thread_id: str, text: str) -> None:
        connection = self._open_socket()
        self.socket = connection
        try:
            self._initialize()
            owner_id = self._discover_owner(thread_id)
            response = self._request(
                "thread-follower-start-turn",
                {
                    "conversationId": thread_id,
                    "turnStart": {
                        "request": {
                            "threadId": thread_id,
                            "input": [
                                {
                                    "type": "text",
                                    "text": text,
                                    "text_elements": [],
                                }
                            ],
                        },
                        "context": {
                            "inheritThreadSettings": True,
                            "threadStartKind": "default",
                        },
                    },
                },
                version=2,
                target_client_id=owner_id,
            )
            self._require_success(response, "电脑 Codex 没有接收这条指令")
        finally:
            connection.close()
            self.socket = None

    def find_owner(self, thread_id: str) -> str:
        """Read-only probe used by diagnostics and tests."""
        connection = self._open_socket()
        self.socket = connection
        try:
            self._initialize()
            return self._discover_owner(thread_id)
        finally:
            connection.close()
            self.socket = None

    def _open_socket(self) -> socket.socket:
        if not self.socket_path.exists():
            raise CodexProtocolError("电脑 Codex 尚未启动")
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            connection.settimeout(self.timeout)
            connection.connect(str(self.socket_path))
            return connection
        except OSError as error:
            connection.close()
            raise CodexProtocolError("无法连接电脑 Codex") from error

    def _initialize(self) -> None:
        response = self._request(
            "initialize",
            {"clientType": "foloos-coding-companion"},
            version=0,
        )
        result = self._require_success(response, "电脑 Codex IPC 初始化失败")
        client_id = str(result.get("clientId") or "")
        if not client_id:
            raise CodexProtocolError("电脑 Codex 没有返回 IPC 客户端编号")
        self.client_id = client_id

    def _discover_owner(self, thread_id: str) -> str:
        deadline = time.monotonic() + self.timeout
        while True:
            remaining_ms = max(100, int((deadline - time.monotonic()) * 1000))
            response = self._request(
                "thread-owner-discovery",
                {"hostId": "local", "conversationId": thread_id},
                version=1,
                timeout_ms=min(1000, remaining_ms),
            )
            if response.get("resultType") == "success":
                owner_id = str(response.get("handledByClientId") or "")
                if owner_id:
                    return owner_id
            elif response.get("error") != "no-client-found":
                self._require_success(response, "无法确认电脑 Codex 任务窗口")
            if time.monotonic() >= deadline:
                raise CodexProtocolError("电脑 Codex 尚未打开所选任务，请稍后重试")
            time.sleep(0.15)

    def _request(
        self,
        method: str,
        params: dict[str, Any],
        *,
        version: int,
        target_client_id: str | None = None,
        timeout_ms: int | None = None,
    ) -> dict[str, Any]:
        request_id = str(uuid.uuid4())
        message: dict[str, Any] = {
            "type": "request",
            "requestId": request_id,
            "sourceClientId": self.client_id,
            "version": version,
            "method": method,
            "params": params,
        }
        if target_client_id:
            message["targetClientId"] = target_client_id
        if timeout_ms is not None:
            message["timeoutMs"] = timeout_ms
        self._write_packet(message)

        deadline = time.monotonic() + self.timeout
        while True:
            response = self._read_packet(deadline)
            response_type = response.get("type")
            if response_type == "client-discovery-request":
                self._write_packet(
                    {
                        "type": "client-discovery-response",
                        "requestId": response.get("requestId"),
                        "response": {"canHandle": False},
                    }
                )
                continue
            if response_type == "response" and response.get("requestId") == request_id:
                return response

    def _write_packet(self, message: dict[str, Any]) -> None:
        connection = self.socket
        if connection is None:
            raise CodexProtocolError("电脑 Codex IPC 未连接")
        payload = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode(
            "utf-8"
        )
        if len(payload) > MAX_DESKTOP_IPC_FRAME:
            raise CodexProtocolError("发送给电脑 Codex 的指令过大")
        try:
            connection.sendall(struct.pack("<I", len(payload)) + payload)
        except OSError as error:
            raise CodexProtocolError("电脑 Codex IPC 已断开") from error

    def _read_packet(self, deadline: float) -> dict[str, Any]:
        header = self._read_exact(4, deadline)
        length = struct.unpack("<I", header)[0]
        if length > MAX_DESKTOP_IPC_FRAME:
            raise CodexProtocolError("电脑 Codex IPC 响应过大")
        payload = self._read_exact(length, deadline)
        try:
            message = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise CodexProtocolError("电脑 Codex IPC 响应格式错误") from error
        if not isinstance(message, dict):
            raise CodexProtocolError("电脑 Codex IPC 响应格式错误")
        return message

    def _read_exact(self, size: int, deadline: float) -> bytes:
        connection = self.socket
        if connection is None:
            raise CodexProtocolError("电脑 Codex IPC 未连接")
        data = bytearray()
        while len(data) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise CodexProtocolError("等待电脑 Codex IPC 响应超时")
            connection.settimeout(remaining)
            try:
                chunk = connection.recv(size - len(data))
            except (OSError, TimeoutError) as error:
                raise CodexProtocolError("等待电脑 Codex IPC 响应超时") from error
            if not chunk:
                raise CodexProtocolError("电脑 Codex IPC 已断开")
            data.extend(chunk)
        return bytes(data)

    @staticmethod
    def _require_success(response: dict[str, Any], fallback: str) -> dict[str, Any]:
        if response.get("resultType") != "success":
            detail = compact_text(response.get("error") or fallback, 160)
            raise CodexProtocolError(detail or fallback)
        result = response.get("result")
        return result if isinstance(result, dict) else {}


def _encode_websocket_frame(
    payload: bytes, *, opcode: int = 0x1, mask_key: bytes | None = None
) -> bytes:
    """Encode one final, masked client WebSocket frame."""
    key = mask_key or os.urandom(4)
    if len(key) != 4:
        raise ValueError("WebSocket mask key must be four bytes")
    length = len(payload)
    header = bytearray([0x80 | (opcode & 0x0F)])
    if length < 126:
        header.append(0x80 | length)
    elif length <= 0xFFFF:
        header.append(0x80 | 126)
        header.extend(struct.pack("!H", length))
    else:
        header.append(0x80 | 127)
        header.extend(struct.pack("!Q", length))
    header.extend(key)
    header.extend(byte ^ key[index % 4] for index, byte in enumerate(payload))
    return bytes(header)


def _take_websocket_frame(
    buffer: bytearray,
) -> tuple[int, bool, bytes] | None:
    """Remove and return one complete WebSocket frame from a receive buffer."""
    if len(buffer) < 2:
        return None
    first, second = buffer[0], buffer[1]
    fin = bool(first & 0x80)
    opcode = first & 0x0F
    masked = bool(second & 0x80)
    length = second & 0x7F
    offset = 2
    if length == 126:
        if len(buffer) < offset + 2:
            return None
        length = struct.unpack("!H", buffer[offset : offset + 2])[0]
        offset += 2
    elif length == 127:
        if len(buffer) < offset + 8:
            return None
        length = struct.unpack("!Q", buffer[offset : offset + 8])[0]
        offset += 8
    key = b""
    if masked:
        if len(buffer) < offset + 4:
            return None
        key = bytes(buffer[offset : offset + 4])
        offset += 4
    if len(buffer) < offset + length:
        return None
    payload = bytes(buffer[offset : offset + length])
    del buffer[: offset + length]
    if masked:
        payload = bytes(byte ^ key[index % 4] for index, byte in enumerate(payload))
    return opcode, fin, payload


def _device_safe_text(value: Any) -> str:
    """Drop glyphs the embedded Chinese pixel font cannot render."""
    output: list[str] = []
    for char in str(value or ""):
        codepoint = ord(char)
        if char.isspace():
            output.append(" ")
        elif 0x20 <= codepoint <= 0x7E or 0x4E00 <= codepoint <= 0x9FFF:
            output.append(char)
        elif char in DEVICE_SYMBOLS:
            output.append(char)
        else:
            ascii_fallback = (
                unicodedata.normalize("NFKD", char)
                .encode("ascii", "ignore")
                .decode("ascii")
            )
            output.append(ascii_fallback)
    return "".join(output)


def compact_text(value: Any, limit: int) -> str:
    text = re.sub(r"\s+", " ", _device_safe_text(value)).strip()
    text = text.replace("`", "").replace("**", "")
    if len(text) <= limit:
        return text
    return text[: max(1, limit - 1)].rstrip() + "…"


def compact_result(value: Any, limit: int) -> str:
    """Keep one complete conclusion on the small device screen."""
    lines = [line.strip(" -*\t") for line in str(value or "").splitlines()]
    first_line = next((line for line in lines if line), "")
    return compact_text(first_line, limit)


class CodexEventRouter:
    """Translate app-server messages into the device's compact event model."""

    APPROVAL_METHODS = {
        "item/commandExecution/requestApproval",
        "item/fileChange/requestApproval",
    }

    def __init__(self) -> None:
        self.active_turn_id: str | None = None
        self.pending_approval: dict[str, Any] | None = None
        self.final_text = ""
        self._rpc_outbox: list[dict[str, Any]] = []
        self._last_status = ""

    def _status(self, text: str) -> list[dict[str, Any]]:
        text = compact_text(text, DEVICE_STATUS_CHARS)
        if not text or text == self._last_status:
            return []
        self._last_status = text
        return [{"type": "task_status", "text": text}]

    def _queue_response(self, request_id: Any, result: dict[str, Any]) -> None:
        self._rpc_outbox.append({"id": request_id, "result": result})

    def _queue_error(self, request_id: Any, message: str) -> None:
        self._rpc_outbox.append(
            {"id": request_id, "error": {"code": -32601, "message": message}}
        )

    def take_rpc_outbox(self) -> list[dict[str, Any]]:
        messages = self._rpc_outbox
        self._rpc_outbox = []
        return messages

    def process(self, message: dict[str, Any]) -> list[dict[str, Any]]:
        method = str(message.get("method") or "")
        params = message.get("params")
        if not isinstance(params, dict):
            params = {}

        if method in self.APPROVAL_METHODS and "id" in message:
            return self._request_approval(message["id"], method, params)
        if "id" in message and method:
            # The device cannot safely answer arbitrary forms or MCP elicitations yet.
            self._queue_error(message["id"], "FoloOS device does not support this request type")
            return self._status("Codex 请求了暂不支持的确认，已安全拒绝")
        if method == "turn/started":
            turn = params.get("turn") or {}
            self.active_turn_id = str(turn.get("id") or self.active_turn_id or "") or None
            self.final_text = ""
            return self._status("Codex 正在分析任务")
        if method == "turn/plan/updated":
            plan = params.get("plan") or []
            active = next(
                (step for step in plan if step.get("status") == "inProgress"),
                plan[0] if plan else None,
            )
            return self._status(f"计划：{active.get('step', '')}" if active else "Codex 正在规划")
        if method == "item/started":
            return self._item_started(params.get("item") or {})
        if method == "item/completed":
            return self._item_completed(params.get("item") or {})
        if method == "item/agentMessage/delta":
            self.final_text += str(params.get("delta") or "")
            return []
        if method == "error":
            error = params.get("error") or {}
            text = compact_text(error.get("message") or "Codex 发生错误", DEVICE_STATUS_CHARS)
            if params.get("willRetry"):
                return self._status(f"遇到问题，正在重试：{text}")
            return [{"type": "error", "text": text}]
        if method == "turn/completed":
            return self._turn_completed(params.get("turn") or {})
        return []

    def _request_approval(
        self, request_id: Any, method: str, params: dict[str, Any]
    ) -> list[dict[str, Any]]:
        if self.pending_approval is not None:
            self._queue_response(request_id, {"decision": "decline"})
            return self._status("已有一个审批等待处理，新请求已拒绝")

        self.pending_approval = {
            "id": request_id,
            "method": method,
            "available": params.get("availableDecisions"),
        }
        reason = compact_text(params.get("reason"), 28)
        if method == "item/commandExecution/requestApproval":
            question = "允许 Codex 执行这条命令吗？"
            command = compact_text(params.get("command") or "未知命令", 58)
            cwd = Path(str(params.get("cwd") or "")).name
            pieces = [f"命令：{command}"]
            if cwd:
                pieces.append(f"目录：{cwd}")
            if reason:
                pieces.append(f"原因：{reason}")
        else:
            question = "允许 Codex 修改这些文件吗？"
            pieces = [f"原因：{reason or 'Codex 请求写入工作区'}"]

        return [
            {
                "type": "approval",
                "question": compact_text(question, DEVICE_APPROVAL_QUESTION_CHARS),
                "detail": compact_text("；".join(pieces), DEVICE_APPROVAL_DETAIL_CHARS),
            }
        ]

    def submit_approval(self, device_decision: str) -> list[dict[str, Any]]:
        pending = self.pending_approval
        if pending is None:
            return [{"type": "error", "text": "当前没有等待处理的 Codex 审批"}]

        decisions = {
            "approve_once": "accept",
            "approve_session": "acceptForSession",
            "deny": "decline",
            "cancel_task": "cancel",
        }
        decision = decisions.get(device_decision, "decline")
        available = pending.get("available")
        if isinstance(available, list) and decision not in available:
            decision = "accept" if decision == "acceptForSession" else "decline"
        self._queue_response(pending["id"], {"decision": decision})
        self.pending_approval = None
        if decision == "cancel":
            return self._status("审批已取消，正在停止任务")
        if decision == "decline":
            return self._status("已拒绝，Codex 将继续寻找安全方案")
        return self._status("审批已确认，Codex 继续执行")

    def cancel_pending_approval(self) -> None:
        if self.pending_approval is None:
            return
        self._queue_response(self.pending_approval["id"], {"decision": "cancel"})
        self.pending_approval = None

    def _item_started(self, item: dict[str, Any]) -> list[dict[str, Any]]:
        item_type = item.get("type")
        if item_type == "commandExecution":
            return self._status(f"准备执行：{compact_text(item.get('command'), 42)}")
        if item_type == "fileChange":
            return self._status("Codex 正在修改项目文件")
        if item_type == "mcpToolCall":
            return self._status(f"正在调用工具：{compact_text(item.get('tool'), 34)}")
        if item_type == "webSearch":
            return self._status("Codex 正在查找资料")
        if item_type == "reasoning":
            return self._status("Codex 正在分析任务")
        return []

    def _item_completed(self, item: dict[str, Any]) -> list[dict[str, Any]]:
        item_type = item.get("type")
        if item_type == "agentMessage":
            self.final_text = str(item.get("text") or self.final_text)
            return []
        if item_type == "commandExecution":
            if item.get("status") == "declined":
                return self._status("命令已拒绝，Codex 正在继续处理")
            if item.get("status") == "failed" or item.get("exitCode") not in (None, 0):
                return self._status("命令执行失败，Codex 正在处理")
            return self._status("命令执行完成")
        if item_type == "fileChange":
            if item.get("status") == "declined":
                return self._status("文件修改已拒绝，Codex 正在继续处理")
            if item.get("status") == "failed":
                return self._status("文件修改失败，Codex 正在处理")
            return self._status("文件修改完成，正在检查结果")
        return []

    def _turn_completed(self, turn: dict[str, Any]) -> list[dict[str, Any]]:
        status = turn.get("status")
        self.active_turn_id = None
        self.pending_approval = None
        if status == "interrupted":
            return [{"type": "done", "text": "真实 Codex 任务已取消"}]
        if status == "failed":
            error = turn.get("error") or {}
            return [
                {
                    "type": "error",
                    "text": compact_text(error.get("message") or "Codex 任务失败", DEVICE_STATUS_CHARS),
                }
            ]
        summary = compact_text(self.final_text, DEVICE_RESULT_CHARS)
        return [{"type": "done", "text": summary or "真实 Codex 任务已完成"}]


class CodexAppServer:
    def __init__(
        self,
        workspace: Path,
        executable: str | None = None,
        *,
        ephemeral: bool = False,
        timeout: float = 20.0,
        verbose: bool = False,
        thread_opener: Callable[[str], None] | None = None,
        desktop_sender: Callable[[str, str], None] | None = None,
        desktop_state_path: Path = DEFAULT_DESKTOP_STATE_PATH,
    ) -> None:
        self.workspace = workspace.expanduser().resolve()
        self.executable = executable or shutil.which("codex") or str(
            Path.home() / ".local" / "bin" / "codex"
        )
        self.ephemeral = ephemeral
        self.timeout = timeout
        self.verbose = verbose
        self.thread_opener = thread_opener
        self.desktop_sender = desktop_sender or self._send_to_desktop
        self.desktop_state_path = desktop_state_path
        self.socket_path = DEFAULT_APP_SERVER_SOCKET
        self.socket: socket.socket | None = None
        self.thread_id: str | None = None
        self.thread_title = ""
        self.selected_project: dict[str, Any] | None = None
        self.projects: dict[str, dict[str, Any]] = {}
        self.project_catalog: list[dict[str, Any]] = []
        self.project_page = 0
        self.desktop_thread_assignments: dict[str, Any] = {}
        self.threads: dict[str, dict[str, Any]] = {}
        self.router = CodexEventRouter()
        self._request_id = 0
        self._responses: dict[Any, dict[str, Any]] = {}
        self._device_outbox: deque[dict[str, Any]] = deque()
        self._rx_buffer = bytearray()
        self._fragment_opcode: int | None = None
        self._fragment_buffer = bytearray()
        self._observed_turn_id: str | None = None
        self._observed_turn_status = ""
        self._observed_thread_flags: tuple[str, ...] = ()
        self._next_thread_poll = 0.0
        self.queued_submission_id: str | None = None
        self._next_queue_poll = 0.0
        self.rollout_path: Path | None = None
        self._rollout_offset = 0
        self._rollout_turn_id: str | None = None
        self._rollout_final_text = ""
        self._rollout_pending_call: str | None = None
        self._rollout_approval_candidates: dict[str, dict[str, Any]] = {}

    def start(self) -> None:
        if not self.workspace.is_dir():
            raise CodexProtocolError(f"Codex 工作目录不存在：{self.workspace}")
        if not Path(self.executable).exists():
            raise CodexProtocolError("没有找到 Codex CLI，请先安装或使用 --codex-bin 指定")

        self._connect_shared_app_server()
        self._request(
            "initialize",
            {
                "clientInfo": {
                    "name": "folos-coding-companion",
                    "title": "FoloOS 编程伴侣",
                    "version": "1.0",
                },
                "capabilities": {"experimentalApi": True},
            },
        )
        self._write({"method": "initialized", "params": {}})

    def _connect_shared_app_server(self) -> None:
        """Join the same local App Server daemon used by Codex Desktop."""
        result = subprocess.run(
            [self.executable, "app-server", "daemon", "start"],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        deadline = time.monotonic() + 5.0
        while not self.socket_path.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        if not self.socket_path.exists():
            detail = result.stderr.strip() or result.stdout.strip()
            raise CodexProtocolError(
                compact_text(detail or "Codex 共享服务没有启动", 160)
            )

        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            connection.settimeout(self.timeout)
            connection.connect(str(self.socket_path))
            key = base64.b64encode(os.urandom(16)).decode("ascii")
            request = (
                "GET / HTTP/1.1\r\n"
                "Host: localhost\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                f"Sec-WebSocket-Key: {key}\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n"
            ).encode("ascii")
            connection.sendall(request)
            response = bytearray()
            while b"\r\n\r\n" not in response:
                chunk = connection.recv(4096)
                if not chunk:
                    raise CodexProtocolError("Codex 共享服务拒绝了连接")
                response.extend(chunk)
                if len(response) > 65536:
                    raise CodexProtocolError("Codex 共享服务握手响应异常")
            header, _, remainder = response.partition(b"\r\n\r\n")
            lines = header.decode("latin-1").split("\r\n")
            if not lines or " 101 " not in f" {lines[0]} ":
                raise CodexProtocolError("Codex 共享服务没有接受 WebSocket 连接")
            headers = {}
            for line in lines[1:]:
                name, separator, value = line.partition(":")
                if separator:
                    headers[name.strip().lower()] = value.strip()
            expected = base64.b64encode(
                hashlib.sha1(
                    (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")
                ).digest()
            ).decode("ascii")
            if headers.get("sec-websocket-accept") != expected:
                raise CodexProtocolError("Codex 共享服务握手校验失败")
            connection.settimeout(None)
            self.socket = connection
            self._rx_buffer = bytearray(remainder)
        except Exception:
            connection.close()
            raise

    def _thread_options(self) -> dict[str, Any]:
        return {
            "sandbox": "workspace-write",
            "approvalPolicy": "on-request",
            "approvalsReviewer": "user",
            "developerInstructions": (
                "你正在通过 FoloOS 编程伴侣接收用户已在硬件上确认的语音指令。"
                "使用简体中文，过程状态和最终结论保持简洁。严格遵守 workspace-write "
                "沙箱；需要超出权限或执行有风险的操作时必须请求用户审批，不能绕过审批。"
            ),
        }

    def _create_thread(self) -> list[dict[str, Any]]:
        # thread/start would make the bridge daemon the task writer and lock
        # Codex Desktop out. New tasks therefore stay a Desktop operation in
        # synchronized mode; the device can select any existing Desktop task.
        return [
            {
                "type": "error",
                "text": "请先在电脑 Codex 新建任务，再回设备选择",
            }
        ]

    def _selected_message(self) -> dict[str, Any]:
        project_name = str((self.selected_project or {}).get("name") or "未分组项目")
        return {
            "type": "codex_selected",
            "title": compact_text(self.thread_title or "未命名任务", DEVICE_CATALOG_TITLE_CHARS),
            "project": compact_text(project_name, DEVICE_CATALOG_DETAIL_CHARS),
        }

    @staticmethod
    def _catalog_item(
        scope: str, key: str, title: Any, detail: Any
    ) -> dict[str, Any]:
        return {
            "type": "codex_catalog_item",
            "scope": scope,
            "id": key,
            "title": compact_text(title, DEVICE_CATALOG_TITLE_CHARS),
            "detail": compact_text(detail, DEVICE_CATALOG_DETAIL_CHARS),
        }

    def _load_desktop_projects(self) -> list[dict[str, Any]]:
        """Read the same local project names and order shown by Codex Desktop."""
        try:
            state = json.loads(self.desktop_state_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError):
            return []
        local_projects = state.get("local-projects")
        if not isinstance(local_projects, dict):
            return []
        order = state.get("project-order")
        if not isinstance(order, list) or not order:
            order = sorted(
                local_projects,
                key=lambda project_id: int(
                    (local_projects.get(project_id) or {}).get("updatedAt") or 0
                ),
                reverse=True,
            )
        assignments = state.get("thread-project-assignments")
        self.desktop_thread_assignments = (
            assignments if isinstance(assignments, dict) else {}
        )
        projects: list[dict[str, Any]] = []
        for project_id in order:
            raw = local_projects.get(str(project_id))
            if not isinstance(raw, dict):
                continue
            roots = [
                str(path)
                for path in (raw.get("rootPaths") or [])
                if isinstance(path, str) and path
            ]
            if not roots:
                continue
            projects.append(
                {
                    "project_id": None,
                    "desktop_project_id": str(project_id),
                    "cwd": roots[0],
                    "roots": roots,
                    "name": str(raw.get("name") or Path(roots[0]).name or "未命名项目"),
                }
            )
        return projects

    def _render_project_page(self, page: int) -> list[dict[str, Any]]:
        projects = self.project_catalog
        page_count = max(1, (len(projects) + CATALOG_PAGE_SIZE - 1) // CATALOG_PAGE_SIZE)
        self.project_page = page % page_count
        start = self.project_page * CATALOG_PAGE_SIZE
        visible = projects[start : start + CATALOG_PAGE_SIZE]
        self.projects = {}
        messages: list[dict[str, Any]] = [
            {"type": "codex_catalog_begin", "scope": "projects"}
        ]
        for index, project in enumerate(visible):
            key = f"p{index}"
            self.projects[key] = project
            messages.append(
                self._catalog_item(
                    "projects",
                    key,
                    project["name"],
                    f"项目 {start + index + 1}/{len(projects)}",
                )
            )
        if page_count > 1:
            next_page = (self.project_page + 1) % page_count
            key = "p_more"
            self.projects[key] = {"page": next_page}
            title = "回到第1页" if next_page == 0 else f"下一页 {next_page + 1}/{page_count}"
            messages.append(
                self._catalog_item("projects", key, title, f"共 {len(projects)} 个项目")
            )
        messages.append({"type": "codex_catalog_end", "scope": "projects"})
        return messages

    def _list_projects(self) -> list[dict[str, Any]]:
        desktop_projects = self._load_desktop_projects()
        if desktop_projects:
            self.project_catalog = desktop_projects
            return self._render_project_page(0)

        result = self._request("project/list", {"limit": 100})
        projects = result.get("data") or []
        self.project_catalog = []
        for project in projects:
            roots = project.get("roots") or []
            cwd = str((roots[0] if roots else {}).get("path") or self.workspace)
            self.project_catalog.append({
                "project_id": str(project.get("id") or "") or None,
                "cwd": cwd,
                "roots": [cwd],
                "name": str(project.get("name") or Path(cwd).name or "未命名项目"),
            })

        if not self.project_catalog:
            threads = self._request(
                "thread/list",
                {"limit": 100, "sortKey": "updated_at", "sortDirection": "desc"},
            ).get("data") or []
            seen: set[str] = set()
            for thread in threads:
                cwd = str(thread.get("cwd") or "")
                if not cwd or cwd in seen:
                    continue
                seen.add(cwd)
                self.project_catalog.append({
                    "project_id": None,
                    "cwd": cwd,
                    "roots": [cwd],
                    "name": Path(cwd).name or "未命名项目",
                })
        if not self.project_catalog:
            self.project_catalog.append({
                "project_id": None,
                "cwd": str(self.workspace),
                "roots": [str(self.workspace)],
                "name": self.workspace.name,
            })
        return self._render_project_page(0)

    @staticmethod
    def _path_is_within(path: str, roots: list[str]) -> bool:
        try:
            candidate = Path(path).expanduser().resolve()
            return any(candidate.is_relative_to(Path(root).expanduser().resolve()) for root in roots)
        except (OSError, RuntimeError, ValueError):
            return False

    def _desktop_project_threads(self, project: dict[str, Any]) -> list[dict[str, Any]]:
        project_id = str(project.get("desktop_project_id") or "")
        roots = [str(root) for root in project.get("roots") or []]
        matches: list[dict[str, Any]] = []
        cursor: Any = None
        for _ in range(10):
            params: dict[str, Any] = {
                "limit": 100,
                "sortKey": "updated_at",
                "sortDirection": "desc",
            }
            if cursor:
                params["cursor"] = cursor
            result = self._request("thread/list", params)
            for thread in result.get("data") or []:
                thread_id = str(thread.get("id") or "")
                assignment = self.desktop_thread_assignments.get(thread_id)
                assigned_project = (
                    str(assignment.get("projectId") or "")
                    if isinstance(assignment, dict)
                    else ""
                )
                belongs = (
                    assigned_project == project_id
                    if assigned_project
                    else self._path_is_within(str(thread.get("cwd") or ""), roots)
                )
                if belongs:
                    matches.append(thread)
                    if len(matches) >= CATALOG_LIMIT:
                        return matches
            cursor = result.get("nextCursor")
            if not cursor:
                break
        return matches

    def _list_threads(self, project_key: str) -> list[dict[str, Any]]:
        project = self.projects.get(project_key)
        if project is None:
            return [{"type": "error", "text": "选择的 Codex 项目已经失效"}]
        if "page" in project:
            return self._render_project_page(int(project["page"]))
        self.selected_project = project
        params: dict[str, Any] = {
            "limit": CATALOG_LIMIT,
            "sortKey": "updated_at",
            "sortDirection": "desc",
        }
        if project.get("desktop_project_id"):
            thread_data = self._desktop_project_threads(project)
        elif project.get("project_id"):
            params["projectId"] = project["project_id"]
            thread_data = self._request("thread/list", params).get("data") or []
        else:
            params["cwd"] = project["cwd"]
            thread_data = self._request("thread/list", params).get("data") or []
        self.threads = {}
        messages: list[dict[str, Any]] = [
            {"type": "codex_catalog_begin", "scope": "threads"}
        ]
        for index, thread in enumerate(thread_data[:CATALOG_LIMIT]):
            key = f"t{index}"
            title = str(thread.get("name") or thread.get("preview") or "未命名任务")
            self.threads[key] = {
                "thread_id": str(thread.get("id") or ""),
                "title": title,
                "path": str(thread.get("path") or ""),
            }
            status = thread.get("status")
            if isinstance(status, dict):
                status = status.get("type") or ""
            detail = "进行中" if status in ("active", "running") else "最近任务"
            messages.append(self._catalog_item("threads", key, title, detail))
        messages.append({"type": "codex_catalog_end", "scope": "threads"})
        return messages

    def _resume_thread(self, thread_key: str) -> list[dict[str, Any]]:
        thread = self.threads.get(thread_key)
        if thread is None or not thread.get("thread_id"):
            return [{"type": "error", "text": "选择的 Codex 任务已经失效"}]
        # Even thread/read loads the task into this separate app-server daemon.
        # That is enough to make Desktop report "running elsewhere". The
        # catalog already contains the id/title we need, so selection must be
        # entirely local; commands are forwarded through the Desktop-owned IPC.
        self.thread_id = str(thread["thread_id"])
        self.thread_title = str(thread["title"])
        self.router = CodexEventRouter()
        self.queued_submission_id = None
        self._observed_turn_id = None
        self._observed_turn_status = ""
        self._observed_thread_flags = ()
        self._set_rollout_baseline(thread.get("path"))
        if self.thread_opener is not None:
            try:
                self.thread_opener(self.thread_id)
            except OSError:
                pass
        return [self._selected_message()]

    def _set_thread_baseline(self, thread: dict[str, Any]) -> None:
        turns = thread.get("turns") or []
        last = turns[-1] if turns else {}
        self._observed_turn_id = str(last.get("id") or "") or None
        self._observed_turn_status = str(last.get("status") or "")
        status = thread.get("status") or {}
        flags = status.get("activeFlags") if isinstance(status, dict) else []
        self._observed_thread_flags = tuple(sorted(str(flag) for flag in (flags or [])))
        if self._observed_turn_id and self._observed_turn_status in (
            "inProgress",
            "running",
        ):
            self.router.active_turn_id = self._observed_turn_id
        else:
            self.router.active_turn_id = None
        self._next_thread_poll = time.monotonic() + 0.75

    def close(self) -> None:
        connection = self.socket
        if connection is None:
            return
        self.router.cancel_pending_approval()
        self._flush_router_rpc()
        try:
            connection.sendall(_encode_websocket_frame(b"", opcode=0x8))
        except OSError:
            pass
        connection.close()
        self.socket = None

    def handle_device_event(self, event: dict[str, Any]) -> list[dict[str, Any]]:
        event_type = event.get("type")
        if event_type == "codex_catalog_request":
            return self._list_projects()
        if event_type == "codex_project_select":
            return self._list_threads(str(event.get("id") or ""))
        if event_type == "codex_thread_select":
            return self._resume_thread(str(event.get("id") or ""))
        if event_type == "codex_thread_new":
            return self._create_thread()
        if event_type == "voice_command":
            command = str(event.get("text") or "").strip()
            if not command:
                return [{"type": "error", "text": "语音指令为空"}]
            if self.thread_id is None:
                return [{"type": "error", "text": "请先选择或新建一个 Codex 任务"}]
            if self.queued_submission_id:
                return [{"type": "error", "text": "上一条指令仍在电脑端等待处理"}]
            self._start_command(command)
            return [
                {
                    "type": "task_status",
                    "text": "电脑端已接收，Codex 正在处理",
                }
            ]
        if event_type == "approval_decision":
            if self.router.pending_approval is not None:
                messages = self.router.submit_approval(
                    str(event.get("decision") or "deny")
                )
                self._flush_router_rpc()
                return messages
            return [{"type": "error", "text": "本次审批请在电脑端 Codex 完成"}]
        if event_type == "cancel_task":
            if self.thread_id and self.queued_submission_id:
                self._request(
                    "thread/queue/delete",
                    {
                        "threadId": self.thread_id,
                        "queuedSubmissionId": self.queued_submission_id,
                    },
                )
                self.queued_submission_id = None
                return [{"type": "task_status", "text": "已取消等待中的指令"}]
            return [{"type": "error", "text": "任务已由电脑端接管，请在 Codex 停止"}]
        return []

    def poll(self) -> list[dict[str, Any]]:
        if self.socket is None:
            return []
        while self._read_one(0.0):
            pass
        now = time.monotonic()
        if self.queued_submission_id and now >= self._next_queue_poll:
            self._next_queue_poll = now + 1.0
            self._poll_queued_submission()
        self._poll_rollout(now)
        messages = list(self._device_outbox)
        self._device_outbox.clear()
        return messages

    def _start_command(self, command: str) -> None:
        if self.thread_id is None:
            raise CodexProtocolError("Codex 任务尚未初始化")
        thread_id = self.thread_id
        if self.thread_opener is not None:
            try:
                self.thread_opener(thread_id)
            except OSError:
                pass
        self.desktop_sender(thread_id, command)

    def _send_to_desktop(self, thread_id: str, command: str) -> None:
        DesktopCodexIpc(timeout=min(self.timeout, 8.0)).send_message(thread_id, command)

    def _poll_queued_submission(self) -> None:
        if self.thread_id is None or self.queued_submission_id is None:
            return
        response = self._request(
            "thread/queue/list", {"threadId": self.thread_id, "limit": 100}
        )
        queued_ids = {
            str(item.get("id") or "") for item in (response.get("data") or [])
        }
        if self.queued_submission_id not in queued_ids:
            self.queued_submission_id = None
            self._device_outbox.append(
                {
                    "type": "task_status",
                    "text": "电脑端已接收，进度和审批请看 Codex",
                }
            )

    def _set_rollout_baseline(self, path: Any) -> None:
        rollout = Path(str(path)).expanduser() if path else None
        self.rollout_path = rollout if rollout and rollout.is_file() else None
        try:
            self._rollout_offset = self.rollout_path.stat().st_size if self.rollout_path else 0
        except OSError:
            self._rollout_offset = 0
        self._rollout_turn_id = None
        self._rollout_final_text = ""
        self._rollout_pending_call = None
        self._rollout_approval_candidates.clear()

    def _poll_rollout(self, now: float) -> None:
        path = self.rollout_path
        if path is None:
            return
        try:
            size = path.stat().st_size
            if size < self._rollout_offset:
                self._rollout_offset = 0
            with path.open("rb") as file:
                file.seek(self._rollout_offset)
                data = file.read()
        except OSError:
            return

        consumed = 0
        for line in data.splitlines(keepends=True):
            if not line.endswith(b"\n"):
                break
            consumed += len(line)
            try:
                record = json.loads(line.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            self._process_rollout_record(record, now)
        self._rollout_offset += consumed

        for call_id, approval in list(self._rollout_approval_candidates.items()):
            if now - float(approval["seen_at"]) < 1.0:
                continue
            self._rollout_pending_call = call_id
            self._rollout_approval_candidates.pop(call_id, None)
            self._device_outbox.append(
                {
                    "type": "approval",
                    "question": approval["question"],
                    "detail": approval["detail"],
                }
            )

    def _process_rollout_record(self, record: dict[str, Any], now: float) -> None:
        record_type = str(record.get("type") or "")
        payload = record.get("payload")
        if not isinstance(payload, dict):
            return

        if record_type == "response_item":
            item_type = str(payload.get("type") or "")
            call_id = str(payload.get("call_id") or "")
            if item_type == "custom_tool_call":
                approval = self._rollout_approval(payload)
                if call_id and approval:
                    approval["seen_at"] = now
                    self._rollout_approval_candidates[call_id] = approval
            elif item_type == "custom_tool_call_output" and call_id:
                self._rollout_approval_candidates.pop(call_id, None)
                if self._rollout_pending_call == call_id:
                    self._rollout_pending_call = None
                    self._device_outbox.append(
                        {"type": "task_status", "text": "电脑端审批已处理，Codex 继续执行"}
                    )
            return

        if record_type != "event_msg":
            return
        event_type = str(payload.get("type") or "")
        if event_type == "task_started":
            self._rollout_turn_id = str(payload.get("turn_id") or "") or None
            self._rollout_final_text = ""
            self._device_outbox.append(
                {"type": "task_status", "text": "电脑端已接收，Codex 正在处理"}
            )
        elif event_type == "agent_message":
            message = str(payload.get("message") or "")
            if payload.get("phase") == "final_answer":
                self._rollout_final_text = message
            elif message:
                self._device_outbox.append(
                    {"type": "task_status", "text": compact_result(message, DEVICE_STATUS_CHARS)}
                )
        elif event_type == "task_complete":
            message = str(payload.get("last_agent_message") or self._rollout_final_text)
            self._device_outbox.append(
                {
                    "type": "done",
                    "text": compact_text(message, DEVICE_RESULT_CHARS)
                    or "真实 Codex 任务已完成",
                }
            )
            self._rollout_turn_id = None
            self._rollout_final_text = ""
            self._rollout_pending_call = None
            self._rollout_approval_candidates.clear()

    def _rollout_approval(self, payload: dict[str, Any]) -> dict[str, str] | None:
        if payload.get("name") != "exec":
            return None
        source = str(payload.get("input") or "")
        if 'sandbox_permissions:"require_escalated"' in source:
            return {
                "question": "电脑端 Codex 请求执行受限操作",
                "detail": "请在电脑端查看完整命令并审批",
            }
        paths = re.findall(r"\*\*\* (?:Add|Update|Delete) File: ([^\\n\n]+)", source)
        if not paths:
            return None
        roots = [str(root) for root in (self.selected_project or {}).get("roots") or []]
        if roots and all(self._path_is_within(path, roots) for path in paths):
            return None
        return {
            "question": "电脑端 Codex 请求修改项目外文件",
            "detail": "请在电脑端查看文件并审批",
        }

    def _request(self, method: str, params: dict[str, Any]) -> dict[str, Any]:
        self._request_id += 1
        request_id = self._request_id
        self._write({"id": request_id, "method": method, "params": params})
        deadline = time.monotonic() + self.timeout
        while request_id not in self._responses:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._read_one(remaining):
                raise CodexProtocolError(f"等待 Codex 响应超时：{method}")
        response = self._responses.pop(request_id)
        if "error" in response:
            error = response.get("error") or {}
            raise CodexProtocolError(
                compact_text(error.get("message") or f"Codex 请求失败：{method}", 160)
            )
        result = response.get("result")
        return result if isinstance(result, dict) else {}

    def _write(self, message: dict[str, Any]) -> None:
        connection = self.socket
        if connection is None:
            raise CodexProtocolError("Codex 服务未运行")
        if self.verbose:
            print(f"桥接 → Codex  {message}")
        wire = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode(
            "utf-8"
        )
        try:
            connection.sendall(_encode_websocket_frame(wire))
        except OSError as error:
            self.socket = None
            raise CodexProtocolError("Codex 共享连接已断开") from error

    def _read_one(self, timeout: float) -> bool:
        connection = self.socket
        if connection is None:
            return False
        deadline = time.monotonic() + max(0.0, timeout)
        while True:
            frame = _take_websocket_frame(self._rx_buffer)
            if frame is not None:
                opcode, fin, payload = frame
                if opcode == 0x8:
                    connection.close()
                    self.socket = None
                    raise CodexProtocolError("Codex 共享连接已关闭")
                if opcode == 0x9:
                    try:
                        connection.sendall(
                            _encode_websocket_frame(payload, opcode=0xA)
                        )
                    except OSError as error:
                        self.socket = None
                        raise CodexProtocolError("Codex 共享连接已断开") from error
                    return True
                if opcode == 0xA:
                    return True
                if opcode == 0x1:
                    if fin:
                        return self._process_wire_message(payload)
                    self._fragment_opcode = opcode
                    self._fragment_buffer = bytearray(payload)
                    return True
                if opcode == 0x0 and self._fragment_opcode == 0x1:
                    self._fragment_buffer.extend(payload)
                    if fin:
                        complete = bytes(self._fragment_buffer)
                        self._fragment_opcode = None
                        self._fragment_buffer.clear()
                        return self._process_wire_message(complete)
                    return True
                return True

            remaining = deadline - time.monotonic()
            if timeout <= 0 or remaining <= 0:
                wait = 0.0
            else:
                wait = remaining
            readable, _, _ = select.select([connection], [], [], wait)
            if not readable:
                return False
            try:
                chunk = connection.recv(65536)
            except (BlockingIOError, InterruptedError):
                return False
            if not chunk:
                connection.close()
                self.socket = None
                raise CodexProtocolError("Codex 共享连接已断开")
            self._rx_buffer.extend(chunk)

    def _process_wire_message(self, payload: bytes) -> bool:
        try:
            message = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return True
        if self.verbose:
            print(f"Codex → 桥接  {message}")
        if "id" in message and not message.get("method"):
            self._responses[message["id"]] = message
            return True
        params = message.get("params")
        if isinstance(params, dict) and self.thread_id is not None:
            message_thread_id = str(params.get("threadId") or "")
            if message_thread_id and message_thread_id != self.thread_id:
                return True
        for device_message in self.router.process(message):
            self._device_outbox.append(device_message)
        self._flush_router_rpc()
        return True

    def _flush_router_rpc(self) -> None:
        for message in self.router.take_rpc_outbox():
            self._write(message)
