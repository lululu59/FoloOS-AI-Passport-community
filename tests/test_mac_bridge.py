import base64
import json
import os
from pathlib import Path
import pty
import select
import socket
import subprocess
import tempfile
import threading
import time
import unittest
import wave

from tools.mac_bridge import (
    AudioCapture,
    MockScenario,
    NetworkBridge,
    SerialBridge,
    WordBearStore,
    encode_message,
    load_word_bear_restore,
    load_wireless_config,
    parse_args,
    save_wireless_config,
    validate_ota_image,
    wire_messages,
)
from tools.install_autostart import (
    LABEL,
    codex_app_server_works,
    find_codex,
    launch_agent_config,
)


class MockScenarioTests(unittest.TestCase):
    def setUp(self):
        self.scenario = MockScenario("检查当前项目并报告错误")

    def test_record_stop_returns_transcript_for_review(self):
        self.assertEqual(
            self.scenario.handle({"type": "record_stop"}),
            [
                (
                    0.25,
                    {
                        "type": "transcript",
                        "text": "检查当前项目并报告错误",
                    },
                )
            ],
        )

    def test_confirmed_voice_reports_progress_then_approval(self):
        responses = self.scenario.handle(
            {"type": "voice_command", "text": "检查当前项目并报告错误"}
        )
        self.assertEqual(
            [message["type"] for _, message in responses],
            ["task_status", "task_status", "approval"],
        )
        self.assertIn("构建命令", responses[-1][1]["question"])

    def test_approval_decision_finishes_mock_task(self):
        responses = self.scenario.handle(
            {"type": "approval_decision", "decision": "approve_once"}
        )
        self.assertEqual(
            [message["type"] for _, message in responses],
            ["task_status", "done"],
        )

    def test_json_wire_format_preserves_chinese_and_is_one_line(self):
        wire = encode_message({"type": "transcript", "text": "第一行\n第二行"})
        self.assertTrue(wire.endswith(b"\n"))
        self.assertEqual(wire.count(b"\n"), 1)
        self.assertIn("第一行".encode(), wire)

    def test_long_chinese_text_is_chunked_without_loss(self):
        text = "请检查当前项目并保留完整的语音转写内容。" * 40
        records = wire_messages({"type": "transcript", "text": text})
        messages = [json.loads(record.decode("utf-8")) for record in records]

        self.assertEqual(messages[0], {"type": "text_begin", "target": "transcript"})
        self.assertEqual(messages[-1], {"type": "text_end", "target": "transcript"})
        self.assertTrue(all(len(record) <= 768 for record in records))
        self.assertEqual(
            "".join(message["text"] for message in messages[1:-1]),
            text,
        )


