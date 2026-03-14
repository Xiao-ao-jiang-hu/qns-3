#include "ns3/quantum-delay-model.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/double.h"
#include "ns3/integer.h"
#include "ns3/uinteger.h"

#include <fstream>
#include <iomanip>
#include <cmath>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("QuantumDelayModel");

//=============================================================================
// DelayRecord
//=============================================================================

// Defined inline in header

//=============================================================================
// QuantumDelayModel
//=============================================================================

NS_OBJECT_ENSURE_REGISTERED (QuantumDelayModel);

TypeId
QuantumDelayModel::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::QuantumDelayModel")
          .SetParent<Object> ()
          .SetGroupName ("Quantum")
          .AddAttribute ("BaseDelay", "Base delay value",
                         TimeValue (MilliSeconds (10)),
                         MakeTimeAccessor (&QuantumDelayModel::m_baseDelay),
                         MakeTimeChecker ())
          .AddAttribute ("MaxDeviation", "Maximum deviation from base delay",
                         TimeValue (MilliSeconds (20)),
                         MakeTimeAccessor (&QuantumDelayModel::m_maxDeviation),
                         MakeTimeChecker ())
          .AddAttribute ("UpdateInterval", "Interval between delay updates",
                         TimeValue (MilliSeconds (100)),
                         MakeTimeAccessor (&QuantumDelayModel::m_updateInterval),
                         MakeTimeChecker ())
          .AddAttribute ("MaxHistorySize", "Maximum number of history records",
                         UintegerValue (10000),
                         MakeUintegerAccessor (&QuantumDelayModel::m_maxHistorySize),
                         MakeUintegerChecker<uint32_t> ());
  return tid;
}

QuantumDelayModel::QuantumDelayModel ()
    : m_baseDelay (MilliSeconds (10)),
      m_currentDelay (MilliSeconds (10)),
      m_maxDeviation (MilliSeconds (20)),
      m_updateInterval (MilliSeconds (100)),
      m_maxHistorySize (10000),
      m_initialized (false)
{
  NS_LOG_FUNCTION (this);
  m_rng = CreateObject<UniformRandomVariable> ();
}

QuantumDelayModel::~QuantumDelayModel ()
{
  NS_LOG_FUNCTION (this);
}

void
QuantumDelayModel::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  
  if (m_updateEvent.IsRunning ())
    {
      m_updateEvent.Cancel ();
    }
  
  m_delayHistory.clear ();
  m_rng = nullptr;
  
  Object::DoDispose ();
}

void
QuantumDelayModel::Initialize (Time updateInterval)
{
  NS_LOG_FUNCTION (this << updateInterval);
  
  if (m_initialized)
    {
      return;
    }
  
  m_updateInterval = updateInterval;
  m_currentDelay = m_baseDelay;
  
  // Record initial delay
  RecordDelay (false);
  
  // Schedule first update
  ScheduleNextUpdate ();
  
  m_initialized = true;
  
  NS_LOG_INFO ("Delay model initialized: base=" << m_baseDelay.As (Time::MS) 
               << ", max_dev=" << m_maxDeviation.As (Time::MS)
               << ", interval=" << m_updateInterval.As (Time::MS));
}

Time
QuantumDelayModel::GetCurrentDelay (void) const
{
  return m_currentDelay;
}

Time
QuantumDelayModel::GetBaseDelay (void) const
{
  return m_baseDelay;
}

void
QuantumDelayModel::SetBaseDelay (Time delay)
{
  NS_LOG_FUNCTION (this << delay);
  m_baseDelay = delay;
  if (!m_initialized)
    {
      m_currentDelay = delay;
    }
}

Time
QuantumDelayModel::GetMaxDeviation (void) const
{
  return m_maxDeviation;
}

void
QuantumDelayModel::SetMaxDeviation (Time deviation)
{
  NS_LOG_FUNCTION (this << deviation);
  m_maxDeviation = deviation;
}

Time
QuantumDelayModel::GetUpdateInterval (void) const
{
  return m_updateInterval;
}

