/*
  Entanglement Swapping Fidelity Test with Automatic Decoherence

  This example creates a simple 3-node chain (A-B-C) to test the physical fidelity
  calculation after entanglement swapping, with automatic decoherence based on
  ns-3 simulation timeline.

  1. Generate perfect EPR pairs (fidelity = 1.0) between A-B and B-C
  2. Wait for 10ms to simulate decoherence (T2 = 100ms) using Simulator::Schedule
  3. Node B performs entanglement swapping (Bell measurement + classical communication)
  4. Measure the fidelity of the resulting A-C entanglement

  The decoherence is applied automatically when quantum operations (gates, measurements)
  are performed on qubits, based on the simulation time elapsed since the last operation.

  Expected final fidelity: 0.83516

  To run this example:
  NS_LOG="QuantumNetworkSimulator=info:QuantumErrorModel=logic:EntSwapFidelityTest=info|logic" ./ns3 run swap-fidelity-test
*/

#include "ns3/core-module.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-error-model.h"
#include "ns3/quantum-memory.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-operation.h"
#include "ns3/quantum-phy-entity.h"

#include "ns3/random-variable-stream.h"

#include <cmath>
#include <complex>
#include <ctime>
#include <iostream>

NS_LOG_COMPONENT_DEFINE("EntSwapFidelityTest");

using namespace ns3;

const double T2_DECOHERENCE_TIME = 0.1;
const double MIN_WAIT_TIME = 0.001;
const double MAX_WAIT_TIME = 0.05;

Ptr<QuantumPhyEntity> g_qphyent;
std::string g_A_qubit;
std::string g_B_qubit_from_A;
std::string g_B_qubit_to_C;
std::string g_C_qubit;

double g_wait_time = 0.0;
double g_fidel_AB_initial = 0.0;
double g_fidel_BC_initial = 0.0;
double g_fidel_AB_after_wait = 0.0;
double g_fidel_BC_after_wait = 0.0;
double g_fidel_AC_final = 0.0;

Ptr<UniformRandomVariable> g_random_wait;

void
PerformEntanglementSwap()
{
    NS_LOG_INFO("");
    NS_LOG_INFO("Step 5: Ensure decoherence is applied to all qubits");
    g_qphyent->EnsureAllDecoherence();

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 6: Node B performs entanglement swapping");
    NS_LOG_INFO("  - Bell measurement on B's qubits: (" << g_B_qubit_from_A << ", " << g_B_qubit_to_C << ")");

    g_qphyent->ApplyGate("God",
                         QNS_GATE_PREFIX + "CNOT",
                         std::vector<std::complex<double>>{},
                         std::vector<std::string>{g_B_qubit_to_C, g_B_qubit_from_A});

    g_qphyent->ApplyGate("God",
                         QNS_GATE_PREFIX + "H",
                         std::vector<std::complex<double>>{},
                         std::vector<std::string>{g_B_qubit_from_A});

    std::pair<unsigned, std::vector<double>> outcome_B_from_A =
        g_qphyent->Measure("God", {g_B_qubit_from_A});
    NS_LOG_INFO("  - Measurement outcome of " << g_B_qubit_from_A << ": " << outcome_B_from_A.first);

    std::pair<unsigned, std::vector<double>> outcome_B_to_C =
        g_qphyent->Measure("God", {g_B_qubit_to_C});
    NS_LOG_INFO("  - Measurement outcome of " << g_B_qubit_to_C << ": " << outcome_B_to_C.first);

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 7: Apply correction gates on C based on measurement outcomes");

    if (outcome_B_to_C.first == 1)
    {
        NS_LOG_INFO("  - Applying X correction to " << g_C_qubit);
        g_qphyent->ApplyGate("God",
                             QNS_GATE_PREFIX + "PX",
                             std::vector<std::complex<double>>{},
                             std::vector<std::string>{g_C_qubit});
    }
    else
    {
        NS_LOG_INFO("  - No X correction needed");
    }

    if (outcome_B_from_A.first == 1)
    {
        NS_LOG_INFO("  - Applying Z correction to " << g_C_qubit);
        g_qphyent->ApplyGate("God",
                             QNS_GATE_PREFIX + "PZ",
                             std::vector<std::complex<double>>{},
                             std::vector<std::string>{g_C_qubit});
    }
    else
    {
        NS_LOG_INFO("  - No Z correction needed");
    }

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 8: Trace out B's measured qubits");
    g_qphyent->PartialTrace({g_B_qubit_from_A, g_B_qubit_to_C});

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 9: Calculate fidelity of resulting A-C entanglement");

    g_qphyent->CalculateFidelity({g_A_qubit, g_C_qubit}, g_fidel_AC_final);

    Simulator::Stop();
}

