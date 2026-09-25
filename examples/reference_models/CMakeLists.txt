# BetterCAD's mechanical reference models (P11-REF-001), their assemblies
# (P13-REFMOD-001) and their drawings (P14-REFMOD-001), built only through
# the public document, sketch, feature, assembly and drawing APIs. The builders live in a
# library so the test suite can use them; the example program builds every
# model, prints its fingerprint and timings, and can write the models and
# their exports.
add_library(bettercad_reference_models STATIC
    AssemblyBasic.cpp
    AssemblyCatalog.cpp
    AssemblyConfigured.cpp
    AssemblyDriven.cpp
    AssemblyJoints.cpp
    AssemblyParts.cpp
    AssemblyParts.hpp
    AssemblyReferenceModels.hpp
    BearingHousing.cpp
    BuildSupport.cpp
    BuildSupport.hpp
    Catalog.cpp
    DrawingAngleBracket.cpp
    DrawingBoltedStack.cpp
    DrawingCatalog.cpp
    DrawingClampSet.cpp
    DrawingGuardedFrame.cpp
    DrawingHolePlate.cpp
    DrawingPocketBlock.cpp
    DrawingReferenceModels.hpp
    DrawingStepPlate.cpp
    DrawingSupport.hpp
    DrawingToleranceBlock.cpp
    Fingerprint.cpp
    GearboxCover.cpp
    IndexPlate.cpp
    ManifoldTube.cpp
    MotorMount.cpp
    MountingBracket.cpp
    Flange.cpp
    Pulley.cpp
    RibbedBracket.cpp
    ReferenceModels.hpp
    Shaft.cpp
    TransitionDuct.cpp
    UBolt.cpp
)
add_library(BetterCAD::reference_models ALIAS bettercad_reference_models)
target_include_directories(bettercad_reference_models PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bettercad_reference_models
    PUBLIC BetterCAD::assembly BetterCAD::core BetterCAD::drawing BetterCAD::features
           BetterCAD::geometry BetterCAD::sketch
    PRIVATE BetterCAD::compiler_options
)

bettercad_add_executable(bettercad_example_reference_models
    SOURCES main.cpp
    LINK BetterCAD::reference_models BetterCAD::io
)
bettercad_copy_runtime_dlls(bettercad_example_reference_models)
