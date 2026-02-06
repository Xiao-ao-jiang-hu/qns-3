/*
  Entanglement Swapping Fidelity Test with Decoherence

  This example creates a simple 3-node chain (A-B-C) to test the physical fidelity
  calculation after entanglement swapping, including decoherence effects.

  1. Generate perfect EPR pairs (fidelity = 1.0) between A-B and B-C
  2. Wait for 10ms to simulate decoherence (T2 = 100ms)
  3. Node B performs entanglement swapping (Bell measurement + classical communication)
  4. Measure the fidelity of the resulting A-C entanglement

  All operations use physical quantum operations provided by the quantum module,
  not theoretical formulas.

  To run this example:
  NS_LOG="QuantumNetworkSimulator=info:QuantumErrorModel=logic:EntSwapFidelityTest=info|logic" ./ns3
  run ent-swap-fidelity-test
*/

#include "ns3/core-module.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-error-model.h"
#include "ns3/quantum-memory.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-operation.h"
#include "ns3/quantum-phy-entity.h"

#include <cmath>
#include <complex>
#include <iostream>

NS_LOG_COMPONENT_DEFINE("EntSwapFidelityTest");

using namespace ns3;

// Decoherence parameters
const double T2_DECOHERENCE_TIME = 0.1; // 100ms decoherence time (T2) in seconds
const double WAIT_TIME = 0.01;          // 10ms wait time before swapping in seconds

