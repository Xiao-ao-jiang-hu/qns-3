/**
 * \file quantum-memory-model.cc
 * \brief Implementation of quantum memory decoherence model
 */

#include "quantum-memory-model.h"
#include "quantum-phy-entity.h"
#include "quantum-basis.h"
#include "quantum-operation.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h"

#include <cmath>
#include <cassert>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumMemoryModel");

NS_OBJECT_ENSURE_REGISTERED (QuantumMemoryModel);

TypeId
QuantumMemoryModel::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::QuantumMemoryModel")
    .SetParent<Object> ()
    .AddConstructor<QuantumMemoryModel> ()
    .AddAttribute ("T1",
                   "Amplitude damping time (seconds)",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&QuantumMemoryModel::m_T1),
                   MakeDoubleChecker<double> (0.0))
    .AddAttribute ("T2",
                   "Dephasing time (seconds)",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&QuantumMemoryModel::m_T2),
                   MakeDoubleChecker<double> (0.0));
  return tid;
}

QuantumMemoryModel::QuantumMemoryModel ()
  : m_T1 (1.0),
    m_T2 (1.0),
    m_timeStep (MilliSeconds (1)),
    m_qphyent (nullptr),
    m_running (false),
    m_decoherenceCount (0)
{
  NS_LOG_FUNCTION (this);
  m_rng = CreateObject<UniformRandomVariable> ();
}

QuantumMemoryModel::QuantumMemoryModel (double T1, double T2)
  : m_T1 (T1),
    m_T2 (T2),
    m_timeStep (MilliSeconds (1)),
    m_qphyent (nullptr),
    m_running (false),
    m_decoherenceCount (0)
{
  NS_LOG_FUNCTION (this << T1 << T2);
  m_rng = CreateObject<UniformRandomVariable> ();
  
  // Physical constraint: T2 <= 2*T1
  if (T2 > 2 * T1)
    {
      NS_LOG_WARN ("T2 > 2*T1 is unphysical, clamping T2 to 2*T1");
      m_T2 = 2 * T1;
    }
}

QuantumMemoryModel::~QuantumMemoryModel ()
{
  NS_LOG_FUNCTION (this);
}

void
QuantumMemoryModel::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Stop ();
  m_qphyent = nullptr;
  m_qubitTimestamps.clear ();
  m_rng = nullptr;
  Object::DoDispose ();
}

void
QuantumMemoryModel::SetT1 (double T1)
{
  NS_LOG_FUNCTION (this << T1);
  m_T1 = T1;
  // Adjust T2 if needed
  if (m_T2 > 2 * m_T1)
    {
      NS_LOG_WARN ("Adjusting T2 to satisfy T2 <= 2*T1");
      m_T2 = 2 * m_T1;
    }
}

double
QuantumMemoryModel::GetT1 () const
{
  return m_T1;
}

void
QuantumMemoryModel::SetT2 (double T2)
{
  NS_LOG_FUNCTION (this << T2);
  if (T2 > 2 * m_T1)
    {
      NS_LOG_WARN ("T2 > 2*T1 is unphysical, clamping T2 to 2*T1");
      m_T2 = 2 * m_T1;
    }
  else
    {
      m_T2 = T2;
    }
}

double
QuantumMemoryModel::GetT2 () const
{
  return m_T2;
}

void
QuantumMemoryModel::SetT2 (Time T2)
{
  SetT2 (T2.GetSeconds ());
}

void
QuantumMemoryModel::SetTimeStep (Time dt)
{
  NS_LOG_FUNCTION (this << dt);
  m_timeStep = dt;
}

Time
QuantumMemoryModel::GetTimeStep () const
{
  return m_timeStep;
}

void
QuantumMemoryModel::Start ()
{
  NS_LOG_FUNCTION (this);
  if (m_running)
    {
      NS_LOG_WARN ("QuantumMemoryModel already running");
      return;
    }
  m_running = true;
  m_periodicEvent = Simulator::Schedule (m_timeStep, &QuantumMemoryModel::PeriodicDecoherence, this);
}

void
QuantumMemoryModel::Stop ()
{
  NS_LOG_FUNCTION (this);
  m_running = false;
  if (m_periodicEvent.IsRunning ())
    {
      Simulator::Cancel (m_periodicEvent);
    }
}

bool
QuantumMemoryModel::IsRunning () const
{
  return m_running;
}

uint64_t
QuantumMemoryModel::GetTotalDecoherenceCount () const
{
  return m_decoherenceCount;
}

int64_t
QuantumMemoryModel::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_rng->SetStream (stream);
  return 1;
}

void
QuantumMemoryModel::PeriodicDecoherence ()
{
  NS_LOG_FUNCTION (this);
  
  if (!m_running)
    {
      return;
    }
  
  Time now = Simulator::Now ();
  
  // Apply decoherence to all registered qubits using ApplyDecoherence
  // which handles the identity gate workaround
  for (auto &pair : m_qubitTimestamps)
    {
      const std::string &qubit = pair.first;
      
      // ApplyDecoherence updates the timestamp internally
      if (ApplyDecoherence (qubit, now))
        {
          m_decoherenceCount++;
        }
    }
  
  // Schedule next periodic event
  m_periodicEvent = Simulator::Schedule (m_timeStep, &QuantumMemoryModel::PeriodicDecoherence, this);
}