void
QuantumDelayModel::SetUpdateInterval (Time interval)
{
  NS_LOG_FUNCTION (this << interval);
  m_updateInterval = interval;
}

std::vector<DelayRecord>
QuantumDelayModel::GetDelayHistory (void) const
{
  return std::vector<DelayRecord> (m_delayHistory.begin (), m_delayHistory.end ());
}

std::vector<DelayRecord>
QuantumDelayModel::GetRecentHistory (uint32_t n) const
{
  std::vector<DelayRecord> recent;
  
  if (n >= m_delayHistory.size ())
    {
      return GetDelayHistory ();
    }
  
  auto it = m_delayHistory.end () - n;
  for (; it != m_delayHistory.end (); ++it)
    {
      recent.push_back (*it);
    }
  
  return recent;
}

void
QuantumDelayModel::ClearHistory (void)
{
  NS_LOG_FUNCTION (this);
  m_delayHistory.clear ();
}

Time
QuantumDelayModel::GetAverageDelay (void) const
{
  if (m_delayHistory.empty ())
    {
      return m_baseDelay;
    }
  
  double sum = 0.0;
  for (const auto &record : m_delayHistory)
    {
      sum += record.delay.GetDouble ();
    }
  
  return Time::FromDouble (sum / m_delayHistory.size (), Time::NS);
}

double
QuantumDelayModel::GetDelayVariance (void) const
{
  if (m_delayHistory.size () < 2)
    {
      return 0.0;
    }
  
  Time avg = GetAverageDelay ();
  double sumSqDiff = 0.0;
  
  for (const auto &record : m_delayHistory)
    {
      double diff = (record.delay - avg).GetDouble ();
      sumSqDiff += diff * diff;
    }
  
  return sumSqDiff / (m_delayHistory.size () - 1);
}

Time
QuantumDelayModel::GetMinDelay (void) const
{
  if (m_delayHistory.empty ())
    {
      return m_baseDelay;
    }
  
  Time minDelay = m_delayHistory[0].delay;
  for (const auto &record : m_delayHistory)
    {
      if (record.delay < minDelay)
        {
          minDelay = record.delay;
        }
    }
  
  return minDelay;
}

Time
QuantumDelayModel::GetMaxDelay (void) const
{
  if (m_delayHistory.empty ())
    {
      return m_baseDelay;
    }
  
  Time maxDelay = m_delayHistory[0].delay;
  for (const auto &record : m_delayHistory)
    {
      if (record.delay > maxDelay)
        {
          maxDelay = record.delay;
        }
    }
  
  return maxDelay;
}

void
QuantumDelayModel::PrintStatistics (void) const
{
  std::cout << "\n=== Delay Model Statistics ===\n";
  std::cout << "Base Delay: " << m_baseDelay.As (Time::MS) << "\n";
  std::cout << "Max Deviation: " << m_maxDeviation.As (Time::MS) << "\n";
  std::cout << "Current Delay: " << m_currentDelay.As (Time::MS) << "\n";
  std::cout << "History Size: " << m_delayHistory.size () << "\n";
  
  if (!m_delayHistory.empty ())
    {
      std::cout << "Average Delay: " << GetAverageDelay ().As (Time::MS) << "\n";
      std::cout << "Min Delay: " << GetMinDelay ().As (Time::MS) << "\n";
      std::cout << "Max Delay: " << GetMaxDelay ().As (Time::MS) << "\n";
      std::cout << "Variance: " << std::fixed << std::setprecision (2) 
                << GetDelayVariance () << " ns²\n";
    }
  
  std::cout << "================================\n";
}

void
QuantumDelayModel::ExportHistoryToFile (const std::string &filename) const
{
  std::ofstream file (filename);
  if (!file.is_open ())
    {
      NS_LOG_ERROR ("Failed to open file: " << filename);
      return;
    }
  
  file << "# Time(ms), Delay(ms), IsBurst\n";
  
  for (const auto &record : m_delayHistory)
    {
      file << record.timestamp.As (Time::MS) << ", "
           << record.delay.As (Time::MS) << ", "
           << (record.isBurst ? 1 : 0) << "\n";
    }
  
  file.close ();
  NS_LOG_INFO ("Delay history exported to " << filename);
}

