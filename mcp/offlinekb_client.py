"""OfflineKB CLI client: manages persistent offlinekb-cli server process."""

from __future__ import annotations

import json
import os
import subprocess
import threading
import time
from pathlib import Path
from typing import Any


class OfflineKbClientError(RuntimeError):
    pass


class OfflineKbClient:
    def __init__(self, cli_path: str | None = None, dict_dir: str | None = None, models_dir: str | None = None):
        self._cli_path = cli_path or os.environ.get("OFFLINEKB_CLI", "")
        self._dict_dir = dict_dir or os.environ.get("OFFLINEKB_DICT_DIR", "")
        self._models_dir = models_dir or os.environ.get("OFFLINEKB_MODELS_DIR", "")
        self._proc: subprocess.Popen[str] | None = None
        self._lock = threading.Lock()
        self._next_id = 1
        self._stderr_thread: threading.Thread | None = None

        if not self._cli_path:
            self._cli_path = self._guess_cli_path()

    def _guess_cli_path(self) -> str:
        root = Path(__file__).resolve().parents[1]
        candidates = [
            root / "build" / "offlinekb-cli.exe",
            root / "build" / "Release" / "offlinekb-cli.exe",
            root / "build" / "offlinekb-cli",
        ]
        for path in candidates:
            if path.exists():
                return str(path)
        raise OfflineKbClientError(
            "找不到 offlinekb-cli，请设置环境变量 OFFLINEKB_CLI 指向可执行文件"
        )

    def _build_env(self) -> dict[str, str]:
        env = os.environ.copy()
        extra = os.environ.get("OFFLINEKB_PATH_EXTRA", "F:/msys2/mingw64/bin;F:/msys2/mingw64/share/qt6/bin")
        if extra:
            env["PATH"] = extra + ";" + env.get("PATH", "")
        return env

    def start(self) -> None:
        if self._proc is not None and self._proc.poll() is None:
            return

        args = [self._cli_path, "server"]
        if self._dict_dir:
            args.extend(["--dict-dir", self._dict_dir])
        if self._models_dir:
            args.extend(["--models-dir", self._models_dir])

        try:
            self._proc = subprocess.Popen(
                args,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
                env=self._build_env(),
            )
        except OSError as exc:
            raise OfflineKbClientError(f"启动 offlinekb-cli 失败: {exc}") from exc

        if self._proc.stdout is None or self._proc.stdin is None:
            raise OfflineKbClientError("offlinekb-cli 管道初始化失败")

        # Wait for ready marker; llama.cpp logs also go to stderr before ready.
        if self._proc.stderr is not None:
            deadline = time.time() + 300.0
            ready = False
            tail: list[str] = []
            while time.time() < deadline:
                line = self._proc.stderr.readline()
                if line == "":
                    if self._proc.poll() is not None:
                        break
                    time.sleep(0.1)
                    continue
                tail.append(line.rstrip())
                if len(tail) > 20:
                    tail.pop(0)
                if "offlinekb-cli ready" in line.lower():
                    ready = True
                    break
            if not ready:
                hint = " | ".join(tail[-5:]) if tail else "no stderr"
                raise OfflineKbClientError(
                    "offlinekb-cli 启动超时或未输出 ready 标记。"
                    "请确认 OFFLINEKB_CLI 路径正确，且 MSYS2 已加入 PATH。"
                    f" stderr: {hint}"
                )

            def _drain_stderr() -> None:
                assert self._proc is not None and self._proc.stderr is not None
                for _ in self._proc.stderr:
                    pass

            self._stderr_thread = threading.Thread(target=_drain_stderr, daemon=True)
            self._stderr_thread.start()

    def close(self) -> None:
        if self._proc is None:
            return
        try:
            self.call("shutdown", {})
        except OfflineKbClientError:
            pass
        if self._proc.poll() is None:
            self._proc.terminate()
        self._proc = None

    def call(self, method: str, params: dict[str, Any] | None = None, timeout: float = 300.0) -> Any:
        self.start()
        assert self._proc is not None
        assert self._proc.stdin is not None
        assert self._proc.stdout is not None

        with self._lock:
            req_id = self._next_id
            self._next_id += 1
            payload = {"id": req_id, "method": method, "params": params or {}}
            line = json.dumps(payload, ensure_ascii=False)
            self._proc.stdin.write(line + "\n")
            self._proc.stdin.flush()

            # Read lines until matching id; ignore unrelated responses if any.
            while True:
                resp_line = self._proc.stdout.readline()
                if resp_line == "":
                    raise OfflineKbClientError("offlinekb-cli 已退出")
                try:
                    resp = json.loads(resp_line)
                except json.JSONDecodeError as exc:
                    raise OfflineKbClientError(f"无效 JSON 响应: {resp_line}") from exc

                if resp.get("id") != req_id:
                    continue

                if not resp.get("ok", False):
                    err = resp.get("error", {})
                    code = err.get("code", "UNKNOWN")
                    message = err.get("message", "未知错误")
                    raise OfflineKbClientError(f"{code}: {message}")
                return resp.get("result")


_client: OfflineKbClient | None = None


def get_client() -> OfflineKbClient:
    global _client
    if _client is None:
        _client = OfflineKbClient()
    return _client
