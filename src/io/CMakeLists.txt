bettercad_add_library(bettercad_io
    ALIAS io
    EXPORT_BASE BETTERCAD_IO
    EXPORT_HEADER bettercad/io/Export.hpp
    SOURCES
        DocumentFile.cpp
        FileIo.cpp
        FileIo.hpp
        ModelExport.cpp
        StlWriter.cpp
        DxfWriter.cpp
        PdfWriter.cpp
        SvgWriter.cpp
        SceneNumbers.hpp
        TextEncoding.hpp
        json/BooleanJson.cpp
        json/ComponentJson.cpp
        json/AnnotationJson.cpp
        json/DimensionJson.cpp
        json/SheetJson.cpp
        json/ViewJson.cpp
        json/MateJson.cpp
        json/DatumJson.cpp
        json/DocumentJson.cpp
        json/DraftJson.cpp
        json/FeatureJson.cpp
        json/HoleStandardsJson.cpp
        json/JsonReader.cpp
        json/JsonReader.hpp
        json/ObjectJson.hpp
        json/ParameterJson.cpp
        json/ParameterJsonDetail.hpp
        json/RibJson.cpp
        json/ShellJson.cpp
        json/SketchJson.cpp
        json/VariableFilletJson.cpp
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::geometry
    PRIVATE_LINK
        BetterCAD::assembly
        BetterCAD::drawing
        BetterCAD::features
        BetterCAD::sketch
        nlohmann_json::nlohmann_json
)

install(TARGETS bettercad_io
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
