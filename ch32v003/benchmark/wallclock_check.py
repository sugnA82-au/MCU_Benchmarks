#!/usr/bin/env python3
"""Compare the chip's own timing against the host's wall clock.

The firmware prints '@BEGIN <name>' immediately before a timed region and
'@END <name> us=<n>' immediately after it. capture.py records when each marker
line arrived on the host. The host-side interval from the end of the BEGIN line
to the start of the END line is wall-clock time for the same region, measured
by a clock that does not depend on the chip at all.

    wallclock_check.py serial_log.timing.json
"""
import json
import re
import sys
from pathlib import Path

markers = json.loads(Path(sys.argv[1]).read_text())["markers"]
begins = {}
rows = []
for m in markers:
    b = re.fullmatch(r"@BEGIN (\w+)", m["line"])
    e = re.fullmatch(r"@END (\w+) us=(\d+)", m["line"])
    if b:
        begins[b.group(1)] = m["newline_s"]
    elif e and e.group(1) in begins:
        host = m["first_byte_s"] - begins[e.group(1)]
        chip = int(e.group(2)) / 1e6
        rows.append((e.group(1), chip, host, (chip - host) / host * 100))

print("%-10s %12s %12s %9s" % ("region", "chip (s)", "host (s)", "diff"))
for name, chip, host, diff in rows:
    print("%-10s %12.4f %12.4f %+8.3f%%" % (name, chip, host, diff))
if rows:
    print("worst |diff| = %.3f%%" % max(abs(r[3]) for r in rows))
