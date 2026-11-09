import io
import re
import sys

Q = r"C:/Users/Hasib/Desktop/CAD/docs/verification/P19-STEADY-001/qualification/"
SINGLE = ["debug-ext", "release-ext", "debug-shared-ext"]
EXPECT = 6903
bad = []


def check(label, got, want):
    ok = got == want
    print("    %-36s %-14s %s" % (label, got, "OK" if ok else "MISMATCH, expected %s" % (want,)))
    if not ok:
        bad.append("%s: got %s want %s" % (label, got, want))


def read(p):
    return io.open(Q + p, encoding="utf-8", errors="replace").read()


print("=" * 76)
print("THE CTEST SUMMARY LINE -- the exact wording a CLEAN run prints")
print("=" * 76)
print("""  ctest prints the failed clause ONLY when something failed:
      clean    100% tests passed out of 6903
      failed    99% tests passed, 1 tests failed out of 6903
  so the PRESENCE of "tests failed out of" is itself a failure marker, and the
  first reconciliation script looked for a clause that cannot exist in a pass.
""")
for p in SINGLE:
    s = read("ctest-%s.log" % p)
    m = re.search(r"^(\d+)% tests passed out of (\d+)$", s, re.M)
    check("%s summary" % p, m.group(0) if m else "ABSENT", "100%% tests passed out of %d" % EXPECT)
    check("%s  failed clause absent" % p, "tests failed out of" in s, False)

print()
print("=" * 76)
print("THE EIGHT IDENTITIES -- parsed per line, and the comparison PROVEN real")
print("=" * 76)
times = read("qualification-times.txt")
lines = times.splitlines()
blocks = []
for i, ln in enumerate(lines):
    if "qualified source trees" in ln:
        d = {}
        for ln2 in lines[i + 1:]:
            m = re.match(r"^\s+(\S+) ([0-9a-f]{40})\s*$", ln2)
            if not m:
                break
            d[m.group(1)] = m.group(2)
        blocks.append((ln.strip(), d))
check("identity blocks found", len(blocks), 2)
for label, d in blocks:
    check("paths in: %s" % label[:40], len(d), 8)

if len(blocks) == 2:
    first, last = blocks[0][1], blocks[1][1]
    check("the two readings are equal", first == last, True)
    # A dict comparison of two EMPTY dicts is also True, which is how the first
    # script reported OK on one path. Prove the comparison has content.
    check("  and both hold 8 paths, so that == is not vacuous",
          len(first) == 8 and len(last) == 8, True)
    FROZEN = {
        "apps": "fc398a3d2e524178cf38e8b667236379d8381086",
        "include": "588dd50bf9a82cb58b3a93ccdcfe752fcdf896df",
        "src": "9fce91ebc86a4095fcc24e821bb86a793d835650",
        "tests": "f3eb700ce67c512b15001b75471cba873a12cc55",
        "examples": "4da234ebf4779f7c16d5df0075f106f7e3d4c958",
        "cmake": "5a382115d7ed7775189d40b55e40f318eaae6cc7",
        "CMakeLists.txt": "3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd",
        "CMakePresets.json": "3229f0f89b236744d7d5c082bed47c28b718288e",
    }
    check("the harness reading == FREEZE.md's table", first == FROZEN, True)
    print()
    for k in ("apps", "include", "src", "tests", "examples", "cmake",
              "CMakeLists.txt", "CMakePresets.json"):
        mark = "OK" if first.get(k) == FROZEN[k] == last.get(k) else "MISMATCH"
        print("    %-18s %s  %s" % (k, first.get(k, "ABSENT"), mark))

print()
print("=" * 76)
if bad:
    print("MISMATCHES: %d" % len(bad))
    for b in bad:
        print("  %s" % b)
    sys.exit(1)
print("Every figure in this pass matches.")
