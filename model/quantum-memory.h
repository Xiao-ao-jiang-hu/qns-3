#ifndef QUANTUM_MEMORY_H
#define QUANTUM_MEMORY_H

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"

#include <map>
#include <string>
#include <vector>

namespace ns3 {

class QuantumPhyEntity;
class QuantumErrorModel;

/**
 * \brief Quantum memory with automatic decoherence tracking.
 *
 * This class manages qubits in quantum memory and automatically tracks
 * decoherence based on ns-3 simulation time. Each qubit stores its last
 * decoherence time, and decoherence is applied to the duration between
 * the last time and the current time when operations are performed.
 */
class QuantumMemory : public Object
{
private:
  /** Names of the qubits in this quantum memory. */
  std::vector<std::string> m_qubits;

  /** Map from qubit name to last decoherence time. */
  std::map<std::string, Time> m_qubit2lastTime;

  /** Map from qubit name to error model. */
  std::map<std::string, Ptr<QuantumErrorModel>> m_qubit2model;

  /** Pointer to physical entity for applying errors. */
  Ptr<QuantumPhyEntity> m_qphyent;

public:
  QuantumMemory (std::vector<std::string> qubits_);
  ~QuantumMemory ();

  QuantumMemory ();
  static TypeId GetTypeId ();

  /**
   * \brief Set the physical entity pointer.
   * \param qphyent Pointer to QuantumPhyEntity.
   */
  void SetPhyEntity (Ptr<QuantumPhyEntity> qphyent);

  /**
   * \brief Set the error model for a qubit.
   * \param qubit Name of the qubit.
   * \param model Pointer to error model.
   */
  void SetErrorModel (const std::string &qubit, Ptr<QuantumErrorModel> model);

  /**
   * \brief Add a qubit to the quantum memory.
   * \param qubit Name of the qubit to be added.
  */
  void AddQubit (std::string qubit);

  /**
   * \brief Remove a qubit from the quantum memory.
   * \param qubit Name of the qubit to be removed.
   * \return True if the qubit is successfully removed.
  */
  bool RemoveQubit (std::string qubit);

  /**
   * \brief Get the size of the quantum memory.
   * \return Number of qubits in the quantum memory.
  */
  unsigned GetSize () const;

  /**
   * \brief Get the qubit at a specific position.
   * \param local The position in the quantum memory to query.
   * \return Name of the qubit at the position.
  */
  std::string GetQubit (unsigned local) const;


/**
   * \brief Get all qubits in the quantum memory.
   * \return Vector of qubit names.
   */
  std::vector<std::string> GetQubits () const;

  /**
   * \brief Check if a qubit is in the quantum memory.
   * \param qubit Name of the qubit to be checked.
   * \return True if the qubit is in the quantum memory.
   */
  bool ContainQubit (std::string qubit) const;

  /**
   * \brief Apply decoherence to a qubit from its last time to current time.
   *
   * This method applies decoherence to the qubit for the duration from
   * the last decoherence time to the current simulation time.
   *
   * \param qubit Name of the qubit.
  */
  void ApplyDecoherence (const std::string &qubit);

  /**
   * \brief Ensure decoherence is applied to a qubit up to current time.
   *
   * This method should be called before any quantum operation on the qubit.
   * It applies decoherence for the duration from the last decoherence time
   * to the current simulation time, and updates the last decoherence time.
   *
   * \param qubit Name of the qubit.
  */
  void EnsureDecoherence (const std::string &qubit);
};

} // namespace ns3

#endif /* QUANTUM_MEMORY_H */
