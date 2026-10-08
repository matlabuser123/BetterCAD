# Script mode (cmake -P): enforce BetterCAD's architecture rules on #include
# directives under include/, src/, apps/ and examples/ of SOURCE_DIR.
#
# The examples are checked like any client of the library: the reference
# models (P11-REF-001) must build their parts through BetterCAD's public
# APIs, never by reaching into Open CASCADE.
#
# Rules
#   1. Open CASCADE headers (*.hxx) may only be included from an "occt"
#      adapter directory under src/ (e.g. src/core/geometry/occt/).
#   2. Qt headers may only be included from apps/bettercad/ and src/renderer/.
#   3. Module layering: a module may include public headers of its own module
#      or of a lower layer only.
#   4. Public headers (include/) must use <bettercad/...> style includes only;
#      quoted includes would reach into private implementation directories.
#   5. A volume-meshing backend's headers may only be included from
#      src/meshing/<backend>/ (ADR-033). Rule 1 does not cover them: it keys on
#      the .hxx extension and every candidate backend ships .h headers.
#   6. The library (include/, src/) must not include anything under apps/.
#      Rules 2 and 3 miss it: rule 2 keys on Qt's header shape, and apps/ is
#      not a module so rule 3 has no layer to compare (ADR-035).
#   7. Eigen headers may only be included from src/, never from a public
#      header (ADR-039). Rule 1 does not cover them either: Eigen's entry
#      headers have no extension at all, so there is nothing to key on.
#
# Exits with an error listing every violation.
if(NOT DEFINED SOURCE_DIR OR NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "CheckLayering.cmake: SOURCE_DIR must name an existing directory")
endif()

# Layer of each module; lower layers must not depend on higher ones.
#
# NUMBERED IN TENS, so that adding a module never renumbers the table again.
# They were consecutive integers until P17-ARCH-001, and the strictly-lower
# rule meant every new module in the middle of the stack had to push the ones
# above it up: ADR-006 moved `io` from 3 to 4 for `assembly`, ADR-015 moved it
# from 4 to 5 for `drawing`, and a `structural` module that USES meshing needs
# a number between `meshing` 4 and `io` 5, where there is none. Two renumbers
# in two additions is a pattern, and the roadmap has four more phases
# (thermal, CFD, optimisation, semantic topology) that sit in the same band.
# So the numbers were respaced once (ADR-035) and the renumber ends here.
#
# The value is an ORDINAL WITH GAPS, not a distance from core. Nine free
# integers sit between any two neighbours; use one rather than shifting the
# table.
set(layer_core 0)
set(layer_sketch 10)
set(layer_features 20)
# assembly sits above features, which it uses, and below io, which must
# serialize it (ADR-006).
set(layer_assembly 30)
# drawing sits above assembly, whose solved placements an assembly drawing
# projects, and below io, which must serialize its objects (ADR-015).
# Projection and hidden-line removal are NOT here: they are kernel work and
# live in core/geometry behind the occt adapter.
set(layer_drawing 40)
# meshing sits above features, whose regeneration and face-name resolution it
# uses, and below io, which must serialize its controls. It SHARES layer 40
# with drawing, which is allowed (renderer and scripting share 70): the two are
# siblings, each deriving a secondary representation from the same geometry and
# neither using the other. Sharing also leaves assembly (30) reachable, so
# meshing an assembly occurrence later needs no renumbering. Volume meshing
# lives here; surface triangulation of a Body is kernel work and stays in
# core/geometry behind the occt adapter (ADR-033).
set(layer_meshing 40)
# structural sits above meshing, whose Tet4 volume mesh, geometry/mesh mapping
# and quality report it CONSUMES, and below io, which must serialize its
# analysis intent. It cannot share meshing's 40: the rule below is
# strictly-lower, so same-layer modules cannot include each other at all, and
# structural must include meshing. That is what forced the respace (ADR-035).
# P18's thermal module is expected to be a sibling here, at 50 if it
# neither uses nor is used by structural, and at 55 or 45 if a coupling says
# otherwise -- the gaps exist so that decision needs no renumber.
set(layer_structural 50)
set(layer_io 60)
set(layer_renderer 70)
set(layer_scripting 70)

