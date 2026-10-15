# BetterCAD's mechanical reference models (P11-REF-001), their assemblies
# (P13-REFMOD-001), their drawings (P14-REFMOD-001), their engineering data
# (P15-REFMOD-001) and their meshes (P16-REFMOD-001), built only through the
# public document, sketch, feature, assembly, drawing and meshing APIs. The builders live in a
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
    MaterialCatalog.cpp
    MaterialModels.cpp
    MaterialReferenceModels.hpp
    MeshCatalog.cpp
    MeshModels.cpp
    MeshReferenceModels.hpp
    MotorMount.cpp
    MountingBracket.cpp
    Flange.cpp
    Pulley.cpp
    RibbedBracket.cpp
    ReferenceModels.hpp
    Shaft.cpp
    StructuralCatalog.cpp
    StructuralModels.cpp
    StructuralReferenceModels.hpp
    TransitionDuct.cpp
    UBolt.cpp
)
add_library(BetterCAD::reference_models ALIAS bettercad_reference_models)
target_include_directories(bettercad_reference_models PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bettercad_reference_models
    # meshing joins the list for P16-REFMOD-001. It is layer 4, beside drawing,
    # which is already here -- so no layer is crossed and nothing needs
    # renumbering. PUBLIC because the test suite reads the models' MeshControl
    # definitions through these headers.
    #
    # structural joins it for P17-REFMOD-001. Layer 50, above meshing, which
    # the examples -- a client of the library, checked like any other -- may
    # use freely. PUBLIC for the same reason: the test suite reads the models'
    # StructuralAnalysis definitions through these headers.
    PUBLIC BetterCAD::assembly BetterCAD::core BetterCAD::drawing BetterCAD::features
           BetterCAD::geometry BetterCAD::meshing BetterCAD::sketch BetterCAD::structural
    PRIVATE BetterCAD::compiler_options
)

bettercad_add_executable(bettercad_example_reference_models
    SOURCES main.cpp
    LINK BetterCAD::reference_models BetterCAD::io
)
bettercad_copy_runtime_dlls(bettercad_example_reference_models)
