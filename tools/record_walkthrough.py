#!/usr/bin/env python3
"""Record real X11 mouse/keyboard demonstrations with event-derived captions."""
import argparse
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


WIDTH, HEIGHT, FPS = 1280, 720, 30
TAKE_SPECS = (
    ("interaction", "interaction", 48, 524288),
    ("overview", "overview", 40, 32768),
    ("occlusion", "occluder", 32, 524288),
)


def run(command, **kwargs):
    timeout = kwargs.pop("timeout", 30)
    return subprocess.run(
        [str(value) for value in command], check=True, timeout=timeout, **kwargs
    )


def screen_frame_evidence(frame, width=WIDTH, height=HEIGHT):
    """Return dependency-free evidence that an RGB screen sample is non-uniform."""
    expected = width * height * 3
    if len(frame) != expected:
        raise ValueError(f"Expected {expected} RGB bytes, got {len(frame)}")
    pixels = width * height
    step = max(1, pixels // 4096)
    lo_luma, hi_luma, lo_channel, hi_channel = 255, 0, 255, 0
    buckets = set()
    samples = 0
    for pixel in range(0, pixels, step):
        offset = pixel * 3
        red, green, blue = frame[offset:offset + 3]
        luma = (3 * red + 6 * green + blue) // 10
        lo_luma, hi_luma = min(lo_luma, luma), max(hi_luma, luma)
        lo_channel = min(lo_channel, red, green, blue)
        hi_channel = max(hi_channel, red, green, blue)
        buckets.add(luma // 4)
        samples += 1
    luma_spread = hi_luma - lo_luma
    channel_spread = hi_channel - lo_channel
    ready = samples >= 128 and luma_spread >= 12 and channel_spread >= 16 and len(buckets) >= 6
    return {
        "content_ready": ready,
        "sample_count": samples,
        "luma_min": lo_luma,
        "luma_max": hi_luma,
        "luma_spread": luma_spread,
        "channel_spread": channel_spread,
        "luma_bucket_count": len(buckets),
        "rgb_sha256": hashlib.sha256(frame).hexdigest(),
    }


def capture_x11_rgb_frame(display, geometry, timeout):
    result = run(
        ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "x11grab",
         "-draw_mouse", "0", "-video_size", f"{WIDTH}x{HEIGHT}", "-i",
         f"{display}+{geometry['X']},{geometry['Y']}", "-frames:v", "1",
         "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"],
        capture_output=True, timeout=timeout,
    )
    return result.stdout


def wait_for_screen_content(app, display, geometry, timeout=60.0):
    """Require two non-uniform samples; this is not renderer pause-state proof."""
    deadline = time.monotonic() + timeout
    consecutive, attempts = 0, 0
    last = None
    while time.monotonic() < deadline:
        status = app.poll()
        if status is not None:
            raise RuntimeError(f"Renderer exited ({status}) before screen readiness")
        remaining = deadline - time.monotonic()
        attempts += 1
        try:
            frame = capture_x11_rgb_frame(display, geometry, min(3.0, remaining))
        except subprocess.TimeoutExpired as error:
            consecutive, last = 0, str(error)
        else:
            last = screen_frame_evidence(frame)
            consecutive = consecutive + 1 if last["content_ready"] else 0
            if consecutive >= 2:
                return {
                    "method": "bounded external ffmpeg X11 RGB sampling",
                    "attempts": attempts,
                    "consecutive_ready_frames": consecutive,
                    "evidence": last,
                    "renderer_pause_state_proven": False,
                }
        time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))
    raise RuntimeError(f"Screen content readiness timed out after {timeout:.1f}s; last={last}")


def parse_ffmpeg_progress(text):
    latest = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            latest[key.strip()] = value.strip()
    try:
        frame = int(latest.get("frame", "0"))
    except ValueError:
        frame = 0
    media_seconds = None
    out_time = latest.get("out_time")
    if out_time:
        parts = out_time.split(":")
        if len(parts) == 3:
            try:
                media_seconds = float(parts[0]) * 3600 + float(parts[1]) * 60 + float(parts[2])
            except ValueError:
                pass
    return {"frame": frame, "out_time": out_time, "media_seconds": media_seconds,
            "progress": latest.get("progress")}


