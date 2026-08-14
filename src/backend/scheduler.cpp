#include "backend/scheduler.hpp"

#include "backend/live_range.hpp"

#include <algorithm>
#include <cstdint>
#include <list>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace backend::aarch64 {
namespace {

using RegisterKey = std::uint64_t;

RegisterKey registerKey(const MachineOperand &operand) {
	if (operand.isPhysicalRegister())
		return (1ULL << 63) | static_cast<unsigned>(operand.physicalRegister());
	return operand.virtualRegister();
}

RegisterKey registerKey(PhysReg reg) {
	return (1ULL << 63) | static_cast<unsigned>(reg);
}

struct Node {
	std::set<RegisterKey> defs;
	std::set<RegisterKey> uses;
	std::set<VReg> virtualDefs;
	std::set<VReg> virtualUses;
	std::vector<unsigned> successors;
	unsigned predecessors = 0;
	unsigned height = 0;
	unsigned latency = 1;
	SchedResource resource = SchedResource::None;
	bool load = false;
	bool store = false;
	const MachineInstr *instruction = nullptr;
};

struct RegisterPressure {
	unsigned gpr = 0;
	unsigned vector = 0;
};

struct PressureMetric {
	unsigned gprArea = 0;
	unsigned vectorArea = 0;
	unsigned peakGPRExcess = 0;
	unsigned gprExcessArea = 0;
	unsigned peakVectorExcess = 0;
	unsigned vectorExcessArea = 0;
};

unsigned allocatableRegisterCount(RegClass regClass) {
	std::set<PhysReg> registers;
	for (PhysReg reg : RegisterInfo::allocationOrder(regClass, true))
		if (!RegisterInfo::isReserved(reg))
			registers.insert(reg);
	return static_cast<unsigned>(registers.size());
}

bool vectorBank(RegClass regClass) {
	return regClass == RegClass::FPR32 || regClass == RegClass::NEON128;
}

// Which shared physical register bank a virtual register consumes, or
// Bank::None when it occupies neither.
enum class Bank { None, GPR, Vector };

Bank bankOf(const MachineFunction &function, VReg reg) {
	if (!function.registerInfo().contains(reg))
		return Bank::None;
	RegClass regClass = function.registerInfo().get(reg).regClass;
	if (vectorBank(regClass))
		return Bank::Vector;
	if (regClass == RegClass::GPR32 || regClass == RegClass::GPR64)
		return Bank::GPR;
	return Bank::None;
}

RegisterPressure pressureOf(const MachineFunction &function,
                            const std::set<VReg> &live) {
	RegisterPressure pressure;
	for (VReg reg : live) {
		switch (bankOf(function, reg)) {
		case Bank::Vector:
			++pressure.vector;
			break;
		case Bank::GPR:
			++pressure.gpr;
			break;
		case Bank::None:
			break;
		}
	}
	return pressure;
}

void updateMetric(PressureMetric &metric, const RegisterPressure &pressure,
                  const RegisterPressure &capacity) {
	metric.gprArea += pressure.gpr;
	metric.vectorArea += pressure.vector;
	const unsigned gprExcess =
	    pressure.gpr > capacity.gpr ? pressure.gpr - capacity.gpr : 0;
	const unsigned vectorExcess = pressure.vector > capacity.vector
	                                  ? pressure.vector - capacity.vector
	                                  : 0;
	metric.peakGPRExcess = std::max(metric.peakGPRExcess, gprExcess);
	metric.gprExcessArea += gprExcess;
	metric.peakVectorExcess = std::max(metric.peakVectorExcess, vectorExcess);
	metric.vectorExcessArea += vectorExcess;
}

bool pressureMetricStrictlyBetter(const PressureMetric &candidate,
                                  const PressureMetric &baseline) {
	const bool nonWorse =
	    candidate.gprArea <= baseline.gprArea &&
	    candidate.vectorArea <= baseline.vectorArea &&
	    candidate.peakGPRExcess <= baseline.peakGPRExcess &&
	    candidate.gprExcessArea <= baseline.gprExcessArea &&
	    candidate.peakVectorExcess <= baseline.peakVectorExcess &&
	    candidate.vectorExcessArea <= baseline.vectorExcessArea;
	const bool strictlyBetter =
	    candidate.peakGPRExcess < baseline.peakGPRExcess ||
	    candidate.gprExcessArea < baseline.gprExcessArea ||
	    candidate.peakVectorExcess < baseline.peakVectorExcess ||
	    candidate.vectorExcessArea < baseline.vectorExcessArea;
	return nonWorse && strictlyBetter;
}

bool intersects(const std::set<RegisterKey> &lhs,
                const std::set<RegisterKey> &rhs) {
	auto a = lhs.begin();
	auto b = rhs.begin();
	while (a != lhs.end() && b != rhs.end()) {
		if (*a == *b)
			return true;
		if (*a < *b)
			++a;
		else
			++b;
	}
	return false;
}

std::optional<unsigned> immediateMemoryBaseIndex(Opcode opcode) {
	switch (opcode) {
	case Opcode::LDRWui:
	case Opcode::STRWui:
	case Opcode::LDRSui:
	case Opcode::STRSui:
	case Opcode::LDRDui:
	case Opcode::STRDui:
	case Opcode::LDRQui:
	case Opcode::STRQui:
	case Opcode::LDRXui:
	case Opcode::STRXui:
		return 1;
	case Opcode::LDPWi:
	case Opcode::STPWi:
	case Opcode::LDPSi:
	case Opcode::STPSi:
	case Opcode::LDPXi:
	case Opcode::STPXi:
	case Opcode::LDPDi:
	case Opcode::STPDi:
	case Opcode::LDPQi:
	case Opcode::STPQi:
		return 2;
	default:
		return std::nullopt;
	}
}

std::optional<std::string_view> memorySymbol(const MachineInstr &instruction) {
	for (const MachineOperand &operand : instruction.operands())
		if (operand.kind() == MachineOperand::Kind::GlobalSymbol)
			return operand.symbol();
	return std::nullopt;
}

bool memoryMayAlias(const MachineInstr &lhs, const MachineInstr &rhs) {
	if (lhs.memoryOperands().empty() || rhs.memoryOperands().empty())
		return true;
	const MachineMemOperand &a = lhs.memoryOperands().front();
	const MachineMemOperand &b = rhs.memoryOperands().front();
	if (a.isVolatile || b.isVolatile)
		return true;

	if (a.frameIndex && b.frameIndex && *a.frameIndex != *b.frameIndex)
		return false;

	const std::optional<std::string_view> aSymbol = memorySymbol(lhs);
	const std::optional<std::string_view> bSymbol = memorySymbol(rhs);
	if (aSymbol && bSymbol && *aSymbol != *bSymbol)
		return false;

	const std::optional<unsigned> aBaseIndex =
	    immediateMemoryBaseIndex(lhs.opcode());
	const std::optional<unsigned> bBaseIndex =
	    immediateMemoryBaseIndex(rhs.opcode());
	if (!aBaseIndex || !bBaseIndex ||
	    lhs.operands().size() <= *aBaseIndex + 1 ||
	    rhs.operands().size() <= *bBaseIndex + 1)
		return true;
	const MachineOperand &aBase = lhs.operands()[*aBaseIndex];
	const MachineOperand &bBase = rhs.operands()[*bBaseIndex];
	const MachineOperand &aOffset = lhs.operands()[*aBaseIndex + 1];
	const MachineOperand &bOffset = rhs.operands()[*bBaseIndex + 1];
	if (!aBase.isRegister() || !bBase.isRegister() ||
	    !aBase.isSameRegisterAs(bBase) ||
	    aOffset.kind() != MachineOperand::Kind::Immediate ||
	    bOffset.kind() != MachineOperand::Kind::Immediate || a.size == 0 ||
	    b.size == 0)
		return true;

	const std::int64_t aBegin = aOffset.immediate();
	const std::int64_t bBegin = bOffset.immediate();
	std::int64_t aEnd = 0;
	std::int64_t bEnd = 0;
	if (__builtin_add_overflow(aBegin, static_cast<std::int64_t>(a.size),
	                           &aEnd) ||
	    __builtin_add_overflow(bBegin, static_cast<std::int64_t>(b.size),
	                           &bEnd))
		return true;
	return !(aEnd <= bBegin || bEnd <= aBegin);
}

bool schedulingBarrier(const MachineInstr &instruction) {
	if (instruction.isTerminator() || instruction.isCall() ||
	    instruction.isPseudo() || instruction.hasSideEffects())
		return true;
	if (instruction.parallelCopyGroup)
		return true;
	for (const MachineMemOperand &memory : instruction.memoryOperands())
		if (memory.isVolatile)
			return true;
	return false;
}

bool scheduleRegion(MachineFunction &function,
                    MachineBasicBlock::InstrList &instructions,
                    MachineBasicBlock::InstrList::iterator begin,
                    MachineBasicBlock::InstrList::iterator end,
                    const std::set<VReg> *regionLiveOut) {
	std::vector<MachineBasicBlock::InstrList::iterator> positions;
	for (auto it = begin; it != end; ++it)
		positions.push_back(it);
	constexpr std::size_t kMinimumRegionSize = 3;
	constexpr std::size_t kMaximumRegionSize = 512;
	if (positions.size() < kMinimumRegionSize ||
	    positions.size() > kMaximumRegionSize)
		return false;

	std::vector<Node> nodes(positions.size());
	for (unsigned i = 0; i < positions.size(); ++i) {
		const MachineInstr &instruction = *positions[i];
		const InstrDesc &descriptor = InstrInfo::get(instruction.opcode());
		nodes[i].latency = std::max(1U, descriptor.latency);
		nodes[i].resource = descriptor.resource;
		nodes[i].load = instruction.mayLoad();
		nodes[i].store = instruction.mayStore();
		nodes[i].instruction = &instruction;
		for (const MachineOperand &operand : instruction.operands()) {
			if (!operand.isRegister())
				continue;
			(operand.isDef ? nodes[i].defs : nodes[i].uses)
			    .insert(registerKey(operand));
			if (operand.isVirtualRegister())
				(operand.isDef ? nodes[i].virtualDefs : nodes[i].virtualUses)
				    .insert(operand.virtualRegister());
		}
		if (instruction.readsRegister(PhysReg::NZCV))
			nodes[i].uses.insert(registerKey(PhysReg::NZCV));
		if (instruction.definesRegister(PhysReg::NZCV))
			nodes[i].defs.insert(registerKey(PhysReg::NZCV));
	}

	for (unsigned i = 0; i < nodes.size(); ++i) {
		for (unsigned j = i + 1; j < nodes.size(); ++j) {
			bool registerDependency =
			    intersects(nodes[i].defs, nodes[j].uses) ||
			    intersects(nodes[i].uses, nodes[j].defs) ||
			    intersects(nodes[i].defs, nodes[j].defs);
			bool memoryDependency =
			    (nodes[i].load || nodes[i].store) &&
			    (nodes[j].load || nodes[j].store) &&
			    (nodes[i].store || nodes[j].store) &&
			    memoryMayAlias(*nodes[i].instruction, *nodes[j].instruction);
			if (!registerDependency && !memoryDependency)
				continue;
			nodes[i].successors.push_back(j);
			++nodes[j].predecessors;
		}
	}

	for (unsigned i = static_cast<unsigned>(nodes.size()); i-- > 0;) {
		unsigned successorHeight = 0;
		for (unsigned successor : nodes[i].successors)
			successorHeight =
			    std::max(successorHeight, nodes[successor].height);
		nodes[i].height = nodes[i].latency + successorHeight;
	}

	const RegisterPressure capacity{
	    allocatableRegisterCount(RegClass::GPR64),
	    allocatableRegisterCount(RegClass::NEON128)};

	auto initialUseCounts = [&]() {
		std::unordered_map<VReg, unsigned> counts;
		for (const Node &node : nodes)
			for (VReg reg : node.virtualUses)
				++counts[reg];
		return counts;
	};
	auto regionLiveIn = [&]() {
		std::set<VReg> live = regionLiveOut ? *regionLiveOut : std::set<VReg>{};
		for (auto node = nodes.rbegin(); node != nodes.rend(); ++node) {
			for (VReg reg : node->virtualDefs)
				live.erase(reg);
			live.insert(node->virtualUses.begin(), node->virtualUses.end());
		}
		return live;
	};
	auto applyNode = [&](unsigned index, std::set<VReg> &live,
	                     std::unordered_map<VReg, unsigned> &remainingUses) {
		const Node &node = nodes[index];
		for (VReg reg : node.virtualUses) {
			auto found = remainingUses.find(reg);
			if (found != remainingUses.end() && found->second > 0)
				--found->second;
			if ((found == remainingUses.end() || found->second == 0) &&
			    (!regionLiveOut || !regionLiveOut->count(reg)))
				live.erase(reg);
		}
		for (VReg reg : node.virtualDefs) {
			live.erase(reg);
			auto found = remainingUses.find(reg);
			const bool usedLater =
			    found != remainingUses.end() && found->second != 0;
			if (usedLater || (regionLiveOut && regionLiveOut->count(reg)))
				live.insert(reg);
		}
	};
	// Pressure after scheduling `index`, computed as a delta against the
	// current pressure instead of rebuilding the live set.  This mirrors
	// applyNode exactly: a use dies when it has no remaining later use and
	// is not live-out, and a def occupies a register only when it is used
	// later or live-out.
	auto projectedPressure =
	    [&](unsigned index, const RegisterPressure &current,
	        const std::set<VReg> &live,
	        const std::unordered_map<VReg, unsigned> &remainingUses) {
		    RegisterPressure projected = current;
		    auto adjust = [&](VReg reg, int delta) {
			    switch (bankOf(function, reg)) {
			    case Bank::Vector:
				    projected.vector = static_cast<unsigned>(
				        static_cast<int>(projected.vector) + delta);
				    break;
			    case Bank::GPR:
				    projected.gpr = static_cast<unsigned>(
				        static_cast<int>(projected.gpr) + delta);
				    break;
			    case Bank::None:
				    break;
			    }
		    };
		    const Node &node = nodes[index];
		    auto finalIn = [&](VReg reg, unsigned consumed) {
			    auto found = remainingUses.find(reg);
			    const unsigned remaining =
			        found == remainingUses.end()
			            ? 0
			            : (found->second > consumed ? found->second - consumed
			                                        : 0);
			    return remaining != 0 ||
			           (regionLiveOut && regionLiveOut->count(reg));
		    };
		    for (VReg reg : node.virtualUses)
			    if (!node.virtualDefs.count(reg) && live.count(reg) &&
			        !finalIn(reg, 1))
				    adjust(reg, -1);
		    for (VReg reg : node.virtualDefs) {
			    const bool wasLive = live.count(reg) != 0;
			    const bool willBeLive =
			        finalIn(reg, node.virtualUses.count(reg) ? 1 : 0);
			    if (wasLive != willBeLive)
				    adjust(reg, willBeLive ? 1 : -1);
		    }
		    return projected;
	    };

	auto buildOrder = [&](bool pressureAware) {
		std::vector<unsigned> remainingPredecessors;
		remainingPredecessors.reserve(nodes.size());
		std::vector<unsigned> ready;
		for (unsigned i = 0; i < nodes.size(); ++i) {
			remainingPredecessors.push_back(nodes[i].predecessors);
			if (!nodes[i].predecessors)
				ready.push_back(i);
		}

		std::set<VReg> live = regionLiveIn();
		auto remainingUses = initialUseCounts();
		std::vector<unsigned> order;
		order.reserve(nodes.size());
		SchedResource previousResource = SchedResource::None;
		RegisterPressure current;
		if (pressureAware)
			current = pressureOf(function, live);
		while (!ready.empty()) {
			auto score = [&](unsigned index) {
				long value = static_cast<long>(nodes[index].height) * 16;
				if (nodes[index].load)
					value += 8;
				if (nodes[index].resource != previousResource)
					value += 2;
				value -= static_cast<long>(index) / 64;
				return value;
			};

			bool pressureRelevant = false;
			if (pressureAware) {
				pressureRelevant = current.gpr > capacity.gpr ||
				                   current.vector > capacity.vector;
				if (!pressureRelevant)
					for (unsigned index : ready) {
						RegisterPressure projected = projectedPressure(
						    index, current, live, remainingUses);
						pressureRelevant |= projected.gpr > capacity.gpr ||
						                    projected.vector > capacity.vector;
					}
			}

			auto best = std::max_element(
			    ready.begin(), ready.end(), [&](unsigned lhs, unsigned rhs) {
				    if (pressureAware && pressureRelevant) {
					    RegisterPressure lhsPressure = projectedPressure(
					        lhs, current, live, remainingUses);
					    RegisterPressure rhsPressure = projectedPressure(
					        rhs, current, live, remainingUses);
					    auto excess = [&](RegisterPressure pressure) {
						    const unsigned gpr =
						        pressure.gpr > capacity.gpr
						            ? pressure.gpr - capacity.gpr
						            : 0;
						    const unsigned vector =
						        pressure.vector > capacity.vector
						            ? pressure.vector - capacity.vector
						            : 0;
						    return std::pair<unsigned, unsigned>{
						        gpr + vector, std::max(gpr, vector)};
					    };
					    auto lhsExcess = excess(lhsPressure);
					    auto rhsExcess = excess(rhsPressure);
					    if (lhsExcess != rhsExcess)
						    return lhsExcess > rhsExcess;
				    }
				    long lhsScore = score(lhs);
				    long rhsScore = score(rhs);
				    return lhsScore != rhsScore ? lhsScore < rhsScore
				                                : lhs > rhs;
			    });
			unsigned selected = *best;
			ready.erase(best);
			order.push_back(selected);
			if (pressureAware)
				current =
				    projectedPressure(selected, current, live, remainingUses);
			applyNode(selected, live, remainingUses);
			previousResource = nodes[selected].resource;
			for (unsigned successor : nodes[selected].successors)
				if (--remainingPredecessors[successor] == 0)
					ready.push_back(successor);
		}
		return order;
	};

	auto metricForOrder = [&](const std::vector<unsigned> &order) {
		PressureMetric metric;
		std::set<VReg> live = regionLiveIn();
		auto remainingUses = initialUseCounts();
		RegisterPressure pressure = pressureOf(function, live);
		updateMetric(metric, pressure, capacity);
		for (unsigned index : order) {
			pressure = projectedPressure(index, pressure, live, remainingUses);
			applyNode(index, live, remainingUses);
			updateMetric(metric, pressure, capacity);
		}
		return metric;
	};

	// Preserve the latency-oriented scheduler as the baseline.  A second
	// order may replace it only when both register banks have a provably
	// non-worse pressure profile.
	std::vector<unsigned> order = buildOrder(false);
	if (regionLiveOut) {
		// Excess pressure is clamped at zero, so a baseline order that
		// already fits both banks can never be improved upon.  Skip the
		// quadratic candidate search entirely for such regions.
		const PressureMetric baselineMetric = metricForOrder(order);
		const bool baselineFits = baselineMetric.peakGPRExcess == 0 &&
		                          baselineMetric.peakVectorExcess == 0;
		if (!baselineFits) {
			std::vector<unsigned> pressureOrder = buildOrder(true);
			if (pressureMetricStrictlyBetter(metricForOrder(pressureOrder),
			                                 baselineMetric))
				order = std::move(pressureOrder);
		}
	}
	if (order.size() != nodes.size())
		return false;
	bool changed = false;
	for (unsigned i = 0; i < order.size(); ++i)
		changed |= order[i] != i;
	if (!changed)
		return false;

	std::list<MachineInstr> scheduled;
	for (unsigned index : order)
		scheduled.splice(scheduled.end(), instructions, positions[index]);
	instructions.splice(end, scheduled);
	return true;
}

} // namespace

