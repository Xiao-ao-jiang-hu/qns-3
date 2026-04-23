#include "ns3/core-module.h"
#include "ns3/quantum-basis.h"
#include "ns3/quantum-fidelity-model.h"
#include "ns3/quantum-phy-entity.h"

#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace ns3;

namespace {

struct SwapResult
{
  double measuredFidelity;
  unsigned outcomeQb;
  unsigned outcomeQc;
};

void
SampleFidelity (Ptr<QuantumPhyEntity> qphyent,
                const std::string &leftQubit,
                const std::string &rightQubit,
                double *fidelity)
{
  if (fidelity == nullptr)
    {
      return;
    }

  qphyent->CalculateFidelity ({leftQubit, rightQubit}, *fidelity);
}

SwapResult
PerformSwap (Ptr<QuantumPhyEntity> qphyent)
{
  qphyent->ApplyGate ("God",
                      QNS_GATE_PREFIX + "CNOT",
                      std::vector<std::complex<double>>{},
                      std::vector<std::string>{"qc", "qb"});
  qphyent->ApplyGate ("God",
                      QNS_GATE_PREFIX + "H",
                      std::vector<std::complex<double>>{},
                      std::vector<std::string>{"qb"});

  auto outcomeQb = qphyent->Measure ("God", {"qb"});
  auto outcomeQc = qphyent->Measure ("God", {"qc"});

  if (outcomeQc.first == 1)
    {
      qphyent->ApplyGate ("God",
                          QNS_GATE_PREFIX + "PX",
                          std::vector<std::complex<double>>{},
                          std::vector<std::string>{"qd"});
    }
  if (outcomeQb.first == 1)
    {
      qphyent->ApplyGate ("God",
                          QNS_GATE_PREFIX + "PZ",
                          std::vector<std::complex<double>>{},
                          std::vector<std::string>{"qd"});
    }

  qphyent->PartialTrace ({"qb", "qc"});

  double measuredFidelity = 0.0;
  qphyent->CalculateFidelity ({"qa", "qd"}, measuredFidelity);
  return {measuredFidelity, outcomeQb.first, outcomeQc.first};
}

double
RunWernerWaitCheck ()
{
  const double coherenceTimeSeconds = 0.1;
  const double waitSeconds = 0.01;
  const double initialFidelity = 0.9;
  double measuredFidelity = 0.0;

  Simulator::Destroy ();

  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
  qphyent->SetTimeModel ("God", coherenceTimeSeconds);
  qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (initialFidelity), {"qa", "qb"});

  Simulator::Schedule (Seconds (waitSeconds),
                       &SampleFidelity,
                       qphyent,
                       std::string ("qa"),
                       std::string ("qb"),
                       &measuredFidelity);

  Simulator::Run ();

  BellDiagonalState expectedState =
      ApplyPhaseFlipMemoryWait (MakeWernerState (initialFidelity),
                                waitSeconds * 1000.0,
                                coherenceTimeSeconds * 1000.0,
                                waitSeconds * 1000.0,
                                coherenceTimeSeconds * 1000.0);
  const double expectedFidelity = GetBellFidelity (expectedState);

  std::cout << "Case 1: Werner pair memory wait" << std::endl;
  std::cout << "  initialFidelity = " << initialFidelity << std::endl;
  std::cout << "  tau = " << coherenceTimeSeconds << " s" << std::endl;
  std::cout << "  wait = " << waitSeconds << " s" << std::endl;
  std::cout << "  measured = " << measuredFidelity << std::endl;
  std::cout << "  expected = " << expectedFidelity << std::endl;
  std::cout << "  abs_error = " << std::abs (measuredFidelity - expectedFidelity) << std::endl;

  qphyent->Dispose ();
  Simulator::Destroy ();
  return std::abs (measuredFidelity - expectedFidelity);
}

double
RunWernerSwapCheck ()
{
  const double leftFidelity = 0.93;
  const double rightFidelity = 0.88;

  Simulator::Destroy ();

  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
  qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (leftFidelity), {"qa", "qb"});
  qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (rightFidelity), {"qc", "qd"});

  const SwapResult result = PerformSwap (qphyent);

  BellDiagonalState expectedState =
      EntanglementSwapBellDiagonal (MakeWernerState (leftFidelity), MakeWernerState (rightFidelity));
  const double expectedFidelity = GetBellFidelity (expectedState);

  std::cout << "Case 2: Werner entanglement swap without wait" << std::endl;
  std::cout << "  leftFidelity = " << leftFidelity << std::endl;
  std::cout << "  rightFidelity = " << rightFidelity << std::endl;
  std::cout << "  measurement_outcomes = (" << result.outcomeQb << ", " << result.outcomeQc << ")"
            << std::endl;
  std::cout << "  measured = " << result.measuredFidelity << std::endl;
  std::cout << "  expected = " << expectedFidelity << std::endl;
  std::cout << "  abs_error = " << std::abs (result.measuredFidelity - expectedFidelity) << std::endl;

  qphyent->Dispose ();
  Simulator::Destroy ();
  return std::abs (result.measuredFidelity - expectedFidelity);
}