set(qt_allowed_regex "^(apps/bettercad|src/renderer)/")
set(occt_allowed_regex "^src/(.+/)?occt/")

# A volume-meshing backend is a third dependency that needs containment, and
# rule 1 does NOT provide it: that rule recognises an OCCT header by its .hxx
# extension, and every candidate backend's entry header is a .h, so without the
# rule below none of the four would fire on one and a backend could be included
# anywhere in the tree (ADR-033).
#
# Keyed on the entry headers themselves, because there is no extension to key
# on. Update this list when a backend is admitted.
#
# Netgen was admitted on 2026-10-01 (INFRA-NETGEN-001), so its remaining public
# headers are named here too. netgen_version.hpp and netgen_config.hpp are
# GENERATED by Netgen's build and installed beside nglib.h; nginterface_v2.hpp
# escaped the original pattern because it matched only a .h extension.
# mydefs.hpp is deliberately NOT listed: Netgen installs it, but the name is
# generic enough that the rule would fire on an unrelated BetterCAD file, and
# nothing can reach it without first including one of the headers below.
set(mesh_backend_header_regex
    "^(nglib\\.h|nginterface[^/]*\\.(h|hpp)|netgen_version\\.hpp|netgen_config\\.hpp|netgen/.+|tetgen\\.h|gmsh\\.h|gmsh/.+|CGAL/.+|mmg/.+|libmmg.+\\.h)$")
set(mesh_backend_allowed_regex "^src/meshing/[^/]+/")

# Eigen is a fourth dependency that needs containment, and none of the rules
# above provides it: Eigen's entry headers have NO extension at all
# (<Eigen/Dense>, <Eigen/SparseCholesky>), so the .hxx rule cannot see them,
# the Qt shape does not match, and the backend list does not name them.
#
# THE INVARIANT IS THAT NO PUBLIC HEADER INCLUDES EIGEN. It held across the
# whole repository by practice before this rule existed -- bettercad_sketch and
# bettercad_assembly link it PRIVATE and include it only from a .cpp or a
# private .hpp beside its sources -- and P17-SOLVE-001 (ADR-039) made it
# load-bearing by admitting Eigen to a third module. A public header including
# it would make Eigen a PUBLIC dependency of that module and propagate it to
# io 60, renderer 70, scripting 70, the app and the CLI.
#
# So the rule is enforced rather than reviewed, which is what rule 1 does for
# OCCT and rule 5 for a mesh backend. Allowed: anything under src/, which is
# where a .cpp or a private header lives. Forbidden: include/, which is the
# public surface.
set(eigen_header_regex "^(Eigen/.+|unsupported/Eigen/.+)$")
set(eigen_allowed_regex "^src/")

file(GLOB_RECURSE files RELATIVE "${SOURCE_DIR}"
    "${SOURCE_DIR}/include/*"
    "${SOURCE_DIR}/src/*"
    "${SOURCE_DIR}/apps/*"
    "${SOURCE_DIR}/examples/*"
)
list(FILTER files INCLUDE REGEX "\\.(h|hh|hpp|hxx|ipp|inl|c|cc|cpp|cxx)$")
list(LENGTH files file_count)
if(file_count EQUAL 0)
    message(FATAL_ERROR "CheckLayering.cmake: no source files found under ${SOURCE_DIR}")
endif()

# Module owning a path: include/bettercad/<module>/... or src/<module>/...
function(module_of path out_var)
    set(module "")
    if(path MATCHES "^include/bettercad/([^/]+)/")
        set(module "${CMAKE_MATCH_1}")
    elseif(path MATCHES "^src/([^/]+)/")
        set(module "${CMAKE_MATCH_1}")
    endif()
    set(${out_var} "${module}" PARENT_SCOPE)
endfunction()

