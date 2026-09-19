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
#
# Exits with an error listing every violation.
if(NOT DEFINED SOURCE_DIR OR NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "CheckLayering.cmake: SOURCE_DIR must name an existing directory")
endif()

# Layer of each module; lower layers must not depend on higher ones.
set(layer_core 0)
set(layer_sketch 1)
set(layer_features 2)
# assembly sits above features, which it uses, and below io, which must
# serialize it. The rule below is strictly-lower, so there is no number
# between them and io moves up with everything above it (ADR-006).
set(layer_assembly 3)
set(layer_io 4)
set(layer_renderer 5)
set(layer_scripting 5)

set(qt_allowed_regex "^(apps/bettercad|src/renderer)/")
set(occt_allowed_regex "^src/(.+/)?occt/")

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
