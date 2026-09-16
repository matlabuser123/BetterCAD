bettercad_add_library(bettercad_core
    ALIAS core
    EXPORT_BASE BETTERCAD_CORE
    EXPORT_HEADER bettercad/core/Export.hpp
    SOURCES
        BuildInfo.cpp
        Error.cpp
        GitRevision.hpp
        Naming.cpp
        Uuid.cpp
        document/CommandHistory.cpp
        document/Commands.cpp
        document/DependencyGraph.cpp
        document/Document.cpp
        document/DocumentObject.cpp
        document/ParameterExpressions.cpp
        document/References.cpp
        math/BSpline.cpp
        math/Direction.cpp
        math/Frame.cpp
        parameters/Expression.cpp
        parameters/Parameter.cpp
        parameters/ParameterTable.cpp
        units/Format.cpp
        units/UnitCatalog.cpp
)

# The build configuration is baked into BuildInfo.cpp only, so switching
# configurations recompiles a single file.
set_source_files_properties(BuildInfo.cpp PROPERTIES
    COMPILE_DEFINITIONS "BETTERCAD_BUILD_CONFIG=\"$<CONFIG>\""
)

# Regenerate the git revision on every build; the file is only rewritten
# (and recompiled) when the revision or dirty state actually changes.
set(_git_revision_cpp "${PROJECT_BINARY_DIR}/generated/src/core/GitRevision.cpp")
add_custom_target(bettercad_git_revision
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DTEMPLATE=${PROJECT_SOURCE_DIR}/cmake/templates/GitRevision.cpp.in"
        "-DOUTPUT_FILE=${_git_revision_cpp}"
        -P "${PROJECT_SOURCE_DIR}/cmake/GitRevision.cmake"
    BYPRODUCTS "${_git_revision_cpp}"
    COMMENT "Checking git revision"
    VERBATIM
)
target_sources(bettercad_core PRIVATE "${_git_revision_cpp}")
add_dependencies(bettercad_core bettercad_git_revision)

install(TARGETS bettercad_core
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
