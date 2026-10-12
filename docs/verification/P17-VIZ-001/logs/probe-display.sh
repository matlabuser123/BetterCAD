#!/bin/bash
# Mutation probes for the deformed display (ADR-043). Same protocol as the
# other batches: hash-verified pristine copies, one verified literal
# substitution that dies on a missing or ambiguous anchor, rebuild, run the
# filter, restore -- and the run ENDS with a restore build.
set -u
CAD=/c/Users/uqhas/OneDrive/Desktop/CAD
SP=/c/Users/uqhas/AppData/Local/Temp/claude/C--Users-uqhas-OneDrive-Desktop-CAD/3f122dc2-e3e9-4e92-bb25-f10e04d7c1e9/scratchpad/vizmut
export BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build
export CMAKE_BUILD_PARALLEL_LEVEL=6
MP="$CAD/src/renderer/occt/OcctMeshPresentation.cpp"
VW="$CAD/src/renderer/occt/OcctViewer.cpp"
# The whole StructuralViz set AND P16's own display tests: a mutation in a
# qualified P16 file must be caught by P16's tests too, and if it is not, that
# is worth knowing.
FILTER='StructuralViz_|unit\.MeshDisplay'
cd "$CAD" || exit 1

restore() {
  cp "$SP/mp.cpp.pristine" "$MP"
  cp "$SP/vw.cpp.pristine" "$VW"
}

probe() {
  local name="$1"; shift
  local target="$1"; shift
  local old="$1"; shift
  local new="$1"; shift
  restore
  # LINE-ENDING AGNOSTIC, and it has to be: the OCCT adapter sources are CRLF
  # (OcctViewer.cpp carries 811 carriage returns) while the renderer sources
  # this milestone added are LF. A multi-line anchor written with \n matches
  # nothing in a CRLF file, and the failure looks exactly like a stale anchor.
  # So: try as written, then retry with the newlines converted, in BOTH the
  # anchor and the replacement so the file's convention is preserved.
  OLD="$old" NEW="$new" perl -0i -pe '
    my $o = $ENV{OLD}; my $n = $ENV{NEW};
    my $i = index($_, $o);
    if ($i < 0) {
      ($o = $ENV{OLD}) =~ s/\n/\r\n/g;
      ($n = $ENV{NEW}) =~ s/\n/\r\n/g;
      $i = index($_, $o);
    }
    die "ANCHOR NOT FOUND\n" if $i < 0;
    die "ANCHOR AMBIGUOUS\n" if index($_, $o, $i + 1) >= 0;
    substr($_, $i, length($o)) = $n;
  ' "$target" || { echo "$name  ANCHOR FAILED"; restore; return; }

  local build
  if ! build=$(cmake --build --preset debug-ext --target bettercad_tests 2>&1); then
    if echo "$build" | grep -qE "ld returned|truncated|Permission denied"; then
      echo "$name  VOID -- collided or failed link"
    else
      echo "$name  KILLED-BY-COMPILER (weak)"
    fi
    echo "$build" | grep -E "error:" | head -2 | sed 's/^/        /'
    restore
    return
  fi
  local out
  out=$(cd /c/Users/uqhas/AppData/Local/bc-build/debug-ext && ctest -R "$FILTER" -j 8 2>&1 | tail -40)
  if echo "$out" | grep -q "100% tests passed"; then
    echo "$name  *** SURVIVED ***"
  else
    echo "$name  KILLED  ($(echo "$out" | grep -oE '[0-9]+ tests failed out of [0-9]+' | head -1))"
    echo "$out" | grep -E "^[[:space:]]+[0-9]+ - " | head -3 | sed 's/^/        /'
  fi
  restore
}

echo "######## the deformed buffer ########"
date

probe "D1  the override span IGNORED    " "$MP" \
  '    const std::span<const double> positions = deformed.empty() ? view.positions() : deformed;' \
  '    (void)deformed;
    const std::span<const double> positions = view.positions();'

probe "D2  the override test INVERTED   " "$MP" \
  '    const std::span<const double> positions = deformed.empty() ? view.positions() : deformed;' \
  '    const std::span<const double> positions = deformed.empty() ? deformed : view.positions();'

probe "D3  the EDGES keep the old shape " "$MP" \
  '        edgeCorners_.push_back(toModelLength(positions[vertex * 3U]));
        edgeCorners_.push_back(toModelLength(positions[(vertex * 3U) + 1U]));
        edgeCorners_.push_back(toModelLength(positions[(vertex * 3U) + 2U]));' \
  '        edgeCorners_.push_back(toModelLength(view.positions()[vertex * 3U]));
        edgeCorners_.push_back(toModelLength(view.positions()[(vertex * 3U) + 1U]));
        edgeCorners_.push_back(toModelLength(view.positions()[(vertex * 3U) + 2U]));'

probe "D4  the millimetre conversion gone" "$MP" \
  '            corners_.push_back(toModelLength(corner[which][0]));' \
  '            corners_.push_back(corner[which][0]);'

echo "######## the entry point guards ########"
date

probe "D5  the length guard removed     " "$VW" \
  '    if (positions.size() != view.positions().size()) {' \
  '    if (false && positions.size() != view.positions().size()) {'

probe "D6  the empty guard removed      " "$VW" \
  '    if (positions.empty()) {
        return makeError(ErrorCode::FailedPrecondition,
                         "viewer: a deformed display needs a position buffer; use displayMesh for "
                         "the undeformed mesh");
    }' \
  '    if (false) {
        return makeError(ErrorCode::FailedPrecondition,
                         "viewer: a deformed display needs a position buffer; use displayMesh for "
                         "the undeformed mesh");
    }'

probe "D7  the guards back in the wrong order" "$VW" \
  '    if (positions.empty()) {' \
  '    if (positions.size() != view.positions().size() && positions.empty()) {'

probe "D8  displayMesh passes the span   " "$VW" \
  '    return displayMeshInternal(source, view, {}, style);' \
  '    return displayMeshInternal(source, view, view.positions(), style);'

echo "######## RESTORE ########"
restore
cmake --build --preset debug-ext --target bettercad_tests 2>&1 | tail -2
sha1sum "$MP" "$VW"
(cd /c/Users/uqhas/AppData/Local/bc-build/debug-ext && ctest -R "$FILTER" -j 8 2>&1 | tail -3)
echo "######## ALL DONE ########"
date