void
QuantumMemoryModel::SetQuantumPhyEntity (Ptr<QuantumPhyEntity> qphyent)
{
  NS_LOG_FUNCTION (this << qphyent);
  m_qphyent = qphyent;
}

Ptr<QuantumPhyEntity>
QuantumMemoryModel::GetQuantumPhyEntity () const
{
  return m_qphyent;
}

void
QuantumMemoryModel::RegisterQubit (const std::string &qubit, Time creationTime)
{
  NS_LOG_FUNCTION (this << qubit << creationTime);
  m_qubitTimestamps[qubit] = creationTime;
}

void
QuantumMemoryModel::RegisterQubit (const std::string &qubit)
{
  NS_LOG_FUNCTION (this << qubit);
  m_qubitTimestamps[qubit] = Simulator::Now ();
}

void
QuantumMemoryModel::UnregisterQubit (const std::string &qubit)
{
  NS_LOG_FUNCTION (this << qubit);
  m_qubitTimestamps.erase (qubit);
}

void
QuantumMemoryModel::UpdateTimestamp (const std::string &qubit, Time time)
{
  NS_LOG_FUNCTION (this << qubit << time);
  if (m_qubitTimestamps.find (qubit) != m_qubitTimestamps.end ())
    {
      m_qubitTimestamps[qubit] = time;
    }
  else
    {
      NS_LOG_WARN ("Qubit " << qubit << " not registered, registering now");
      m_qubitTimestamps[qubit] = time;
    }
}

Time
QuantumMemoryModel::GetTimestamp (const std::string &qubit) const
{
  auto it = m_qubitTimestamps.find (qubit);
  if (it != m_qubitTimestamps.end ())
    {
      return it->second;
    }
  return Seconds (-1);  // Invalid timestamp
}

bool
QuantumMemoryModel::IsRegistered (const std::string &qubit) const
{
  return m_qubitTimestamps.find (qubit) != m_qubitTimestamps.end ();
}

std::vector<std::string>
QuantumMemoryModel::GetRegisteredQubits () const
{
  std::vector<std::string> qubits;
  for (const auto &pair : m_qubitTimestamps)
    {
      qubits.push_back (pair.first);
    }
  return qubits;
}

std::vector<std::vector<std::complex<double>>>
QuantumMemoryModel::GetAmplitudeDampingKraus (double duration) const
{
  /**
   * Amplitude Damping Channel (T1 relaxation)
   * 
   * Physical model: |1⟩ decays to |0⟩ with probability p = 1 - exp(-t/T1)
   * 
   * Kraus operators:
   *   K0 = |0⟩⟨0| + sqrt(1-p)|1⟩⟨1| = [[1, 0], [0, sqrt(1-p)]]
   *   K1 = sqrt(p)|0⟩⟨1|             = [[0, sqrt(p)], [0, 0]]
   * 
   * Verify: K0†K0 + K1†K1 = I
   */
  
  double p = 1.0 - std::exp (-duration / m_T1);
  double sqrt_1mp = std::sqrt (1.0 - p);
  double sqrt_p = std::sqrt (p);
  
  // K0 = [[1, 0], [0, sqrt(1-p)]]
  std::vector<std::complex<double>> K0 = {
    {1.0, 0.0}, {0.0, 0.0},
    {0.0, 0.0}, {sqrt_1mp, 0.0}
  };
  
  // K1 = [[0, sqrt(p)], [0, 0]]
  std::vector<std::complex<double>> K1 = {
    {0.0, 0.0}, {sqrt_p, 0.0},
    {0.0, 0.0}, {0.0, 0.0}
  };
  
  return {K0, K1};
}

