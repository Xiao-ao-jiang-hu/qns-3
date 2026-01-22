/**
 * \file quantum-memory-model.h
 * \brief Quantum memory decoherence model with T1/T2 relaxation
 * 
 * This model implements realistic quantum memory decoherence using Kraus operators.
 * It tracks qubit timestamps and applies appropriate decoherence based on elapsed time.
 * 
 * Physical Model:
 * - T1 (amplitude damping): |1⟩ → |0⟩ decay with rate 1/T1
 * - T2 (pure dephasing): Coherence decay with rate 1/T2
 * - Combined T2* = 1/(1/T2 + 1/(2*T1))
 * 
 * The decoherence is applied using Kraus operators directly on the density matrix,
 * NOT using any expectation formulas. This ensures true quantum simulation.
 */

#ifndef QUANTUM_MEMORY_MODEL_H
#define QUANTUM_MEMORY_MODEL_H

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/random-variable-stream.h"

#include <string>
#include <vector>
#include <map>
#include <complex>

namespace ns3 {

class QuantumPhyEntity;

/**
 * \brief Quantum memory model with T1/T2 decoherence
 * 
 * This class models the decoherence of qubits stored in quantum memory.
 * It uses the standard T1/T2 relaxation model:
 * 
 * - T1 (longitudinal relaxation): Energy decay from |1⟩ to |0⟩
 *   Kraus operators: K0 = |0⟩⟨0| + sqrt(1-p)|1⟩⟨1|, K1 = sqrt(p)|0⟩⟨1|
 *   where p = 1 - exp(-t/T1)
 * 
 * - T2 (transverse relaxation): Phase coherence decay
 *   For pure dephasing (T2 without T1 contribution):
 *   Kraus operators: K0 = sqrt(1-p/2)I, K1 = sqrt(p/2)Z
 *   where p = 1 - exp(-t/T_phi) and 1/T_phi = 1/T2 - 1/(2*T1)
 * 
 * The model applies decoherence at discrete time points when:
 * 1. A quantum operation is about to be performed on the qubit
 * 2. The qubit's fidelity is being measured
 * 3. Explicitly requested by the user
 */
class QuantumMemoryModel : public Object
{
public:
  /**
   * \brief Get the TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Default constructor
   */
  QuantumMemoryModel ();

  /**
   * \brief Constructor with T1 and T2 parameters
   * \param T1 Amplitude damping time in seconds
   * \param T2 Dephasing time in seconds (must satisfy T2 <= 2*T1)
   */
  QuantumMemoryModel (double T1, double T2);

  /**
   * \brief Destructor
   */
  virtual ~QuantumMemoryModel ();

  /**
   * \brief Set the T1 (amplitude damping) time
   * \param T1 T1 time in seconds
   */
  void SetT1 (double T1);

  /**
   * \brief Get the T1 time
   * \return T1 in seconds
   */
  double GetT1 () const;

  /**
   * \brief Set the T2 (dephasing) time
   * \param T2 T2 time in seconds
   */
  void SetT2 (double T2);

  /**
   * \brief Set the T2 (dephasing) time using ns-3 Time
   * \param T2 T2 time
   */
  void SetT2 (Time T2);

  /**
   * \brief Get the T2 time
   * \return T2 in seconds
   */
  double GetT2 () const;

  /**
   * \brief Set the associated QuantumPhyEntity
   * \param qphyent Pointer to the quantum physical entity
   */
  void SetQuantumPhyEntity (Ptr<QuantumPhyEntity> qphyent);

  /**
   * \brief Get the associated QuantumPhyEntity
   * \return Pointer to the quantum physical entity
   */
  Ptr<QuantumPhyEntity> GetQuantumPhyEntity () const;

  /**
   * \brief Set the time step for periodic decoherence
   * \param dt Time step for applying decoherence
   */
  void SetTimeStep (Time dt);

  /**
   * \brief Get the time step
   * \return Time step for periodic decoherence
   */
  Time GetTimeStep () const;

  /**
   * \brief Start periodic decoherence scheduling
   * 
   * This schedules decoherence events at regular time intervals (determined by SetTimeStep).
   * Each event applies decoherence to all registered qubits.
   */
  void Start ();

