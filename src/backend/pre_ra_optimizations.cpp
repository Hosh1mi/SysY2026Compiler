// Pre-register-allocation machine optimizations preserve virtual-register
// structure and run before graph coloring.
#include "backend/pre_ra_optimizations.hpp"
#include "backend/machine_analysis.hpp"
#include "backend/vector_immediate.hpp"

#include <deque>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace backend::aarch64 {
namespace {

bool isZeroIdentity(const MachineInstr &instruction) {
    switch (instruction.opcode()) {
    case Opcode::ADDWri:
    case Opcode::SUBWri:
    case Opcode::ADDXri:
    case Opcode::SUBXri:
        return instruction.operands().size() == 3 &&
               instruction.operands()[2].kind() ==
                   MachineOperand::Kind::Immediate &&
               instruction.operands()[2].immediate() == 0;
    default:
        return false;
    }
}

bool removableInstruction(const MachineInstr &instruction) {
    if (instruction.isTerminator() || instruction.isCall() ||
        instruction.mayLoad() || instruction.mayStore() ||
        instruction.hasSideEffects())
        return false;
    bool hasVirtualDef = false;
    for (const MachineOperand &operand : instruction.operands()) {
        if (operand.isPhysicalRegister())
            return false;
        hasVirtualDef |= operand.isVirtualRegister() && operand.isDef;
    }
    return hasVirtualDef;
}

bool flagsUsedAfter(
    MachineBasicBlock *block,
    MachineBasicBlock::InstrList::const_iterator begin) {
    auto inspectRange = [](auto first, auto last) {
        for (; first != last; ++first) {
            // An instruction that both reads and writes flags consumes the
            // incoming value before defining the outgoing one.
            if (first->readsRegister(PhysReg::NZCV))
                return 1;
            if (first->definesRegister(PhysReg::NZCV))
                return -1;
        }
        return 0;
    };

    int local = inspectRange(begin, block->instructions().end());
    if (local != 0)
        return local > 0;

    std::deque<MachineBasicBlock *> worklist;
    std::unordered_set<MachineBasicBlock *> visited;
    for (MachineBasicBlock *successor : block->successors())
        if (successor && visited.insert(successor).second)
            worklist.push_back(successor);

    while (!worklist.empty()) {
        MachineBasicBlock *current = worklist.front();
        worklist.pop_front();
        int access = inspectRange(
            current->instructions().begin(),
            current->instructions().end());
        if (access > 0)
            return true;
        if (access < 0)
            continue;
        for (MachineBasicBlock *successor :
             current->successors())
            if (successor && visited.insert(successor).second)
                worklist.push_back(successor);
    }
    return false;
}

} // namespace

bool MachineConstantCSE::run(MachineFunction &function) const {
  bool changed = false;
  // Local Machine CSE for materialized constants.  Keeping this before RA
  // lets graph coloring decide whether the shared value belongs in a
  // caller-saved register, a callee-saved register, or a spill slot.  The
  // table is cleared at calls so this transformation does not manufacture
  // a new live-across-call range.
  MachineRegisterIndex registers(function);
  for (auto &block : function.blocks()) {
    std::unordered_map<std::string, VReg> available;
    auto &instructions = block->instructions();
    for (auto it = instructions.begin(); it != instructions.end();) {
        if (it->isCall()) {
          available.clear();
          ++it;
          continue;
        }
        bool cseCandidate = it->opcode() == Opcode::MOVi32 ||
                            it->opcode() == Opcode::MOVi64 ||
                            it->opcode() == Opcode::MOVIv4Zero ||
                            it->opcode() == Opcode::MOVIv4s ||
                            it->opcode() == Opcode::MOVIv4sMsl ||
                            it->opcode() == Opcode::MVNIv4s ||
                            it->opcode() == Opcode::MOVIv16b ||
                            it->opcode() == Opcode::FMOVv4s ||
                            it->opcode() == Opcode::DUPv4i32 ||
                            it->opcode() == Opcode::DUPv4f32;
        if (!cseCandidate || it->operands().empty() ||
            !it->operands()[0].isVirtualRegister() ||
            !it->operands()[0].isDef) {
          ++it;
          continue;
        }

        std::string key = std::to_string(static_cast<unsigned>(it->opcode()));
        if (it->opcode() == Opcode::MOVIv4Zero) {
          // zero vector has no immediate payload
        } else if (it->opcode() == Opcode::DUPv4i32 ||
                   it->opcode() == Opcode::DUPv4f32) {
          if (it->operands().size() != 2 ||
              !it->operands()[1].isVirtualRegister()) {
            ++it;
            continue;
          }
          key += ":v" + std::to_string(it->operands()[1].virtualRegister());
        } else if (it->opcode() == Opcode::FMOVv4s) {
          if (it->operands().size() != 2 ||
              it->operands()[1].kind() != MachineOperand::Kind::FloatingBits) {
            ++it;
            continue;
          }
          key += ":" + std::to_string(it->operands()[1].floatingBits());
        } else if (it->opcode() == Opcode::MOVIv4s ||
                   it->opcode() == Opcode::MOVIv4sMsl ||
                   it->opcode() == Opcode::MVNIv4s) {
          if (it->operands().size() != 3 ||
              it->operands()[1].kind() != MachineOperand::Kind::Immediate ||
              it->operands()[2].kind() != MachineOperand::Kind::Immediate) {
            ++it;
            continue;
          }
          key += ":" + std::to_string(it->operands()[1].immediate()) + ":" +
                 std::to_string(it->operands()[2].immediate());
        } else {
          if (it->operands().size() != 2 ||
              it->operands()[1].kind() != MachineOperand::Kind::Immediate) {
            ++it;
            continue;
          }
          key += ":" + std::to_string(it->operands()[1].immediate());
        }
        VReg duplicate = it->operands()[0].virtualRegister();
        auto canonical = available.find(key);
        if (canonical == available.end()) {
          available.emplace(std::move(key), duplicate);
          ++it;
          continue;
        }

        VReg replacement = canonical->second;
        registers.replaceUses(duplicate, replacement);
        it = instructions.erase(it);
        function.registerInfo().eraseVirtualRegister(duplicate);
        changed = true;
    }
  }
  return changed;
}

bool AArch64VectorImmediateSelection::run(
    MachineFunction &function) const {
  bool changed = false;
  // GPR vs NEON bank choice for splat immediates.
  //
  // ISel keeps Splat as DUP from a scalar so integer users can share the
  // same MOVi.  When that scalar immediate has no non-broadcast users,
  // rewrite the DUP into a NEON immediate.  Dead scalar materializations
  // are left for Machine DCE.
  MachineRegisterIndex registers(function);

  for (auto &block : function.blocks()) {
      auto &instructions = block->instructions();
      for (auto it = instructions.begin(); it != instructions.end(); ++it) {
        const bool integerDup = it->opcode() == Opcode::DUPv4i32;
        const bool floatDup = it->opcode() == Opcode::DUPv4f32;
        if ((!integerDup && !floatDup) || it->operands().size() != 2 ||
            !it->operands()[0].isVirtualRegister() ||
            !it->operands()[0].isDef || !it->operands()[1].isVirtualRegister())
          continue;

        VReg scalar = it->operands()[1].virtualRegister();
        MachineInstr *scalarDef = registers.uniqueDefinition(scalar);
        std::uint32_t bits = 0;
        if (integerDup) {
          if (!scalarDef || scalarDef->opcode() != Opcode::MOVi32 ||
              scalarDef->operands().size() != 2 ||
              scalarDef->operands()[1].kind() !=
                  MachineOperand::Kind::Immediate ||
              !registers.allUsesHaveOpcode(scalar, Opcode::DUPv4i32))
            continue;
          bits =
              static_cast<std::uint32_t>(scalarDef->operands()[1].immediate());
        } else {
          // Prefer FMOVSW -> DUP when the FPR value is broadcast
          // only.  Walk through an optional MOVi32 of the bit
          // pattern so float splats share the same policy.
          if (!scalarDef || scalarDef->opcode() != Opcode::FMOVSW ||
              scalarDef->operands().size() != 2 ||
              !scalarDef->operands()[1].isVirtualRegister() ||
              !registers.allUsesHaveOpcode(scalar, Opcode::DUPv4f32))
            continue;
          VReg bitsReg = scalarDef->operands()[1].virtualRegister();
          MachineInstr *bitsDef = registers.uniqueDefinition(bitsReg);
          if (!bitsDef || bitsDef->opcode() != Opcode::MOVi32 ||
              bitsDef->operands().size() != 2 ||
              bitsDef->operands()[1].kind() !=
                  MachineOperand::Kind::Immediate ||
              !registers.allUsesHaveOpcode(bitsReg, Opcode::FMOVSW))
            continue;
          bits = static_cast<std::uint32_t>(bitsDef->operands()[1].immediate());
        }

        auto immediate = classifyNeonSplatImmediate(bits);
        if (!immediate)
          continue;
        MachineOperand destination = it->operands()[0];
        *it = makeNeonSplatImmediate(*immediate, destination);
        if (destination.isVirtualRegister())
          function.registerInfo().setDefinition(destination.virtualRegister(),
                                                &*it);
        changed = true;
    }
  }
  return changed;
}

