#pragma once

// This file defines the small, source-language validation pass that runs after
// parsing and before IR generation.  It contains only constraints that are
// awkward or unsafe to encode in the grammar; type lowering remains in GenIR.

#include <string>

class CompUnitAST;

bool validateFrontend(const CompUnitAST &unit, std::string &error);