  /**
   * \brief Stop periodic decoherence scheduling
   */
  void Stop ();

  /**
   * \brief Check if periodic decoherence is running
   * \return true if running
   */
  bool IsRunning () const;

  /**
   * \brief Get the total number of decoherence events applied
   * \return Total decoherence count
   */
  uint64_t GetTotalDecoherenceCount () const;

  /**
   * \brief Assign random streams for reproducibility
   * \param stream First stream index to use
   * \return Number of streams used
   */
  int64_t AssignStreams (int64_t stream);

  /**
   * \brief Apply decoherence to a qubit based on elapsed time
   * 
   * This method calculates the time elapsed since the qubit's last update
   * and applies the appropriate Kraus operators for T1 and T2 decoherence.
   * 
   * \param qubit Name of the qubit
   * \param currentTime Current simulation time
   * \return true if decoherence was applied, false if qubit not found or no time elapsed
   */
  bool ApplyDecoherence (const std::string &qubit, Time currentTime);

  /**
   * \brief Apply decoherence to multiple qubits
   * \param qubits Names of the qubits
   * \param currentTime Current simulation time
   * \return Number of qubits that had decoherence applied
   */
  int ApplyDecoherence (const std::vector<std::string> &qubits, Time currentTime);

  /**
   * \brief Register a qubit for decoherence tracking
   * \param qubit Name of the qubit
   * \param creationTime Time when the qubit was created
   */
  void RegisterQubit (const std::string &qubit, Time creationTime);

  /**
   * \brief Register a qubit for decoherence tracking (uses current simulation time)
   * \param qubit Name of the qubit
   */
  void RegisterQubit (const std::string &qubit);

  /**
   * \brief Unregister a qubit (e.g., after measurement or partial trace)
   * \param qubit Name of the qubit
   */
  void UnregisterQubit (const std::string &qubit);

  /**
   * \brief Update the timestamp of a qubit
   * \param qubit Name of the qubit
   * \param time New timestamp
   */
  void UpdateTimestamp (const std::string &qubit, Time time);

  /**
   * \brief Get the timestamp of a qubit
   * \param qubit Name of the qubit
   * \return Last update time of the qubit
   */
  Time GetTimestamp (const std::string &qubit) const;

  /**
   * \brief Check if a qubit is registered
   * \param qubit Name of the qubit
   * \return true if registered
   */
  bool IsRegistered (const std::string &qubit) const;

  /**
   * \brief Get all registered qubits
   * \return Vector of qubit names
   */
  std::vector<std::string> GetRegisteredQubits () const;

  /**
   * \brief Calculate amplitude damping Kraus operators for given time
   * \param duration Time duration in seconds
   * \return Vector of {K0, K1} where each Ki is a 2x2 matrix in row-major order
   */
  std::vector<std::vector<std::complex<double>>> GetAmplitudeDampingKraus (double duration) const;

  /**
   * \brief Calculate pure dephasing Kraus operators for given time
   * \param duration Time duration in seconds
   * \return Vector of {K0, K1} where each Ki is a 2x2 matrix in row-major order
   */
  std::vector<std::vector<std::complex<double>>> GetPureDephasingKraus (double duration) const;

protected:
  virtual void DoDispose (void) override;

private:
  /**
   * \brief Periodic decoherence callback
   * 
   * Called at each time step to apply decoherence to all registered qubits.
   */
  void PeriodicDecoherence ();

  double m_T1;  ///< Amplitude damping time (seconds)
  double m_T2;  ///< Dephasing time (seconds)
  Time m_timeStep;  ///< Time step for periodic decoherence
  
  Ptr<QuantumPhyEntity> m_qphyent;  ///< Associated quantum physical entity
  
  std::map<std::string, Time> m_qubitTimestamps;  ///< Qubit -> last update time
  
  EventId m_periodicEvent;  ///< Scheduled periodic decoherence event
  bool m_running;  ///< Whether periodic decoherence is running
  uint64_t m_decoherenceCount;  ///< Total decoherence events applied
  
  Ptr<UniformRandomVariable> m_rng;  ///< Random number generator
};

} // namespace ns3

#endif /* QUANTUM_MEMORY_MODEL_H */
