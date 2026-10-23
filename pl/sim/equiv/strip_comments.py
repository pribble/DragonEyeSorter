#!/usr/bin/env python3
"""Strip Verilog comments (byte level) from stdin to stdout.

Used by run_equiv.sh for golden sources that have vendor comment pathologies
(e.g. ui_isp/demosaic top_demosaic.v has `/*//input` opening a block comment that
iverilog rejects as "possible nested comment"; TD itself accepts it). Comments never
affect simulation behaviour, so stripping them keeps the golden build semantically
identical. Synthesis attributes like `//synthesis keep` are irrelevant to iverilog.

Block comments are not nested: the first `*/` closes, exactly as a lenient compiler
treats the vendor file. Newlines inside block comments are preserved (line numbers).
"""
import sys

data = sys.stdin.buffer.read()
out = bytearray()
i = 0
n = len(data)
state = 0  # 0 normal, 1 line comment, 2 block comment
while i < n:
    c = data[i]
    if state == 0:
        if c == 0x2F and i + 1 < n and data[i + 1] == 0x2F:      # //
            state = 1
            i += 2
        elif c == 0x2F and i + 1 < n and data[i + 1] == 0x2A:    # /*
            state = 2
            i += 2
        else:
            out.append(c)
            i += 1
    elif state == 1:
        if c == 0x0A:                                            # newline ends //
            out.append(c)
            state = 0
        i += 1
    else:
        if c == 0x2A and i + 1 < n and data[i + 1] == 0x2F:      # */
            state = 0
            i += 2
        else:
            if c == 0x0A:
                out.append(c)
            i += 1

sys.stdout.buffer.write(bytes(out))