def wait_for_recorder_ready(recording, app, progress_path, timeout=10.0):
    deadline = time.monotonic() + timeout
    last = {"frame": 0, "out_time": None, "media_seconds": None, "progress": None}
    while time.monotonic() < deadline:
        app_status, recording_status = app.poll(), recording.poll()
        if app_status is not None:
            raise RuntimeError(f"Renderer exited ({app_status}) before recorder readiness")
        if recording_status is not None:
            raise RuntimeError(f"FFmpeg exited ({recording_status}) before its first progress frame")
        try:
            text = progress_path.read_text(encoding="utf-8", errors="replace")
        except FileNotFoundError:
            text = ""
        last = parse_ffmpeg_progress(text)
        if last["frame"] >= 1:
            observed = time.monotonic()
            media_seconds = last["media_seconds"]
            if media_seconds is None or media_seconds < 0:
                media_seconds = max(0.0, (last["frame"] - 1) / FPS)
            return {**last, "media_seconds": media_seconds, "observed_monotonic": observed}
        time.sleep(min(0.02, max(0.0, deadline - time.monotonic())))
    raise RuntimeError(f"FFmpeg first-frame readiness timed out after {timeout:.1f}s; last={last}")


def inspect_validation_log(text):
    """Classify validation evidence and retain only counters actually emitted."""
    error_lines, warning_lines, summaries = [], [], []
    for raw_line in text.splitlines():
        line, lowered = raw_line.strip(), raw_line.lower()
        if not line:
            continue
        is_error = "vuid-" in lowered or "vulkan error:" in lowered or "validation error" in lowered
        is_warning = "vulkan warning:" in lowered or "validation warning" in lowered
        if "validation" in lowered:
            for match in re.finditer(
                    r"\b(?P<count>\d+)\s+(?P<kind>error|warning)(?:s|\(s\))?(?!\w)",
                    line, flags=re.IGNORECASE):
                count, kind = int(match.group("count")), match.group("kind").lower()
                summaries.append({"kind": kind, "count": count, "line": line})
                is_error = is_error or (kind == "error" and count > 0)
                is_warning = is_warning or (kind == "warning" and count > 0)
        if is_error and line not in error_lines:
            error_lines.append(line)
        if is_warning and line not in warning_lines:
            warning_lines.append(line)
    return {
        "clean": not error_lines and not warning_lines,
        "matched_error_line_count": len(error_lines),
        "matched_warning_line_count": len(warning_lines),
        "error_evidence": error_lines,
        "warning_evidence": warning_lines,
        "reported_summaries": summaries,
        "reported_error_counts": [item["count"] for item in summaries
                                  if item["kind"] == "error"],
        "reported_warning_counts": [item["count"] for item in summaries
                                    if item["kind"] == "warning"],
        "reported_summary_count": len(summaries),
        "note": "reported_summaries contains only counters actually present in the log",
    }


