"""Compares measured-values files (values.py output) of different builds.

    python compare-values.py <label>=<values file> <label>=<values file> ...

Every file is compared with the first. Before comparing, lines holding a
pointer value (0x0000...) are dropped, and the parts that differ by design
between runs are normalized: temporary directory names
(bettercad-test-<uuid>) and document UUIDs. The files must then have the
same lines in the same order; each differing line is printed and classified:

  expected only   an "X is within T of Y" or "X and Y are within" entry
                  whose measured value X is identical and whose expected
                  value Y (computed in the test) differs;
  measured        anything else.

Exits non-zero if any file differs in a measured value or in its lines.
"""
import hashlib
import re
import sys

POINTER = "0x0000"
TEMP = re.compile(r"bettercad-test-[0-9a-f-]+")
UUID = re.compile(r"\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b")
WITHIN = re.compile(r"^\s*=> (\S+) is within (\S+) of (\S+)(.*)$")
RELATIVE = re.compile(r"^\s*=> (\S+) and (\S+) are within (.*)$")


def load(path):
    with open(path, encoding="utf-8-sig") as file:
        lines = [line.rstrip("\r\n") for line in file]
    kept = []
    for line in lines:
        if POINTER in line:
            continue
        kept.append(UUID.sub("<uuid>", TEMP.sub("bettercad-test-*", line)))
    return kept


def measured(line):
    """The measured value of a tolerance check, or None."""
    for pattern in (WITHIN, RELATIVE):
        match = pattern.match(line)
        if match:
            return match.group(1)
    return None


def main(argv):
    runs = []
    for arg in argv:
        label, _, path = arg.partition("=")
        runs.append((label, load(path)))
    ok = True
    for label, lines in runs:
        digest = hashlib.md5("\n".join(lines).encode("utf-8")).hexdigest()
        print(f"{label}: {sum(1 for x in lines if x.strip())} non-empty lines after normalizing, MD5 {digest}")
    base_label, base = runs[0]
    for label, lines in runs[1:]:
        print(f"\n{label} against {base_label}:")
        if len(lines) != len(base):
            print(f"  different line counts: {len(lines)} and {len(base)}")
            ok = False
            continue
        expected_only = 0
        other = 0
        for number, (a, b) in enumerate(zip(base, lines), start=1):
            if a == b:
                continue
            if measured(a) is not None and measured(a) == measured(b):
                expected_only += 1
                kind = "expected only"
            else:
                other += 1
                kind = "MEASURED"
            print(f"  line {number} ({kind}):")
            print(f"    {base_label}: {a.strip()}")
            print(f"    {label}: {b.strip()}")
        print(f"  differing lines: {expected_only} expected only, {other} measured")
        ok = ok and other == 0
    print("\nRESULT:", "measured values identical" if ok else "MEASURED VALUES DIFFER")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
