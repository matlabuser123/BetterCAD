# Meshing layer (layer 4): the engineering mesh data model, and later the
# meshing controls, the mesher, regions, quality and the volume backend. Sits
# above features, whose regeneration and face-name resolution it will use, and
# below io, which must serialize its controls (ADR-033).
#
# It shares layer 4 with drawing, which the strictly-lower rule permits: the two
# are siblings, each deriving a secondary representation from the same geometry
# and neither using the other.
#
# Surface TRIANGULATION of a Body is NOT here: that is kernel work and lives in
# core/geometry behind the occt adapter, like projection. This module contains
# no OCCT, and a volume-meshing backend, when one is admitted, lives in
# src/meshing/<backend>/ where the architecture check confines it.
#
# P16-DATA-001 needs core alone: the data model holds no CAD reference, because
# geometry correspondence is P16-MAP-001's.
bettercad_add_library(bettercad_meshing
    ALIAS meshing
    EXPORT_BASE BETTERCAD_MESHING
    EXPORT_HEADER bettercad/meshing/Export.hpp
    SOURCES
        Mesh.cpp
        MeshValidation.cpp
    PUBLIC_LINK
        BetterCAD::core
)

install(TARGETS bettercad_meshing
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