void
QuantumDelayModel::RecordDelay (bool isBurst)
{
  DelayRecord record (Simulator::Now (), m_currentDelay, isBurst);
  m_delayHistory.push_back (record);
  
  // Limit history size
  while (m_delayHistory.size () > m_maxHistorySize)
    {
      m_delayHistory.pop_front ();
    }
}

void
QuantumDelayModel::ScheduleNextUpdate (void)
{
  m_updateEvent = Simulator::Schedule (m_updateInterval, 
                                        &QuantumDelayModel::UpdateDelay, 
                                        this);
}

void
QuantumDelayModel::UpdateDelay (void)
{
  // Call subclass implementation
  DoUpdateDelay ();
  
  // Schedule next update
  ScheduleNextUpdate ();
}

//=============================================================================
// BurstDelayModel
//=============================================================================

NS_OBJECT_ENSURE_REGISTERED (BurstDelayModel);

TypeId
BurstDelayModel::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("ns3::BurstDelayModel")
          .SetParent<QuantumDelayModel> ()
          .SetGroupName ("Quantum")
          .AddConstructor<BurstDelayModel> ()
          .AddAttribute ("BurstProbability", "Probability of burst per update",
                         DoubleValue (0.05),
                         MakeDoubleAccessor (&BurstDelayModel::m_burstProbability),
                         MakeDoubleChecker<double> (0.0, 1.0))
          .AddAttribute ("BurstDuration", "Average burst duration",
                         TimeValue (Seconds (0.5)),
                         MakeTimeAccessor (&BurstDelayModel::m_burstDuration),
                         MakeTimeChecker ())
          .AddAttribute ("BurstMultiplier", "Delay multiplier during burst",
                         DoubleValue (2.0),
                         MakeDoubleAccessor (&BurstDelayModel::m_burstMultiplier),
                         MakeDoubleChecker<double> (1.0, 10.0));
  return tid;
}

BurstDelayModel::BurstDelayModel ()
    : m_burstProbability (0.05),
      m_burstDuration (Seconds (0.5)),
      m_burstMultiplier (2.0),
      m_inBurst (false),
      m_burstCount (0),
      m_totalBurstDuration (Seconds (0))
{
  NS_LOG_FUNCTION (this);
  m_burstRng = CreateObject<UniformRandomVariable> ();
  m_durationRng = CreateObject<ExponentialRandomVariable> ();
  m_durationRng->SetAttribute ("Mean", DoubleValue (m_burstDuration.GetSeconds ()));
}

BurstDelayModel::~BurstDelayModel ()
{
  NS_LOG_FUNCTION (this);
}

void
BurstDelayModel::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  
  if (m_burstEndEvent.IsRunning ())
    {
      m_burstEndEvent.Cancel ();
    }
  
  m_burstRng = nullptr;
  m_durationRng = nullptr;
  
  QuantumDelayModel::DoDispose ();
}

void
BurstDelayModel::SetBurstProbability (double prob)
{
  NS_LOG_FUNCTION (this << prob);
  m_burstProbability = prob;
}

double
BurstDelayModel::GetBurstProbability (void) const
{
  return m_burstProbability;
}

void
BurstDelayModel::SetBurstDuration (Time duration)
{
  NS_LOG_FUNCTION (this << duration);
  m_burstDuration = duration;
  if (m_durationRng)
    {
      m_durationRng->SetAttribute ("Mean", DoubleValue (duration.GetSeconds ()));
    }
}

Time
BurstDelayModel::GetBurstDuration (void) const
{
  return m_burstDuration;
}

void
BurstDelayModel::SetBurstMultiplier (double multiplier)
{
  NS_LOG_FUNCTION (this << multiplier);
  m_burstMultiplier = multiplier;
}

double
BurstDelayModel::GetBurstMultiplier (void) const
{
  return m_burstMultiplier;
}

