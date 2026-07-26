#pragma once

class ArgumentAliasAnalysis;

namespace hira {

class HiraRegion;

bool hoistLoopInvariants(
    HiraRegion &region,
    const ::ArgumentAliasAnalysis *aliasAnalysis = nullptr);

} // namespace hira
