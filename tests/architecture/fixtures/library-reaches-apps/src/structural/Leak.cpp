// Fixture for tests/architecture/CheckLayering.cmake - never compiled.
// Violation: the library reaches into an application (rule 6).
//
// Not caught by any earlier rule: rule 2 keys on Qt's header shape and this
// is not a Qt header, and apps/ is not a module so rule 3 has no layer to
// compare. The dependency direction is apps -> library, always.
#include "../../apps/bettercad_cli/Commands.hpp"
