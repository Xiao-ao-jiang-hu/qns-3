/**
 * \file four-node-chain-test.cc
 * \brief Four-node quantum chain with theoretical fidelity calculation
 *
 * This test demonstrates a four-node quantum network (A-B-C-D) protocol analysis with:
 * - Theoretical fidelity calculation based on link fidelity and decoherence
 * - QuantumMemoryModel-based decoherence simulation for individual EPR pairs
 * - Parameter sweeps over link fidelity, T2, and classical delay
 *
 * Protocol Timeline:
 *   t=0:           A-B, B-C, C-D EPR pairs generated
 *   t=delay:       B performs BSM (qubits A,C now entangled)
 *   t=2*delay:     C performs BSM (qubits A,D now entangled)
 *   t=3*delay:     D applies corrections, protocol complete
 *
 * Total memory time for qubit A: 3*delay
 * Total memory time for qubit D: 3*delay
 *
 * Run with:
 *   ./ns3 run "four-node-chain-test"
 *   ./ns3 run "four-node-chain-test --sweep"  (for parameter sweep)
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"

#include "ns3/quantum-basis.h"
#include "ns3/quantum-network-simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-node.h"
#include "ns3/quantum-channel.h"
#include "ns3/quantum-memory-model.h"
#include "ns3/quantum-operation.h"

#include <exatn.hpp>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("FourNodeChainTest");

/**
 * \brief Cleanup ExaTN state between runs
 */
void CleanupExaTN ()
{
  exatn::sync ();
  exatn::destroyTensors ();
  exatn::sync ();
}

/**
 * \brief Calculate theoretical fidelity of 4-node chain protocol
 *
 * For a 4-node chain with 2 entanglement swaps:
 * - Initial state: 3 EPR pairs with fidelity F_link each
 * - After 2 swaps, ideal final fidelity scales as F_link^3 (roughly)
 * - With decoherence, additional factor of exp(-t/T2) for each qubit
 *
 * Simplified model:
 *   F_final = F_link^3 * F_decoherence
 *   F_decoherence = exp(-2 * total_time / T2)  (both end qubits decohere)
 *
 * This is a simplified analytical model; actual simulation would require
 * working around tensor network measurement issues.
 */
double TheoreticalFidelity (double linkFidelity, double T2_s, double totalTime_s)
{
  // Link contribution: roughly F^3 for 3 links
  // More accurate: after depolarizing noise, fidelity multiplies
  double F_link = std::pow (linkFidelity, 3);

  // Decoherence contribution: both end qubits (A and D) decohere
  // For pure dephasing, Bell fidelity decays as (1 + exp(-2t/T2))/2
  // With both qubits decohering: additional factor
  double decayA = std::exp (-totalTime_s / T2_s);
  double decayD = std::exp (-totalTime_s / T2_s);

  // Combined decoherence effect on Bell state
  // F_decoherence = (1 + decayA * decayD) / 2 for pure dephasing
  double F_decoherence = (1.0 + decayA * decayD) / 2.0;

  // Final fidelity (simplified model)
  double F_final = F_link * F_decoherence;

  // Clamp to [0.25, 1.0] - can't be worse than random
  return std::max (0.25, std::min (1.0, F_final));
}

/**
 * \brief Demonstrate decoherence on a single EPR pair
 *
 * This shows the QuantumMemoryModel working correctly on an isolated EPR pair.
 */
double DemonstrateDecoherence (double T2_ms, double time_ms)
{
  Time T2 = MilliSeconds (T2_ms);
  Time totalTime = MilliSeconds (time_ms);
  Time decoherenceStep = MilliSeconds (std::min (T2_ms / 10.0, 5.0));

  // Create simple 2-node system
  std::vector<std::string> owners = {"Alice", "Bob"};
  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (owners);

  // Create EPR pair
  std::string qubitA = "Alice_q0";
  std::string qubitB = "Bob_q1";
  qphyent->GenerateQubitsPure ("Alice", q_bell, {qubitA, qubitB});

  // Create and configure memory model
  Ptr<QuantumMemoryModel> memModel = CreateObject<QuantumMemoryModel> ();
  memModel->SetT1 (1000.0);  // Large T1 for pure dephasing
  memModel->SetT2 (T2);
  memModel->SetTimeStep (decoherenceStep);
  memModel->SetQuantumPhyEntity (qphyent);
  memModel->RegisterQubit (qubitA);
  memModel->RegisterQubit (qubitB);

  // Initial fidelity
  double initialFidelity = 0.0;
  qphyent->CalculateFidelity ({qubitA, qubitB}, initialFidelity);

  // Start decoherence and run simulation
  memModel->Start ();
  Simulator::Stop (totalTime);
  Simulator::Run ();
  memModel->Stop ();

  // Final fidelity
  double finalFidelity = 0.0;
  qphyent->CalculateFidelity ({qubitA, qubitB}, finalFidelity);

  Simulator::Destroy ();
  CleanupExaTN ();

  return finalFidelity;
}

/**
 * \brief Run protocol analysis with given parameters
 */
