#!/usr/bin/env python3
"""A control bound to a value nothing else reads does nothing.

An example is a showcase, and a slider that moves while the picture
does not is worse than no slider: a reader who tries it learns that
the controls here are decoration. This is mechanical rather than
semantic — a bound name occurring only at its declaration and at its
control is read nowhere, which is exactly that mistake.
"""

import pathlib
import re
import sys

BINDERS = "Slider|Checkbox|IconToggle|Choice|Input|Drag|Color"
BOUND = re.compile(
    r'\.(' + BINDERS + r')\(\s*(?:sv::Icon::\w+\s*,\s*)?"[^"]*"\s*,\s*'
    r'([A-Za-z_]\w*)')


LITERAL = re.compile(r'"(?:[^"\\]|\\.)*"')


def dead(path):
    src = path.read_text()
    # Counted with the string literals blanked: a control is usually
    # LABELLED with the name of the thing it moves, and counting the
    # label as a use is how this check first passed a dead slider.
    bare = LITERAL.sub('""', src)
    out = []
    for m in BOUND.finditer(src):
        name = m.group(2)
        if len(re.findall(r'\b%s\b' % re.escape(name), bare)) <= 2:
            out.append((src[:m.start()].count("\n") + 1, name, m.group(1)))
    return out


def main():
    roots = sys.argv[1:] or ["examples"]
    bad = 0
    for root in roots:
        for f in sorted(pathlib.Path(root).rglob("*.cpp")):
            for line, name, kind in dead(f):
                print("%s:%d: %s is bound by %s and read nowhere" %
                      (f, line, name, kind))
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
