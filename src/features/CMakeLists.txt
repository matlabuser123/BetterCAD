# Feature layer (layer 2): parametric features that turn sketches into solids.
bettercad_add_library(bettercad_features
    ALIAS features
    EXPORT_BASE BETTERCAD_FEATURES
    EXPORT_HEADER bettercad/features/Export.hpp
    SOURCES
        Feature.cpp
        Regenerator.cpp
        ResultBodies.cpp
        SolidSupport.cpp
        SolidSupport.hpp
        Validation.cpp
        boolean/BooleanRegeneration.cpp
        boolean/CombineFeature.cpp
        boolean/SplitFeature.cpp
        chamfer/ChamferFeature.cpp
        chamfer/ChamferRegeneration.cpp
        datum/DatumResolution.cpp
        datum/Datums.cpp
        draft/DraftFeature.cpp
        draft/DraftRegeneration.cpp
        reference/FaceReferences.cpp
        extrude/ExtrudeFeature.cpp
        extrude/ExtrudeRegeneration.cpp
        fillet/FilletFeature.cpp
        fillet/FilletRegeneration.cpp
        hole/HoleFeature.cpp
        hole/HoleRegeneration.cpp
        loft/LoftFeature.cpp
        loft/LoftRegeneration.cpp
        pattern/CircularPatternFeature.cpp
        pattern/CircularPatternRegeneration.cpp
        pattern/LinearPatternFeature.cpp
        pattern/LinearPatternRegeneration.cpp
        pattern/MirrorFeature.cpp
        pattern/MirrorRegeneration.cpp
        pattern/PatternSupport.cpp
        pattern/PatternSupport.hpp
        profile/Profiles.cpp
        revolve/RevolveFeature.cpp
        revolve/RevolveRegeneration.cpp
        shell/ShellFeature.cpp
        shell/ShellRegeneration.cpp
        sweep/SweepFeature.cpp
        sweep/SweepRegeneration.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::geometry
        BetterCAD::sketch
)

install(TARGETS bettercad_features
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