double
RunWernerWaitThenSwapCheck ()
{
  const double coherenceTimeSeconds = 0.1;
  const double waitSeconds = 0.01;
  const double leftFidelity = 0.93;
  const double rightFidelity = 0.88;
  double measuredFidelity = 0.0;

  Simulator::Destroy ();

  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
  qphyent->SetTimeModel ("God", coherenceTimeSeconds);
  qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (leftFidelity), {"qa", "qb"});
  qphyent->GenerateQubitsMixed ("God", GetEPRwithFidelity (rightFidelity), {"qc", "qd"});

  Simulator::Schedule (
      Seconds (waitSeconds),
      [&measuredFidelity, qphyent]() {
        measuredFidelity = PerformSwap (qphyent).measuredFidelity;
      });

  Simulator::Run ();

  const double waitMs = waitSeconds * 1000.0;
  const double tauMs = coherenceTimeSeconds * 1000.0;
  BellDiagonalState waitedLeft =
      ApplyPhaseFlipMemoryWait (MakeWernerState (leftFidelity), waitMs, tauMs, waitMs, tauMs);
  BellDiagonalState waitedRight =
      ApplyPhaseFlipMemoryWait (MakeWernerState (rightFidelity), waitMs, tauMs, waitMs, tauMs);
  BellDiagonalState expectedState = EntanglementSwapBellDiagonal (waitedLeft, waitedRight);
  const double expectedFidelity = GetBellFidelity (expectedState);

  std::cout << "Case 3: Werner memory wait then entanglement swap" << std::endl;
  std::cout << "  leftFidelity = " << leftFidelity << std::endl;
  std::cout << "  rightFidelity = " << rightFidelity << std::endl;
  std::cout << "  tau = " << coherenceTimeSeconds << " s" << std::endl;
  std::cout << "  wait = " << waitSeconds << " s" << std::endl;
  std::cout << "  measured = " << measuredFidelity << std::endl;
  std::cout << "  expected = " << expectedFidelity << std::endl;
  std::cout << "  abs_error = " << std::abs (measuredFidelity - expectedFidelity) << std::endl;

  qphyent->Dispose ();
  Simulator::Destroy ();
  return std::abs (measuredFidelity - expectedFidelity);
}

double
RunPhaseFlipWaitCheck ()
{
  const double coherenceTimeSeconds = 0.1;
  const double waitSeconds = 0.01;
  const double initialFidelity = 0.9;
  double measuredFidelity = 0.0;

  Simulator::Destroy ();

  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
  qphyent->SetTimeModel ("God", coherenceTimeSeconds);
  qphyent->GenerateQubitsMixed (
      "God",
      GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, initialFidelity),
      {"qa", "qb"});

  Simulator::Schedule (Seconds (waitSeconds),
                       &SampleFidelity,
                       qphyent,
                       std::string ("qa"),
                       std::string ("qb"),
                       &measuredFidelity);

  Simulator::Run ();

  BellDiagonalState expectedState =
      ApplyPhaseFlipMemoryWait (MakePhaseFlipState (initialFidelity),
                                waitSeconds * 1000.0,
                                coherenceTimeSeconds * 1000.0,
                                waitSeconds * 1000.0,
                                coherenceTimeSeconds * 1000.0);
  const double expectedFidelity = GetBellFidelity (expectedState);

  std::cout << "Case 4: Phase-flip pair memory wait" << std::endl;
  std::cout << "  initialFidelity = " << initialFidelity << std::endl;
  std::cout << "  tau = " << coherenceTimeSeconds << " s" << std::endl;
  std::cout << "  wait = " << waitSeconds << " s" << std::endl;
  std::cout << "  measured = " << measuredFidelity << std::endl;
  std::cout << "  expected = " << expectedFidelity << std::endl;
  std::cout << "  abs_error = " << std::abs (measuredFidelity - expectedFidelity) << std::endl;

  qphyent->Dispose ();
  Simulator::Destroy ();
  return std::abs (measuredFidelity - expectedFidelity);
}

double
RunPhaseFlipSwapCheck ()
{
  const double leftFidelity = 0.93;
  const double rightFidelity = 0.88;

  Simulator::Destroy ();

  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
  qphyent->GenerateQubitsMixed (
      "God",
      GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, leftFidelity),
      {"qa", "qb"});
  qphyent->GenerateQubitsMixed (
      "God",
      GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, rightFidelity),
      {"qc", "qd"});

  const SwapResult result = PerformSwap (qphyent);

  BellDiagonalState expectedState =
      EntanglementSwapBellDiagonal (MakePhaseFlipState (leftFidelity), MakePhaseFlipState (rightFidelity));
  const double expectedFidelity = GetBellFidelity (expectedState);

  std::cout << "Case 5: Phase-flip entanglement swap without wait" << std::endl;
  std::cout << "  leftFidelity = " << leftFidelity << std::endl;
  std::cout << "  rightFidelity = " << rightFidelity << std::endl;
  std::cout << "  measurement_outcomes = (" << result.outcomeQb << ", " << result.outcomeQc << ")"
            << std::endl;
  std::cout << "  measured = " << result.measuredFidelity << std::endl;
  std::cout << "  expected = " << expectedFidelity << std::endl;
  std::cout << "  abs_error = " << std::abs (result.measuredFidelity - expectedFidelity) << std::endl;

  qphyent->Dispose ();
  Simulator::Destroy ();
  return std::abs (result.measuredFidelity - expectedFidelity);
}

