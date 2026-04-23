#ifndef QUANTUM_FIDELITY_MODEL_H
#define QUANTUM_FIDELITY_MODEL_H

namespace ns3 {

enum class BellPairNoiseFamily
{
  WERNER,
  PHASE_FLIP
};

struct BellDiagonalState
{
  double phiPlus;
  double phiMinus;
  double psiPlus;
  double psiMinus;
};

BellDiagonalState MakeWernerState (double fidelity);
BellDiagonalState MakePhaseFlipState (double fidelity);
BellDiagonalState MakeBellDiagonalState (BellPairNoiseFamily family, double fidelity);

double ComputePhaseFlipDecay (double waitMs, double tauMs);

BellDiagonalState ApplyPhaseFlipMemoryWait (const BellDiagonalState& state,
                                            double leftWaitMs,
                                            double leftTauMs,
                                            double rightWaitMs,
                                            double rightTauMs);

BellDiagonalState EntanglementSwapBellDiagonal (const BellDiagonalState& lhs,
                                                const BellDiagonalState& rhs);

double GetBellFidelity (const BellDiagonalState& state);

} // namespace ns3

#endif /* QUANTUM_FIDELITY_MODEL_H */
