// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: a GENERATED volume-meshing backend header outside
// src/meshing/<backend>/.
//
// Distinct from mesh-backend-leak, which uses nglib.h. Netgen's build also
// generates and installs netgen_version.hpp and netgen_config.hpp beside it,
// and those were not covered when the backend was first admitted: a file
// could reach Netgen through them without rule 5 firing (INFRA-NETGEN-001).
#include <bettercad/core/BuildInfo.hpp>

#include <netgen_version.hpp>
