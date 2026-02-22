#include "ns3/quantum-memory.h"

#include "ns3/simulator.h"
#include "ns3/quantum-phy-entity.h"
#include "ns3/quantum-error-model.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("QuantumMemory");

QuantumMemory::QuantumMemory(std::vector<std::string> qubits_)
    : m_qubits(qubits_),
      m_qphyent(nullptr)
{
    // Initialize last decoherence time to current time for all qubits
    Time now = Simulator::Now();
    for (const auto& qubit : m_qubits) {
        m_qubit2lastTime[qubit] = now;
    }
}

QuantumMemory::~QuantumMemory()
{
    m_qubits.clear();
    m_qubit2lastTime.clear();
    m_qubit2model.clear();
}

QuantumMemory::QuantumMemory()
    : m_qubits({}),
      m_qphyent(nullptr)
{
}

TypeId
QuantumMemory::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::QuantumMemory").SetParent<Object>().AddConstructor<QuantumMemory>();
    return tid;
}

void
QuantumMemory::SetPhyEntity(Ptr<QuantumPhyEntity> qphyent)
{
    m_qphyent = qphyent;
}

void
QuantumMemory::SetErrorModel(const std::string& qubit, Ptr<QuantumErrorModel> model)
{
    if (!ContainQubit(qubit)) {
        NS_LOG_WARN("Qubit " << qubit << " not found in memory");
        return;
    }
    m_qubit2model[qubit] = model;
}

void
QuantumMemory::ApplyDecoherence(const std::string& qubit)
{
    if (!ContainQubit(qubit)) {
        NS_LOG_WARN("Qubit " << qubit << " not found, skipping decoherence");
        return;
    }

    if (!m_qphyent) {
        NS_LOG_ERROR("No physical entity set, cannot apply decoherence");
        return;
    }

    Time now = Simulator::Now();
    auto it = m_qubit2lastTime.find(qubit);
    if (it == m_qubit2lastTime.end()) {
        m_qubit2lastTime[qubit] = now;
        return;
    }

    Time lastTime = it->second;

    if (now > lastTime) {
        Ptr<QuantumErrorModel> model = m_qphyent->GetErrorModel(qubit);
        if (model) {
            m_qphyent->SetQubitTime(qubit, lastTime);
            model->ApplyErrorModel(m_qphyent, {qubit}, now);
            NS_LOG_LOGIC("Applied decoherence to qubit " << qubit
                         << " (duration = " << (now - lastTime).As(Time::MS) << ")");
        }
    }

    m_qubit2lastTime[qubit] = now;
}

void
QuantumMemory::EnsureDecoherence(const std::string& qubit)
{
    if (!ContainQubit(qubit)) {
        return;
    }

    // Apply any pending decoherence
    ApplyDecoherence(qubit);
}

void
QuantumMemory::AddQubit(std::string qubit)
{
    m_qubits.push_back(qubit);
    // Record current time as last decoherence time
    m_qubit2lastTime[qubit] = Simulator::Now();
    NS_LOG_LOGIC("Added qubit " << qubit << " to memory at time " << Simulator::Now().As(Time::S));
}

bool
QuantumMemory::RemoveQubit(std::string qubit)
{
    for (unsigned i = 0; i < m_qubits.size(); ++i) {
        if (m_qubits[i] == qubit) {
            m_qubits.erase(m_qubits.begin() + i);
            m_qubit2lastTime.erase(qubit);
            m_qubit2model.erase(qubit);
            NS_LOG_LOGIC("Removed qubit " << qubit << " from memory");
            return true;
        }
    }
    return false;
}

unsigned
QuantumMemory::GetSize() const
{
    return m_qubits.size();
}

std::string
QuantumMemory::GetQubit(unsigned local) const
{
    return m_qubits[local];
}

bool
QuantumMemory::ContainQubit(std::string qubit) const
{
    for (unsigned i = 0; i < m_qubits.size(); ++i) {
        if (m_qubits[i] == qubit) {
            return true;
        }
    }
    return false;
}

std::vector<std::string>
QuantumMemory::GetQubits() const
{
    return m_qubits;
}

} // namespace ns3