set(violations)
foreach(file IN LISTS files)
    module_of("${file}" file_module)
    if(NOT file_module STREQUAL "" AND NOT DEFINED layer_${file_module})
        list(APPEND violations "${file}: unknown module '${file_module}' (add it to the layer table)")
    endif()

    file(STRINGS "${SOURCE_DIR}/${file}" include_lines
        REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]")
    foreach(line IN LISTS include_lines)
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*([<\"])([^>\"]+)[>\"].*$" "\\1" delimiter "${line}")
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*([<\"])([^>\"]+)[>\"].*$" "\\2" header "${line}")

        # Rule 1: Open CASCADE containment.
        if(header MATCHES "\\.hxx$" AND NOT file MATCHES "${occt_allowed_regex}")
            list(APPEND violations
                "${file}: OCCT header <${header}> outside an occt adapter directory")
        endif()

        # Rule 2: Qt containment.
        if(header MATCHES "^(Q[A-Z][A-Za-z0-9]*|Qt[A-Za-z0-9]+/.+)$"
           AND NOT file MATCHES "${qt_allowed_regex}")
            list(APPEND violations "${file}: Qt header <${header}> outside the GUI/renderer layer")
        endif()

        # Rule 3: layering between BetterCAD modules.
        if(header MATCHES "^bettercad/([^/]+)/" AND NOT file_module STREQUAL "")
            set(target_module "${CMAKE_MATCH_1}")
            if(NOT DEFINED layer_${target_module})
                list(APPEND violations "${file}: includes unknown module '${target_module}'")
            elseif(DEFINED layer_${file_module} AND NOT target_module STREQUAL file_module
                   AND NOT layer_${target_module} LESS layer_${file_module})
                list(APPEND violations
                    "${file}: layering violation: '${file_module}' must not depend on '${target_module}'")
            endif()
        endif()

        # Rule 5: volume-meshing backend containment (ADR-033).
        if(header MATCHES "${mesh_backend_header_regex}"
           AND NOT file MATCHES "${mesh_backend_allowed_regex}")
            list(APPEND violations
                "${file}: mesh backend header <${header}> outside src/meshing/<backend>/")
        endif()

        # Rule 7: Eigen containment (ADR-039).
        #
        # Eigen may be included from src/, never from a public header. See the
        # note beside eigen_header_regex for why no earlier rule covers it.
        if(header MATCHES "${eigen_header_regex}"
           AND NOT file MATCHES "${eigen_allowed_regex}")
            list(APPEND violations
                "${file}: Eigen header <${header}> in a public header -- it is PRIVATE to the modules that link it and may be included only from src/")
        endif()

        # Rule 6: the library must not reach into an application (P17-ARCH-001).
        #
        # Rules 2 and 3 do not cover this. Rule 2 catches Qt by its header
        # shape, so it would stop `#include <QWidget>` in src/structural/ but
        # not `#include "../../apps/bettercad_cli/Commands.hpp"`; rule 3 keys on
        # `bettercad/<module>/`, and apps/ is not a module, so it has no layer
        # and cannot be compared. A library file reaching a CLI parser, a
        # file-dialog or a GUI selection would therefore have passed.
        #
        # Named by P17's brief, which requires that a structural solver cannot
        # depend on the GUI or on the CLI, but written for every module: the
        # dependency direction is apps -> library, never the reverse, and
        # nothing under include/ or src/ had such an include when the rule was
        # added.
        if(header MATCHES "(^|/)apps/" AND file MATCHES "^(include|src)/")
            list(APPEND violations
                "${file}: include <${header}> reaches into apps/ from the library")
        endif()

        # Rule 4: public headers only use angle-bracket includes.
        if(file MATCHES "^include/" AND delimiter STREQUAL "\"")
            list(APPEND violations
                "${file}: quoted include \"${header}\" in a public header")
        endif()
    endforeach()
endforeach()

list(LENGTH violations violation_count)
if(violation_count GREATER 0)
    list(JOIN violations "\n  " violation_text)
    message(FATAL_ERROR
        "Architecture check failed: ${violation_count} violation(s) in ${file_count} files\n  ${violation_text}")
endif()
message(STATUS "Architecture check passed: ${file_count} files, 0 violations")
