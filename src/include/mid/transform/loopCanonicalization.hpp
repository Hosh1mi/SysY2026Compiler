#pragma once

class Function;

// Establish the structural loop form required by region-based transforms:
// dedicated preheaders, a single backedge, and dedicated exits.
bool canonicalizeLoopForm(Function *function);
