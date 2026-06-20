#include "../../include/backend/riscv/machine.hpp"

#include <sstream>

namespace riscv {

std::string printMFunction(const MFunction &func) {
    std::ostringstream os;
    for (const auto &mi : func.insts) {
        if (mi.isLabel || mi.isDirective)
            os << mi.text << "\n";
        else
            os << "\t" << mi.text << "\n";
    }
    return os.str();
}

}  // namespace riscv
