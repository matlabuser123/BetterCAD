"""Hashes the reference-model measured values of each preset's values file.

The whole-suite cross-preset comparison (values-comparison*.txt) shows a
handful of last-bit differences between optimization levels. This narrows the
question to the part that matters most -- the twelve production reference
models -- and asks whether those are bit-identical.

    python reference-model-hashes.py <release> <debug> <debug-shared>
"""
import hashlib
import re
import sys

CASE = re.compile(r"ReferenceModel_")


def digest(path):
    kept, keep = [], False
    with open(path, encoding="utf-8-sig", errors="replace") as file:
        for line in file:
            line = line.rstrip("\r\n")
            if line.startswith("== "):
                keep = bool(CASE.search(line[3:]))
            # Pointer values and temporary directory names are not results.
            if keep and line.strip() and "0x0000" not in line:
                kept.append(re.sub(r"bettercad-test-[0-9a-f-]+", "tmp", line))
    return len(kept), hashlib.md5("\n".join(kept).encode()).hexdigest()


def main(*paths):
    results = {}
    for path in paths:
        label = re.sub(r".*qual001-|-values\.txt$", "", path)
        results[label] = digest(path)
        print(f"  {label:13} {results[label][0]:6} lines  MD5 {results[label][1]}")
    same = len({h for _, h in results.values()}) == 1
    print()
    print(f"  reference-model measured values identical across all presets: {'YES' if same else 'NO'}")
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