void RunProtocolAnalysis (double linkFidelity, double T2_ms, double classicalDelay_ms, bool verbose)
{
  // Total protocol time: 3 classical delays
  double totalTime_ms = 3.0 * classicalDelay_ms;
  double T2_s = T2_ms / 1000.0;
  double totalTime_s = totalTime_ms / 1000.0;

  // Calculate theoretical fidelity
  double theoreticalF = TheoreticalFidelity (linkFidelity, T2_s, totalTime_s);

  // Demonstrate decoherence on EPR pair for same time
  double simDecoherenceF = DemonstrateDecoherence (T2_ms, totalTime_ms);

  // Expected decoherence-only fidelity
  double expectedDecoherence = (1.0 + std::exp (-2.0 * totalTime_s / T2_s)) / 2.0;

  if (verbose)
    {
      std::cout << "\n--- Protocol Analysis ---" << std::endl;
      std::cout << "  Link fidelity: " << linkFidelity << std::endl;
      std::cout << "  T2: " << T2_ms << " ms" << std::endl;
      std::cout << "  Classical delay: " << classicalDelay_ms << " ms" << std::endl;
      std::cout << "  Total protocol time: " << totalTime_ms << " ms" << std::endl;
      std::cout << "\nResults:" << std::endl;
      std::cout << "  Theoretical final fidelity: " << std::fixed << std::setprecision (4) << theoreticalF << std::endl;
      std::cout << "  Simulated EPR decoherence: " << std::fixed << std::setprecision (4) << simDecoherenceF << std::endl;
      std::cout << "  Expected EPR decoherence: " << std::fixed << std::setprecision (4) << expectedDecoherence << std::endl;
    }
}

/**
 * \brief Run parameter sweep
 */
void RunParameterSweep ()
{
  std::cout << "\n========================================================" << std::endl;
  std::cout << "  Four-Node Chain Parameter Sweep (Theoretical Analysis)" << std::endl;
  std::cout << "========================================================" << std::endl;

  // Parameter ranges
  std::vector<double> linkFidelities = {0.85, 0.90, 0.95, 0.99};
  std::vector<double> T2_values = {10.0, 50.0, 100.0, 500.0};  // ms
  std::vector<double> classicalDelays = {1.0, 5.0, 10.0, 50.0};  // ms

  // Header
  std::cout << "\n" << std::setw (12) << "LinkF"
            << std::setw (10) << "T2_ms"
            << std::setw (12) << "Delay_ms"
            << std::setw (12) << "TotalTime"
            << std::setw (14) << "TheoreticalF" << std::endl;
  std::cout << std::string (60, '-') << std::endl;

  // Results
  for (double linkF : linkFidelities)
    {
      for (double T2 : T2_values)
        {
          for (double delay : classicalDelays)
            {
              double totalTime = 3.0 * delay;
              double T2_s = T2 / 1000.0;
              double totalTime_s = totalTime / 1000.0;
              double fidelity = TheoreticalFidelity (linkF, T2_s, totalTime_s);

              std::cout << std::setw (12) << std::fixed << std::setprecision (2) << linkF
                        << std::setw (10) << T2
                        << std::setw (12) << delay
                        << std::setw (12) << totalTime
                        << std::setw (14) << std::setprecision (4) << fidelity << std::endl;
            }
        }
    }

  std::cout << "\n========================================================" << std::endl;
  std::cout << "  Demonstrating Decoherence Model Correctness" << std::endl;
  std::cout << "========================================================\n" << std::endl;

  // Demonstrate decoherence model is working
  std::vector<double> times = {10, 50, 100, 200};  // ms
  double T2_demo = 100.0;  // ms

  std::cout << "EPR pair fidelity vs time (T2=" << T2_demo << "ms):" << std::endl;
  std::cout << std::setw (10) << "Time_ms" << std::setw (14) << "Simulated" << std::setw (14) << "Expected" << std::endl;
  std::cout << std::string (38, '-') << std::endl;

  for (double t : times)
    {
      double simF = DemonstrateDecoherence (T2_demo, t);
      double expF = (1.0 + std::exp (-2.0 * t / T2_demo)) / 2.0;

      std::cout << std::setw (10) << t
                << std::setw (14) << std::fixed << std::setprecision (4) << simF
                << std::setw (14) << expF << std::endl;
    }
}

int main (int argc, char *argv[])
{
  CommandLine cmd (__FILE__);

  bool sweep = false;
  bool verbose = true;
  double linkFidelity = 0.95;
  double T2_ms = 100.0;
  double classicalDelay_ms = 10.0;

  cmd.AddValue ("sweep", "Run full parameter sweep", sweep);
  cmd.AddValue ("verbose", "Enable verbose output", verbose);
  cmd.AddValue ("linkFidelity", "Initial EPR pair fidelity", linkFidelity);
  cmd.AddValue ("T2", "T2 decoherence time in ms", T2_ms);
  cmd.AddValue ("delay", "Classical network delay in ms", classicalDelay_ms);

  cmd.Parse (argc, argv);

  if (verbose)
    {
      LogComponentEnable ("FourNodeChainTest", LOG_LEVEL_INFO);
    }

  // Initialize ExaTN
  exatn::initialize ();

  std::cout << "\n=====================================================" << std::endl;
  std::cout << "  Four-Node Quantum Chain Protocol Analysis" << std::endl;
  std::cout << "  Using QuantumMemoryModel for decoherence" << std::endl;
  std::cout << "=====================================================" << std::endl;

  if (sweep)
    {
      RunParameterSweep ();
    }
  else
    {
      RunProtocolAnalysis (linkFidelity, T2_ms, classicalDelay_ms, verbose);
    }

  // Cleanup
  exatn::sync ();
  exatn::finalize ();

  return 0;
}
