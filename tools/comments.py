#!/usr/bin/env python3
"""No paragraphs in src/.

Code is what a reader should read. A comment earns its place by saying
something the code cannot — a platform bug, an ordering that looks
arbitrary and is not, a unit that is not in a name — and that fits in a
line or two. Anything longer is documentation, and documentation goes
in docs/ where it can be found.
"""

import pathlib
import re
import sys

LIMIT = 3


def runs(path):
    out = []
    run, start = 0, 0
    for n, line in enumerate(path.read_text().split("\n"), 1):
        if re.match(r"\s*//", line):
            if run == 0:
                start = n
            run += 1
        else:
            if run > LIMIT:
                out.append((start, run))
            run = 0
    if run > LIMIT:
        out.append((start, run))
    return out


def main():
    roots = sys.argv[1:] or ["src"]
    bad = 0
    for root in roots:
        for f in sorted(pathlib.Path(root).rglob("*")):
            if f.suffix not in (".h", ".cpp") or "bytecode" in f.parts:
                continue
            for start, run in runs(f):
                print("%s:%d: %d comment lines in a row" % (f, start, run))
                bad += 1
    if bad:
        print("%d paragraphs; the limit is %d lines" % (bad, LIMIT))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
