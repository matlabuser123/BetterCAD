"""Counts the qualification-gate test groups from the Catch2 registry."""
import re, sys, xml.etree.ElementTree as ET
cases = {}
for c in ET.parse(sys.argv[1]).getroot().iter("TestCase"):
    tags = set(re.findall(r"\[([^\]]+)\]", c.findtext("Tags") or ""))
    path = (c.find("SourceInfo").findtext("File") or "").replace("\\", "/")
    cases[c.findtext("Name")] = (tags, path)
GROUPS = [
    ("failure paths / diagnostics", lambda t, p: bool({"failure", "errors", "diagnostics"} & t)
        or "Refuses" in p or False),
    ("persistence (io round trip)", lambda t, p: "io" in t),
    ("determinism", lambda t, p: "determinism" in t),
    ("undo / redo", lambda t, p: bool({"undo", "history", "commands"} & t)),
    ("STEP", lambda t, p: "step" in t),
    ("STL", lambda t, p: "stl" in t),
    ("CLI (in-process)", lambda t, p: "cli" in t),
    ("validation", lambda t, p: "validation" in t),
    ("acceptance", lambda t, p: "acceptance" in t),
    ("reference models", lambda t, p: "reference" in t),
]
for label, match in GROUPS:
    n = sum(1 for k, (t, p) in cases.items() if match(t, k))
    print(f"  {label:32} {n}")
# Failure-path cases by NAME pattern, which is how they are written here.
fail = sorted(k for k in cases if re.search(r"Refuses|Fails|Rejects|Invalid|Reports|Cannot|Bad|Unsafe|Degenerate|Cycle", k))
print(f"\n  failure-path cases by name       {len(fail)}")
