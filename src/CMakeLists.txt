# Library modules, lowest layer first. A module may depend only on modules
# listed above it (enforced by tests/architecture/CheckLayering.cmake).
# Modules are added here as they are implemented; see TODO.md.
add_subdirectory(core)
add_subdirectory(core/geometry)
add_subdirectory(sketch)
add_subdirectory(features)
add_subdirectory(assembly)
add_subdirectory(drawing)
add_subdirectory(io)
