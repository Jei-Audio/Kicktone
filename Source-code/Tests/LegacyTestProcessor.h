#pragma once
#include "../Source/PluginProcessor.h"
// Historical absolute-sample assertions exercise the legacy zero-latency mode.
// New default FIR/PDC behavior is covered separately by LinearIntegrationTests.
struct LegacyTestProcessor : PunchTailProcessor {
    LegacyTestProcessor(){auto* p=params.getParameter("tailVerbLinearPhase");p->setValueNotifyingHost(0.f);}
};