std::vector<std::vector<std::complex<double>>>
QuantumMemoryModel::GetPureDephasingKraus (double duration) const
{
  /**
   * Pure Dephasing Channel (T_phi contribution to T2)
   * 
   * Physical model: 1/T2 = 1/(2*T1) + 1/T_phi
   * Pure dephasing rate: 1/T_phi = 1/T2 - 1/(2*T1)
   * 
   * If T2 = 2*T1 (pure amplitude damping limit), T_phi = infinity, no pure dephasing
   * If T2 < 2*T1, there is additional pure dephasing
   * 
   * Kraus operators for pure dephasing:
   *   K0 = sqrt(1-p/2) * I = [[sqrt(1-p/2), 0], [0, sqrt(1-p/2)]]
   *   K1 = sqrt(p/2) * Z   = [[sqrt(p/2), 0], [0, -sqrt(p/2)]]
   * 
   * where p = 1 - exp(-t/T_phi)
   */
  
  // Calculate T_phi (pure dephasing time)
  // 1/T_phi = 1/T2 - 1/(2*T1)
  double inv_T_phi = (1.0 / m_T2) - (1.0 / (2.0 * m_T1));
  
  if (inv_T_phi <= 0)
    {
      // No pure dephasing (T2 = 2*T1), return identity
      std::vector<std::complex<double>> K0 = {
        {1.0, 0.0}, {0.0, 0.0},
        {0.0, 0.0}, {1.0, 0.0}
      };
      std::vector<std::complex<double>> K1 = {
        {0.0, 0.0}, {0.0, 0.0},
        {0.0, 0.0}, {0.0, 0.0}
      };
      return {K0, K1};
    }
  
  double T_phi = 1.0 / inv_T_phi;
  double p = 1.0 - std::exp (-duration / T_phi);
  
  double sqrt_1mp2 = std::sqrt (1.0 - p / 2.0);
  double sqrt_p2 = std::sqrt (p / 2.0);
  
  // K0 = sqrt(1-p/2) * I
  std::vector<std::complex<double>> K0 = {
    {sqrt_1mp2, 0.0}, {0.0, 0.0},
    {0.0, 0.0}, {sqrt_1mp2, 0.0}
  };
  
  // K1 = sqrt(p/2) * Z
  std::vector<std::complex<double>> K1 = {
    {sqrt_p2, 0.0}, {0.0, 0.0},
    {0.0, 0.0}, {-sqrt_p2, 0.0}
  };
  
  return {K0, K1};
}

bool
QuantumMemoryModel::ApplyDecoherence (const std::string &qubit, Time currentTime)
{
  NS_LOG_FUNCTION (this << qubit << currentTime);
  
  if (!m_qphyent)
    {
      NS_LOG_ERROR ("QuantumPhyEntity not set");
      return false;
    }
  
  auto it = m_qubitTimestamps.find (qubit);
  if (it == m_qubitTimestamps.end ())
    {
      NS_LOG_WARN ("Qubit " << qubit << " not registered");
      return false;
    }
  
  Time lastTime = it->second;
  Time duration = currentTime - lastTime;
  
  if (duration.GetSeconds () <= 0)
    {
      NS_LOG_LOGIC ("No time elapsed for qubit " << qubit);
      return false;
    }
  
  double dt = duration.GetSeconds ();
  
  NS_LOG_INFO ("Applying decoherence to qubit " << qubit 
               << ": duration=" << dt * 1000 << "ms"
               << ", T1=" << m_T1 << "s, T2=" << m_T2 << "s");
  
  /**
   * WORKAROUND: Apply identity gate IMMEDIATELY before each ApplyOperation.
   * This is needed because ApplyOperation has issues when applied to qubits
   * in certain tensor network configurations. The identity gate "refreshes"
   * the qubit's position in the tensor network.
   * Use "God" as owner since it has special privileges.
   */
  
  /**
   * Use the same approach as the existing TimeModel: create a QuantumOperation
   * with probability weights that get sqrt'd by the constructor.
   * 
   * For dephasing: K0 = sqrt(1-p)*I, K1 = sqrt(p)*Z
   * Pass {pauli_I, pauli_Z} with probs {1-p, p}
   * Constructor stores: sqrt(1-p)*I, sqrt(p)*Z
   * 
   * Coherence decay: (1 - 2*prob) = exp(-dt/T2)
   * So: prob = (1 - exp(-dt/T2)) / 2
   */
  double prob_dephase = (1.0 - std::exp(-dt / m_T2)) / 2.0;
  
  if (prob_dephase > 1e-10)
    {
      // WORKAROUND: Apply identity gates to ALL registered qubits before the operation
      // This is needed because ApplyOperation has issues with tensor network state
      // after previous operations on other qubits
      for (const auto &pair : m_qubitTimestamps)
        {
          NS_LOG_DEBUG ("Applying identity gate to " << pair.first << " (refreshing tensor network)");
          m_qphyent->ApplyGate ("God", QNS_GATE_PREFIX + "I", pauli_I, {pair.first});
        }
      
      NS_LOG_DEBUG ("Applying dephasing operation to " << qubit << " with prob=" << prob_dephase);
      QuantumOperation dephas ({"I", "PZ"}, {pauli_I, pauli_Z}, {1.0 - prob_dephase, prob_dephase});
      m_qphyent->ApplyOperation (dephas, {qubit});
    }
  else
    {
      NS_LOG_DEBUG ("Skipping dephasing for " << qubit << " (prob=" << prob_dephase << " too small)");
    }
  
  // Update timestamp
  m_qubitTimestamps[qubit] = currentTime;
  
  return true;
}

int
QuantumMemoryModel::ApplyDecoherence (const std::vector<std::string> &qubits, Time currentTime)
{
  NS_LOG_FUNCTION (this << currentTime);
  
  int count = 0;
  for (const auto &qubit : qubits)
    {
      if (ApplyDecoherence (qubit, currentTime))
        {
          count++;
        }
    }
  return count;
}

} // namespace ns3
