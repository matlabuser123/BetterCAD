// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: an example builds geometry with Open CASCADE directly instead
// of through BetterCAD's public API.
#include <bettercad/core/document/Document.hpp>

#include <BRepPrimAPI_MakeBox.hxx>