void
AfterWait()
{
    NS_LOG_INFO("");
    NS_LOG_INFO("Step 4: Verify EPR pair fidelities after decoherence (at t=" << g_wait_time * 1000 << " ms)");

    g_qphyent->CalculateFidelity({g_A_qubit, g_B_qubit_from_A}, g_fidel_AB_after_wait);
    NS_LOG_INFO("  - Fidelity of A-B EPR pair: " << g_fidel_AB_after_wait);

    g_qphyent->CalculateFidelity({g_B_qubit_to_C, g_C_qubit}, g_fidel_BC_after_wait);
    NS_LOG_INFO("  - Fidelity of B-C EPR pair: " << g_fidel_BC_after_wait);

    PerformEntanglementSwap();
}

void
ScheduleWaitAndSwap()
{
    g_wait_time = g_random_wait->GetValue(MIN_WAIT_TIME, MAX_WAIT_TIME);
    g_wait_time = 0.01;

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 3: Schedule swap operation after wait");
    NS_LOG_INFO("  - Wait time: " << g_wait_time * 1000 << " ms");
    NS_LOG_INFO("  - Automatic decoherence will be applied based on simulation time");

    Simulator::Schedule(Seconds(g_wait_time), &AfterWait);
}

int
main()
{
    RngSeedManager::SetSeed(time(nullptr));
    g_random_wait = CreateObject<UniformRandomVariable>();

    NS_LOG_INFO("=== Entanglement Swapping Fidelity Test with Automatic Decoherence ===");
    NS_LOG_INFO("Creating 3-node chain: A - B - C");
    NS_LOG_INFO("Decoherence time T2 = " << T2_DECOHERENCE_TIME * 1000 << " ms");
    NS_LOG_INFO("Wait time range: " << MIN_WAIT_TIME * 1000 << " - " << MAX_WAIT_TIME * 1000 << " ms");

    std::vector<std::string> owners = {"God", "A", "B", "C"};
    g_qphyent = CreateObject<QuantumPhyEntity>(owners);

    NS_LOG_INFO("");
    NS_LOG_INFO("Setting up quantum memory with T2 = " << T2_DECOHERENCE_TIME * 1000 << " ms");
    g_qphyent->SetTimeModel("God", T2_DECOHERENCE_TIME);
    g_qphyent->SetTimeModel("A", T2_DECOHERENCE_TIME);
    g_qphyent->SetTimeModel("B", T2_DECOHERENCE_TIME);
    g_qphyent->SetTimeModel("C", T2_DECOHERENCE_TIME);

    g_A_qubit = "A_QubitToB";
    g_B_qubit_from_A = "B_QubitFromA";
    g_B_qubit_to_C = "B_QubitToC";
    g_C_qubit = "C_QubitFromB";

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 1: Generate perfect EPR pairs (fidelity = 1.0)");
    NS_LOG_INFO("  - EPR pair between A and B: (" << g_A_qubit << ", " << g_B_qubit_from_A << ")");
    NS_LOG_INFO("  - EPR pair between B and C: (" << g_B_qubit_to_C << ", " << g_C_qubit << ")");

    g_qphyent->GenerateQubitsPure("God", q_bell, {g_A_qubit, g_B_qubit_from_A});
    g_qphyent->GenerateQubitsPure("God", q_bell, {g_B_qubit_to_C, g_C_qubit});

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 2: Verify initial EPR pair fidelities (at t=0)");

    g_qphyent->CalculateFidelity({g_A_qubit, g_B_qubit_from_A}, g_fidel_AB_initial);
    NS_LOG_INFO("  - Fidelity of A-B EPR pair: " << g_fidel_AB_initial);

    g_qphyent->CalculateFidelity({g_B_qubit_to_C, g_C_qubit}, g_fidel_BC_initial);
    NS_LOG_INFO("  - Fidelity of B-C EPR pair: " << g_fidel_BC_initial);

    ScheduleWaitAndSwap();

    Simulator::Run();
    Simulator::Destroy();

    NS_LOG_INFO("");
    NS_LOG_INFO("=== Results ===");
    NS_LOG_INFO("Random wait time: " << g_wait_time * 1000 << " ms");
    NS_LOG_INFO("Initial A-B fidelity: " << g_fidel_AB_initial);
    NS_LOG_INFO("Initial B-C fidelity: " << g_fidel_BC_initial);
    NS_LOG_INFO("After wait A-B fidelity: " << g_fidel_AB_after_wait);
    NS_LOG_INFO("After wait B-C fidelity: " << g_fidel_BC_after_wait);
    NS_LOG_INFO("Final A-C fidelity: " << g_fidel_AC_final);

    return 0;
}