def validate_final_probe(probe, byte_size):
    failures = []
    streams = [stream for stream in probe.get("streams", []) if stream.get("codec_type") == "video"]
    if not streams:
        raise RuntimeError("Final video QA failed: ffprobe reported no video stream")
    stream = streams[0]
    width, height = stream.get("width"), stream.get("height")
    if (width, height) != (WIDTH, HEIGHT):
        failures.append(f"dimensions {width}x{height}, expected {WIDTH}x{HEIGHT}")
    rate_text = stream.get("avg_frame_rate")
    try:
        rate = Fraction(rate_text)
    except (TypeError, ValueError, ZeroDivisionError):
        rate = None
        failures.append(f"unparseable avg_frame_rate {rate_text!r}")
    if rate is not None and rate != Fraction(FPS, 1):
        failures.append(f"frame rate {rate_text}, expected exactly {FPS}/1")
    duration_source = "format.duration"
    duration_text = probe.get("format", {}).get("duration")
    if duration_text is None:
        duration_source, duration_text = "stream.duration", stream.get("duration")
    try:
        duration = float(duration_text)
    except (TypeError, ValueError):
        duration = None
        failures.append(f"unparseable duration {duration_text!r}")
    if duration is not None and not 119.0 <= duration <= 122.0:
        failures.append(f"duration {duration:.6f}s, expected 119..122s")
    try:
        frames = int(stream.get("nb_read_frames"))
    except (TypeError, ValueError):
        frames = None
        failures.append(f"unparseable nb_read_frames {stream.get('nb_read_frames')!r}")
    if frames is not None and not 3570 <= frames <= 3660:
        failures.append(f"frame count {frames}, expected 3570..3660")
    if byte_size >= 50_000_000:
        failures.append(f"size {byte_size}, must be strictly below 50000000 bytes")
    if failures:
        raise RuntimeError("Final video QA failed: " + "; ".join(failures))
    return {
        "passed": True, "width": width, "height": height, "avg_frame_rate": rate_text,
        "duration_seconds": duration, "duration_source": duration_source,
        "frame_count": frames, "bytes": byte_size,
        "required_duration_range_seconds": [119.0, 122.0],
        "required_frame_range": [3570, 3660], "strict_byte_limit": 50_000_000,
    }


def stop_and_reap(process, label, graceful=None, graceful_timeout=3.0,
                  terminate_timeout=5.0, kill_timeout=5.0):
    """Use bounded graceful/TERM/KILL escalation and always attempt a final wait."""
    errors = []
    if process is None:
        return errors

    def running():
        try:
            return process.poll() is None
        except Exception as error:
            errors.append(f"{label} poll failed: {error}")
            return True

    def bounded_wait(timeout):
        try:
            process.wait(timeout=timeout)
            return True
        except subprocess.TimeoutExpired:
            return False
        except Exception as error:
            errors.append(f"{label} wait failed: {error}")
            return False

    if running() and graceful is not None:
        try:
            graceful()
        except Exception as error:
            errors.append(f"{label} graceful stop failed: {error}")
        bounded_wait(graceful_timeout)
    if running():
        try:
            process.terminate()
        except Exception as error:
            errors.append(f"{label} terminate failed: {error}")
        bounded_wait(terminate_timeout)
    if running():
        try:
            process.kill()
        except Exception as error:
            errors.append(f"{label} kill failed: {error}")
        bounded_wait(kill_timeout)
    if running():
        errors.append(f"{label} could not be reaped after KILL")
    else:
        bounded_wait(0.1)
    return errors


def cleanup_take(held_keys, held_buttons, xdo, app, recording, log_handles):
    """Release each input and clean each resource independently."""
    errors = []
    for symbol in list(held_keys):
        try:
            xdo("keyup", symbol)
        except Exception as error:
            errors.append(f"keyup {symbol!r} failed: {error}")
        finally:
            held_keys.discard(symbol)
    for button in list(held_buttons):
        try:
            xdo("mouseup", str(button))
        except Exception as error:
            errors.append(f"mouseup {button!r} failed: {error}")
        finally:
            held_buttons.discard(button)

    def stop_recording():
        if recording.stdin is not None and not recording.stdin.closed:
            recording.stdin.write(b"q\n")
            recording.stdin.flush()

    errors.extend(stop_and_reap(
        recording, "recorder", graceful=stop_recording if recording is not None else None
    ))
    errors.extend(stop_and_reap(app, "renderer"))
    for label, handle in log_handles:
        if handle is None:
            continue
        try:
            handle.close()
        except Exception as error:
            errors.append(f"closing {label} failed: {error}")
    return errors