bool A53MachineScheduler::run(MachineFunction &function) const {
	const bool preRegAlloc = !function.hasProperty(MachineProperty::NoVRegs);
	LivenessResult liveness;
	if (preRegAlloc)
		liveness = MachineLiveness().run(function);

	bool changed = false;
	for (auto &block : function.blocks()) {
		auto &instructions = block->instructions();
		struct Region {
			MachineBasicBlock::InstrList::iterator begin;
			MachineBasicBlock::InstrList::iterator end;
			std::set<VReg> liveOut;
		};

		std::vector<Region> regions;
		auto regionBegin = instructions.begin();
		for (auto it = instructions.begin(); it != instructions.end();) {
			if (!schedulingBarrier(*it)) {
				++it;
				continue;
			}
			regions.push_back(Region{regionBegin, it, {}});
			++it;
			regionBegin = it;
		}
		regions.push_back(Region{regionBegin, instructions.end(), {}});

		if (preRegAlloc) {
			// Derive every region boundary in one reverse walk.  Calls,
			// copies, and other barriers are part of the following live set
			// even though they are not themselves rescheduled.
			std::set<VReg> live = liveness.blockLiveOut[block.get()];
			auto updateLive = [&](const MachineInstr &instruction) {
				for (const MachineOperand &operand : instruction.operands())
					if (operand.isVirtualRegister() && operand.isDef)
						live.erase(operand.virtualRegister());
				for (const MachineOperand &operand : instruction.operands())
					if (operand.isVirtualRegister() && !operand.isDef)
						live.insert(operand.virtualRegister());
			};
			for (std::size_t index = regions.size(); index-- > 0;) {
				Region &region = regions[index];
				region.liveOut = live;
				for (auto it = region.end; it != region.begin;) {
					--it;
					updateLive(*it);
				}
				if (index != 0)
					updateLive(*regions[index - 1].end);
			}
		}

		for (Region &region : regions)
			changed |=
			    scheduleRegion(function, instructions, region.begin, region.end,
			                   preRegAlloc ? &region.liveOut : nullptr);
	}
	if (changed)
		function.clearProperty(MachineProperty::TracksLiveness);
	return changed;
}

} // namespace backend::aarch64