bool AArch64PreRAPeephole::run(MachineFunction &function) const {
  bool changed = false;
  for (auto &block : function.blocks()) {
      auto &instructions = block->instructions();
      for (auto it = instructions.begin(); it != instructions.end();) {
        if (isZeroIdentity(*it)) {
          MachineOperand destination = it->operands()[0];
          MachineOperand source = it->operands()[1];
          it->setOpcode(Opcode::COPY);
          it->operands().clear();
          it->addOperand(std::move(destination)).addOperand(std::move(source));
          changed = true;
        }
        if (it->opcode() == Opcode::COPY && it->operands().size() == 2 &&
            it->operands()[0].isSameRegisterAs(it->operands()[1]) &&
            it->operands()[0].isPhysicalRegister()) {
          it = instructions.erase(it);
          changed = true;
          continue;
        }
        ++it;
    }
  }
  return changed;
}

bool AArch64LoadStoreOptimization::run(
    MachineFunction &function) const {
  bool changed = false;
  // Pair adjacent same-width LDR/STR into LDP/STP.  Element size is the
  // architectural scale (4/8/16): the pair offset must be a multiple of that
  // scale and fit the signed 7-bit scaled immediate.
    struct AddressForm {
      VReg root = 0;
      std::int64_t offset = 0;
      bool valid = false;
    };
    struct PairKind {
      Opcode loadOpcode;
      Opcode storeOpcode;
      Opcode pairLoadOpcode;
      Opcode pairStoreOpcode;
      std::int64_t stride;
    };
    static const PairKind kPairKinds[] = {
        {Opcode::LDRWui, Opcode::STRWui, Opcode::LDPWi, Opcode::STPWi, 4},
        {Opcode::LDRSui, Opcode::STRSui, Opcode::LDPSi, Opcode::STPSi, 4},
        {Opcode::LDRXui, Opcode::STRXui, Opcode::LDPXi, Opcode::STPXi, 8},
        {Opcode::LDRQui, Opcode::STRQui, Opcode::LDPQi, Opcode::STPQi, 16},
    };
    auto definesVirtual = [](const MachineInstr &instruction, VReg reg) {
      if (!reg)
        return false;
      for (const MachineOperand &operand : instruction.operands())
        if (operand.isVirtualRegister() && operand.isDef &&
            operand.virtualRegister() == reg)
          return true;
      return false;
    };
  for (auto &block : function.blocks()) {
      bool fused = true;
      while (fused) {
        fused = false;
        auto &instructions = block->instructions();
        std::unordered_map<VReg, AddressForm> addresses;
        auto addressOf = [&](const MachineOperand &operand) {
          AddressForm form;
          if (!operand.isVirtualRegister() ||
              operand.regClass() != RegClass::GPR64)
            return form;
          auto found = addresses.find(operand.virtualRegister());
          if (found != addresses.end())
            return found->second;
          form.root = operand.virtualRegister();
          form.valid = true;
          return form;
        };

        struct MemoryCandidate {
          MachineBasicBlock::InstrList::iterator instruction;
          AddressForm address;
          const PairKind *kind = nullptr;
        };
        std::vector<MemoryCandidate> loads;
        std::vector<MemoryCandidate> stores;
        for (auto it = instructions.begin(); it != instructions.end(); ++it) {
          if (it->opcode() == Opcode::COPY && it->operands().size() == 2 &&
              it->operands()[0].isVirtualRegister() &&
              it->operands()[0].regClass() == RegClass::GPR64) {
            AddressForm form = addressOf(it->operands()[1]);
            if (form.valid)
              addresses[it->operands()[0].virtualRegister()] = form;
          } else if (it->opcode() == Opcode::ADDXri &&
                     it->operands().size() == 3 &&
                     it->operands()[0].isVirtualRegister() &&
                     it->operands()[2].kind() ==
                         MachineOperand::Kind::Immediate) {
            AddressForm form = addressOf(it->operands()[1]);
            if (form.valid &&
                !__builtin_add_overflow(
                    form.offset, it->operands()[2].immediate(),
                    &form.offset)) {
              addresses[it->operands()[0].virtualRegister()] = form;
            }
          }

          const PairKind *kind = nullptr;
          bool load = false;
          bool store = false;
          for (const PairKind &candidate : kPairKinds) {
            if (it->opcode() == candidate.loadOpcode) {
              kind = &candidate;
              load = true;
              break;
            }
            if (it->opcode() == candidate.storeOpcode) {
              kind = &candidate;
              store = true;
              break;
            }
          }
          if ((!load && !store) || it->operands().size() != 3 ||
              it->operands()[2].kind() != MachineOperand::Kind::Immediate ||
              it->memoryOperands().empty() ||
              it->memoryOperands().front().isVolatile)
            continue;
          AddressForm form = addressOf(it->operands()[1]);
          if (!form.valid)
            continue;
          if (__builtin_add_overflow(
                  form.offset, it->operands()[2].immediate(),
                  &form.offset))
            continue;
          (load ? loads : stores).push_back(MemoryCandidate{it, form, kind});
        }

        auto tryPair = [&](std::vector<MemoryCandidate> &candidates,
                           bool load) {
          for (std::size_t i = 0; i < candidates.size(); ++i) {
            for (std::size_t j = i + 1; j < candidates.size(); ++j) {
              auto &lhs = candidates[i];
              auto &rhs = candidates[j];
              if (lhs.kind != rhs.kind ||
                  lhs.address.root != rhs.address.root)
                continue;
              std::int64_t difference = 0;
              const std::int64_t high =
                  std::max(lhs.address.offset, rhs.address.offset);
              const std::int64_t low =
                  std::min(lhs.address.offset, rhs.address.offset);
              if (__builtin_sub_overflow(high, low, &difference) ||
                  difference != lhs.kind->stride)
                continue;
              std::int64_t stride = lhs.kind->stride;
              std::int64_t lowerOffset =
                  std::min(lhs.address.offset, rhs.address.offset);
              if (lowerOffset % stride != 0 || lowerOffset / stride < -64 ||
                  lowerOffset / stride > 63)
                continue;

              // Loads are rewritten at the earlier instruction so
              // both values become available without moving uses.
              // Stores must stay at the later instruction so both
              // data operands are already defined.
              VReg earlyData = 0;
              if (!load && lhs.instruction->operands()[0].isVirtualRegister())
                earlyData = lhs.instruction->operands()[0].virtualRegister();

              bool memoryBarrier = false;
              for (auto scan = std::next(lhs.instruction);
                   scan != rhs.instruction; ++scan) {
                if (scan->isCall() || scan->mayStore() ||
                    (!load && scan->mayLoad()) ||
                    definesVirtual(*scan, lhs.address.root) ||
                    (!load && definesVirtual(*scan, earlyData))) {
                  memoryBarrier = true;
                  break;
                }
              }
              if (memoryBarrier)
                continue;

              // LDP with Rt == Rt2 is unpredictable.
              if (load && lhs.instruction->operands()[0].isSameRegisterAs(
                              rhs.instruction->operands()[0]))
                continue;

              auto lower = lhs.address.offset < rhs.address.offset
                               ? lhs.instruction
                               : rhs.instruction;
              auto upper = lhs.address.offset < rhs.address.offset
                               ? rhs.instruction
                               : lhs.instruction;
              MachineInstr pair(load ? lhs.kind->pairLoadOpcode
                                     : lhs.kind->pairStoreOpcode);
              pair.addOperand(lower->operands()[0])
                  .addOperand(upper->operands()[0])
                  .addOperand(
                      MachineOperand::vreg(lhs.address.root, RegClass::GPR64))
                  .addOperand(MachineOperand::immediate(lowerOffset));
              unsigned pairBytes = static_cast<unsigned>(stride * 2);
              unsigned align = static_cast<unsigned>(stride);
              pair.addMemoryOperand(MachineMemOperand{
                  load ? MachineMemOperand::Access::Load
                       : MachineMemOperand::Access::Store,
                  pairBytes, align, nullptr, std::nullopt, lowerOffset, false});

              auto replacement = load ? lhs.instruction : rhs.instruction;
              auto removed = load ? rhs.instruction : lhs.instruction;
              *replacement = std::move(pair);
              if (load) {
                for (unsigned operand = 0; operand < 2; ++operand)
                  if (replacement->operands()[operand].isVirtualRegister())
                    function.registerInfo().setDefinition(
                        replacement->operands()[operand].virtualRegister(),
                        &*replacement);
              }
              instructions.erase(removed);
              return true;
            }
          }
          return false;
        };

        fused = tryPair(loads, true);
        if (!fused)
          fused = tryPair(stores, false);
        changed |= fused;
    }
  }
  return changed;
}

