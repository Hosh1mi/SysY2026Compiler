#pragma once

class Module;

// Materialize the internal i64 implementation for the declaration emitted by
// LoopRepFold.  This runs after target-independent optimization so i64 remains
// an implementation detail of the compiler runtime.
void materializePiecewiseModSumRuntime(Module *module);
