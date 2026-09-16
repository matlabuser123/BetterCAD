# BetterCAD's mechanical reference models (P11-REF-001), built only through
# the public document, sketch and feature APIs. The builders live in a
# library so the test suite can use them; the example program builds every
# model, prints its fingerprint and timings, and can write the models and
# their exports.
add_library(bettercad_reference_models STATIC
    BearingHousing.cpp
    BuildSupport.cpp
    BuildSupport.hpp
    Catalog.cpp
    Fingerprint.cpp
    MountingBracket.cpp
    Flange.cpp
    Pulley.cpp
    ReferenceModels.hpp
    Shaft.cpp
    UBolt.cpp
)
add_library(BetterCAD::reference_models ALIAS bettercad_reference_models)
target_include_directories(bettercad_reference_models PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bettercad_reference_models
    PUBLIC BetterCAD::core BetterCAD::features BetterCAD::geometry BetterCAD::sketch
    PRIVATE BetterCAD::compiler_options
)

bettercad_add_executable(bettercad_example_reference_models
    SOURCES main.cpp
    LINK BetterCAD::reference_models BetterCAD::io
)
bettercad_copy_runtime_dlls(bettercad_example_reference_models)