bool MachineSink::run(MachineFunction &function) const {
  bool changed = false;
  if (function.hasProperty(MachineProperty::IsSSA)) {
    std::unordered_map<VReg, MachineInstr *> uniqueUse;
    std::unordered_map<VReg, MachineBasicBlock *> useBlock;
    for (auto &owned : function.blocks())
      for (MachineInstr &instruction : owned->instructions())
        for (const MachineOperand &operand : instruction.operands())
          if (operand.isVirtualRegister() && !operand.isDef) {
            const VReg reg = operand.virtualRegister();
            auto [use, inserted] = uniqueUse.emplace(reg, &instruction);
            if (!inserted && use->second != &instruction)
              use->second = nullptr;
            auto [block, blockInserted] = useBlock.emplace(reg, owned.get());
            if (!blockInserted && block->second != owned.get())
              block->second = nullptr;
          }

    auto isFixedStackLoad = [&](const MachineInstr &instruction) {
      if (instruction.opcode() != Opcode::SPILL_LOAD ||
          instruction.operands().size() < 2 ||
          instruction.operands()[1].kind() !=
              MachineOperand::Kind::FrameIndex ||
          instruction.memoryOperands().empty() ||
          instruction.memoryOperands().front().isVolatile)
        return false;
      const int index = instruction.operands()[1].frameIndex();
      return index >= 0 &&
             static_cast<std::size_t>(index) <
                 function.frameInfo().objects().size() &&
             function.frameInfo().objects()[index].fixed;
    };
    auto locallySinkable = [&](const MachineInstr &instruction) {
      if (instruction.isTerminator() || instruction.isCall() ||
          instruction.mayStore() || instruction.hasSideEffects() ||
          instruction.parallelCopyGroup)
        return false;
      if (instruction.mayLoad() && !isFixedStackLoad(instruction))
        return false;
      const InstrDesc &descriptor = InstrInfo::get(instruction.opcode());
      if (descriptor.setsFlags || descriptor.usesFlags ||
          (descriptor.pseudo && instruction.opcode() != Opcode::SPILL_LOAD))
        return false;
      for (const MachineOperand &operand : instruction.operands())
        if (operand.isPhysicalRegister())
          return false;
      return true;
    };

    for (auto &owned : function.blocks()) {
      auto &instructions = owned->instructions();
      unsigned fixedGPRLoads = 0;
      unsigned fixedVectorLoads = 0;
      for (const MachineInstr &instruction : instructions) {
        if (!isFixedStackLoad(instruction))
          continue;
        const RegClass regClass = instruction.operands()[0].regClass();
        if (regClass == RegClass::GPR32 || regClass == RegClass::GPR64)
          ++fixedGPRLoads;
        else if (regClass == RegClass::FPR32 ||
                 regClass == RegClass::NEON128)
          ++fixedVectorLoads;
      }
      // Ordinary scheduling should retain freedom when register pressure is
      // below capacity.  Large incoming argument sets are different: their
      // eager fixed-stack loads necessarily create avoidable live ranges.
      if (fixedGPRLoads <=
              RegisterInfo::allocationOrder(RegClass::GPR32).size() &&
          fixedVectorLoads <=
              RegisterInfo::allocationOrder(RegClass::FPR32).size())
        continue;

      using Iterator = MachineBasicBlock::InstrList::iterator;
      struct Candidate {
        Iterator instruction;
        MachineInstr *use = nullptr;
      };
      std::vector<Candidate> candidates;
      std::unordered_map<MachineInstr *, Iterator> position;
      std::unordered_map<MachineInstr *, unsigned> ordinal;
      unsigned nextOrdinal = 0;
      for (auto instruction = instructions.begin();
           instruction != instructions.end(); ++instruction) {
        position.emplace(&*instruction, instruction);
        ordinal.emplace(&*instruction, nextOrdinal++);
      }

      for (auto instruction = instructions.begin();
           instruction != instructions.end(); ++instruction) {
        if (!locallySinkable(*instruction))
          continue;
        VReg definition = 0;
        bool singleDefinition = true;
        for (const MachineOperand &operand : instruction->operands()) {
          if (!operand.isVirtualRegister() || !operand.isDef)
            continue;
          if (definition) {
            singleDefinition = false;
            break;
          }
          definition = operand.virtualRegister();
        }
        if (!singleDefinition || !definition ||
            useBlock[definition] != owned.get() || !uniqueUse[definition] ||
            ordinal[&*instruction] >= ordinal[uniqueUse[definition]] ||
            uniqueUse[definition]->opcode() == Opcode::PHI)
          continue;
        candidates.push_back(
            Candidate{instruction, uniqueUse[definition]});
      }

      // Process consumers before their operands.  If both are moved, the
      // producer then follows its consumer to the new location rather than
      // being stranded at the consumer's old position.
      for (auto candidate = candidates.rbegin();
           candidate != candidates.rend(); ++candidate) {
        Iterator use = position.at(candidate->use);
        if (std::next(candidate->instruction) == use)
          continue;
        instructions.splice(use, instructions, candidate->instruction);
        changed = true;
      }
    }
    if (changed)
      function.clearProperty(MachineProperty::TracksLiveness);
  }

  if (!function.hasProperty(MachineProperty::HasPHIs)) {
    auto sinkableOpcode = [](Opcode opcode) {
      switch (opcode) {
      case Opcode::MOVi32:
      case Opcode::MOVi64:
      case Opcode::MOVIv4Zero:
      case Opcode::FMOVWS:
      case Opcode::FMOVSW:
      case Opcode::COPYXtoW:
      case Opcode::SXTW:
      case Opcode::UXTW:
        return true;
      default:
        return false;
      }
    };

    // Sink a single-use, side-effect-free materialization through a
    // single-predecessor edge.  This is deliberately narrower than
    // general code sinking: the edge proves availability and prevents a
    // PHI or alternate predecessor from observing an undefined value.
    bool sunk = true;
    while (sunk) {
      sunk = false;
      std::unordered_map<VReg, unsigned> useCount;
      std::unordered_map<VReg, MachineBasicBlock *> useBlock;
      std::unordered_map<VReg, MachineInstr *> useInstruction;
      for (auto &owned : function.blocks())
        for (MachineInstr &instruction : owned->instructions())
          for (const MachineOperand &operand : instruction.operands())
            if (operand.isVirtualRegister() && !operand.isDef) {
              VReg reg = operand.virtualRegister();
              ++useCount[reg];
              useBlock[reg] = owned.get();
              useInstruction[reg] = &instruction;
            }

      for (auto &owned : function.blocks()) {
        MachineBasicBlock *source = owned.get();
        auto &sourceInstructions = source->instructions();
        for (auto instruction = sourceInstructions.begin();
             instruction != sourceInstructions.end(); ++instruction) {
          if (!sinkableOpcode(instruction->opcode()) ||
              instruction->isTerminator() || instruction->isCall() ||
              instruction->mayLoad() || instruction->mayStore() ||
              instruction->hasSideEffects())
            continue;
          VReg definition = 0;
          bool valid = true;
          for (const MachineOperand &operand : instruction->operands()) {
            if (operand.isPhysicalRegister()) {
              valid = false;
              break;
            }
            if (!operand.isVirtualRegister() || !operand.isDef)
              continue;
            if (definition) {
              valid = false;
              break;
            }
            definition = operand.virtualRegister();
          }
          if (!valid || !definition || useCount[definition] != 1)
            continue;
          MachineBasicBlock *destination = useBlock[definition];
          if (!destination || destination == source ||
              destination->predecessors().size() != 1 ||
              destination->predecessors().front() != source)
            continue;

          auto &destinationInstructions = destination->instructions();
          auto insertion = std::find_if(
              destinationInstructions.begin(), destinationInstructions.end(),
              [&](const MachineInstr &candidate) {
                return &candidate == useInstruction[definition];
              });
          if (insertion == destinationInstructions.end())
            continue;
          destinationInstructions.splice(insertion, sourceInstructions,
                                         instruction);
          function.clearProperty(MachineProperty::TracksLiveness);
          changed = true;
          sunk = true;
          break;
        }
        if (sunk)
          break;
      }
    }
  }
  return changed;
}

