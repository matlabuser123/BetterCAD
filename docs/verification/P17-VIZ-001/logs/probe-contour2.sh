#!/bin/bash
# Mutation probes for the CONTOUR display and the stale-contour fix -- the code
# that was still unprobed when a qualification was launched for the third time
# too early.
#
# Line-ending agnostic: the OCCT adapter sources are CRLF while the renderer
# sources are LF, and a multi-line anchor written with \n matches nothing in a
# CRLF file while LOOKING exactly like a stale anchor.
set -u
CAD=/c/Users/uqhas/OneDrive/Desktop/CAD
SP=/c/Users/uqhas/AppData/Local/Temp/claude/C--Users-uqhas-OneDrive-Desktop-CAD/3f122dc2-e3e9-4e92-bb25-f10e04d7c1e9/scratchpad/vizmut
export BETTERCAD_BUILD_ROOT=C:/Users/uqhas/AppData/Local/bc-build
export CMAKE_BUILD_PARALLEL_LEVEL=6
CC="$CAD/src/renderer/StructuralContour.cpp"
MP="$CAD/src/renderer/occt/OcctMeshPresentation.cpp"
VW="$CAD/src/renderer/occt/OcctViewer.cpp"
FILTER='StructuralViz_|unit\.MeshDisplay'
cd "$CAD" || exit 1

restore() {
  cp "$SP/c2.cc.pristine" "$CC"
  cp "$SP/c2.mp.pristine" "$MP"
  cp "$SP/c2.vw.pristine" "$VW"
}

probe() {
  local name="$1"; shift
  local target="$1"; shift
  local old="$1"; shift
  local new="$1"; shift
  restore
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

echo "######## the corner mapping ########"
date

probe "E1  a nodal channel goes per FACET " "$CC" \
  '        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const std::size_t vertex = indices[(triangle * 3U) + corner];' \
  '        for (std::size_t corner = 0U; corner < 3U; ++corner) {
            const std::size_t vertex = indices[triangle * 3U];'

probe "E2  an element channel interpolates" "$CC" \
  '            out.push_back(entityBands[at]);
            out.push_back(entityBands[at]);
            out.push_back(entityBands[at]);' \
  '            out.push_back(entityBands[at]);
            out.push_back(entityBands[(at + 1U) % elements.size()]);
            out.push_back(entityBands[at]);'

probe "E3  the ascending check removed   " "$CC" \
  '    if (nodal ? !std::ranges::is_sorted(nodes) : !std::ranges::is_sorted(elements)) {' \
  '    if (false) {'

probe "E4  a missing entity takes band 0 " "$CC" \
  '            const std::size_t at = indexOf(nodes, *node);
            if (at >= nodes.size()) {' \
  '            std::size_t at = indexOf(nodes, *node);
            if (at >= nodes.size()) { at = 0U; }
            if (false) {'

probe "E5  the parallel-length check gone" "$CC" \
  '    if (entityBands.size() != field.values.size()) {' \
  '    if (false) {'

probe "E6  colours read the wrong band   " "$CC" \
  '        out.push_back(legend.bands[band].red);
        out.push_back(legend.bands[band].green);
        out.push_back(legend.bands[band].blue);' \
  '        out.push_back(legend.bands[band].green);
        out.push_back(legend.bands[band].red);
        out.push_back(legend.bands[band].blue);'

probe "E7  the composition skips banding " "$CC" \
  '    const Result<std::vector<std::size_t>> bands = contourBandsOf(field, legend);
    if (!bands.has_value()) {
        return std::unexpected(bands.error());
    }' \
  '    const Result<std::vector<std::size_t>> bands = contourBandsOf(field, legend);
    if (false) {
        return std::unexpected(bands.error());
    }'

echo "######## the presentation and the entry point ########"
date

probe "E8  the STALE contour is drawn    " "$MP" \
  '        if (const Handle(Graphic3d_ArrayOfTriangles) array = facets(plain, !stale_);' \
  '        if (const Handle(Graphic3d_ArrayOfTriangles) array = facets(plain, true);'

probe "E9  the HIGHLIGHT takes band colour" "$MP" \
  '        if (const Handle(Graphic3d_ArrayOfTriangles) array = facets(highlighted_, false);' \
  '        if (const Handle(Graphic3d_ArrayOfTriangles) array = facets(highlighted_, true);'

probe "E10 the VertexColor flag dropped  " "$MP" \
  '        coloured ? (Graphic3d_ArrayFlags_VertexNormal | Graphic3d_ArrayFlags_VertexColor)
                 : Graphic3d_ArrayFlags_VertexNormal;' \
  '        Graphic3d_ArrayFlags_VertexNormal;'

probe "E11 setFacetColours never stores  " "$MP" \
  '        facetColours_.assign(rgb.begin(), rgb.end());' \
  '        facetColours_.clear();'

probe "E12 the contour length guard gone " "$VW" \
  '    if (rgb.size() != expected) {' \
  '    if (false) {'

probe "E13 an out-of-range component ok  " "$VW" \
  '        if (!std::isfinite(rgb[i]) || rgb[i] < 0.0 || rgb[i] > 1.0) {' \
  '        if (false) {'

probe "E14 clearMeshContour does nothing " "$VW" \
  '        (*mesh)->setFacetColours({});' \
  '        (void)mesh;'

echo "######## RESTORE ########"
restore
cmake --build --preset debug-ext --target bettercad_tests 2>&1 | tail -2
sha1sum "$CC" "$MP" "$VW"
(cd /c/Users/uqhas/AppData/Local/bc-build/debug-ext && ctest -R "$FILTER" -j 8 2>&1 | tail -3)
echo "######## ALL DONE ########"
date
