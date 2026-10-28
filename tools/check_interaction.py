#!/usr/bin/env python3
"""Run a bounded X11 interaction, resize, pause, and early-close smoke test."""

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time
import traceback
from typing import Any, Dict, List, Optional, Sequence, Tuple


TOTAL_BUDGET_SECONDS = 55.0
REQUESTED_EARLY_CLOSE_FRAMES = 100_000_000
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

VALIDATION_SUMMARY_RE = re.compile(
    r"Vulkan validation reported\s+(\d+)\s+error\(s\)", re.IGNORECASE
)
AUTOMATION_SUMMARY_RE = re.compile(
    r"Automated run submitted\s+(\d+)\s+of\s+(\d+)\s+requested\s+"
    r"frame(?:\(s\)|s)",
    re.IGNORECASE,
)
BENCHMARK_SUMMARY_RE = re.compile(
    r"Benchmark wrote\s+(\d+)\s+measured\s+row(?:\(s\)|s)\s*[,;]?\s*"
    r"expected\s+(\d+)",
    re.IGNORECASE,
)
VERIFICATION_PROGRESS_RE = re.compile(
    r"Verification processed\s+(\d+)\s+of\s+(\d+)\s+measured\s+"
    r"frame(?:\(s\)|s)",
    re.IGNORECASE,
)


class SmokeFailure(RuntimeError):
    """A required smoke-test action or acceptance condition failed."""


class EvidenceGap(RuntimeError):
    """Evidence was unavailable or unreliable, so acceptance is inconclusive."""


class CommandFailure(SmokeFailure):
    """A bounded external command failed."""


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def compact_text(value: str, limit: int = 3000) -> str:
    value = value.strip()
    if len(value) <= limit:
        return value
    return value[:limit] + f"... <{len(value) - limit} characters omitted>"


