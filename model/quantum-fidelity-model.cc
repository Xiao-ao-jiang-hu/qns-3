#include "ns3/quantum-fidelity-model.h"

#include <algorithm>
#include <cmath>

namespace ns3 {

namespace {

double
ClampUnitInterval (double value)
{
  return std::max (0.0, std::min (1.0, value));
}

BellDiagonalState
Normalize (const BellDiagonalState& state)
{
  BellDiagonalState normalized = {
      std::max (0.0, state.phiPlus),
      std::max (0.0, state.phiMinus),
      std::max (0.0, state.psiPlus),
      std::max (0.0, state.psiMinus)};

  double total = normalized.phiPlus + normalized.phiMinus + normalized.psiPlus +
                 normalized.psiMinus;
  if (total <= 0.0)
    {
      return {0.25, 0.25, 0.25, 0.25};
    }

  normalized.phiPlus /= total;
  normalized.phiMinus /= total;
  normalized.psiPlus /= total;
  normalized.psiMinus /= total;
  return normalized;
}

} // namespace

BellDiagonalState
MakeWernerState (double fidelity)
{
  double clampedFidelity = ClampUnitInterval (fidelity);
  double otherBellWeight = (1.0 - clampedFidelity) / 3.0;
  return {clampedFidelity, otherBellWeight, otherBellWeight, otherBellWeight};
}

BellDiagonalState
MakePhaseFlipState (double fidelity)
{
  double clampedFidelity = ClampUnitInterval (fidelity);
  return {clampedFidelity, 1.0 - clampedFidelity, 0.0, 0.0};
}

BellDiagonalState
MakeBellDiagonalState (BellPairNoiseFamily family, double fidelity)
{
  switch (family)
    {
    case BellPairNoiseFamily::PHASE_FLIP:
      return MakePhaseFlipState (fidelity);
    case BellPairNoiseFamily::WERNER:
    default:
      return MakeWernerState (fidelity);
    }
}

double
ComputePhaseFlipDecay (double waitMs, double tauMs)
{
  if (waitMs <= 0.0)
    {
      return 1.0;
    }
  if (tauMs <= 0.0)
    {
      return 0.0;
    }
  return ClampUnitInterval (std::exp (-waitMs / tauMs));
}

BellDiagonalState
ApplyPhaseFlipMemoryWait (const BellDiagonalState& state,
                          double leftWaitMs,
                          double leftTauMs,
                          double rightWaitMs,
                          double rightTauMs)
{
  double lambda = ComputePhaseFlipDecay (leftWaitMs, leftTauMs) *
                  ComputePhaseFlipDecay (rightWaitMs, rightTauMs);
  double alpha = 0.5 * (1.0 + lambda);
  double beta = 0.5 * (1.0 - lambda);

  return Normalize ({
      alpha * state.phiPlus + beta * state.phiMinus,
      beta * state.phiPlus + alpha * state.phiMinus,
      alpha * state.psiPlus + beta * state.psiMinus,
      beta * state.psiPlus + alpha * state.psiMinus});
}

BellDiagonalState
EntanglementSwapBellDiagonal (const BellDiagonalState& lhs, const BellDiagonalState& rhs)
{
  return Normalize ({
      lhs.phiPlus * rhs.phiPlus + lhs.phiMinus * rhs.phiMinus +
          lhs.psiPlus * rhs.psiPlus + lhs.psiMinus * rhs.psiMinus,
      lhs.phiPlus * rhs.phiMinus + lhs.phiMinus * rhs.phiPlus +
          lhs.psiPlus * rhs.psiMinus + lhs.psiMinus * rhs.psiPlus,
      lhs.phiPlus * rhs.psiPlus + lhs.psiPlus * rhs.phiPlus +
          lhs.phiMinus * rhs.psiMinus + lhs.psiMinus * rhs.phiMinus,
      lhs.phiPlus * rhs.psiMinus + lhs.psiMinus * rhs.phiPlus +
          lhs.phiMinus * rhs.psiPlus + lhs.psiPlus * rhs.phiMinus});
}

double
GetBellFidelity (const BellDiagonalState& state)
{
  return ClampUnitInterval (state.phiPlus);
}

} // namespace ns3
