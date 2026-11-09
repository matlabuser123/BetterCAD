"""P19-STEADY-001: does the pasted authorization match TODO.md's own section?

The scope of a milestone is TODO.md's checklist, not a paste of it.  Nine
milestones in this phase have established the practice of diffing the two
BEFORE any implementation, because a paste that silently differs -- an item
added, reworded or dropped -- would widen or narrow the scope invisibly.

Run from the repository root.  Exits non-zero on any difference.
"""
import hashlib
import io
import re
import sys

PASTED = "docs/verification/P19-STEADY-001/experiments/pasted-checklist.txt"
TODO = "TODO.md"
MILESTONE = "P19-STEADY-001"


def normalize(text):
    """CR-normalize only.  Nothing else: whitespace differences are differences."""
    return text.replace("\r\n", "\n").replace("\r", "\n")


pasted = normalize(io.open(PASTED, encoding="utf-8").read())
todo = normalize(io.open(TODO, encoding="utf-8").read())

# The pasted block runs from its leading '---' to its trailing '---'.  Find the
# same span in TODO.md: the '---' line before '# P19-STEADY-001' through the
# '---' line that ends the section.
start = todo.index("\n# " + MILESTONE + "\n")
sep = todo.rindex("\n---\n", 0, start + 1)
after = todo.index("\n---\n", start)
section = todo[sep + 1:after + len("\n---\n")]

print("pasted  : %d chars, %d lines" % (len(pasted), len(pasted.splitlines())))
print("TODO.md : %d chars, %d lines" % (len(section), len(section.splitlines())))
print()

ph = hashlib.sha256(pasted.encode()).hexdigest()
th = hashlib.sha256(section.encode()).hexdigest()
print("sha256 pasted  : %s" % ph)
print("sha256 TODO.md : %s" % th)
print("IDENTICAL      : %s" % ("YES" if ph == th else "NO"))
print()

if ph != th:
    import difflib
    print("--- differences (TODO.md is authoritative) ---")
    for line in difflib.unified_diff(section.splitlines(), pasted.splitlines(),
                                     "TODO.md", "pasted", lineterm="", n=1):
        print("  " + line)

# Count the items on both sides rather than assuming the hash covers it.
pi = re.findall(r"(?m)^\* \[([x ])\] (.+)$", pasted)
ti = re.findall(r"(?m)^\* \[([x ])\] (.+)$", section)
print("items pasted   : %d  (%d ticked, %d unticked)"
      % (len(pi), sum(1 for m, _ in pi if m == "x"), sum(1 for m, _ in pi if m == " ")))
print("items TODO.md  : %d  (%d ticked, %d unticked)"
      % (len(ti), sum(1 for m, _ in ti if m == "x"), sum(1 for m, _ in ti if m == " ")))

ok = ph == th and len(pi) == len(ti) == 19 and all(m == " " for m, _ in ti)
print()
print("VERDICT: %s" % ("the scope is TODO.md's checklist unaltered, 19 items, none ticked"
                       if ok else "*** MISMATCH -- STOP ***"))
sys.exit(0 if ok else 1)