class Recorder:
    def __init__(self, output: Path, executable: Path) -> None:
        self.output = output
        self.started_monotonic = time.monotonic()
        self.actions_path = output / "actions.jsonl"
        self.report_path = output / "report.json"
        self._actions = self.actions_path.open("w", encoding="utf-8", buffering=1)
        self.report: Dict[str, Any] = {
            "schema_version": 1,
            "started_at": utc_now(),
            "finished_at": None,
            "duration_seconds": 0.0,
            "executable": str(executable),
            "output_directory": str(output),
            "actions_log": str(self.actions_path),
            "preflight": {"status": "pending", "checks": []},
            "cases": {},
            "successes": [],
            "failures": [],
            "gaps": [],
            "overall_status": "running",
            "acceptance_passed": False,
        }
        self.flush_report()

    def action(
        self,
        event: str,
        command: Optional[Sequence[str]] = None,
        case: Optional[str] = None,
        **details: Any,
    ) -> None:
        record: Dict[str, Any] = {
            "timestamp": utc_now(),
            "elapsed_seconds": round(time.monotonic() - self.started_monotonic, 6),
            "case": case,
            "event": event,
        }
        if command is not None:
            record["command"] = [str(part) for part in command]
        record.update(details)
        self._actions.write(json.dumps(record, sort_keys=True) + "\n")
        self._actions.flush()

    def new_case(self, name: str, command: Sequence[str]) -> Dict[str, Any]:
        case: Dict[str, Any] = {
            "name": name,
            "status": "running",
            "command": [str(part) for part in command],
            "pid": None,
            "window_id": None,
            "returncode": None,
            "checks": [],
            "successes": [],
            "failures": [],
            "gaps": [],
            "screenshots": [],
        }
        self.report["cases"][name] = case
        self.flush_report()
        return case

    def check(
        self,
        case: Optional[Dict[str, Any]],
        name: str,
        passed: bool,
        details: Optional[Any] = None,
        gap: bool = False,
    ) -> None:
        status = "pass" if passed else ("gap" if gap else "fail")
        scope = case["name"] if case is not None else "preflight"
        entry: Dict[str, Any] = {"scope": scope, "name": name, "status": status}
        if details is not None:
            entry["details"] = details
        if case is None:
            self.report["preflight"]["checks"].append(entry)
        else:
            case["checks"].append(entry)
            case[{"pass": "successes", "fail": "failures", "gap": "gaps"}[status]].append(entry)
        self.report[{"pass": "successes", "fail": "failures", "gap": "gaps"}[status]].append(entry)
        self.flush_report()

    def finish_case(self, case: Dict[str, Any]) -> None:
        if case["failures"]:
            case["status"] = "fail"
        elif case["gaps"]:
            case["status"] = "gap"
        else:
            case["status"] = "pass"
        self.flush_report()

    def finish(self) -> None:
        case_statuses = [case["status"] for case in self.report["cases"].values()]
        passed = (
            self.report["preflight"]["status"] == "pass"
            and case_statuses == ["pass", "pass"]
            and not self.report["failures"]
            and not self.report["gaps"]
        )
        self.report["acceptance_passed"] = passed
        if passed:
            self.report["overall_status"] = "pass"
        elif self.report["failures"]:
            self.report["overall_status"] = "fail"
        else:
            self.report["overall_status"] = "gap"
        self.report["finished_at"] = utc_now()
        self.report["duration_seconds"] = round(
            time.monotonic() - self.started_monotonic, 6
        )
        self.flush_report()

    def flush_report(self) -> None:
        self.report["duration_seconds"] = round(
            time.monotonic() - self.started_monotonic, 6
        )
        temporary = self.output / "report.json.tmp"
        temporary.write_text(
            json.dumps(self.report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        os.replace(temporary, self.report_path)

    def close(self) -> None:
        self._actions.flush()
        self._actions.close()


class ManagedApp:
    def __init__(
        self,
        driver: "InteractionDriver",
        case_name: str,
        command: Sequence[str],
        stdout_path: Path,
        stderr_path: Path,
    ) -> None:
        self.driver = driver
        self.case_name = case_name
        self.command = [str(part) for part in command]
        self.stdout_path = stdout_path
        self.stderr_path = stderr_path
        self._stdout = stdout_path.open("wb")
        try:
            self._stderr = stderr_path.open("wb")
        except Exception:
            self._stdout.close()
            raise
        self.process: Optional[subprocess.Popen] = None

    def start(self) -> None:
        self.driver.recorder.action(
            "process_launch",
            self.command,
            self.case_name,
            cwd=str(self.driver.executable.parent),
            stdout=str(self.stdout_path),
            stderr=str(self.stderr_path),
        )
        try:
            self.process = subprocess.Popen(
                self.command,
                cwd=str(self.driver.executable.parent),
                stdout=self._stdout,
                stderr=self._stderr,
                start_new_session=True,
                shell=False,
            )
        except Exception:
            self.close_logs()
            raise
        self.driver.recorder.action(
            "process_started", self.command, self.case_name, pid=self.process.pid
        )

    @property
    def pid(self) -> int:
        if self.process is None:
            raise SmokeFailure("application process was not started")
        return self.process.pid

    def poll(self) -> Optional[int]:
        if self.process is None:
            return None
        return self.process.poll()

    def wait(self, timeout: float) -> int:
        if self.process is None:
            raise SmokeFailure("application process was not started")
        bounded = self.driver.bounded_timeout(timeout)
        self.driver.recorder.action(
            "process_wait",
            ["waitpid", str(self.process.pid)],
            self.case_name,
            timeout_seconds=bounded,
        )
        try:
            returncode = self.process.wait(timeout=bounded)
        except subprocess.TimeoutExpired as error:
            self.driver.recorder.action(
                "process_wait_timeout",
                ["waitpid", str(self.process.pid)],
                self.case_name,
                timeout_seconds=bounded,
            )
            raise CommandFailure(
                f"application PID {self.process.pid} did not exit within {bounded:.1f}s"
            ) from error
        self.driver.recorder.action(
            "process_exited",
            ["waitpid", str(self.process.pid)],
            self.case_name,
            returncode=returncode,
        )
        return returncode

    def terminate(self) -> List[str]:
        errors: List[str] = []
        if self.process is None or self.process.poll() is not None:
            self.close_logs()
            return errors
        try:
            process_group = os.getpgid(self.process.pid)
            self.driver.recorder.action(
                "cleanup_signal",
                ["killpg", str(process_group), "SIGTERM"],
                self.case_name,
                pid=self.process.pid,
            )
            os.killpg(process_group, signal.SIGTERM)
            try:
                self.process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.driver.recorder.action(
                    "cleanup_signal",
                    ["killpg", str(process_group), "SIGKILL"],
                    self.case_name,
                    pid=self.process.pid,
                )
                os.killpg(process_group, signal.SIGKILL)
                self.process.wait(timeout=2.0)
        except Exception as error:  # Cleanup must continue through every target.
            errors.append(f"failed to stop PID {self.process.pid}: {error}")
        if self.process.poll() is None:
            try:
                self.driver.recorder.action(
                    "cleanup_signal",
                    ["process.kill", str(self.process.pid), "SIGKILL"],
                    self.case_name,
                    pid=self.process.pid,
                    fallback=True,
                )
                self.process.kill()
                self.process.wait(timeout=2.0)
            except Exception as error:
                errors.append(
                    f"direct kill fallback failed for PID {self.process.pid}: {error}"
                )
        self.close_logs()
        return errors

    def close_logs(self) -> None:
        for stream in (self._stdout, self._stderr):
            if not stream.closed:
                stream.flush()
                stream.close()

    def read_logs(self) -> Tuple[str, str]:
        self.close_logs()
        stdout = self.stdout_path.read_text(encoding="utf-8", errors="replace")
        stderr = self.stderr_path.read_text(encoding="utf-8", errors="replace")
        return stdout, stderr


@dataclass
class Capture:
    path: Path
    x: int
    y: int
    width: int
    height: int
    rgba: bytes
    sha256: str
    started_monotonic: float
    finished_monotonic: float

    def report_entry(self, started_monotonic: float) -> Dict[str, Any]:
        return {
            "path": str(self.path),
            "geometry": {
                "x": self.x,
                "y": self.y,
                "width": self.width,
                "height": self.height,
            },
            "rgba_bytes": len(self.rgba),
            "rgba_sha256": self.sha256,
            "capture_started_seconds": round(
                self.started_monotonic - started_monotonic, 6
            ),
            "capture_finished_seconds": round(
                self.finished_monotonic - started_monotonic, 6
            ),
        }


class InteractionDriver:
    def __init__(self, executable: Path, output: Path) -> None:
        self.executable = executable
        self.output = output
        self.recorder = Recorder(output, executable)
        self.deadline = time.monotonic() + TOTAL_BUDGET_SECONDS
        self.display = ""
        self.xdotool = ""
        self.ffmpeg = ""
        self.current_case: Optional[str] = None
        self.active_app: Optional[ManagedApp] = None
        self.held_keys: List[str] = []

    def bounded_timeout(self, requested: float) -> float:
        remaining = self.deadline - time.monotonic()
        if remaining <= 0.1:
            raise CommandFailure(
                f"overall {TOTAL_BUDGET_SECONDS:.0f}s smoke-test budget exhausted"
            )
        return max(0.1, min(requested, remaining))

    @staticmethod
    def parse_window_id(value: str) -> int:
        value = value.strip()
        return int(value, 16 if value.lower().startswith("0x") else 10)

    def sleep(self, seconds: float, reason: str) -> None:
        if time.monotonic() + seconds > self.deadline:
            raise CommandFailure(
                f"cannot wait {seconds:.2f}s for {reason}: overall budget exhausted"
            )
        self.recorder.action(
            "wait",
            ["internal:sleep", f"{seconds:.3f}"],
            self.current_case,
            reason=reason,
        )
        time.sleep(seconds)

    def _output_summary(self, value: Any, binary: bool) -> Any:
        if value is None:
            return None
        if binary:
            data = bytes(value)
            return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        return compact_text(str(value))

    def run_command(
        self,
        command: Sequence[str],
        timeout: float = 3.0,
        check: bool = True,
        binary: bool = False,
        enforce_deadline: bool = True,
    ) -> subprocess.CompletedProcess:
        argv = [str(part) for part in command]
        bounded = self.bounded_timeout(timeout) if enforce_deadline else max(0.1, timeout)
        self.recorder.action(
            "command_start",
            argv,
            self.current_case,
            timeout_seconds=bounded,
        )
        started = time.monotonic()
        try:
            result = subprocess.run(
                argv,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=not binary,
                encoding=None if binary else "utf-8",
                errors=None if binary else "replace",
                timeout=bounded,
                check=False,
                shell=False,
            )
        except subprocess.TimeoutExpired as error:
            self.recorder.action(
                "command_timeout",
                argv,
                self.current_case,
                timeout_seconds=bounded,
                command_duration_seconds=round(time.monotonic() - started, 6),
                stdout=self._output_summary(error.stdout, binary),
                stderr=self._output_summary(error.stderr, binary),
            )
            raise CommandFailure(
                f"command timed out after {bounded:.1f}s: {argv}"
            ) from error
        except OSError as error:
            self.recorder.action(
                "command_error", argv, self.current_case, error=str(error)
            )
            raise CommandFailure(f"could not run command {argv}: {error}") from error

        self.recorder.action(
            "command_end",
            argv,
            self.current_case,
            returncode=result.returncode,
            command_duration_seconds=round(time.monotonic() - started, 6),
            stdout=self._output_summary(result.stdout, binary),
            stderr=self._output_summary(result.stderr, binary),
        )
        if check and result.returncode != 0:
            stderr = (
                result.stderr.decode("utf-8", "replace")
                if binary
                else str(result.stderr)
            )
            raise CommandFailure(
                f"command exited {result.returncode}: {argv}; {compact_text(stderr, 800)}"
            )
        return result

    def preflight(self) -> bool:
        executable_exists = self.executable.is_file()
        executable_allowed = executable_exists and os.access(self.executable, os.X_OK)
        self.display = os.environ.get("DISPLAY", "").strip()
        found_xdotool = shutil.which("xdotool")
        found_ffmpeg = shutil.which("ffmpeg")
        self.xdotool = found_xdotool or ""
        self.ffmpeg = found_ffmpeg or ""

        self.recorder.check(
            None,
            "executable is a regular executable file",
            executable_allowed,
            {"path": str(self.executable), "exists": executable_exists},
        )
        self.recorder.check(
            None,
            "DISPLAY is set",
            bool(self.display),
            {"display": self.display or None},
        )
        self.recorder.check(
            None,
            "xdotool is available",
            bool(self.xdotool),
            {"path": self.xdotool or None},
        )
        self.recorder.check(
            None,
            "ffmpeg is available",
            bool(self.ffmpeg),
            {"path": self.ffmpeg or None},
        )
        passed = executable_allowed and bool(self.display and self.xdotool and self.ffmpeg)
        self.recorder.report["preflight"]["status"] = "pass" if passed else "fail"
        self.recorder.flush_report()
        return passed

    def start_app(
        self, case: Dict[str, Any], command: Sequence[str], log_stem: str
    ) -> ManagedApp:
        app = ManagedApp(
            self,
            case["name"],
            command,
            self.output / f"{log_stem}.stdout.log",
            self.output / f"{log_stem}.stderr.log",
        )
        case["stdout_log"] = str(app.stdout_path)
        case["stderr_log"] = str(app.stderr_path)
        app.start()
        case["pid"] = app.pid
        self.active_app = app
        self.recorder.flush_report()
        return app

    def find_window(self, app: ManagedApp, timeout: float = 8.0) -> int:
        end = min(self.deadline, time.monotonic() + timeout)
        last_stderr = ""
        while time.monotonic() < end:
            returncode = app.poll()
            if returncode is not None:
                raise SmokeFailure(
                    f"application exited {returncode} before a visible PID-scoped window appeared"
                )
            result = self.run_command(
                [
                    self.xdotool,
                    "search",
                    "--onlyvisible",
                    "--pid",
                    str(app.pid),
                ],
                timeout=2.0,
                check=False,
            )
            last_stderr = str(result.stderr).strip()
            if result.returncode == 0:
                tokens = [token.strip() for token in str(result.stdout).splitlines() if token.strip()]
                try:
                    windows = sorted({self.parse_window_id(token) for token in tokens})
                except ValueError as error:
                    raise EvidenceGap(
                        f"xdotool returned an unparseable PID window list: {tokens}"
                    ) from error
                if len(windows) == 1:
                    return windows[0]
                if len(windows) > 1:
                    raise EvidenceGap(
                        f"PID {app.pid} has multiple visible windows; selection is ambiguous: {windows}"
                    )
            self.sleep(0.10, "wait for PID-scoped X11 window")
        raise SmokeFailure(
            f"no visible xdotool window found for PID {app.pid}; last error: {last_stderr}"
        )

    def query_geometry(self, window: int) -> Dict[str, int]:
        result = self.run_command(
            [self.xdotool, "getwindowgeometry", "--shell", str(window)], timeout=2.0
        )
        values: Dict[str, int] = {}
        allowed = {"WINDOW", "X", "Y", "WIDTH", "HEIGHT", "SCREEN"}
        for line in str(result.stdout).splitlines():
            if "=" not in line:
                continue
            name, value = line.split("=", 1)
            name = name.strip().upper()
            value = value.strip()
            if name not in allowed:
                continue
            if not re.fullmatch(r"-?\d+", value):
                raise EvidenceGap(
                    f"unparseable geometry field {name}={value!r} for window {window}"
                )
            values[name] = int(value, 10)
        missing = [name for name in ("X", "Y", "WIDTH", "HEIGHT") if name not in values]
        if missing or values.get("WIDTH", 0) <= 0 or values.get("HEIGHT", 0) <= 0:
            raise EvidenceGap(
                f"incomplete or invalid geometry for window {window}: {values}; missing {missing}"
            )
        return values

    def wait_for_geometry(
        self,
        window: int,
        expected_width: Optional[int] = None,
        expected_height: Optional[int] = None,
        expected_x: Optional[int] = None,
        expected_y: Optional[int] = None,
        timeout: float = 3.0,
    ) -> Dict[str, int]:
        end = min(self.deadline, time.monotonic() + timeout)
        last: Dict[str, int] = {}
        while time.monotonic() < end:
            last = self.query_geometry(window)
            matches = (
                (expected_width is None or last["WIDTH"] == expected_width)
                and (expected_height is None or last["HEIGHT"] == expected_height)
                and (expected_x is None or last["X"] == expected_x)
                and (expected_y is None or last["Y"] == expected_y)
            )
            if matches:
                return last
            self.sleep(0.10, "wait for confirmed X11 geometry")
        raise SmokeFailure(
            "window geometry did not converge to "
            f"x={expected_x}, y={expected_y}, width={expected_width}, "
            f"height={expected_height}; last={last}"
        )

    def focus_window(self, window: int) -> None:
        self.run_command(
            [self.xdotool, "windowfocus", "--sync", str(window)], timeout=3.0
        )
        result = self.run_command([self.xdotool, "getwindowfocus"], timeout=2.0)
        text = str(result.stdout).strip()
        try:
            focused = self.parse_window_id(text)
        except ValueError as error:
            raise SmokeFailure(f"xdotool returned an invalid focused window: {text!r}") from error
        if focused != window:
            raise SmokeFailure(
                f"focused window {focused} does not match PID-scoped window {window}"
            )

    def move_to_origin(self, window: int) -> Dict[str, int]:
        geometry = self.query_geometry(window)
        if geometry["X"] != 0 or geometry["Y"] != 0:
            self.run_command(
                [self.xdotool, "windowmove", "--sync", str(window), "0", "0"],
                timeout=3.0,
            )
            geometry = self.wait_for_geometry(
                window, expected_x=0, expected_y=0, timeout=3.0
            )
        return geometry

    def resize_window(self, window: int, width: int, height: int) -> Dict[str, int]:
        self.run_command(
            [
                self.xdotool,
                "windowsize",
                "--sync",
                str(window),
                str(width),
                str(height),
            ],
            timeout=4.0,
        )
        geometry = self.wait_for_geometry(
            window, expected_width=width, expected_height=height, timeout=4.0
        )
        self.sleep(0.20, "allow resize and swapchain work to settle")
        confirmed = self.query_geometry(window)
        if confirmed["WIDTH"] != width or confirmed["HEIGHT"] != height:
            raise SmokeFailure(
                f"window changed after resize confirmation: expected {width}x{height}, "
                f"got {confirmed['WIDTH']}x{confirmed['HEIGHT']}"
            )
        return confirmed

    def key_down(self, key: str) -> None:
        if key in self.held_keys:
            raise SmokeFailure(f"key {key!r} is already tracked as held")
        self.run_command([self.xdotool, "keydown", key], timeout=2.0)
        self.held_keys.append(key)

    def key_up(self, key: str) -> None:
        self.run_command([self.xdotool, "keyup", key], timeout=2.0)
        if key in self.held_keys:
            self.held_keys.remove(key)

    def tap_key(self, key: str, hold: float = 0.08, gap: float = 0.07) -> None:
        self.key_down(key)
        self.sleep(hold, f"hold key {key}")
        self.key_up(key)
        if gap > 0.0:
            self.sleep(gap, f"separate key edge for {key}")

    def hold_key(self, key: str, duration: float = 0.14) -> None:
        self.key_down(key)
        self.sleep(duration, f"exercise held key {key}")
        self.key_up(key)
        self.sleep(0.05, f"separate held input {key}")

    def release_held_keys(self) -> List[str]:
        errors: List[str] = []
        for key in list(reversed(self.held_keys)):
            try:
                result = self.run_command(
                    [self.xdotool, "keyup", key],
                    timeout=1.0,
                    check=False,
                    enforce_deadline=False,
                )
                if result.returncode != 0:
                    errors.append(f"cleanup keyup {key!r} exited {result.returncode}")
                else:
                    self.held_keys.remove(key)
            except Exception as error:
                errors.append(f"cleanup keyup {key!r} failed: {error}")
        return errors

    def capture_frame(
        self, case: Dict[str, Any], window: int, filename: str
    ) -> Capture:
        geometry = self.query_geometry(window)
        x, y = geometry["X"], geometry["Y"]
        width, height = geometry["WIDTH"], geometry["HEIGHT"]
        path = self.output / filename
        input_spec = f"{self.display}+{x},{y}"
        capture_command = [
            self.ffmpeg,
            "-nostdin",
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-f",
            "x11grab",
            "-draw_mouse",
            "0",
            "-framerate",
            "30",
            "-video_size",
            f"{width}x{height}",
            "-i",
            input_spec,
            "-frames:v",
            "1",
            "-c:v",
            "png",
            str(path),
        ]
        started = time.monotonic()
        try:
            self.run_command(capture_command, timeout=6.0)
        except CommandFailure as error:
            raise EvidenceGap(f"FFmpeg X11 screenshot acquisition failed: {error}") from error
        if not path.is_file() or path.stat().st_size <= len(PNG_SIGNATURE):
            raise EvidenceGap(f"FFmpeg did not create a usable screenshot: {path}")
        with path.open("rb") as stream:
            signature = stream.read(len(PNG_SIGNATURE))
        if signature != PNG_SIGNATURE:
            raise EvidenceGap(f"screenshot has an invalid PNG signature: {path}")

        decode_command = [
            self.ffmpeg,
            "-nostdin",
            "-hide_banner",
            "-loglevel",
            "error",
            "-i",
            str(path),
            "-frames:v",
            "1",
            "-pix_fmt",
            "rgba",
            "-f",
            "rawvideo",
            "pipe:1",
        ]
        try:
            decoded = self.run_command(decode_command, timeout=6.0, binary=True)
        except CommandFailure as error:
            raise EvidenceGap(f"FFmpeg PNG-to-RGBA decode failed: {error}") from error
        rgba = bytes(decoded.stdout)
        expected = width * height * 4
        if len(rgba) != expected:
            raise EvidenceGap(
                f"decoded screenshot length is unreliable: expected {expected}, got {len(rgba)}"
            )
        finished = time.monotonic()
        capture = Capture(
            path=path,
            x=x,
            y=y,
            width=width,
            height=height,
            rgba=rgba,
            sha256=hashlib.sha256(rgba).hexdigest(),
            started_monotonic=started,
            finished_monotonic=finished,
        )
        case["screenshots"].append(
            capture.report_entry(self.recorder.started_monotonic)
        )
        self.recorder.flush_report()
        return capture

    @staticmethod
    def compare_pixels(first: Capture, second: Capture) -> Dict[str, Any]:
        first_geometry = (first.x, first.y, first.width, first.height)
        second_geometry = (second.x, second.y, second.width, second.height)
        if first_geometry != second_geometry:
            raise EvidenceGap(
                f"capture geometry changed from {first_geometry} to {second_geometry}"
            )
        if len(first.rgba) != len(second.rgba):
            raise EvidenceGap(
                f"decoded capture lengths differ: {len(first.rgba)} vs {len(second.rgba)}"
            )
        differing_bytes = sum(a != b for a, b in zip(first.rgba, second.rgba))
        differing_pixels = sum(
            first.rgba[index : index + 4] != second.rgba[index : index + 4]
            for index in range(0, len(first.rgba), 4)
        )
        total_pixels = first.width * first.height
        return {
            "first_sha256": first.sha256,
            "second_sha256": second.sha256,
            "differing_bytes": differing_bytes,
            "differing_pixels": differing_pixels,
            "total_pixels": total_pixels,
            "differing_pixel_ratio": (
                differing_pixels / total_pixels if total_pixels else None
            ),
        }

    @staticmethod
    def validation_evidence(stdout: str, stderr: str) -> Dict[str, Any]:
        combined = stdout + "\n" + stderr
        error_lines = [
            line.strip() for line in combined.splitlines() if "Vulkan error:" in line
        ]
        warning_lines = [
            line.strip() for line in combined.splitlines() if "Vulkan warning:" in line
        ]
        summary_counts = [int(value) for value in VALIDATION_SUMMARY_RE.findall(combined)]
        return {
            "error_lines": error_lines,
            "warning_count": len(warning_lines),
            "summary_error_counts": summary_counts,
            "has_errors": bool(error_lines) or any(value != 0 for value in summary_counts),
        }

    def run_interactive_case(self, case: Dict[str, Any]) -> None:
        command = case["command"]
        app = self.start_app(case, command, "interactive")
        window = self.find_window(app)
        case["window_id"] = window
        self.recorder.check(
            case,
            "found exactly one visible PID-scoped window",
            True,
            {"pid": app.pid, "window_id": window},
        )
        self.focus_window(window)
        self.recorder.check(case, "focused PID-scoped window with windowfocus", True)

        origin_geometry = self.move_to_origin(window)
        case["origin_geometry"] = origin_geometry
        self.recorder.check(
            case,
            "window position is 0,0",
            origin_geometry["X"] == 0 and origin_geometry["Y"] == 0,
            origin_geometry,
        )

        geometry_960 = self.resize_window(window, 960, 540)
        case["resize_960x540"] = geometry_960
        self.recorder.check(
            case,
            "confirmed 960x540 resize",
            geometry_960["WIDTH"] == 960 and geometry_960["HEIGHT"] == 540,
            geometry_960,
        )
        geometry_1280 = self.resize_window(window, 1280, 720)
        case["resize_1280x720"] = geometry_1280
        self.recorder.check(
            case,
            "confirmed 1280x720 resize",
            geometry_1280["WIDTH"] == 1280 and geometry_1280["HEIGHT"] == 720,
            geometry_1280,
        )

        self.sleep(0.75, "allow renderer initialization and input polling to settle")
        self.focus_window(window)
        toggle_keys = ["1", "2", "3", "4", "l", "f", "h"]
        for key in toggle_keys:
            self.tap_key(key)
            self.tap_key(key)
        self.recorder.check(
            case,
            "exercised edge toggles twice and restored defaults",
            True,
            {"keys": toggle_keys},
        )

        held_inputs = ["Left", "Right", "Up", "Down", "w", "a", "s", "d", "q", "e"]
        for key in held_inputs:
            self.hold_key(key)
        self.recorder.check(
            case,
            "exercised wind and sphere held inputs",
            True,
            {"keys": held_inputs},
        )

        self.focus_window(window)
        self.tap_key("h")
        self.tap_key("space")
        self.sleep(0.35, "drain frames after pausing with overlay hidden")
        paused_first = self.capture_frame(case, window, "paused-1.png")
        self.sleep(0.60, "separate paused screenshots by at least 0.5 seconds")
        paused_second = self.capture_frame(case, window, "paused-2.png")
        paused_separation = paused_second.started_monotonic - paused_first.started_monotonic
        case["paused_capture_separation_seconds"] = round(paused_separation, 6)
        self.recorder.check(
            case,
            "paused screenshots started at least 0.5 seconds apart",
            paused_separation >= 0.5,
            {"seconds": paused_separation},
        )

        paused_metrics = self.compare_pixels(paused_first, paused_second)
        case["paused_pixel_comparison"] = paused_metrics
        paused_equal = (
            paused_metrics["differing_bytes"] == 0
            and paused_metrics["differing_pixels"] == 0
            and paused_first.sha256 == paused_second.sha256
        )
        self.recorder.check(
            case,
            "paused frames are exactly equal in decoded RGBA pixels",
            paused_equal,
            paused_metrics,
        )

        self.focus_window(window)
        self.tap_key("space")
        self.sleep(0.55, "allow unpaused simulation to advance")
        resumed = self.capture_frame(case, window, "resumed.png")
        resumed_metrics = self.compare_pixels(paused_second, resumed)
        case["resumed_pixel_comparison"] = resumed_metrics
        resumed_changed = resumed_metrics["differing_pixels"] > 0
        self.recorder.check(
            case,
            "unpaused frame changes and proves screenshot liveness",
            resumed_changed,
            resumed_metrics,
            gap=not resumed_changed,
        )

        self.focus_window(window)
        self.tap_key("h")
        self.tap_key("r")
        self.sleep(0.15, "allow reset request to be consumed")
        self.tap_key("Escape", gap=0.0)
        returncode = app.wait(12.0)
        case["returncode"] = returncode
        stdout, stderr = app.read_logs()
        validation = self.validation_evidence(stdout, stderr)
        case["validation"] = validation
        renderer_errors = [
            line.strip()
            for line in (stdout + "\n" + stderr).splitlines()
            if "Renderer error:" in line
        ]
        case["renderer_errors"] = renderer_errors
        self.recorder.check(
            case,
            "interactive process exited normally with code 0",
            returncode == 0,
            {"returncode": returncode},
        )
        self.recorder.check(
            case,
            "interactive run has no Vulkan validation errors",
            not validation["has_errors"],
            validation,
        )
        self.recorder.check(
            case,
            "interactive run has no Renderer error",
            not renderer_errors,
            {"lines": renderer_errors},
        )

    @staticmethod
    def expected_incomplete_renderer_error(line: str) -> bool:
        lowered = line.lower()
        scope = any(word in lowered for word in ("automated", "benchmark", "verification"))
        reason = any(
            phrase in lowered
            for phrase in (
                "incomplete",
                "not complete",
                "before all",
                "submitted",
                "processed",
                "requested frame",
            )
        )
        return scope and reason

    def run_early_close_case(self, case: Dict[str, Any]) -> None:
        benchmark_path = self.output / "early-close.csv"
        if benchmark_path.exists():
            self.recorder.action(
                "remove_stale_artifact",
                ["internal:unlink", str(benchmark_path)],
                case["name"],
            )
            benchmark_path.unlink()
        case["benchmark_path"] = str(benchmark_path)

        app = self.start_app(case, case["command"], "early-close")
        window = self.find_window(app)
        case["window_id"] = window
        self.recorder.check(
            case,
            "found exactly one visible PID-scoped early-close window",
            True,
            {"pid": app.pid, "window_id": window},
        )
        self.focus_window(window)
        self.recorder.check(case, "focused early-close window with windowfocus", True)
        self.sleep(0.30, "submit a small amount of automated work before early close")
        if app.poll() is not None:
            raise SmokeFailure(
                f"automated application exited {app.poll()} before Escape was sent"
            )
        self.tap_key("Escape", gap=0.0)
        returncode = app.wait(12.0)
        case["returncode"] = returncode
        stdout, stderr = app.read_logs()
        combined = stdout + "\n" + stderr

        validation = self.validation_evidence(stdout, stderr)
        case["validation"] = validation
        self.recorder.check(
            case,
            "early-close process exited with expected code 1",
            returncode == 1,
            {"returncode": returncode, "negative_or_signal": returncode < 0},
        )
        self.recorder.check(
            case,
            "early-close run has no Vulkan validation errors",
            not validation["has_errors"],
            validation,
        )

        automation_matches = AUTOMATION_SUMMARY_RE.findall(combined)
        automation_counts = (
            tuple(int(value) for value in automation_matches[-1])
            if automation_matches
            else None
        )
        benchmark_matches = BENCHMARK_SUMMARY_RE.findall(combined)
        benchmark_counts = (
            tuple(int(value) for value in benchmark_matches[-1])
            if benchmark_matches
            else None
        )
        verification_matches = VERIFICATION_PROGRESS_RE.findall(combined)
        verification_counts = (
            tuple(int(value) for value in verification_matches[-1])
            if verification_matches
            else None
        )
        case["automation_progress"] = {
            "submitted": automation_counts[0] if automation_counts else None,
            "requested": automation_counts[1] if automation_counts else None,
            "benchmark_rows": benchmark_counts[0] if benchmark_counts else None,
            "benchmark_expected": benchmark_counts[1] if benchmark_counts else None,
            "verification_processed": verification_counts[0]
            if verification_counts
            else None,
            "verification_expected": verification_counts[1]
            if verification_counts
            else None,
        }

        automation_incomplete = bool(
            automation_counts
            and automation_counts[0] < automation_counts[1]
            and automation_counts[1] == REQUESTED_EARLY_CLOSE_FRAMES
        )
        self.recorder.check(
            case,
            "automation summary proves submitted work was incomplete",
            automation_incomplete,
            {"counts": automation_counts, "expected_requested": REQUESTED_EARLY_CLOSE_FRAMES},
        )
        benchmark_incomplete = bool(
            benchmark_counts
            and benchmark_counts[0] < benchmark_counts[1]
            and benchmark_counts[1] == REQUESTED_EARLY_CLOSE_FRAMES
        )
        self.recorder.check(
            case,
            "benchmark summary proves measured rows were incomplete",
            benchmark_incomplete,
            {"counts": benchmark_counts, "expected_requested": REQUESTED_EARLY_CLOSE_FRAMES},
        )
        verification_incomplete = bool(
            verification_counts
            and verification_counts[0] < verification_counts[1]
            and verification_counts[1] == REQUESTED_EARLY_CLOSE_FRAMES
        )
        self.recorder.check(
            case,
            "verification summary proves measured frames were incomplete",
            verification_incomplete,
            {"counts": verification_counts, "expected_requested": REQUESTED_EARLY_CLOSE_FRAMES},
        )

        verification_passed = re.search(r"Verification passed", combined, re.IGNORECASE)
        self.recorder.check(
            case,
            "incomplete run made no Verification passed claim",
            verification_passed is None,
            {"claim_found": verification_passed.group(0) if verification_passed else None},
        )

        renderer_errors = [
            line.strip() for line in combined.splitlines() if "Renderer error:" in line
        ]
        unrelated_errors = [
            line
            for line in renderer_errors
            if not self.expected_incomplete_renderer_error(line)
        ]
        case["renderer_errors"] = renderer_errors
        self.recorder.check(
            case,
            "no unrelated Renderer error explains the expected exit",
            not unrelated_errors,
            {"all_renderer_errors": renderer_errors, "unrelated": unrelated_errors},
        )
        case["benchmark_artifact"] = {
            "path": str(benchmark_path),
            "exists": benchmark_path.is_file(),
            "bytes": benchmark_path.stat().st_size if benchmark_path.is_file() else 0,
        }
        self.recorder.flush_report()

    def run_case(self, name: str, command: Sequence[str], function: Any) -> None:
        case = self.recorder.new_case(name, command)
        self.current_case = name
        completed = False
        interruption: Optional[BaseException] = None
        try:
            function(case)
            completed = True
        except EvidenceGap as error:
            self.recorder.check(
                case,
                "acceptance evidence is reliable",
                False,
                {"error": str(error)},
                gap=True,
            )
        except (SmokeFailure, subprocess.SubprocessError, OSError) as error:
            self.recorder.check(
                case,
                "case completed without action failure",
                False,
                {"error": str(error), "type": type(error).__name__},
            )
        except (KeyboardInterrupt, InterruptedError) as error:
            self.recorder.check(
                case,
                "case was not interrupted",
                False,
                {"error": str(error), "type": type(error).__name__},
            )
            interruption = error
        except Exception as error:  # Preserve a report for unexpected QA-driver bugs.
            self.recorder.check(
                case,
                "driver encountered no unexpected exception",
                False,
                {
                    "error": str(error),
                    "type": type(error).__name__,
                    "traceback": traceback.format_exc(limit=8),
                },
            )
        finally:
            key_errors = self.release_held_keys()
            for error in key_errors:
                self.recorder.check(
                    case, "released every held XTEST key", False, {"error": error}
                )
            if self.active_app is not None:
                was_running = self.active_app.poll() is None
                cleanup_errors = self.active_app.terminate()
                for error in cleanup_errors:
                    self.recorder.check(
                        case, "cleaned up application process group", False, {"error": error}
                    )
                if completed and was_running:
                    self.recorder.check(
                        case,
                        "case did not leave its application running",
                        False,
                        {"pid": self.active_app.pid},
                    )
                self.active_app = None
            self.recorder.finish_case(case)
            self.current_case = None
        if interruption is not None:
            raise interruption

    def run(self) -> int:
        try:
            if self.preflight():
                interactive_command = [
                    str(self.executable),
                    "--preset",
                    "final",
                    "--blades",
                    "8192",
                    "--validation",
                    "on",
                    "--width",
                    "1024",
                    "--height",
                    "576",
                ]
                self.run_case(
                    "interactive", interactive_command, self.run_interactive_case
                )

                early_benchmark = self.output / "early-close.csv"
                early_command = [
                    str(self.executable),
                    "--preset",
                    "final",
                    "--blades",
                    "8192",
                    "--validation",
                    "on",
                    "--width",
                    "1024",
                    "--height",
                    "576",
                    "--verify",
                    "--benchmark",
                    str(early_benchmark),
                    "--warmup",
                    "0",
                    "--frames",
                    str(REQUESTED_EARLY_CLOSE_FRAMES),
                ]
                self.run_case(
                    "early_automated_close",
                    early_command,
                    self.run_early_close_case,
                )
        except (KeyboardInterrupt, InterruptedError) as error:
            self.recorder.check(
                None,
                "driver was not interrupted",
                False,
                {"error": str(error), "type": type(error).__name__},
            )
        except Exception as error:
            self.recorder.check(
                None,
                "driver completed without top-level exception",
                False,
                {
                    "error": str(error),
                    "type": type(error).__name__,
                    "traceback": traceback.format_exc(limit=8),
                },
            )
        finally:
            key_errors = self.release_held_keys() if self.held_keys else []
            for error in key_errors:
                self.recorder.check(
                    None, "released every held XTEST key", False, {"error": error}
                )
            if self.active_app is not None:
                cleanup_errors = self.active_app.terminate()
                for error in cleanup_errors:
                    self.recorder.check(
                        None,
                        "cleaned up application process group",
                        False,
                        {"error": error},
                    )
                self.active_app = None
            self.recorder.finish()
            self.recorder.close()

        status = self.recorder.report["overall_status"]
        print(f"Interaction smoke {status}: {self.recorder.report_path}", flush=True)
        return 0 if self.recorder.report["acceptance_passed"] else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="renderer executable to launch")
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="directory for actions.jsonl, report.json, logs, CSV, and screenshots",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    executable = args.executable.expanduser().resolve()
    output = args.output.expanduser().resolve()
    try:
        output.mkdir(parents=True, exist_ok=True)
        driver = InteractionDriver(executable, output)
    except OSError as error:
        print(f"Could not initialize output directory {output}: {error}", file=sys.stderr)
        return 1

    def handle_termination(signum: int, _frame: Any) -> None:
        raise InterruptedError(f"received signal {signum}")

    previous_term = signal.signal(signal.SIGTERM, handle_termination)
    try:
        return driver.run()
    finally:
        signal.signal(signal.SIGTERM, previous_term)


if __name__ == "__main__":
    raise SystemExit(main())
