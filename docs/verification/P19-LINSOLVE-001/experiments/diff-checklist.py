import io, hashlib

S = r"C:/Users/Hasib/AppData/Local/Temp/claude/8b17a8f29dfea518/527dbd1e-d40d-42f7-8715-a186b5519185/scratchpad"
todo = io.open(r"C:/Users/Hasib/Desktop/CAD/TODO.md", encoding="utf-8").read()
pasted = io.open(S + "/pasted_linsolve.txt", encoding="utf-8").read()

lines = todo.split("\n")
# the section as TODO.md holds it: the --- before the heading through the --- after
start = next(i for i, L in enumerate(lines) if L.rstrip() == "# P19-LINSOLVE-001") - 1
assert lines[start].rstrip() == "---", "line before the heading is %r" % lines[start]
end = next(i for i in range(start + 1, len(lines)) if lines[i].rstrip() == "---")
own = "\n".join(L.rstrip("\r") for L in lines[start:end + 1]) + "\n"

pas = "\n".join(L.rstrip("\r") for L in pasted.split("\n"))
if pas.endswith("\n\n"):
    pas = pas[:-1]

print("TODO.md section  lines %d..%d  (%d lines)" % (start + 1, end + 1, end - start + 1))
print("pasted           %d lines" % (len(pas.rstrip('\n').split('\n'))))
print()

a = own.rstrip("\n").split("\n")
b = pas.rstrip("\n").split("\n")
print("line counts      TODO %d   pasted %d   %s"
      % (len(a), len(b), "EQUAL" if len(a) == len(b) else "*** DIFFER ***"))

bad = 0
for i in range(max(len(a), len(b))):
    x = a[i] if i < len(a) else "<missing>"
    y = b[i] if i < len(b) else "<missing>"
    if x != y:
        bad += 1
        print("  line %2d DIFFERS" % (i + 1))
        print("    TODO   %r" % x)
        print("    pasted %r" % y)
print("differing lines  %d" % bad)
print()

ha = hashlib.sha256(own.replace("\r\n", "\n").encode("utf-8")).hexdigest()
hb = hashlib.sha256(pas.replace("\r\n", "\n").encode("utf-8")).hexdigest()
print("sha256 TODO      %s" % ha[:16])
print("sha256 pasted    %s" % hb[:16])
print("IDENTICAL        %s" % (ha == hb))
print()
ticked = sum(1 for L in a if L.startswith("* [x]"))
unticked = sum(1 for L in a if L.startswith("* [ ]"))
print("boxes in the authoritative section: %d unticked, %d ticked" % (unticked, ticked))
assert ticked == 0, "a box is already ticked in the authorized section"
print("items:")
for L in a:
    if L.startswith("* ["):
        print("   ", L)