def find_pid_window(app, timeout=20.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if app.poll() is not None:
            raise RuntimeError(f"Renderer exited ({app.returncode}) before its window appeared")
        remaining = deadline - time.monotonic()
        try:
            found = subprocess.run(
                ["xdotool", "search", "--onlyvisible", "--pid", str(app.pid)],
                check=False, capture_output=True, text=True,
                timeout=min(0.75, max(0.1, remaining)),
            )
        except subprocess.TimeoutExpired:
            found = None
        if found is not None and found.returncode == 0 and found.stdout.strip():
            return found.stdout.splitlines()[-1]
        time.sleep(min(0.025, max(0.0, deadline - time.monotonic())))
    raise RuntimeError(f"No visible PID-scoped window appeared within {timeout:.1f}s")


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path, default=Path("img/walkthrough-source"))
    parser.add_argument("--video", type=Path, default=Path("img/controls-walkthrough.mp4"))
    args = parser.parse_args()
    executable = args.executable.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    video = args.video.resolve()
    video.parent.mkdir(parents=True, exist_ok=True)
    report_path = output / "report.json"
    report_path.unlink(missing_ok=True)
    events, takes = [], []
    display = os.environ.get("DISPLAY", ":0")

    def xdo(*words):
        return run(["xdotool", *words], capture_output=True, text=True,
                   timeout=3.0).stdout.strip()

    for name, camera, duration, count in TAKE_SPECS:
        command = [str(executable), "--preset", "final", "--camera", camera,
                   "--width", str(WIDTH), "--height", str(HEIGHT),
                   "--blades", str(count), "--validation", "on"]
        if name == "interaction":
            command += ["--sphere-motion", "sweep", "--wind-strength", "0"]

        log_path = output / f"{name}.log"
        capture_log_path = output / f"{name}-capture.log"
        progress_path = output / f"{name}-capture-progress.log"
        raw = output / f"{name}-raw.mp4"
        log = encoder_log = app = recording = None
        held_keys, held_buttons, captions = set(), set(), []
        geometry = handshake = None
        primary_error = primary_traceback = None

        try:
            log = log_path.open("w", encoding="utf-8")
            app_launch = time.monotonic()
            app = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            window = find_pid_window(app)

            # GLFW already owns this PID-scoped window. Hold Space before its first poll.
            xdo("windowfocus", "--sync", window)
            held_keys.add("space")
            xdo("keydown", "space")
            startup_down = {
                "event": "startup_space_keydown", "take": name, "input": "space",
                "action": "keydown", "captured_seconds": None,
                "seconds_after_app_launch": time.monotonic() - app_launch,
                "evidence": "xdotool keydown completed after PID-scoped window focus",
            }
            events.append(startup_down)

            xdo("windowmove", window, "0", "0")
            xdo("windowfocus", "--sync", window)
            geometry = dict(line.split("=", 1) for line in
                            xdo("getwindowgeometry", "--shell", window).splitlines())
            if (int(geometry["WIDTH"]), int(geometry["HEIGHT"])) != (WIDTH, HEIGHT):
                raise RuntimeError("Unexpected recording viewport")
            screen_ready = wait_for_screen_content(app, display, geometry)
            events.append({"event": "screen_content_ready", "take": name,
                           "captured_seconds": None, "evidence": screen_ready})

            progress_path.unlink(missing_ok=True)
            encoder_log = capture_log_path.open("w", encoding="utf-8")
            recording = subprocess.Popen(
                ["ffmpeg", "-hide_banner", "-loglevel", "warning", "-nostats",
                 "-stats_period", "0.05", "-y", "-f", "x11grab",
                 "-framerate", str(FPS), "-video_size", f"{WIDTH}x{HEIGHT}", "-i",
                 f"{display}+{geometry['X']},{geometry['Y']}", "-t", str(duration),
                 "-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency",
                 "-crf", "18",
                 "-pix_fmt", "yuv420p", "-progress", str(progress_path), str(raw)],
                stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=encoder_log,
            )
            recorder_launch = time.monotonic()
            recorder_ready = wait_for_recorder_ready(recording, app, progress_path)
            # Encoder progress can lag behind x11grab because of codec buffering. Use the
            # post-Popen anchor for media timing, but only after progress proves frame >= 1.
            started = recorder_launch

            def elapsed():
                return time.monotonic() - started

            events.append({
                "event": "recorder_ready", "take": name,
                "captured_seconds": max(
                    0.0, recorder_ready["observed_monotonic"] - started),
                "progress_frame": recorder_ready["frame"],
                "progress_out_time": recorder_ready["out_time"],
                "origin_method": "post-Popen monotonic anchor confirmed by FFmpeg frame >= 1",
                "evidence": "FFmpeg progress reported at least one captured frame",
            })
            xdo("keyup", "space")
            held_keys.discard("space")
            startup_up = {
                "event": "startup_space_keyup", "take": name, "input": "space",
                "action": "keyup", "captured_seconds": max(0.0, elapsed()),
                "evidence": "xdotool keyup completed after screen and recorder readiness",
            }
            events.append(startup_up)
            resume_at = max(0.8, startup_up["captured_seconds"] + 0.10)
            if resume_at > 1.10:
                raise RuntimeError(
                    f"Recorder readiness left no timely startup release/resume gap: {resume_at:.3f}s"
                )
            handshake = {
                "window": window, "keydown": startup_down,
                "screen_readiness": screen_ready,
                "recorder_readiness": {key: value for key, value in recorder_ready.items()
                                       if key != "observed_monotonic"},
                "keyup": startup_up,
                "resume_target_seconds": resume_at,
                "runtime_pause_state": "not instrumented; X11 ordering is evidenced",
            }

            def ensure_live(context):
                app_status, recorder_status = app.poll(), recording.poll()
                if app_status is not None:
                    raise RuntimeError(f"{name} renderer exited ({app_status}) {context}")
                if recorder_status is not None:
                    raise RuntimeError(f"{name} recorder exited ({recorder_status}) {context}")

            def until(target):
                deadline = time.monotonic() + max(2.0, target - elapsed() + 2.0)
                while elapsed() < target:
                    ensure_live(f"while waiting for captured second {target}")
                    if time.monotonic() >= deadline:
                        raise RuntimeError(f"Timed out waiting for captured second {target}")
                    time.sleep(max(0.0, min(0.05, target - elapsed())))

            def caption(text, seconds=2.5, end=None):
                begin = max(0.0, elapsed())
                finish = min(duration, end if end is not None else begin + seconds)
                if captions and captions[-1][1] > begin:
                    prior = captions[-1]
                    captions[-1] = (prior[0], begin, prior[2])
                captions.append((begin, max(begin, finish), text))

            def press_key(symbol):
                held_keys.add(symbol)  # Track before xdotool in case it partially succeeds.
                xdo("keydown", symbol)

            def release_key(symbol):
                xdo("keyup", symbol)
                held_keys.discard(symbol)

            def key(symbol, seconds=0.12, text=None, caption_end=None):
                begin = max(0.0, elapsed())
                press_key(symbol)
                caption(text or symbol, max(2.0, seconds + 0.5), end=caption_end)
                time.sleep(seconds)
                release_key(symbol)
                events.append({"event": "input", "take": name, "seconds": begin,
                               "input": symbol, "action": "press", "hold": seconds})

            def drag(button, dx, dy, text):
                x = int(geometry["X"]) + WIDTH // 2
                y = int(geometry["Y"]) + HEIGHT // 2
                xdo("mousemove", "--sync", str(x), str(y))
                begin = max(0.0, elapsed())
                caption(text, 3.0)
                held_buttons.add(button)  # Track before xdotool for partial failures.
                xdo("mousedown", str(button))
                for step in range(1, 21):
                    ensure_live("during mouse drag")
                    xdo("mousemove", str(x + dx * step // 20), str(y + dy * step // 20))
                    time.sleep(0.025)
                xdo("mouseup", str(button))
                held_buttons.discard(button)
                events.append({"event": "input", "take": name, "seconds": begin,
                               "input": f"mouse{button}", "action": "drag",
                               "dx": dx, "dy": dy})

            # This caption states only observable input ordering; the runtime pause state is
            # intentionally not claimed. A short key-up gap guarantees a separate resume edge.
            captions.append((0.0, resume_at, "Startup Space press/release: captured baseline"))
            until(resume_at)
            if name == "interaction":
                key("space", text="Space: resume sweep | baseline, contact, elastic recovery",
                    caption_end=10.0)
                until(10)
                drag(1, 60, -15, "Left mouse drag: orbit camera")
                until(12)
                drag(3, 0, -18, "Right mouse drag: zoom camera")
                for target, symbol, text in [
                    (14, "w", "W: sphere forward"), (17, "s", "S: sphere backward"),
                    (20, "a", "A: sphere left"), (23, "d", "D: sphere right"),
                    (26, "e", "E: raise sphere"),
                    (29, "q", "Q: lower sphere to the ground")]:
                    until(target)
                    key(symbol, 0.22 if symbol in {"q", "e"} else 0.55, text)
                for target, symbol, text in [
                    (32, "Up", "Up: increase wind"),
                    (34, "Right", "Right: rotate wind"),
                    (36, "Left", "Left: rotate wind"),
                    (38, "Down", "Down: decrease wind")]:
                    until(target)
                    key(symbol, 0.55, text)
                until(40); key("space", text="Space: pause simulation")
                until(42); key("space", text="Space: resume simulation")
                until(44); key("r", text="R: reset grass")
                until(46); key("h", text="H: hide controls overlay")
                until(47); key("h", text="H: show controls overlay")
            elif name == "overview":
                key("space", text="Space: resume simulation | Overview: visibility tests and geometry detail",
                    caption_end=4.0)
                for target, symbol, text in [
                    (4, "1", "1: orientation culling"),
                    (10, "2", "2: frustum culling"),
                    (16, "3", "3: distance culling")]:
                    until(target); key(symbol, text=text + " off")
                    until(target + 3); key(symbol, text=text + " on")
                until(22); key("f", text="F: show tessellated wireframe")
                until(25); key("l", text="L: fixed tessellation")
                until(28); key("l", text="L: adaptive tessellation")
                until(31); key("f", text="F: return to shaded rendering")
                until(33); key("space", text="Freeze identical geometry for shading comparison")
                until(34); key("b", text="B: basic shading")
                until(37); key("b", text="B: enhanced afternoon shading")
            else:
                key("space", text="Space: resume simulation | Occlusion: identical scene, fewer submitted blades",
                    caption_end=5.0)
                until(5); key("4", text="4: occlusion disabled - inspect visible blade count")
                until(11); key("4", text="4: occlusion enabled - image preserved")
                until(17); key("4", text="4: disable occlusion again")
                until(21); key("4", text="4: restore occlusion")
                until(25); caption("Didrik Wiig-Andersen | CIS 565 Project 5", 4)
                until(29); caption(
                    "github.com/didrikwiigandersen | Source and build instructions in README", 3)

            until(duration - 0.35)
            key("Escape", text="Escape: exit renderer", seconds=0.08)
            app.wait(timeout=5)
            recording.wait(timeout=10)
            if app.returncode != 0 or recording.returncode != 0:
                raise RuntimeError(
                    f"Failed take {name}: app={app.returncode}, recording={recording.returncode}")
        except BaseException as error:
            primary_error, primary_traceback = error, error.__traceback__
        finally:
            cleanup_errors = cleanup_take(
                held_keys, held_buttons, xdo, app, recording,
                [("renderer log", log), ("encoder log", encoder_log)])

        if primary_error is not None:
            if cleanup_errors:
                message = "; ".join(cleanup_errors)
                if hasattr(primary_error, "add_note"):
                    primary_error.add_note("Cleanup failures: " + message)
                print("Cleanup failures after primary error: " + message, file=sys.stderr)
            raise primary_error.with_traceback(primary_traceback)
        if cleanup_errors:
            raise RuntimeError("Take cleanup failed: " + "; ".join(cleanup_errors))

        diagnostic = log_path.read_text(encoding="utf-8", errors="replace")
        validation = inspect_validation_log(diagnostic)
        if not validation["clean"]:
            evidence = validation["error_evidence"] + validation["warning_evidence"]
            raise RuntimeError(
                f"Validation issue in {name}: errors={validation['matched_error_line_count']}, "
                f"warnings={validation['matched_warning_line_count']}, evidence={evidence}")

        filters = []
        for index, (begin, end, label) in enumerate(captions):
            label_file = output / f"{name}-caption-{index}.txt"
            label_file.write_text(label, encoding="utf-8")
            filters.append(
                f"drawtext=textfile='{label_file}':fontcolor=white:fontsize=21:"
                f"box=1:boxcolor=black@0.75:boxborderw=10:x=20:y=h-50:"
                f"enable='between(t,{begin:.3f},{end:.3f})'")
        filter_file = output / f"{name}-filters.txt"
        filter_file.write_text(",".join(filters), encoding="utf-8")
        annotated = output / f"{name}.mp4"
        run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", raw,
             "-filter_script:v", filter_file, "-c:v", "libx264", "-preset", "veryfast",
             "-crf", "22", "-maxrate", "3M", "-bufsize", "6M",
             "-pix_fmt", "yuv420p", annotated], timeout=240)
        takes.append({
            "name": name, "command": command, "duration": duration,
            "video": str(annotated), "application_exit": app.returncode,
            "recorder_exit": recording.returncode, "startup_handshake": handshake,
            "validation": validation,
        })

    listing = output / "takes.txt"
    listing.write_text(
        "".join(f"file '{take['video']}'\n" for take in takes), encoding="utf-8")
    run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "concat",
         "-safe", "0", "-i", listing, "-c", "copy", "-movflags", "+faststart", video])

    expected = {"w", "a", "s", "d", "q", "e", "Up", "Down", "Left", "Right",
                "1", "2", "3", "4", "l", "f", "h", "b", "space", "r", "Escape",
                "mouse1", "mouse3"}
    exercised = {event["input"] for event in events
                 if event.get("event") == "input" and "input" in event}
    missing = sorted(expected - exercised)
    if missing:
        raise RuntimeError("Walkthrough did not exercise: " + ", ".join(missing))

    probe_result = run(
        ["ffprobe", "-v", "error", "-count_frames", "-show_streams",
         "-show_format", "-of", "json", video],
        capture_output=True, text=True, timeout=30)
    probe = json.loads(probe_result.stdout)
    byte_size = video.stat().st_size
    probe_validation = validate_final_probe(probe, byte_size)
    validation_summary = {
        "all_take_logs_clean": all(take["validation"]["clean"] for take in takes),
        "matched_error_line_count": sum(
            take["validation"]["matched_error_line_count"] for take in takes),
        "matched_warning_line_count": sum(
            take["validation"]["matched_warning_line_count"] for take in takes),
        "reported_summaries": [
            {"take": take["name"], **summary}
            for take in takes for summary in take["validation"]["reported_summaries"]],
        "reported_error_counts": [
            {"take": take["name"], "count": count}
            for take in takes for count in take["validation"]["reported_error_counts"]],
        "reported_warning_counts": [
            {"take": take["name"], "count": count}
            for take in takes for count in take["validation"]["reported_warning_counts"]],
        "note": "reported_summaries lists only counters actually emitted by renderer logs",
    }
    report = {
        "takes": takes, "events": events, "probe": probe,
        "probe_validation": probe_validation,
        "validation_summary": validation_summary,
        "bytes": byte_size, "sha256": sha256_file(video),
        "all_inputs_exercised": True,
    }
    temporary_report = output / "report.json.tmp"
    temporary_report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    temporary_report.replace(report_path)
    print(f"Recorded all controls: {video}")


if __name__ == "__main__":
    main()
