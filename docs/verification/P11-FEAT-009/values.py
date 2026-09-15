"""Turns Catch2's XML report (run with -s) into the measured-values evidence file.

Keeps every passed CHECK_THAT / REQUIRE_THAT (volumes, areas, bounds, distances,
messages) and every passed CHECK whose INFO context reports a measured error or
value, with the enclosing test case and section path. Expansions over 1000
characters (whole-file text) are left out.
"""
import sys
import xml.etree.ElementTree as ET

KEYWORDS = ("worst", "error", "meshed", "rounding", "largest", "det A", "expected", "A L", "lo[axis]")


def text(element, tag):
    child = element.find(tag)
    return " ".join((child.text or "").split()) if child is not None else ""


def walk(node, path, out, infos):
    for child in node:
        if child.tag == "Section":
            walk(child, path + [child.get("name")], out, [])
        elif child.tag == "Info":
            infos.append(" ".join((child.text or "").split()))
        elif child.tag == "Expression":
            kind = child.get("type", "")
            original = text(child, "Original")
            expanded = text(child, "Expanded")
            where = f"{child.get('filename', '').replace(chr(92), '/').split('/')[-1]}:{child.get('line')}"
            # A bare true/false expansion carries no measured value.
            keep = child.get("success") == "true" and expanded not in ("true", "false") and (
                kind.endswith("_THAT") or any(k in " ".join(infos) for k in KEYWORDS))
            if keep and len(expanded) <= 1000:
                entry = f"{where}  {kind}( {original} )\n        => {expanded}"
                if infos:
                    entry += " with message: " + "; ".join(infos)
                out.append((" / ".join(path), entry))
            infos.clear()


def main(xml_path, header_path):
    root = ET.parse(xml_path).getroot()
    lines = [open(header_path, encoding="utf-8").read().rstrip("\n"), ""]
    for case in root.iter("TestCase"):
        entries = []
        walk(case, [case.get("name")], entries, [])
        current = None
        for path, entry in entries:
            if path != current:
                lines.append("")
                lines.append(f"== {path}")
                current = path
            lines.append(entry)
    totals = root.find("OverallResults")
    lines.append("")
    lines.append(f"Overall: {totals.get('successes')} assertions passed, {totals.get('failures')} failed.")
    sys.stdout.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