class AudioCaptureTests(unittest.TestCase):
    def test_pcm_chunks_are_written_as_16k_mono_wav(self):
        pcm = b"\x10\x00" * 4000
        with tempfile.TemporaryDirectory() as temp_dir:
            capture = AudioCapture(Path(temp_dir))
            capture.start(
                {"sample_rate": 16000, "bits": 16, "channels": 1}
            )
            capture.add_chunk(
                {"data": base64.b64encode(pcm).decode("ascii")}
            )
            wav_path = capture.finish()

            with wave.open(str(wav_path), "rb") as wav_file:
                self.assertEqual(wav_file.getframerate(), 16000)
                self.assertEqual(wav_file.getnchannels(), 1)
                self.assertEqual(wav_file.getsampwidth(), 2)
                self.assertEqual(wav_file.readframes(4000), pcm)

    def test_too_short_recording_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            capture = AudioCapture(Path(temp_dir))
            capture.start({})
            capture.add_chunk({"data": base64.b64encode(b"\0" * 100).decode()})
            with self.assertRaisesRegex(ValueError, "太短"):
                capture.finish()

    def test_cancel_discards_an_interrupted_recording(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            capture = AudioCapture(Path(temp_dir))
            capture.start({})
            capture.add_chunk({"data": base64.b64encode(b"\0" * 8000).decode()})
            capture.cancel()
            self.assertFalse(capture.active)
            self.assertEqual(capture.duration_seconds, 0.0)


class SerialBridgeIntegrationTests(unittest.TestCase):
    def test_completed_answer_is_displayed_then_streamed_as_speech(self):
        class FakeTTS:
            sample_rate = 16000

            def __init__(self):
                self.spoken = ""

            def iter_pcm(self, text, chunk_bytes):
                self.spoken = text
                pcm = b"\x10\x00" * 500
                for offset in range(0, len(pcm), chunk_bytes):
                    yield pcm[offset : offset + chunk_bytes]

        tts = FakeTTS()
        bridge = SerialBridge("unused", tts=tts)
        sent = []
        bridge.send = sent.append

        bridge.deliver({"type": "done", "text": "任务已完成，这是完整回答。"})

        self.assertEqual(tts.spoken, "任务已完成，这是完整回答。")
        self.assertEqual(sent[0]["type"], "done")
        self.assertEqual(sent[1], {"type": "speech_start", "sample_rate": 16000})
        self.assertEqual(sent[-1], {"type": "speech_end"})
        pcm = b"".join(
            base64.b64decode(message["data"])
            for message in sent[2:-1]
        )
        self.assertEqual(pcm, b"\x10\x00" * 500)

    def test_word_audio_is_generated_and_streamed_on_demand(self):
        class FakeTTS:
            sample_rate = 16000

            def __init__(self):
                self.word = ""

            def iter_word_pcm(self, word, chunk_bytes):
                self.word = word
                yield b"\x20\x00" * 120

        tts = FakeTTS()
        bridge = SerialBridge("unused", tts=tts)
        sent = []
        bridge.send = sent.append
        bridge.handle_line(
            encode_message(
                {"type": "word_bear_audio_request", "id": 42, "word": "family"}
            ).strip()
        )

        self.assertEqual(tts.word, "family")
        self.assertEqual(sent[0], {"type": "speech_start", "sample_rate": 16000})
        self.assertEqual(sent[-1], {"type": "speech_end"})

    def test_heartbeat_continues_while_main_loop_is_busy(self):
        master_fd, slave_fd = pty.openpty()
        port = os.ttyname(slave_fd)
        os.close(slave_fd)
        bridge = SerialBridge(port, heartbeat_seconds=0.02)
        bridge.open()
        bridge.start_heartbeat()
        try:
            received = bytearray()
            deadline = time.monotonic() + 0.3
            while time.monotonic() < deadline:
                readable, _, _ = select.select([master_fd], [], [], 0.05)
                if readable:
                    received.extend(os.read(master_fd, 4096))
            messages = [
                json.loads(line.decode("utf-8"))
                for line in received.splitlines()
                if line
            ]
            self.assertGreaterEqual(
                sum(message.get("type") == "bridge_ready" for message in messages),
                3,
            )
        finally:
            bridge.stop_heartbeat()
            bridge.close()
            os.close(master_fd)

    def test_connection_requests_word_bear_day_and_full_sync(self):
        bridge = SerialBridge("unused", heartbeat_seconds=60)
        sent = []
        bridge.send = sent.append
        bridge.start_heartbeat()
        bridge.stop_heartbeat()

        self.assertEqual(sent[0], {"type": "bridge_ready"})
        self.assertEqual(sent[1]["type"], "word_bear_day")
        self.assertGreater(sent[1]["day"], 700000)
        self.assertEqual(sent[2], {"type": "word_bear_sync_request"})

    def test_codex_poll_does_not_block_active_audio_capture(self):
        class FakeCodex:
            def __init__(self):
                self.poll_count = 0

            def poll(self):
                self.poll_count += 1
                return []

        codex = FakeCodex()
        bridge = SerialBridge("unused", codex=codex)
        bridge.capture.start({})
        bridge.poll_codex()
        self.assertEqual(codex.poll_count, 0)

        bridge.capture.cancel()
        bridge.poll_codex()
        self.assertEqual(codex.poll_count, 1)

    def test_orphan_audio_chunk_recovers_after_wifi_reconnect(self):
        bridge = SerialBridge("unused")
        bridge.handle_line(
            encode_message(
                {
                    "type": "audio_chunk",
                    "data": base64.b64encode(b"\0" * 640).decode("ascii"),
                }
            ).strip()
        )
        self.assertTrue(bridge.capture.active)
        self.assertEqual(len(bridge.capture.pcm), 640)

    def test_pty_round_trip_from_device_event_to_mac_response(self):
        master_fd, slave_fd = pty.openpty()
        port = os.ttyname(slave_fd)
        os.close(slave_fd)
        now = [100.0]
        bridge = SerialBridge(
            port,
            MockScenario("检查当前项目并报告错误"),
            clock=lambda: now[0],
        )
        bridge.open()
        try:
            os.write(master_fd, encode_message({"type": "record_stop"}))
            for _ in range(20):
                bridge.read_available()
                if bridge.pending:
                    break
                time.sleep(0.01)
            self.assertTrue(bridge.pending)

            now[0] = 101.0
            bridge.flush_pending()
            readable, _, _ = select.select([master_fd], [], [], 1.0)
            self.assertTrue(readable)
            response = json.loads(os.read(master_fd, 4096).decode("utf-8"))
            self.assertEqual(response["type"], "transcript")
            self.assertIn("检查当前项目", response["text"])
        finally:
            bridge.close()
            os.close(master_fd)


class WirelessBridgeTests(unittest.TestCase):
    def test_ota_image_rejects_merged_firmware_and_accepts_app_image(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            app = Path(temp_dir) / "app.bin"
            app.write_bytes(b"\xe9" + b"\0" * 31)
            self.assertEqual(validate_ota_image(app), app.read_bytes())
            merged = Path(temp_dir) / "merged.bin"
            merged.write_bytes(b"\0" * 32)
            with self.assertRaisesRegex(RuntimeError, "应用固件"):
                validate_ota_image(merged)

    def test_config_round_trip_does_not_store_wifi_password(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "bridge.json"
            save_wireless_config("192.168.1.23", "a" * 32, path=path)
            self.assertEqual(
                load_wireless_config(path),
                ("192.168.1.23", 8765, "a" * 32),
            )
            self.assertNotIn("password", path.read_text(encoding="utf-8"))

    def test_network_bridge_authenticates_with_saved_token(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        host, port = server.getsockname()
        received = []

        def accept_once():
            client, _ = server.accept()
            with client:
                received.append(json.loads(client.recv(1024).decode("utf-8")))
                client.sendall(encode_message({"type": "bridge_auth_ok"}))

        worker = threading.Thread(target=accept_once)
        worker.start()
        with tempfile.TemporaryDirectory() as temp_dir:
            bridge = NetworkBridge(
                host,
                port,
                "b" * 32,
                MockScenario(),
                config_path=Path(temp_dir) / "bridge.json",
            )
            try:
                bridge.open()
                self.assertEqual(received[0]["type"], "bridge_auth")
                self.assertEqual(received[0]["token"], "b" * 32)
            finally:
                bridge.close()
                worker.join(timeout=2)
                server.close()


class WordBearStoreTests(unittest.TestCase):
    @staticmethod
    def item(item_id, revision=1, **updates):
        item = {
            "type": "word_bear_sync_item",
            "revision": revision,
            "id": item_id,
            "group": 0,
            "word": f"word{item_id}",
            "part": "n.",
            "meaning": f"释义{item_id}",
            "example": f"Example {item_id}.",
            "learn": 0,
            "correct": 0,
            "wrong": 0,
            "last_day": 0,
            "due_day": 0,
            "streak": 0,
            "flags": 0,
        }
        item.update(updates)
        return item

    def test_complete_snapshot_builds_durable_report_and_exports(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            store = WordBearStore(Path(temp_dir))
            self.assertTrue(
                store.handle(
                    {
                        "type": "word_bear_sync_begin",
                        "schema": 2,
                        "revision": 1,
                        "count": 2,
                        "day": 739000,
                        "active_group": 0,
                    }
                )
            )
            store.handle(self.item(0, learn=1, wrong=1, flags=6))
            store.handle(self.item(1))
            self.assertTrue(
                store.handle({"type": "word_bear_sync_end", "revision": 1})
            )

            self.assertTrue(store.data_path.exists())
            self.assertTrue(store.csv_path.exists())
            self.assertTrue(store.export_json_path.exists())
            report = store.report_path.read_text(encoding="utf-8")
            self.assertIn("未学习", report)
            self.assertIn("错词", report)
            self.assertIn("Example 0", report)

    def test_incomplete_snapshot_does_not_replace_valid_data(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            store = WordBearStore(Path(temp_dir))
            store.handle(
                {
                    "type": "word_bear_sync_begin",
                    "schema": 2,
                    "revision": 1,
                    "count": 1,
                }
            )
            store.handle(self.item(0))
            store.handle({"type": "word_bear_sync_end", "revision": 1})
            store.handle(
                {
                    "type": "word_bear_sync_begin",
                    "schema": 2,
                    "revision": 2,
                    "count": 2,
                }
            )
            store.handle(self.item(0, revision=2, learn=9))
            self.assertFalse(
                store.handle({"type": "word_bear_sync_end", "revision": 2})
            )
            self.assertEqual(store.data["revision"], 1)
            self.assertEqual(store.data["words"][0]["learn"], 0)

    def test_progress_is_idempotent_and_backup_recovers_corrupt_main(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            store = WordBearStore(root)
            store.handle(
                {
                    "type": "word_bear_sync_begin",
                    "schema": 2,
                    "revision": 1,
                    "count": 1,
                }
            )
            store.handle(self.item(0))
            store.handle({"type": "word_bear_sync_end", "revision": 1})
            progress = self.item(
                0,
                revision=2,
                type="word_bear_progress",
                learn=1,
                correct=1,
            )
            store.handle(progress)
            store.handle(dict(progress, learn=99))
            self.assertEqual(store.data["words"][0]["learn"], 1)

            store.data_path.write_text("broken", encoding="utf-8")
            recovered = WordBearStore(root)
            self.assertEqual(recovered.data["revision"], 2)
            self.assertEqual(recovered.data["words"][0]["learn"], 1)

    def test_restore_file_requires_all_100_stable_ids(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "restore.json"
            data = {
                "schema": 2,
                "revision": 5,
                "words": [self.item(item_id) for item_id in range(100)],
            }
            path.write_text(json.dumps(data), encoding="utf-8")
            self.assertEqual(len(load_word_bear_restore(path)["words"]), 100)

            data["words"].pop()
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "100"):
                load_word_bear_restore(path)


class AutostartTests(unittest.TestCase):
    def test_wifi_password_can_be_read_from_stdin_without_command_line_secret(self):
        args = parse_args(
            ["--setup-wifi", "--ssid", "Office 2.4G", "--password-stdin"]
        )
        self.assertTrue(args.setup_wifi)
        self.assertTrue(args.password_stdin)
        self.assertEqual(args.ssid, "Office 2.4G")

    def test_find_codex_rejects_cli_without_app_server(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            broken = root / "codex"
            broken.write_text(
                "#!/bin/sh\n"
                "[ \"$1\" = \"--version\" ] && exit 0\n"
                "echo 'managed standalone install not found' >&2\n"
                "exit 1\n",
                encoding="utf-8",
            )
            broken.chmod(0o755)
            from unittest.mock import patch

            with patch("tools.install_autostart.shutil.which", return_value=str(broken)):
                with patch("tools.install_autostart.Path.home", return_value=root):
                    def fake_run(arguments, **kwargs):
                        if arguments[0] == "/bin/zsh":
                            return subprocess.CompletedProcess(
                                arguments, 0, stdout="", stderr=""
                            )
                        self.assertEqual(
                            arguments[1:],
                            ["app-server", "daemon", "start"],
                        )
                        return subprocess.CompletedProcess(arguments, 1)

                    with patch(
                        "tools.install_autostart.subprocess.run",
                        side_effect=fake_run,
                    ):
                        with self.assertRaisesRegex(RuntimeError, "app-server"):
                            find_codex()

    def test_find_codex_requires_real_daemon_and_socket(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            working = root / "codex"
            working.write_text(
                "#!/bin/sh\n"
                "[ \"$1 $2 $3\" = \"app-server daemon start\" ] && exit 0\n"
                "exit 1\n",
                encoding="utf-8",
            )
            working.chmod(0o755)
            from unittest.mock import patch

            with patch("tools.install_autostart.shutil.which", return_value=str(working)):
                with patch("tools.install_autostart.Path.home", return_value=root):
                    def fake_run(arguments, **kwargs):
                        if arguments[0] == "/bin/zsh":
                            return subprocess.CompletedProcess(
                                arguments, 0, stdout="", stderr=""
                            )
                        self.assertEqual(
                            arguments[1:],
                            ["app-server", "daemon", "start"],
                        )
                        return subprocess.CompletedProcess(arguments, 0)

                    with patch(
                        "tools.install_autostart.subprocess.run",
                        side_effect=fake_run,
                    ):
                        with patch(
                            "tools.install_autostart.app_server_socket_works",
                            return_value=True,
                        ):
                            self.assertEqual(find_codex(), str(working))

    def test_codex_app_server_rejects_zero_exit_without_socket(self):
        from unittest.mock import patch

        result = subprocess.CompletedProcess(
            ["/tmp/codex", "app-server", "daemon", "start"], 0
        )
        with patch("tools.install_autostart.subprocess.run", return_value=result):
            with patch(
                "tools.install_autostart.app_server_socket_works",
                return_value=False,
            ):
                self.assertFalse(codex_app_server_works("/tmp/codex"))

    def test_find_codex_skips_help_only_chatgpt_wrapper(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            wrapper = root / "chatgpt-codex"
            standalone = (
                root / ".codex" / "packages" / "standalone" / "current" / "codex"
            )
            wrapper.write_text("wrapper", encoding="utf-8")
            standalone.parent.mkdir(parents=True)
            standalone.write_text("standalone", encoding="utf-8")
            wrapper.chmod(0o755)
            standalone.chmod(0o755)
            from unittest.mock import patch

            def fake_run(arguments, **kwargs):
                if arguments[0] == "/bin/zsh":
                    return subprocess.CompletedProcess(
                        arguments, 0, stdout="", stderr=""
                    )
                self.assertEqual(
                    arguments[1:],
                    ["app-server", "daemon", "start"],
                )
                if arguments[0] == str(wrapper):
                    return subprocess.CompletedProcess(
                        arguments,
                        1,
                        stdout="",
                        stderr="managed standalone Codex install not found",
                    )
                return subprocess.CompletedProcess(arguments, 0, stdout="", stderr="")

            with patch("tools.install_autostart.shutil.which", return_value=str(wrapper)):
                with patch("tools.install_autostart.Path.home", return_value=root):
                    with patch(
                        "tools.install_autostart.subprocess.run",
                        side_effect=fake_run,
                    ):
                        with patch(
                            "tools.install_autostart.app_server_socket_works",
                            return_value=True,
                        ):
                            self.assertEqual(find_codex(), str(standalone))

    def test_launch_agent_starts_real_codex_and_keeps_running(self):
        root = Path("/tmp/FoloOS project")
        runtime = Path("/tmp/FoloOS runtime")
        config = launch_agent_config(
            root, "/usr/bin/python3", "/tmp/codex", runtime
        )
        self.assertEqual(config["Label"], LABEL)
        self.assertTrue(config["RunAtLoad"])
        self.assertTrue(config["KeepAlive"])
        arguments = config["ProgramArguments"]
        self.assertEqual(arguments[1], str(runtime / "mac_bridge.py"))
        self.assertIn("--real-codex", arguments)
        self.assertEqual(arguments[arguments.index("--workspace") + 1], str(root))
        self.assertEqual(arguments[arguments.index("--codex-bin") + 1], "/tmp/codex")
        self.assertEqual(config["WorkingDirectory"], str(runtime))

    def test_launch_agent_path_contains_codex_and_node_directories(self):
        from tools.install_autostart import launch_agent_path

        path = launch_agent_path(
            "/Users/test/.local/bin/codex",
            "/Users/test/.nvm/versions/node/v22.0.0/bin/node",
        ).split(":")
        self.assertIn("/Users/test/.local/bin", path)
        self.assertIn("/Users/test/.nvm/versions/node/v22.0.0/bin", path)
        self.assertEqual(len(path), len(set(path)))

    def test_confirmed_voice_is_routed_to_real_codex_backend(self):
        class FakeCodex:
            def __init__(self):
                self.events = []

            def handle_device_event(self, event):
                self.events.append(event)
                return [{"type": "task_status", "text": "真实任务已收到"}]

            def poll(self):
                return []

        master_fd, slave_fd = pty.openpty()
        port = os.ttyname(slave_fd)
        os.close(slave_fd)
        codex = FakeCodex()
        bridge = SerialBridge(port, codex=codex)
        bridge.open()
        try:
            command = {"type": "voice_command", "text": "只检查项目，不修改文件"}
            os.write(master_fd, encode_message(command))
            for _ in range(20):
                bridge.read_available()
                if codex.events:
                    break
                time.sleep(0.01)

            self.assertEqual(codex.events, [command])
            readable, _, _ = select.select([master_fd], [], [], 1.0)
            self.assertTrue(readable)
            raw = os.read(master_fd, 4096).decode("utf-8")
            responses = [json.loads(line) for line in raw.splitlines() if line]
            self.assertEqual(
                [response["type"] for response in responses],
                ["task_queued", "task_status"],
            )
            self.assertEqual(responses[0]["text"], "电脑端已收到指令")
            self.assertEqual(responses[1]["text"], "真实任务已收到")
        finally:
            bridge.close()
            os.close(master_fd)


if __name__ == "__main__":
    unittest.main()