bool DeadMachineInstructionElimination::run(MachineFunction &function) const {
  if (!function.hasProperty(MachineProperty::IsSSA))
    return false;

  MachineRegisterIndex registers(function);
  std::unordered_map<VReg, unsigned> uses;
  std::unordered_map<VReg, MachineInstr *> definition;
  for (const auto &[reg, info] :
       function.registerInfo().virtualRegisters()) {
    (void)info;
    uses.emplace(reg, registers.useCount(reg));
    if (MachineInstr *instruction = registers.uniqueDefinition(reg))
      definition.emplace(reg, instruction);
  }

  std::deque<VReg> worklist;
  for (const auto &[reg, instruction] : definition)
    if (!uses[reg])
      worklist.push_back(reg);

  std::unordered_set<MachineInstr *> dead;
  while (!worklist.empty()) {
    VReg reg = worklist.front();
    worklist.pop_front();
    auto found = definition.find(reg);
    if (found == definition.end())
      continue;
    MachineInstr *instruction = found->second;
    if (dead.count(instruction) || !removableInstruction(*instruction))
      continue;

    bool allDefsDead = true;
    for (const MachineOperand &operand : instruction->operands())
      if (operand.isVirtualRegister() && operand.isDef &&
          uses[operand.virtualRegister()] != 0) {
        allDefsDead = false;
        break;
      }
    if (!allDefsDead)
      continue;

    dead.insert(instruction);
    for (const MachineOperand &operand : instruction->operands()) {
      if (!operand.isVirtualRegister() || operand.isDef)
        continue;
      VReg used = operand.virtualRegister();
      if (uses[used] && --uses[used] == 0)
        worklist.push_back(used);
    }
  }

  if (dead.empty())
    return false;
  for (auto &block : function.blocks()) {
    auto &instructions = block->instructions();
    for (auto it = instructions.begin(); it != instructions.end();) {
      if (!dead.count(&*it)) {
        ++it;
        continue;
      }
      for (const MachineOperand &operand : it->operands())
        if (operand.isVirtualRegister() && operand.isDef)
          function.registerInfo().eraseVirtualRegister(
              operand.virtualRegister());
      it = instructions.erase(it);
    }
  }
  bool hasPHIs = false;
  for (const auto &block : function.blocks())
    hasPHIs |= !block->instructions().empty() &&
               block->instructions().front().opcode() == Opcode::PHI;
  if (!hasPHIs)
    function.clearProperty(MachineProperty::HasPHIs);
  function.clearProperty(MachineProperty::TracksLiveness);
  return true;
}

bool MachineInvariantConstantMotion::run(
    MachineFunction &function) const {
    if (function.blocks().empty())
        return false;

    std::vector<MachineBasicBlock *> blocks;
    for (const auto &block : function.blocks())
        blocks.push_back(block.get());

    MachineDominatorTree dominators;
    dominators.analyze(function);
    MachineLoopInfo loopInfo;
    loopInfo.analyze(function, dominators);

    struct Loop {
        MachineBasicBlock *header = nullptr;
        MachineBasicBlock *preheader = nullptr;
        std::unordered_set<MachineBasicBlock *> blocks;
    };
    std::vector<Loop> loops;
    for (const MachineLoop &naturalLoop : loopInfo.loops()) {
        Loop loop;
        loop.header = naturalLoop.header;
        loop.blocks = naturalLoop.blocks;
        std::vector<MachineBasicBlock *> outside;
        for (MachineBasicBlock *predecessor :
             loop.header->predecessors())
            if (!loop.blocks.count(predecessor))
                outside.push_back(predecessor);
        if (outside.size() == 1 &&
            outside.front()->successors().size() == 1)
            loop.preheader = outside.front();
        if (loop.preheader)
            loops.push_back(std::move(loop));
    }
    std::sort(loops.begin(), loops.end(),
              [](const Loop &lhs, const Loop &rhs) {
                  return lhs.blocks.size() < rhs.blocks.size();
              });

    MachineRegisterIndex registers(function);
    std::unordered_map<VReg, MachineBasicBlock *> definitionBlock;
    for (const auto &[reg, info] :
         function.registerInfo().virtualRegisters()) {
        (void)info;
        if (MachineBasicBlock *block =
                registers.uniqueDefinitionBlock(reg))
            definitionBlock.emplace(reg, block);
    }

    auto hoistableOpcode = [](Opcode opcode) {
        switch (opcode) {
        case Opcode::MOVi32:
        case Opcode::MOVi64:
            // Scalar immediates rematerialize cheaply, so lengthening their
            // live ranges is fine.  Encoded NEON immediates (including
            // movi #0) must stay near their uses: hoisting them occupies a
            // vector register across nested loops and invites spill churn.
            return true;
        default:
            return false;
        }
    };

    bool changed = false;
    std::unordered_set<MachineInstr *> hoistedThisRun;
    std::unordered_set<MachineBasicBlock *> nestedPreheaders;
    for (const Loop &loop : loops)
        nestedPreheaders.insert(loop.preheader);
    for (Loop &loop : loops) {
        auto insertion = std::find_if(
            loop.preheader->instructions().begin(),
            loop.preheader->instructions().end(),
            [](const MachineInstr &instruction) {
                return instruction.isTerminator();
            });
        bool localChange = true;
        while (localChange) {
            localChange = false;
            for (MachineBasicBlock *block : blocks) {
                if (!loop.blocks.count(block))
                    continue;
                auto &instructions = block->instructions();
                for (auto it = instructions.begin();
                     it != instructions.end();) {
                    auto current = it++;
                    if (!hoistableOpcode(current->opcode()))
                        continue;
                    if (hoistedThisRun.count(&*current))
                        continue;
                    // A nested loop preheader is already the profitable
                    // placement for cheap rematerializable constants.  Do
                    // not repeatedly lift them through surrounding loops,
                    // which needlessly lengthens live ranges and can evict
                    // the actual induction state.
                    if (nestedPreheaders.count(block) &&
                        block != loop.header)
                        continue;
                    bool invariant = true;
                    for (const MachineOperand &operand :
                         current->operands()) {
                        if (operand.isPhysicalRegister()) {
                            invariant = false;
                            break;
                        }
                        if (!operand.isVirtualRegister() ||
                            operand.isDef)
                            continue;
                        auto definition = definitionBlock.find(
                            operand.virtualRegister());
                        if (definition != definitionBlock.end() &&
                            loop.blocks.count(definition->second)) {
                            invariant = false;
                            break;
                        }
                    }
                    if (!invariant)
                        continue;
                    for (const MachineOperand &operand :
                         current->operands())
                        if (operand.isVirtualRegister() &&
                            operand.isDef)
                            definitionBlock[operand.virtualRegister()] =
                                loop.preheader;
                    loop.preheader->instructions().splice(
                        insertion, instructions, current);
                    hoistedThisRun.insert(&*current);
                    localChange = true;
                    changed = true;
                }
            }
        }
    }
    if (changed)
        function.clearProperty(MachineProperty::TracksLiveness);
    return changed;
}

