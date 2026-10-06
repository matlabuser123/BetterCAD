// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: a mesh-backend header outside src/meshing/<backend>/.
//
// Rule 5 confines a backend to the meshing adapter, and this proves it still
// holds for a module that CONSUMES meshes. A solver reading nglib directly
// would bypass P16's validation, its currency and its orientation correction
// in one line (ADR-033, ADR-036).
#include <nglib.h>
