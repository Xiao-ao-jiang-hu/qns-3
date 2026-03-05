#ifndef QUANTUM_LINK_LAYER_SERVICE_H
#define QUANTUM_LINK_LAYER_SERVICE_H

#include "ns3/object.h"
#include "ns3/callback.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"

#include <string>
#include <functional>

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

    virtual void RequestEntanglement (
        const std::string &srcNode,
        const std::string &dstNode,
        double minFidelity,
        EntanglementCallback callback
    ) = 0;

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

} // namespace ns3

#endif /* QUANTUM_LINK_LAYER_SERVICE_H */