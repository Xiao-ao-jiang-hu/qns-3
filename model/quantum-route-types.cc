#include "ns3/quantum-basis.h"
#include "ns3/quantum-route-types.h"
#include "ns3/quantum-channel.h"
#include <sstream>

namespace ns3 {

std::string QuantumRoute::ToString() const
{
  std::stringstream ss;
  ss << "QuantumRoute[src=" << source
     << ", dst=" << destination
     << ", hops=" << GetHopCount()
     << ", cost=" << totalCost
     << ", fidelity=" << estimatedFidelity
     << ", delay=" << estimatedDelay.GetSeconds() << "s"
     << ", strategy=" << (int)strategy
     << ", expires=" << expirationTime.GetSeconds() << "s"
     << ", id=" << routeId
     << "]";
  return ss.str();
}

} // namespace ns3