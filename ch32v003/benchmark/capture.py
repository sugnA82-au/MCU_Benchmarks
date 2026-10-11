#!/usr/bin/env python3
"""Reset the target and capture its debug-channel console to a file.

Runs minichlink in terminal mode (-b reboots the target first, -T attaches the
WCH-Link debug console on SWIO) and saves the raw output. It also writes
<output>.timing.json: the host arrival time of every '@' marker line, which
wallclock_check.py uses to cross-check the chip's timebase against the host's
clock.

    capture.py --output serial_log.txt --done SUITE_DONE
"""
import argparse
import bisect
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import time

DEFAULT_TOOL = Path.home() / ".platformio/packages/tool-minichlink/minichlink"

parser = argparse.ArgumentParser()
parser.add_argument("--tool", default=str(DEFAULT_TOOL), help="path to minichlink")
parser.add_argument("--output", required=True)
parser.add_argument("--timeout", type=float, default=400)
parser.add_argument("--done", default="SUITE_DONE", help="marker that ends the capture")
args = parser.parse_args()

output = Path(args.output)
started = time.monotonic()
data = bytearray()
chunk_offsets, chunk_times = [], []        # in memory only: arrival time per chunk
success = False

proc = subprocess.Popen([args.tool, "-b", "-T"], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, bufsize=0)
selector = selectors.DefaultSelector()
selector.register(proc.stdout, selectors.EVENT_READ)


def arrival(offset):
    """Host time (s) at which the byte at `offset` arrived."""
    return chunk_times[bisect.bisect_right(chunk_offsets, offset) - 1]


try:
    with output.open("wb") as log:
        while time.monotonic() - started < args.timeout:
            if not selector.select(0.2):
                if proc.poll() is not None:
                    break
                continue
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            chunk_offsets.append(len(data))
            chunk_times.append(time.monotonic() - started)
            data.extend(chunk)
            log.write(chunk)
            log.flush()
            print(chunk.decode("utf-8", errors="replace"), end="", flush=True)
            if args.done.encode() in data and data.endswith(b"\n"):
                success = True                  # marker seen and its line is complete
                break
finally:
    if proc.poll() is None:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    selector.close()
    markers, pos = [], 0
    for raw in bytes(data).split(b"\n")[:-1]:
        end = pos + len(raw)                      # offset of this line's newline
        if raw.startswith(b"@"):
            markers.append({"line": raw.decode(), "first_byte_s": arrival(pos),
                            "newline_s": arrival(end)})
        pos = end + 1
    output.with_suffix(".timing.json").write_text(json.dumps({
        "tool": "minichlink -b -T",
        "host_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "complete": success, "markers": markers}, indent=2) + "\n")
if not success:
    raise SystemExit("done marker %r never seen" % args.done)