bool
BurstDelayModel::IsInBurst (void) const
{
  return m_inBurst;
}

std::pair<uint32_t, Time>
BurstDelayModel::GetBurstStatistics (void) const
{
  return {m_burstCount, m_totalBurstDuration};
}

void
BurstDelayModel::DoUpdateDelay (void)
{
  if (m_inBurst)
    {
      // Already in burst, check if it should end
      if (Simulator::Now () >= m_burstEndTime)
        {
          EndBurst ();
        }
      else
        {
          // Still in burst, maintain high delay with some variation
          double variation = m_rng->GetValue (-0.1, 0.1);  // ±10% variation
          Time burstDelay = m_baseDelay + m_maxDeviation * m_burstMultiplier;
          m_currentDelay = burstDelay * (1.0 + variation);
          
          // Clamp to reasonable bounds
          Time maxDelay = m_baseDelay + m_maxDeviation * m_burstMultiplier * 1.5;
          if (m_currentDelay > maxDelay)
            {
              m_currentDelay = maxDelay;
            }
          if (m_currentDelay < m_baseDelay)
            {
              m_currentDelay = m_baseDelay;
            }
          
          RecordDelay (true);
          NS_LOG_DEBUG ("Burst continuing: delay=" << m_currentDelay.As (Time::MS));
          return;
        }
    }
  
  // Not in burst, normal operation
  // Check if new burst should start
  if (m_burstRng->GetValue () < m_burstProbability)
    {
      StartBurst ();
      return;
    }
  
  // Normal delay with small random variation
  double variation = m_rng->GetValue (-1.0, 1.0);  // -1 to +1
  Time deviation = m_maxDeviation * variation * 0.3;  // Use 30% of max deviation in normal mode
  m_currentDelay = m_baseDelay + deviation;
  
  // Ensure non-negative
  if (m_currentDelay < MicroSeconds (1))
    {
      m_currentDelay = MicroSeconds (1);
    }
  
  RecordDelay (false);
  NS_LOG_DEBUG ("Normal delay updated: " << m_currentDelay.As (Time::MS));
}

void
BurstDelayModel::StartBurst (void)
{
  NS_LOG_FUNCTION (this);
  
  m_inBurst = true;
  m_burstCount++;
  
  // Determine burst duration (exponentially distributed)
  double durationSec = m_durationRng->GetValue ();
  if (durationSec < 0.1)
    {
      durationSec = 0.1;  // Minimum 100ms burst
    }
  if (durationSec > 2.0)
    {
      durationSec = 2.0;  // Maximum 2s burst
    }
  
  Time burstDuration = Seconds (durationSec);
  m_burstEndTime = Simulator::Now () + burstDuration;
  m_totalBurstDuration += burstDuration;
  
  // Set initial burst delay
  m_currentDelay = m_baseDelay + m_maxDeviation * m_burstMultiplier;
  RecordDelay (true);
  
  // Schedule burst end
  m_burstEndEvent = Simulator::Schedule (burstDuration, 
                                          &BurstDelayModel::EndBurst, 
                                          this);
  
  NS_LOG_INFO ("BURST STARTED at " << Simulator::Now ().As (Time::S) 
               << " for " << burstDuration.As (Time::MS) 
               << ", delay=" << m_currentDelay.As (Time::MS));
}

void
BurstDelayModel::EndBurst (void)
{
  NS_LOG_FUNCTION (this);
  
  m_inBurst = false;
  m_burstEndEvent.Cancel ();
  
  // Return to normal delay
  double variation = m_rng->GetValue (-1.0, 1.0);
  Time deviation = m_maxDeviation * variation * 0.3;
  m_currentDelay = m_baseDelay + deviation;
  
  if (m_currentDelay < MicroSeconds (1))
    {
      m_currentDelay = MicroSeconds (1);
    }
  
  RecordDelay (false);
  
  NS_LOG_INFO ("BURST ENDED at " << Simulator::Now ().As (Time::S) 
               << ", delay returned to " << m_currentDelay.As (Time::MS));
}

} // namespace ns3
