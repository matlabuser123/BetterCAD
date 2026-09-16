# Small programs that demonstrate the BetterCAD APIs.
bettercad_add_executable(bettercad_example_geometry_accuracy
    SOURCES geometry_accuracy/main.cpp
    LINK BetterCAD::geometry
)
bettercad_copy_runtime_dlls(bettercad_example_geometry_accuracy)

add_subdirectory(reference_models)