int
main()
{
    NS_LOG_INFO("=== Entanglement Swapping Fidelity Test with Decoherence ===");
    NS_LOG_INFO("Creating 3-node chain: A - B - C");
    NS_LOG_INFO("Decoherence time T2 = " << T2_DECOHERENCE_TIME * 1000 << " ms");
    NS_LOG_INFO("Wait time before swapping = " << WAIT_TIME * 1000 << " ms");

    //
    // Create a quantum physical entity with 3 nodes (A, B, C) plus "God" for global operations
    //
    std::vector<std::string> owners = {"God", "A", "B", "C"};
    Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity>(owners);

    //
    // Set up quantum memory decoherence model (TimeModel) for each node
    // T2 = 100ms decoherence time
    //
    NS_LOG_INFO("");
    NS_LOG_INFO("Setting up quantum memory with T2 = " << T2_DECOHERENCE_TIME * 1000 << " ms");
    qphyent->SetTimeModel("A", T2_DECOHERENCE_TIME);
    qphyent->SetTimeModel("B", T2_DECOHERENCE_TIME);
    qphyent->SetTimeModel("C", T2_DECOHERENCE_TIME);

    //
    // Define qubit names
    // A_QubitToB: A's qubit entangled with B
    // B_QubitFromA: B's qubit entangled with A
    // B_QubitToC: B's qubit entangled with C
    // C_QubitFromB: C's qubit entangled with B
    //
    std::string A_qubit = "A_QubitToB";
    std::string B_qubit_from_A = "B_QubitFromA";
    std::string B_qubit_to_C = "B_QubitToC";
    std::string C_qubit = "C_QubitFromB";

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 1: Generate perfect EPR pairs (fidelity = 1.0)");
    NS_LOG_INFO("  - EPR pair between A and B: (" << A_qubit << ", " << B_qubit_from_A << ")");
    NS_LOG_INFO("  - EPR pair between B and C: (" << B_qubit_to_C << ", " << C_qubit << ")");

    //
    // Step 1: Generate perfect EPR pairs with fidelity = 1.0
    // Using pure Bell state |Φ+⟩ = (|00⟩ + |11⟩)/√2
    //

    // Generate EPR pair between A and B (perfect fidelity)
    qphyent->GenerateQubitsPure("God", q_bell, {A_qubit, B_qubit_from_A});

    // Generate EPR pair between B and C (perfect fidelity)
    qphyent->GenerateQubitsPure("God", q_bell, {B_qubit_to_C, C_qubit});

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 2: Verify initial EPR pair fidelities (at t=0)");

    // Calculate fidelity of A-B EPR pair before swapping
    double fidel_AB_initial;
    qphyent->CalculateFidelity({A_qubit, B_qubit_from_A}, fidel_AB_initial);
    NS_LOG_INFO("  - Fidelity of A-B EPR pair: " << fidel_AB_initial);

    // Calculate fidelity of B-C EPR pair before swapping
    double fidel_BC_initial;
    qphyent->CalculateFidelity({B_qubit_to_C, C_qubit}, fidel_BC_initial);
    NS_LOG_INFO("  - Fidelity of B-C EPR pair: " << fidel_BC_initial);

    //
    // Step 3: Wait for 10ms to simulate decoherence
    // Apply time-dependent dephasing error to all qubits
    // The dephasing probability is: p = (1 - e^(-t/T2)) / 2
    //
    NS_LOG_INFO("");
    NS_LOG_INFO("Step 3: Wait for " << WAIT_TIME * 1000 << " ms (simulating storage decoherence)");

    // Calculate dephasing probability for the wait time
    double prob_dephase = (1 - exp(-WAIT_TIME / T2_DECOHERENCE_TIME)) / 2;
    NS_LOG_INFO("  - Dephasing probability per qubit: " << prob_dephase);

    // Create dephasing operation (phase flip channel)
    // With probability (1-p): apply I (no error)
    // With probability p: apply Z (phase flip)
    QuantumOperation time_error = {{"I", "PZ"},
                                   {pauli_I, pauli_Z},
                                   {1 - prob_dephase, prob_dephase}};

    // Apply decoherence to all qubits (simulating storage in quantum memory)
    NS_LOG_INFO("  - Applying dephasing to all qubits...");
    qphyent->ApplyOperation(time_error, {A_qubit});
    qphyent->ApplyOperation(time_error, {B_qubit_from_A});
    qphyent->ApplyOperation(time_error, {B_qubit_to_C});
    qphyent->ApplyOperation(time_error, {C_qubit});

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 4: Verify EPR pair fidelities after decoherence (at t=" << WAIT_TIME * 1000
                                                                              << " ms)");

    double fidel_AB_after_wait;
    qphyent->CalculateFidelity({A_qubit, B_qubit_from_A}, fidel_AB_after_wait);
    NS_LOG_INFO("  - Fidelity of A-B EPR pair: " << fidel_AB_after_wait);

    double fidel_BC_after_wait;
    qphyent->CalculateFidelity({B_qubit_to_C, C_qubit}, fidel_BC_after_wait);
    NS_LOG_INFO("  - Fidelity of B-C EPR pair: " << fidel_BC_after_wait);

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 5: Node B performs entanglement swapping");
    NS_LOG_INFO("  - Bell measurement on B's qubits: (" << B_qubit_from_A << ", " << B_qubit_to_C
                                                        << ")");

    //
    // Step 5: Node B performs entanglement swapping
    // Bell measurement = CNOT + Hadamard + Measurement
    //

    // Apply CNOT gate: control = B_qubit_to_C, target = B_qubit_from_A
    qphyent->ApplyGate("God",
                       QNS_GATE_PREFIX + "CNOT",
                       std::vector<std::complex<double>>{},
                       std::vector<std::string>{B_qubit_to_C, B_qubit_from_A});

    // Apply Hadamard gate to B_qubit_from_A
    qphyent->ApplyGate("God",
                       QNS_GATE_PREFIX + "H",
                       std::vector<std::complex<double>>{},
                       std::vector<std::string>{B_qubit_from_A});

    // Measure B's qubits
    std::pair<unsigned, std::vector<double>> outcome_B_from_A =
        qphyent->Measure("God", {B_qubit_from_A});
    NS_LOG_INFO("  - Measurement outcome of " << B_qubit_from_A << ": " << outcome_B_from_A.first);

    std::pair<unsigned, std::vector<double>> outcome_B_to_C =
        qphyent->Measure("God", {B_qubit_to_C});
    NS_LOG_INFO("  - Measurement outcome of " << B_qubit_to_C << ": " << outcome_B_to_C.first);

    NS_LOG_INFO("");
    NS_LOG_INFO("Step 6: Apply correction gates on C based on measurement outcomes");

    //
    // Step 6: Apply correction gates on C based on measurement outcomes
    // If outcome_B_to_C = 1, apply X gate to C
    // If outcome_B_from_A = 1, apply Z gate to C
    //

    if (outcome_B_to_C.first == 1)
    {
        NS_LOG_INFO("  - Applying X correction to " << C_qubit);
        qphyent->ApplyGate("God",
                           QNS_GATE_PREFIX + "PX",
                           std::vector<std::complex<double>>{},
                           std::vector<std::string>{C_qubit});
    }
    else
    {
        NS_LOG_INFO("  - No X correction needed");
    }

    if (outcome_B_from_A.first == 1)
    {
        NS_LOG_INFO("  - Applying Z correction to " << C_qubit);
        qphyent->ApplyGate("God",
                           QNS_GATE_PREFIX + "PZ",
                           std::vector<std::complex<double>>{},
                           std::vector<std::string>{C_qubit});
    }
    else
    {
        NS_LOG_INFO("  - No Z correction needed");
    }

    //
    // Step 7: Trace out B's measured qubits (they are no longer part of the system)
    //
    NS_LOG_INFO("");
    NS_LOG_INFO("Step 7: Trace out B's measured qubits");
    qphyent->PartialTrace({B_qubit_from_A, B_qubit_to_C});

    //
    // Step 8: Calculate the fidelity of the resulting A-C entanglement
    //
    NS_LOG_INFO("");
    NS_LOG_INFO("Step 8: Calculate fidelity of resulting A-C entanglement");

    double fidel_AC;
    qphyent->CalculateFidelity({A_qubit, C_qubit}, fidel_AC);

    NS_LOG_INFO("");
    NS_LOG_INFO("=== Results ===");
    NS_LOG_INFO("Decoherence time T2: " << T2_DECOHERENCE_TIME * 1000 << " ms");
    NS_LOG_INFO("Wait time: " << WAIT_TIME * 1000 << " ms");
    NS_LOG_INFO("");
    NS_LOG_INFO("Initial A-B EPR fidelity (t=0): " << fidel_AB_initial);
    NS_LOG_INFO("Initial B-C EPR fidelity (t=0): " << fidel_BC_initial);
    NS_LOG_INFO("");
    NS_LOG_INFO("A-B EPR fidelity after wait: " << fidel_AB_after_wait);
    NS_LOG_INFO("B-C EPR fidelity after wait: " << fidel_BC_after_wait);
    NS_LOG_INFO("");
    NS_LOG_INFO("Final A-C EPR fidelity after swapping: " << fidel_AC);

    // Calculate theoretical fidelity degradation
    // For dephasing channel: F = (1 + e^(-t/T2))/2 for each qubit
    // For EPR pair with both qubits dephasing: more complex
    double expected_single_qubit_fidelity = (1 + exp(-WAIT_TIME / T2_DECOHERENCE_TIME)) / 2;
    NS_LOG_INFO("");
    NS_LOG_INFO(
        "Theoretical single-qubit coherence after wait: " << expected_single_qubit_fidelity);

    // For perfect initial EPR pairs, the final fidelity should be less than 1.0 due to decoherence
    if (fidel_AC < 1.0 - 1e-6)
    {
        NS_LOG_INFO("");
        NS_LOG_INFO("SUCCESS: Decoherence effect observed! Fidelity reduced from 1.0 to "
                    << fidel_AC);
    }
    else
    {
        NS_LOG_INFO("Note: No significant decoherence observed.");
    }

    return 0;
}