double
RunPhaseFlipWaitThenSwapCheck ()
{
  const double coherenceTimeSeconds = 0.1;
  const double waitSeconds = 0.01;
  const double leftFidelity = 0.93;
  const double rightFidelity = 0.88;
  double measuredFidelity = 0.0;

  Simulator::Destroy ();

  Ptr<QuantumPhyEntity> qphyent = CreateObject<QuantumPhyEntity> (std::vector<std::string>{"God"});
  qphyent->SetTimeModel ("God", coherenceTimeSeconds);
  qphyent->GenerateQubitsMixed (
      "God",
      GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, leftFidelity),
      {"qa", "qb"});
  qphyent->GenerateQubitsMixed (
      "God",
      GetEPRwithNoiseFamily (BellPairNoiseFamily::PHASE_FLIP, rightFidelity),
      {"qc", "qd"});

  Simulator::Schedule (
      Seconds (waitSeconds),
      [&measuredFidelity, qphyent]() {
        measuredFidelity = PerformSwap (qphyent).measuredFidelity;
      });

  Simulator::Run ();

  const double waitMs = waitSeconds * 1000.0;
  const double tauMs = coherenceTimeSeconds * 1000.0;
  BellDiagonalState waitedLeft =
      ApplyPhaseFlipMemoryWait (MakePhaseFlipState (leftFidelity), waitMs, tauMs, waitMs, tauMs);
  BellDiagonalState waitedRight =
      ApplyPhaseFlipMemoryWait (MakePhaseFlipState (rightFidelity), waitMs, tauMs, waitMs, tauMs);
  BellDiagonalState expectedState = EntanglementSwapBellDiagonal (waitedLeft, waitedRight);
  const double expectedFidelity = GetBellFidelity (expectedState);

  std::cout << "Case 6: Phase-flip memory wait then entanglement swap" << std::endl;
  std::cout << "  leftFidelity = " << leftFidelity << std::endl;
  std::cout << "  rightFidelity = " << rightFidelity << std::endl;
  std::cout << "  tau = " << coherenceTimeSeconds << " s" << std::endl;
  std::cout << "  wait = " << waitSeconds << " s" << std::endl;
  std::cout << "  measured = " << measuredFidelity << std::endl;
  std::cout << "  expected = " << expectedFidelity << std::endl;
  std::cout << "  abs_error = " << std::abs (measuredFidelity - expectedFidelity) << std::endl;

  qphyent->Dispose ();
  Simulator::Destroy ();
  return std::abs (measuredFidelity - expectedFidelity);
}

} // namespace

int
main ()
{
  std::cout << std::fixed << std::setprecision (15);

  const double waitError = RunWernerWaitCheck ();
  const double swapError = RunWernerSwapCheck ();
  const double waitThenSwapError = RunWernerWaitThenSwapCheck ();
  const double phaseFlipWaitError = RunPhaseFlipWaitCheck ();
  const double phaseFlipSwapError = RunPhaseFlipSwapCheck ();
  const double phaseFlipWaitThenSwapError = RunPhaseFlipWaitThenSwapCheck ();
  const double tolerance = 1e-9;

  std::cout << "Summary" << std::endl;
  std::cout << "  wait_check_pass = " << (waitError <= tolerance ? "true" : "false") << std::endl;
  std::cout << "  swap_check_pass = " << (swapError <= tolerance ? "true" : "false") << std::endl;
  std::cout << "  wait_then_swap_check_pass = "
            << (waitThenSwapError <= tolerance ? "true" : "false") << std::endl;
  std::cout << "  phase_flip_wait_check_pass = "
            << (phaseFlipWaitError <= tolerance ? "true" : "false") << std::endl;
  std::cout << "  phase_flip_swap_check_pass = "
            << (phaseFlipSwapError <= tolerance ? "true" : "false") << std::endl;
  std::cout << "  phase_flip_wait_then_swap_check_pass = "
            << (phaseFlipWaitThenSwapError <= tolerance ? "true" : "false") << std::endl;

  return (waitError <= tolerance && swapError <= tolerance && waitThenSwapError <= tolerance &&
          phaseFlipWaitError <= tolerance && phaseFlipSwapError <= tolerance &&
          phaseFlipWaitThenSwapError <= tolerance)
             ? 0
             : 1;
}
