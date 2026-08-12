// This file exposes the optional, read-only AST dump used to inspect parser
// output without adding printing concerns to the semantic AST nodes.
#pragma once

#include <iosfwd>

class CompUnitAST;

void dumpAST(CompUnitAST &root, std::ostream &out);