bool AArch64ConditionOptimizer::run(
    MachineFunction &function) const {
    if (!function.hasProperty(MachineProperty::IsSSA))
        return false;
    MachineRegisterIndex registers(function);

    bool changed = false;

    // Preserve the original compare flags through an integerized boolean
    // when that boolean is used only to drive a conditional select:
    //
    //   NZCV = cmp a, b
    //   %p   = cset cc, NZCV
    //          ... instructions that do not define NZCV ...
    //   NZCV = cmp %p, 0
    //   %r   = csel %t, %f, ne, NZCV
    //
    // becomes a csel using `cc` and the first NZCV definition.  This is the
    // same flags-glue selection LLVM performs before RA; it removes no
    // observable comparison and introduces no physical scratch register.
    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto set = instructions.begin();
             set != instructions.end();) {
            if (set->opcode() != Opcode::CSETW ||
                set->operands().size() < 2 ||
                !set->operands()[0].isVirtualRegister() ||
                !set->operands()[0].isDef) {
                ++set;
                continue;
            }
            VReg conditionReg =
                set->operands()[0].virtualRegister();
            if (registers.useCount(conditionReg) != 1) {
                ++set;
                continue;
            }

            auto booleanCompare = std::next(set);
            auto isBooleanCompare = [&](const MachineInstr &instruction) {
                return instruction.opcode() == Opcode::CMPWri &&
                       instruction.operands().size() >= 2 &&
                       instruction.operands()[0].isVirtualRegister() &&
                       instruction.operands()[0].virtualRegister() ==
                           conditionReg &&
                       instruction.operands()[1].kind() ==
                           MachineOperand::Kind::Immediate &&
                       instruction.operands()[1].immediate() == 0;
            };
            while (booleanCompare != instructions.end()) {
                if (isBooleanCompare(*booleanCompare))
                    break;
                const InstrDesc &descriptor =
                    InstrInfo::get(booleanCompare->opcode());
                bool clobbersFlags =
                    descriptor.setsFlags ||
                    booleanCompare->isCall();
                for (const MachineOperand &operand :
                     booleanCompare->operands())
                    clobbersFlags |=
                        operand.isPhysicalRegister() &&
                        operand.isDef &&
                        operand.physicalRegister() ==
                            PhysReg::NZCV;
                if (clobbersFlags ||
                    booleanCompare->isTerminator())
                    break;
                ++booleanCompare;
            }
            if (booleanCompare == instructions.end() ||
                !isBooleanCompare(*booleanCompare)) {
                ++set;
                continue;
            }

            CondCode originalCondition =
                set->operands()[1].condition();
            bool rewroteSelect = false;
            for (auto select = std::next(booleanCompare);
                 select != instructions.end();) {
                bool candidate =
                    (select->opcode() == Opcode::CSELW ||
                     select->opcode() == Opcode::CSELX ||
                     select->opcode() == Opcode::FCSELS) &&
                    select->operands().size() >= 4 &&
                    select->operands()[3].kind() ==
                        MachineOperand::Kind::ConditionCode;
                if (candidate) {
                    CondCode booleanCondition =
                        select->operands()[3].condition();
                    if (booleanCondition != CondCode::NE &&
                        booleanCondition != CondCode::EQ)
                        break;
                    select->operands()[3] = MachineOperand::condition(
                        booleanCondition == CondCode::NE
                            ? originalCondition
                            : InstrInfo::inverseCondition(originalCondition));
                    rewroteSelect = true;
                    ++select;
                    continue;
                }
                const InstrDesc &descriptor = InstrInfo::get(select->opcode());
                if (descriptor.setsFlags || descriptor.usesFlags ||
                    select->isCall() || select->isTerminator())
                    break;
                ++select;
            }
            if (!rewroteSelect) {
                ++set;
                continue;
            }
            instructions.erase(booleanCompare);
            set = instructions.erase(set);
            function.registerInfo().eraseVirtualRegister(
                conditionReg);
            changed = true;
        }
    }

    for (auto &block : function.blocks()) {
        auto &instructions = block->instructions();
        for (auto it = instructions.begin(); it != instructions.end();) {
            if (it->opcode() != Opcode::CSETW ||
                it->operands().size() < 2 ||
                !it->operands()[0].isVirtualRegister() ||
                !it->operands()[0].isDef) {
                ++it;
                continue;
            }
            VReg conditionReg =
                it->operands()[0].virtualRegister();
            auto branch = std::next(it);
            if (registers.useCount(conditionReg) != 1) {
                ++it;
                continue;
            }
            while (branch != instructions.end()) {
                const InstrDesc &descriptor =
                    InstrInfo::get(branch->opcode());
                bool clobbersFlags = descriptor.setsFlags ||
                    branch->isCall();
                for (const MachineOperand &operand :
                     branch->operands())
                    clobbersFlags |=
                        operand.isPhysicalRegister() &&
                        operand.isDef &&
                        operand.physicalRegister() ==
                            PhysReg::NZCV;
                if (clobbersFlags)
                    break;
                bool candidate =
                    (branch->opcode() == Opcode::CBNZ ||
                     branch->opcode() == Opcode::CBZ) &&
                    branch->operands().size() == 2 &&
                    branch->operands()[0].isVirtualRegister() &&
                    branch->operands()[0].virtualRegister() ==
                        conditionReg;
                if (candidate)
                    break;
                if (branch->isTerminator())
                    break;
                ++branch;
            }
            if (branch == instructions.end() ||
                (branch->opcode() != Opcode::CBNZ &&
                 branch->opcode() != Opcode::CBZ) ||
                branch->operands().size() != 2 ||
                !branch->operands()[0].isVirtualRegister() ||
                branch->operands()[0].virtualRegister() !=
                    conditionReg) {
                ++it;
                continue;
            }

            CondCode condition = it->operands()[1].condition();
            if (branch->opcode() == Opcode::CBZ)
                condition = InstrInfo::inverseCondition(condition);
            MachineBasicBlock *target =
                branch->operands()[1].basicBlock();
            branch->setOpcode(Opcode::Bcc);
            branch->operands().clear();
            branch->addOperand(
                      MachineOperand::condition(condition))
                .addOperand(MachineOperand::block(target))
                .addOperand(MachineOperand::physReg(
                    PhysReg::NZCV, RegClass::CCR, false, true));
            function.registerInfo().eraseVirtualRegister(
                conditionReg);
            it = instructions.erase(it);
            changed = true;
        }
    }
    if (changed)
        function.clearProperty(MachineProperty::TracksLiveness);
    return changed;
}

