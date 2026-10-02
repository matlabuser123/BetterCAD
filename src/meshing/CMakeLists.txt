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
# The data model itself needs core alone -- it holds no CAD reference, because
# geometry correspondence is P16-MAP-001's. Geometry PREPARATION (P16-GEOM-001)
# needs features, for the regeneration state and currency that decide whether a
# body may be meshed at all, which is why meshing sits above features.

# The volume-meshing backend (INFRA-NETGEN-001). Exactly one of these two
# translation units is compiled, so bettercad::meshing::volumeBackend() is
# always defined and callers never branch on configuration. The Netgen one is
# the only file in BetterCAD permitted to include a mesh-backend header, and
# it lives under src/meshing/netgen/ because rule 5 of
# tests/architecture/CheckLayering.cmake requires exactly that.
if(BETTERCAD_NETGEN_ENABLED)
    set(bettercad_volume_backend_sources netgen/NetgenBackend.cpp)
else()
    set(bettercad_volume_backend_sources NoVolumeBackend.cpp)
endif()

bettercad_add_library(bettercad_meshing
    ALIAS meshing
    EXPORT_BASE BETTERCAD_MESHING
    EXPORT_HEADER bettercad/meshing/Export.hpp
    SOURCES
        GeometryPreparation.cpp
        Mesh.cpp
        MeshValidation.cpp
        MeshSizing.cpp
        SurfaceMesh.cpp
        VolumeMesh.cpp
        ${bettercad_volume_backend_sources}
    PUBLIC_LINK
        BetterCAD::core
        BetterCAD::geometry
        BetterCAD::features
)

if(BETTERCAD_NETGEN_ENABLED)
    # PRIVATE: Netgen is an implementation detail. Nothing that links
    # BetterCAD::meshing may see nglib.h, and the architecture check enforces
    # that independently of this line.
    target_link_libraries(bettercad_meshing PRIVATE Netgen::nglib)
endif()

install(TARGETS bettercad_meshing
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
