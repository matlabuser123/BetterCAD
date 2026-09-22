# Drawing layer (layer 4): sheets, and later views, dimensions, annotations
# and the drawing scene. Sits above assembly, whose solved placements an
# assembly drawing projects, and below io, which must serialize it (ADR-015).
#
# Projection and hidden-line removal are NOT here: they are kernel work and
# belong in core/geometry behind the occt adapter. This module contains no
# OCCT.
bettercad_add_library(bettercad_drawing
    ALIAS drawing
    EXPORT_BASE BETTERCAD_DRAWING
    EXPORT_HEADER bettercad/drawing/Export.hpp
    SOURCES
        Sheet.cpp
        Sheets.cpp
    PUBLIC_LINK
        BetterCAD::core
)

install(TARGETS bettercad_drawing
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