bool PreRACFGOptimizer::run(MachineFunction &function) const {
    if (function.hasProperty(MachineProperty::NoVRegs))
        return false;

    auto sameVReg = [](const MachineOperand &lhs,
                       const MachineOperand &rhs) {
        return lhs.isVirtualRegister() && rhs.isVirtualRegister() &&
               lhs.virtualRegister() == rhs.virtualRegister();
    };

    bool changed = false;

    struct MaterializedI32 {
        MachineInstr *instruction = nullptr;
        VReg reg = 0;
        std::uint32_t value = 0;
    };
    auto materializedI32 =
        [&](const MachineOperand &operand)
            -> std::optional<MaterializedI32> {
        if (!operand.isVirtualRegister() ||
            !function.registerInfo().contains(
                operand.virtualRegister()))
            return std::nullopt;
        const VRegInfo &info = function.registerInfo().get(
            operand.virtualRegister());
        MachineInstr *definition = info.definition;
        if (!definition ||
            definition->opcode() != Opcode::MOVi32 ||
            definition->operands().size() != 2 ||
            !sameVReg(definition->operands()[0], operand) ||
            definition->operands()[1].kind() !=
                MachineOperand::Kind::Immediate)
            return std::nullopt;
        return MaterializedI32{
            definition, operand.virtualRegister(),
            static_cast<std::uint32_t>(
                definition->operands()[1].immediate())};
    };
    auto eraseInstruction = [&](MachineInstr *target) {
        if (!target)
            return;
        for (auto &owned : function.blocks()) {
            auto &instructions = owned->instructions();
            auto found = std::find_if(
                instructions.begin(), instructions.end(),
                [&](const MachineInstr &instruction) {
                    return &instruction == target;
                });
            if (found != instructions.end()) {
                instructions.erase(found);
                return;
            }
        }
    };

    // Fold an AND whose result only feeds an equality-to-zero branch.
    // One-bit masks use TBZ/TBNZ; encodable constants use TST-immediate;
    // all other masks use TST-register.  The transformation is restricted to
    // EQ/NE and rejects live-out flags because TST does not reproduce every
    // CMP flag.
    for (auto &owned : function.blocks()) {
        auto &instructions = owned->instructions();
        bool localChange = true;
        while (localChange) {
            localChange = false;
            std::unordered_map<VReg, unsigned> useCount;
            for (const auto &useBlock : function.blocks())
                for (const MachineInstr &instruction :
                     useBlock->instructions())
                    for (const MachineOperand &operand :
                         instruction.operands())
                        if (operand.isVirtualRegister() &&
                            !operand.isDef)
                            ++useCount[operand.virtualRegister()];

            for (auto bitAnd = instructions.begin();
                 bitAnd != instructions.end(); ++bitAnd) {
                if ((bitAnd->opcode() != Opcode::ANDWrr &&
                     bitAnd->opcode() != Opcode::ANDWri) ||
                    bitAnd->operands().size() != 3 ||
                    !bitAnd->operands()[0].isVirtualRegister())
                    continue;

                VReg extracted =
                    bitAnd->operands()[0].virtualRegister();
                if (useCount[extracted] != 1)
                    continue;

                auto compare = std::next(bitAnd);
                if (compare == instructions.end() ||
                    compare->opcode() != Opcode::CMPWri ||
                    compare->operands().size() < 2 ||
                    !sameVReg(compare->operands()[0],
                              bitAnd->operands()[0]) ||
                    compare->operands()[1].kind() !=
                        MachineOperand::Kind::Immediate ||
                    compare->operands()[1].immediate() != 0)
                    continue;
                auto branch = std::next(compare);
                if (branch == instructions.end() ||
                    branch->opcode() != Opcode::Bcc ||
                    branch->operands().size() < 2 ||
                    branch->operands()[0].kind() !=
                        MachineOperand::Kind::ConditionCode ||
                    branch->operands()[1].kind() !=
                        MachineOperand::Kind::BasicBlock)
                    continue;
                CondCode condition =
                    branch->operands()[0].condition();
                if (condition != CondCode::EQ &&
                    condition != CondCode::NE)
                    continue;
                if (flagsUsedAfter(
                        owned.get(), std::next(branch)))
                    continue;

                MachineOperand source;
                MachineOperand maskOperand;
                std::optional<MaterializedI32> constantMask;
                if (bitAnd->opcode() == Opcode::ANDWri) {
                    if (!bitAnd->operands()[1]
                             .isVirtualRegister() ||
                        bitAnd->operands()[2].kind() !=
                            MachineOperand::Kind::Immediate)
                        continue;
                    source = bitAnd->operands()[1];
                    constantMask = MaterializedI32{
                        nullptr, 0,
                        static_cast<std::uint32_t>(
                            bitAnd->operands()[2].immediate())};
                } else {
                    auto rhsConstant =
                        materializedI32(bitAnd->operands()[2]);
                    auto lhsConstant =
                        materializedI32(bitAnd->operands()[1]);
                    if (rhsConstant) {
                        source = bitAnd->operands()[1];
                        maskOperand = bitAnd->operands()[2];
                        constantMask = rhsConstant;
                    } else if (lhsConstant) {
                        source = bitAnd->operands()[2];
                        maskOperand = bitAnd->operands()[1];
                        constantMask = lhsConstant;
                    } else {
                        source = bitAnd->operands()[1];
                        maskOperand = bitAnd->operands()[2];
                    }
                }
                if (!source.isVirtualRegister())
                    continue;

                bool oneBitMask =
                    constantMask &&
                    constantMask->value != 0 &&
                    (constantMask->value &
                     (constantMask->value - 1)) == 0;
                std::int64_t maskImmediate =
                    constantMask
                        ? static_cast<std::int64_t>(
                              constantMask->value)
                        : 0;
                bool immediateTest =
                    constantMask && !oneBitMask &&
                    InstrInfo::acceptsImmediate(
                        Opcode::TSTWri, maskImmediate);

                if (oneBitMask) {
                    unsigned bit = 0;
                    while (((constantMask->value >> bit) & 1U) ==
                           0)
                        ++bit;
                    MachineBasicBlock *target =
                        branch->operands()[1].basicBlock();
                    branch->setOpcode(condition == CondCode::EQ
                                          ? Opcode::TBZ
                                          : Opcode::TBNZ);
                    branch->operands().clear();
                    branch->addOperand(source)
                        .addOperand(
                            MachineOperand::immediate(bit))
                        .addOperand(
                            MachineOperand::block(target));
                    instructions.erase(compare);
                } else {
                    MachineInstr test(
                        immediateTest ? Opcode::TSTWri
                                      : Opcode::TSTWrr);
                    test.addOperand(source);
                    if (immediateTest) {
                        test.addOperand(
                            MachineOperand::immediate(
                                maskImmediate));
                    } else {
                        if (!maskOperand.isVirtualRegister())
                            continue;
                        test.addOperand(maskOperand);
                    }
                    test.addOperand(MachineOperand::physReg(
                        PhysReg::NZCV, RegClass::CCR, true,
                        true));
                    *compare = std::move(test);
                }
                instructions.erase(bitAnd);
                if (constantMask &&
                    constantMask->instruction &&
                    useCount[constantMask->reg] == 1 &&
                    (oneBitMask || immediateTest)) {
                    eraseInstruction(
                        constantMask->instruction);
                    function.registerInfo().eraseVirtualRegister(
                        constantMask->reg);
                }
                function.registerInfo().eraseVirtualRegister(
                    extracted);
                localChange = true;
                changed = true;
                break;
            }
        }
    }

    // A standalone equality comparison against zero is redundant when the
    // branch is its only flag consumer.  CBZ/CBNZ performs the same test
    // without creating an NZCV dependency.
    for (auto &owned : function.blocks()) {
        auto &instructions = owned->instructions();
        for (auto compare = instructions.begin();
             compare != instructions.end();) {
            if (compare->opcode() != Opcode::CMPWri ||
                compare->operands().size() < 2 ||
                !compare->operands()[0].isVirtualRegister() ||
                compare->operands()[1].kind() !=
                    MachineOperand::Kind::Immediate ||
                compare->operands()[1].immediate() != 0) {
                ++compare;
                continue;
            }
            auto branch = std::next(compare);
            if (branch == instructions.end() ||
                branch->opcode() != Opcode::Bcc ||
                branch->operands().size() < 2 ||
                branch->operands()[0].kind() !=
                    MachineOperand::Kind::ConditionCode ||
                branch->operands()[1].kind() !=
                    MachineOperand::Kind::BasicBlock) {
                ++compare;
                continue;
            }
            CondCode condition =
                branch->operands()[0].condition();
            if ((condition != CondCode::EQ &&
                 condition != CondCode::NE) ||
                flagsUsedAfter(owned.get(),
                               std::next(branch))) {
                ++compare;
                continue;
            }

            MachineOperand tested = compare->operands()[0];
            MachineBasicBlock *target =
                branch->operands()[1].basicBlock();
            branch->setOpcode(condition == CondCode::EQ
                                  ? Opcode::CBZ
                                  : Opcode::CBNZ);
            branch->operands().clear();
            branch->addOperand(std::move(tested))
                .addOperand(MachineOperand::block(target));
            compare = instructions.erase(compare);
            ++compare;
            changed = true;
        }
    }

    // Batch an exact-halving loop.  The matched even edge contains only
    // `state >>= 1`; the shared latch contains only `count += 1` and a test
    // against an odd sentinel.  CTZ therefore gives exactly the number of
    // consecutive iterations that can be collapsed.  The latch is cloned
    // onto the even edge so the odd predecessor keeps its unit increment.
    bool batched = true;
    while (batched) {
        batched = false;
        for (auto &parityOwned : function.blocks()) {
            MachineBasicBlock *parity = parityOwned.get();
            auto &parityInstructions = parity->instructions();
            if (parityInstructions.size() != 2)
                continue;
            auto bitBranch = parityInstructions.begin();
            auto parityFallthrough = std::next(bitBranch);
            if (bitBranch->opcode() != Opcode::TBZ ||
                bitBranch->operands().size() != 3 ||
                !bitBranch->operands()[0].isVirtualRegister() ||
                bitBranch->operands()[1].kind() !=
                    MachineOperand::Kind::Immediate ||
                bitBranch->operands()[1].immediate() != 0 ||
                bitBranch->operands()[2].kind() !=
                    MachineOperand::Kind::BasicBlock ||
                parityFallthrough->opcode() != Opcode::B)
                continue;

            MachineOperand state = bitBranch->operands()[0];
            MachineBasicBlock *even =
                bitBranch->operands()[2].basicBlock();
            if (!even || (even->instructions().size() != 3 &&
                          even->instructions().size() != 5))
                continue;
            bool incrementOnEdges = even->instructions().size() == 5;
            auto shift = even->instructions().begin();
            auto increment = shift;
            auto countCopy = shift;
            auto stateCopy = std::next(shift);
            if (incrementOnEdges) {
                increment = std::next(shift);
                countCopy = std::next(increment);
                stateCopy = std::next(countCopy);
            }
            auto toLatch = std::next(stateCopy);
            if (shift->opcode() != Opcode::ASRWri ||
                shift->operands().size() != 3 ||
                !shift->operands()[0].isVirtualRegister() ||
                !sameVReg(shift->operands()[1], state) ||
                shift->operands()[2].kind() !=
                    MachineOperand::Kind::Immediate ||
                shift->operands()[2].immediate() != 1 ||
                stateCopy->opcode() != Opcode::COPY ||
                stateCopy->operands().size() != 2 ||
                !stateCopy->operands()[0].isVirtualRegister() ||
                !sameVReg(stateCopy->operands()[1],
                          shift->operands()[0]) ||
                toLatch->opcode() != Opcode::B ||
                toLatch->operands().size() != 1 ||
                toLatch->operands()[0].kind() !=
                    MachineOperand::Kind::BasicBlock)
                continue;

            if (incrementOnEdges &&
                (increment->opcode() != Opcode::ADDWri ||
                 increment->operands().size() != 3 ||
                 !increment->operands()[0].isVirtualRegister() ||
                 !increment->operands()[1].isVirtualRegister() ||
                 increment->operands()[2].kind() !=
                     MachineOperand::Kind::Immediate ||
                 increment->operands()[2].immediate() != 1 ||
                 countCopy->opcode() != Opcode::COPY ||
                 countCopy->operands().size() != 2 ||
                 !sameVReg(countCopy->operands()[1],
                           increment->operands()[0])))
                continue;

            MachineBasicBlock *latch =
                toLatch->operands()[0].basicBlock();
            const std::size_t expectedLatchSize =
                incrementOnEdges ? 3 : 4;
            if (!latch || latch->instructions().size() != expectedLatchSize)
                continue;
            auto compare = latch->instructions().begin();
            if (!incrementOnEdges) {
                increment = compare;
                compare = std::next(increment);
            }
            auto exitBranch = std::next(compare);
            auto continueBranch = std::next(exitBranch);
            if (increment->opcode() != Opcode::ADDWri ||
                increment->operands().size() != 3 ||
                !increment->operands()[0].isVirtualRegister() ||
                !increment->operands()[1].isVirtualRegister() ||
                increment->operands()[2].kind() !=
                    MachineOperand::Kind::Immediate ||
                increment->operands()[2].immediate() != 1 ||
                compare->opcode() != Opcode::CMPWri ||
                compare->operands().size() < 2 ||
                !sameVReg(compare->operands()[0],
                          stateCopy->operands()[0]) ||
                compare->operands()[1].kind() !=
                    MachineOperand::Kind::Immediate ||
                (compare->operands()[1].immediate() & 1) == 0 ||
                exitBranch->opcode() != Opcode::Bcc ||
                exitBranch->operands().size() < 2 ||
                exitBranch->operands()[0].kind() !=
                    MachineOperand::Kind::ConditionCode ||
                exitBranch->operands()[0].condition() !=
                    CondCode::EQ ||
                exitBranch->operands()[1].kind() !=
                    MachineOperand::Kind::BasicBlock ||
                continueBranch->opcode() != Opcode::B ||
                continueBranch->operands().size() != 1 ||
                continueBranch->operands()[0].kind() !=
                    MachineOperand::Kind::BasicBlock)
                continue;

            MachineBasicBlock *exit =
                exitBranch->operands()[1].basicBlock();
            MachineBasicBlock *continuation =
                continueBranch->operands()[0].basicBlock();
            if (!exit || !continuation ||
                continuation->instructions().size() != 3)
                continue;
            bool copiesCount = false;
            bool copiesState = false;
            MachineInstr *continuationCountCopy = nullptr;
            const MachineOperand &edgeCount =
                incrementOnEdges ? countCopy->operands()[0]
                                 : increment->operands()[0];
            auto continuationIt =
                continuation->instructions().begin();
            for (unsigned i = 0; i < 2;
                 ++i, ++continuationIt) {
                if (continuationIt->opcode() != Opcode::COPY ||
                    continuationIt->operands().size() != 2)
                    break;
                bool isCountCopy =
                    sameVReg(continuationIt->operands()[0],
                             increment->operands()[1]) &&
                    sameVReg(continuationIt->operands()[1],
                             edgeCount);
                copiesCount |= isCountCopy;
                if (isCountCopy)
                    continuationCountCopy = &*continuationIt;
                copiesState |=
                    sameVReg(continuationIt->operands()[0],
                             state) &&
                    sameVReg(continuationIt->operands()[1],
                             stateCopy->operands()[0]);
            }
            if (!copiesCount || !copiesState ||
                continuationIt ==
                    continuation->instructions().end() ||
                continuationIt->opcode() != Opcode::B ||
                continuationIt->operands().size() != 1 ||
                continuationIt->operands()[0].kind() !=
                    MachineOperand::Kind::BasicBlock ||
                continuationIt->operands()[0].basicBlock() !=
                    parity)
                continue;

            MachineInstr *exitCountCopy = nullptr;
            unsigned edgeCountUses = 0;
            for (auto &candidateBlock : function.blocks()) {
                for (MachineInstr &candidate :
                     candidateBlock->instructions()) {
                    for (unsigned operandIndex = 0;
                         operandIndex < candidate.operands().size();
                         ++operandIndex) {
                        MachineOperand &operand =
                            candidate.operands()[operandIndex];
                        if (operand.isDef || !sameVReg(operand, edgeCount))
                            continue;
                        ++edgeCountUses;
                        if (candidateBlock.get() == exit &&
                            candidate.opcode() == Opcode::COPY &&
                            operandIndex == 1)
                            exitCountCopy = &candidate;
                    }
                }
            }
            bool canReuseLoopCount =
                incrementOnEdges && continuationCountCopy &&
                exitCountCopy && edgeCountUses == 2;

            MachineBasicBlock *oddContinuation = nullptr;
            if (parityFallthrough->operands().size() == 1 &&
                parityFallthrough->operands()[0].kind() ==
                    MachineOperand::Kind::BasicBlock) {
                MachineBasicBlock *oddCompute =
                    parityFallthrough->operands()[0].basicBlock();
                for (MachineBasicBlock *predecessor :
                     latch->predecessors()) {
                    if (predecessor == even ||
                        (predecessor->instructions().size() != 3 &&
                         predecessor->instructions().size() != 5))
                        continue;
                    bool oddIncrementOnEdge =
                        predecessor->instructions().size() == 5;
                    if (oddIncrementOnEdge != incrementOnEdges)
                        continue;
                    auto oddIncrement =
                        predecessor->instructions().begin();
                    auto update = oddIncrementOnEdge
                                      ? std::next(oddIncrement)
                                      : oddIncrement;
                    auto oddCountCopy = std::next(update);
                    auto copy = oddIncrementOnEdge
                                    ? std::next(oddCountCopy)
                                    : std::next(update);
                    auto branch = std::next(copy);
                    if (oddIncrementOnEdge &&
                        (oddIncrement->opcode() != Opcode::ADDWri ||
                         oddIncrement->operands().size() != 3 ||
                         !sameVReg(oddIncrement->operands()[1],
                                   increment->operands()[1]) ||
                         oddIncrement->operands()[2].kind() !=
                             MachineOperand::Kind::Immediate ||
                         oddIncrement->operands()[2].immediate() != 1 ||
                         oddCountCopy->opcode() != Opcode::COPY ||
                         oddCountCopy->operands().size() != 2 ||
                         !sameVReg(oddCountCopy->operands()[0],
                                   countCopy->operands()[0]) ||
                         !sameVReg(oddCountCopy->operands()[1],
                                   oddIncrement->operands()[0])))
                        continue;
                    if (update->opcode() != Opcode::ADDWri ||
                        update->operands().size() != 3 ||
                        update->operands()[2].kind() !=
                            MachineOperand::Kind::Immediate ||
                        update->operands()[2].immediate() != 1 ||
                        copy->opcode() != Opcode::COPY ||
                        copy->operands().size() != 2 ||
                        !sameVReg(copy->operands()[0],
                                  stateCopy->operands()[0]) ||
                        !sameVReg(copy->operands()[1],
                                  update->operands()[0]) ||
                        branch->opcode() != Opcode::B ||
                        branch->operands().size() != 1 ||
                        branch->operands()[0].kind() !=
                            MachineOperand::Kind::BasicBlock ||
                        branch->operands()[0].basicBlock() !=
                            latch ||
                        predecessor->predecessors().size() != 1 ||
                        predecessor->predecessors()[0] !=
                            oddCompute)
                        continue;

                    bool computesThreeTimesOdd = false;
                    for (const MachineInstr &instruction :
                         oddCompute->instructions())
                        if (instruction.opcode() ==
                                Opcode::ADDWlsl &&
                            instruction.operands().size() == 4 &&
                            sameVReg(
                                instruction.operands()[0],
                                update->operands()[1]) &&
                            sameVReg(
                                instruction.operands()[1],
                                state) &&
                            sameVReg(
                                instruction.operands()[2],
                                state) &&
                            instruction.operands()[3].kind() ==
                                MachineOperand::Kind::Immediate &&
                            instruction.operands()[3].immediate() ==
                                1)
                            computesThreeTimesOdd = true;
                    if (computesThreeTimesOdd) {
                        oddContinuation = predecessor;
                        break;
                    }
                }
            }

            VReg reversed = function.registerInfo()
                .createVirtualRegister(RegClass::GPR32,
                                       ValueType::I32);
            MachineInstr reverse(Opcode::RBITW);
            reverse
                .addOperand(MachineOperand::vreg(
                    reversed, RegClass::GPR32, true))
                .addOperand(state);
            auto reversedDefinition =
                even->instructions().insert(shift,
                                             std::move(reverse));
            function.registerInfo().setDefinition(
                reversed, &*reversedDefinition);

            VReg shiftAmount = function.registerInfo()
                .createVirtualRegister(RegClass::GPR32,
                                       ValueType::I32);
            MachineInstr countZeros(Opcode::CLZW);
            countZeros
                .addOperand(MachineOperand::vreg(
                    shiftAmount, RegClass::GPR32, true))
                .addOperand(MachineOperand::vreg(
                    reversed, RegClass::GPR32));
            auto countDefinition =
                even->instructions().insert(
                    shift, std::move(countZeros));
            function.registerInfo().setDefinition(
                shiftAmount, &*countDefinition);

            shift->setOpcode(Opcode::ASRWrr);
            shift->operands()[2] = MachineOperand::vreg(
                shiftAmount, RegClass::GPR32);
            even->instructions().erase(toLatch);

            MachineInstr batchedIncrement(Opcode::ADDWrr);
            batchedIncrement
                .addOperand(canReuseLoopCount
                                ? MachineOperand::vreg(
                                      increment->operands()[1]
                                          .virtualRegister(),
                                      increment->operands()[1]
                                          .regClass(),
                                      true)
                                : increment->operands()[0])
                .addOperand(increment->operands()[1])
                .addOperand(MachineOperand::vreg(
                    shiftAmount, RegClass::GPR32));
            if (incrementOnEdges)
                *increment = std::move(batchedIncrement);
            else
                even->append(std::move(batchedIncrement));
            even->append(*compare);
            even->append(*exitBranch);
            even->append(*continueBranch);

            even->removeSuccessor(latch);
            even->addSuccessor(exit);
            even->addSuccessor(continuation);

            // The fallthrough of TBZ proves the state is odd.  If an edge
            // computes `3*state + 1`, modular low-bit arithmetic proves the
            // result is even, so it may enter the same batched halving
            // sequence directly.  Clone with fresh virtual temporaries;
            // no physical scratch or input-dependent speculation is used.
            if (oddContinuation) {
                bool oddIncrementOnEdge =
                    oddContinuation->instructions().size() == 5;
                auto oddIncrement =
                    oddContinuation->instructions().begin();
                auto update = oddIncrementOnEdge
                                  ? std::next(oddIncrement)
                                  : oddIncrement;
                auto oddCountCopy = std::next(update);
                auto copy = oddIncrementOnEdge
                                ? std::next(oddCountCopy)
                                : std::next(update);
                auto oldBranch = std::next(copy);
                auto oddBatchInsert = oddIncrementOnEdge
                                          ? oddCountCopy
                                          : copy;
                MachineOperand updatedState =
                    MachineOperand::vreg(
                        update->operands()[0]
                            .virtualRegister(),
                        update->operands()[0].regClass());

                VReg oddReversed = function.registerInfo()
                    .createVirtualRegister(RegClass::GPR32,
                                           ValueType::I32);
                MachineInstr reverseOdd(Opcode::RBITW);
                reverseOdd
                    .addOperand(MachineOperand::vreg(
                        oddReversed, RegClass::GPR32, true))
                    .addOperand(updatedState);
                auto reverseOddDefinition =
                    oddContinuation->instructions().insert(
                        oddBatchInsert, std::move(reverseOdd));
                function.registerInfo().setDefinition(
                    oddReversed, &*reverseOddDefinition);

                VReg oddShiftAmount = function.registerInfo()
                    .createVirtualRegister(RegClass::GPR32,
                                           ValueType::I32);
                MachineInstr countOddZeros(Opcode::CLZW);
                countOddZeros
                    .addOperand(MachineOperand::vreg(
                        oddShiftAmount, RegClass::GPR32, true))
                    .addOperand(MachineOperand::vreg(
                        oddReversed, RegClass::GPR32));
                auto oddCountDefinition =
                    oddContinuation->instructions().insert(
                        oddBatchInsert, std::move(countOddZeros));
                function.registerInfo().setDefinition(
                    oddShiftAmount, &*oddCountDefinition);

                MachineOperand shiftedState =
                    copy->operands()[0];
                copy->setOpcode(Opcode::ASRWrr);
                copy->operands().clear();
                copy->addOperand(shiftedState)
                    .addOperand(updatedState)
                    .addOperand(MachineOperand::vreg(
                        oddShiftAmount, RegClass::GPR32));
                oddContinuation->instructions().erase(oldBranch);

                if (oddIncrementOnEdge) {
                    MachineOperand countDestination = canReuseLoopCount
                        ? MachineOperand::vreg(
                              increment->operands()[1]
                                  .virtualRegister(),
                              increment->operands()[1].regClass(),
                              true)
                        : oddCountCopy->operands()[0];
                    oddCountCopy->setOpcode(Opcode::ADDWrr);
                    oddCountCopy->operands().clear();
                    oddCountCopy
                        ->addOperand(countDestination)
                        .addOperand(oddIncrement->operands()[0])
                        .addOperand(MachineOperand::vreg(
                            oddShiftAmount, RegClass::GPR32));
                } else {
                    VReg batchedCount = function.registerInfo()
                        .createVirtualRegister(RegClass::GPR32,
                                               ValueType::I32);
                    MachineInstr addShifts(Opcode::ADDWrr);
                    addShifts
                        .addOperand(MachineOperand::vreg(
                            batchedCount, RegClass::GPR32, true))
                        .addOperand(increment->operands()[1])
                        .addOperand(MachineOperand::vreg(
                            oddShiftAmount, RegClass::GPR32));
                    MachineInstr &addShiftsDefinition =
                        oddContinuation->append(
                            std::move(addShifts));
                    function.registerInfo().setDefinition(
                        batchedCount, &addShiftsDefinition);

                    MachineInstr addOddIteration(Opcode::ADDWri);
                    addOddIteration
                        .addOperand(increment->operands()[0])
                        .addOperand(MachineOperand::vreg(
                            batchedCount, RegClass::GPR32))
                        .addOperand(MachineOperand::immediate(1));
                    oddContinuation->append(
                        std::move(addOddIteration));
                }
                oddContinuation->append(*compare);
                oddContinuation->append(*exitBranch);
                oddContinuation->append(*continueBranch);
                oddContinuation->removeSuccessor(latch);
                oddContinuation->addSuccessor(exit);
                oddContinuation->addSuccessor(continuation);
            }
            if (canReuseLoopCount) {
                exitCountCopy->operands()[1] = MachineOperand::vreg(
                    increment->operands()[1].virtualRegister(),
                    increment->operands()[1].regClass());
                even->instructions().erase(countCopy);
                for (auto copyIt = continuation->instructions().begin();
                     copyIt != continuation->instructions().end();
                     ++copyIt) {
                    if (&*copyIt != continuationCountCopy)
                        continue;
                    continuation->instructions().erase(copyIt);
                    break;
                }
            }
            function.clearProperty(
                MachineProperty::TracksLiveness);
            changed = true;
            batched = true;
            break;
        }
    }
    if (changed)
        function.clearProperty(
            MachineProperty::TracksLiveness);
    return changed;
}

} // namespace backend::aarch64
