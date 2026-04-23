#ifndef QUANTUM_LINK_LAYER_SERVICE_H
#define QUANTUM_LINK_LAYER_SERVICE_H

#include "ns3/object.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/quantum-routing-protocol.h"
#include "ns3/random-variable-stream.h"

#include <string>
#include <map>

namespace ns3 {

class QuantumChannel;
class QuantumPhyEntity;

typedef uint32_t EntanglementId;
typedef uint32_t PathId;

static const EntanglementId INVALID_ENTANGLEMENT_ID = 0;
static const PathId INVALID_PATH_ID = 0;

struct EntanglementInfo
{
    EntanglementId id;
    std::string localNode;
    std::string remoteNode;
    std::string localQubit;
    std::string remoteQubit;
    double fidelity;
    Time createdAt;
    bool isValid;
};

enum class EntanglementState
{
    PENDING,
    READY,
    CONSUMED,
    FAILED
};

typedef Callback<void, EntanglementId, EntanglementState> EntanglementCallback;
typedef Callback<void, PathId, bool> PathReadyCallback;

class ILinkLayerService : public Object
{
public:
    static TypeId GetTypeId ();

    ILinkLayerService ();
    ~ILinkLayerService () override;

    virtual EntanglementId RequestEntanglement (
        const std::string &srcNode,
        const std::string &dstNode,
        double minFidelity,
        EntanglementCallback callback
    ) = 0;

    virtual void AddOrUpdateLink (const std::string &srcNode,
                                  const std::string &dstNode,
                                  const LinkMetrics &metrics) = 0;

    virtual void RemoveLink (const std::string &srcNode,
                             const std::string &dstNode) = 0;

    virtual EntanglementInfo GetEntanglementInfo (EntanglementId id) const = 0;

    virtual bool ConsumeEntanglement (EntanglementId id) = 0;

    virtual bool IsEntanglementReady (EntanglementId id) const = 0;

    virtual void SetPhyEntity (Ptr<QuantumPhyEntity> qphyent) = 0;

    virtual Ptr<QuantumPhyEntity> GetPhyEntity () const = 0;

    virtual std::string GetOwner () const = 0;
};

class IEntanglementManager : public Object
{
public:
    static TypeId GetTypeId ();

    IEntanglementManager ();
    ~IEntanglementManager () override;

    virtual EntanglementId CreateEntanglementRequest (
        const std::string &neighbor,
        double minFidelity
    ) = 0;

    virtual void NotifyEntanglementReady (
        EntanglementId id,
        const std::string &localQubit,
        const std::string &remoteQubit,
        double fidelity
    ) = 0;

    virtual void NotifyEntanglementFailed (EntanglementId id) = 0;

    virtual EntanglementState GetState (EntanglementId id) const = 0;

    virtual EntanglementInfo GetInfo (EntanglementId id) const = 0;

    virtual bool Consume (EntanglementId id) = 0;

    virtual void RegisterCallback (EntanglementId id, EntanglementCallback callback) = 0;
};

class SimpleEntanglementManager : public IEntanglementManager
{
public:
    static TypeId GetTypeId ();

    SimpleEntanglementManager ();
    ~SimpleEntanglementManager () override;

    void DoDispose () override;

    void SetOwner (const std::string &owner);
    std::string GetOwner () const;

    EntanglementId CreateEntanglementRequest (const std::string &neighbor,
                                             double minFidelity) override;
    void NotifyEntanglementReady (EntanglementId id,
                                  const std::string &localQubit,
                                  const std::string &remoteQubit,
                                  double fidelity) override;
    void NotifyEntanglementFailed (EntanglementId id) override;
    EntanglementState GetState (EntanglementId id) const override;
    EntanglementInfo GetInfo (EntanglementId id) const override;
    bool Consume (EntanglementId id) override;
    void RegisterCallback (EntanglementId id, EntanglementCallback callback) override;

private:
    std::string m_owner;
    std::map<EntanglementId, EntanglementInfo> m_infos;
    std::map<EntanglementId, EntanglementState> m_states;
    std::map<EntanglementId, EntanglementCallback> m_callbacks;

    static EntanglementId s_nextId;
};

class QuantumLinkLayerService : public ILinkLayerService
{
public:
    static TypeId GetTypeId ();

    QuantumLinkLayerService ();
    ~QuantumLinkLayerService () override;

    void DoDispose () override;

    EntanglementId RequestEntanglement (const std::string &srcNode,
                                        const std::string &dstNode,
                                        double minFidelity,
                                        EntanglementCallback callback) override;

    void AddOrUpdateLink (const std::string &srcNode,
                          const std::string &dstNode,
                          const LinkMetrics &metrics) override;

    void RemoveLink (const std::string &srcNode,
                     const std::string &dstNode) override;

    EntanglementInfo GetEntanglementInfo (EntanglementId id) const override;
    bool ConsumeEntanglement (EntanglementId id) override;
    bool IsEntanglementReady (EntanglementId id) const override;
    void SetPhyEntity (Ptr<QuantumPhyEntity> qphyent) override;
    Ptr<QuantumPhyEntity> GetPhyEntity () const override;
    std::string GetOwner () const override;

    void SetOwner (const std::string &owner);

private:
    struct PendingRequest
    {
        std::string srcNode;
        std::string dstNode;
        double minFidelity;
    };

    void CompleteRequest (EntanglementId id);
    Ptr<SimpleEntanglementManager> GetOrCreateManager (const std::string &owner);
    LinkMetrics ResolveMetrics (const std::string &srcNode, const std::string &dstNode) const;

    std::string m_owner;
    Ptr<QuantumPhyEntity> m_qphyent;
    Ptr<UniformRandomVariable> m_rng;
    std::map<std::pair<std::string, std::string>, LinkMetrics> m_links;
    std::map<std::string, Ptr<SimpleEntanglementManager>> m_managers;
    std::map<EntanglementId, std::string> m_entanglementOwners;
    std::map<EntanglementId, PendingRequest> m_pendingRequests;
    std::map<EntanglementId, EventId> m_pendingEvents;
};

} // namespace ns3

#endif /* QUANTUM_LINK_LAYER_SERVICE_H */
