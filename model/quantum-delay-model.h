#ifndef QUANTUM_DELAY_MODEL_H
#define QUANTUM_DELAY_MODEL_H

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/random-variable-stream.h"
#include "ns3/event-id.h"

#include <vector>
#include <deque>

namespace ns3 {

/**
 * \brief Delay record structure for history tracking
 */
struct DelayRecord
{
    Time timestamp;      // When the delay was recorded
    Time delay;          // The delay value
    bool isBurst;        // Whether this was during a burst period
    
    DelayRecord () : isBurst (false) {}
    DelayRecord (Time t, Time d, bool b = false) 
        : timestamp (t), delay (d), isBurst (b) {}
};

/**
 * \brief Abstract base class for quantum network delay models
 * 
 * This class defines the interface for time-varying delay models
 * used in quantum network simulations.
 */
class QuantumDelayModel : public Object
{
public:
    static TypeId GetTypeId (void);

    QuantumDelayModel ();
    ~QuantumDelayModel () override;

    void DoDispose (void) override;

    /**
     * \brief Initialize the delay model
     * \param updateInterval Interval between delay updates
     */
    virtual void Initialize (Time updateInterval);

    /**
     * \brief Get current delay
     * \return Current delay value
     */
    virtual Time GetCurrentDelay (void) const;

    /**
     * \brief Get base delay
     * \return Base delay value
     */
    Time GetBaseDelay (void) const;

    /**
     * \brief Set base delay
     * \param delay Base delay value
     */
    void SetBaseDelay (Time delay);

    /**
     * \brief Get maximum deviation from base delay
     * \return Maximum deviation
     */
    Time GetMaxDeviation (void) const;

    /**
     * \brief Set maximum deviation
     * \param deviation Maximum deviation from base
     */
    void SetMaxDeviation (Time deviation);

    /**
     * \brief Get update interval
     * \return Update interval
     */
    Time GetUpdateInterval (void) const;

    /**
     * \brief Set update interval
     * \param interval Update interval
     */
    void SetUpdateInterval (Time interval);

    /**
     * \brief Get delay history
     * \return Vector of delay records
     */
    std::vector<DelayRecord> GetDelayHistory (void) const;

    /**
     * \brief Get recent delay history (last N records)
     * \param n Number of recent records to return
     * \return Vector of recent delay records
     */
    std::vector<DelayRecord> GetRecentHistory (uint32_t n) const;

    /**
     * \brief Clear delay history
     */
    void ClearHistory (void);

    /**
     * \brief Get average delay over history
     * \return Average delay
     */
    Time GetAverageDelay (void) const;

    /**
     * \brief Get delay variance
     * \return Variance of delay
     */
    double GetDelayVariance (void) const;

    /**
     * \brief Get minimum delay in history
     * \return Minimum delay
     */
    Time GetMinDelay (void) const;

    /**
     * \brief Get maximum delay in history
     * \return Maximum delay
     */
    Time GetMaxDelay (void) const;

    /**
     * \brief Print delay statistics
     */
    void PrintStatistics (void) const;

    /**
     * \brief Export delay history to file
     * \param filename Output filename
     */
    void ExportHistoryToFile (const std::string &filename) const;

protected:
    /**
     * \brief Update delay - called periodically
     * To be implemented by subclasses
     */
    virtual void DoUpdateDelay (void) = 0;

    /**
     * \brief Record current delay to history
     */
    void RecordDelay (bool isBurst = false);

    /**
     * \brief Schedule next update
     */
    void ScheduleNextUpdate (void);

    // Callback for update
    void UpdateDelay (void);

    // Current state
    Time m_baseDelay;
    Time m_currentDelay;
    Time m_maxDeviation;
    Time m_updateInterval;
    
    // History (circular buffer for efficiency)
    std::deque<DelayRecord> m_delayHistory;
    uint32_t m_maxHistorySize;
    
    // Update scheduling
    EventId m_updateEvent;
    bool m_initialized;
    
    // Random number generator
    Ptr<UniformRandomVariable> m_rng;
};

/**
 * \brief Burst delay model
 * 
 * Models network congestion with burst periods of high delay:
 * - Normal periods: small random fluctuations
 * - Burst periods: significantly higher delays
 * - Bursts occur randomly with configurable probability and duration
 */
class BurstDelayModel : public QuantumDelayModel
{
public:
    static TypeId GetTypeId (void);

    BurstDelayModel ();
    ~BurstDelayModel () override;

    void DoDispose (void) override;

    /**
     * \brief Set burst probability (per update)
     * \param prob Probability of burst starting (0.0 - 1.0)
     */
    void SetBurstProbability (double prob);

    /**
     * \brief Get burst probability
     * \return Burst probability
     */
    double GetBurstProbability (void) const;

    /**
     * \brief Set average burst duration
     * \param duration Average burst duration
     */
    void SetBurstDuration (Time duration);

    /**
     * \brief Get burst duration
     * \return Average burst duration
     */
    Time GetBurstDuration (void) const;

    /**
     * \brief Set burst delay multiplier
     * During bursts, delay = base + max_deviation * multiplier
     * \param multiplier Multiplier for burst delay
     */
    void SetBurstMultiplier (double multiplier);

    /**
     * \brief Get burst multiplier
     * \return Burst multiplier
     */
    double GetBurstMultiplier (void) const;

    /**
     * \brief Check if currently in burst period
     * \return True if in burst
     */
    bool IsInBurst (void) const;

    /**
     * \brief Get burst statistics
     * \return Pair of (total bursts, total burst duration)
     */
    std::pair<uint32_t, Time> GetBurstStatistics (void) const;

protected:
    void DoUpdateDelay (void) override;

private:
    /**
     * \brief Start a burst period
     */
    void StartBurst (void);

    /**
     * \brief End current burst period
     */
    void EndBurst (void);

    // Burst parameters
    double m_burstProbability;      // Probability of burst per update
    Time m_burstDuration;           // Average burst duration
    double m_burstMultiplier;       // Delay multiplier during burst
    
    // State
    bool m_inBurst;
    Time m_burstEndTime;
    EventId m_burstEndEvent;
    
    // Statistics
    uint32_t m_burstCount;
    Time m_totalBurstDuration;
    
    // Random generators
    Ptr<UniformRandomVariable> m_burstRng;
    Ptr<ExponentialRandomVariable> m_durationRng;
};

/**
 * \brief Fixed delay model used for deterministic control-plane timings.
 */
class StaticDelayModel : public QuantumDelayModel
{
public:
    static TypeId GetTypeId (void);

    StaticDelayModel ();
    ~StaticDelayModel () override;

    void DoDispose (void) override;

    void SetFixedDelay (Time delay);
    Time GetFixedDelay (void) const;

protected:
    void DoUpdateDelay (void) override;
};

} // namespace ns3

#endif /* QUANTUM_DELAY_MODEL_H */
