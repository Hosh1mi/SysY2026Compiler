#pragma once

// Compatibility facade for the retired backend.  New code must include the
// rewrite interface instead.  Keeping the old implementation reachable only
// through this explicitly deprecated name makes differential diagnosis
// possible without allowing it back into the default pipeline.
#include "../../arm64/codegen.hpp"

using DeprecatedArm64CodeGen = Arm64CodeGen;
