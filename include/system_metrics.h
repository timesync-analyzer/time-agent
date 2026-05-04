#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "config.h"

/**
 * @brief Filesystem entry points used by low-level Linux metric collectors.
 */
struct SystemPaths {
    std::string hwmon = "/sys/class/hwmon";
    std::string procStat = "/proc/stat";
    std::string procMeminfo = "/proc/meminfo";
    std::string netStats = "/sys/class/net";
};

/**
 * @brief Parsed ptp4l timing statistics.
 */
struct Ptp4lStats {
    std::string unit;
    /**
     * @brief Metric timestamp in microseconds since Unix epoch.
     */
    uint64_t timestamp_us;
    /**
     * @brief Clock offset in nanoseconds.
     */
    int64_t offset = 0;
    /**
     * @brief Frequency adjustment reported by ptp4l.
     */
    int64_t freq = 0;
    /**
     * @brief Path delay in nanoseconds.
     */
    int64_t path_delay = 0;
    /**
     * @brief ptp4l servo state value from the source log.
     */
    int state = 0;
};

/**
 * @brief Parsed phc2sys timing statistics.
 */
struct Phc2SysStats {
    std::string unit;
    /**
     * @brief Metric timestamp in microseconds since Unix epoch.
     */
    uint64_t timestamp_us;
    /**
     * @brief Clock offset in nanoseconds.
     */
    int64_t offset = 0;
    /**
     * @brief Frequency adjustment reported by phc2sys.
     */
    int64_t freq = 0;
    /**
     * @brief Delay value reported by phc2sys.
     */
    int64_t path_delay = 0;
    /**
     * @brief phc2sys servo state value from the source log.
     */
    int state = 0;
};

/**
 * @brief Parsed ppswatch offset statistics.
 */
struct PPSStats {
    std::string unit;
    /**
     * @brief Metric timestamp in microseconds since Unix epoch.
     */
    uint64_t timestamp_us;
    /**
     * @brief PPS offset in nanoseconds.
     */
    int64_t offset = 0;
};

/**
 * @brief Generic interface for synchronous point-in-time metric collectors.
 * @tparam T Metric value type returned by collect().
 */
template <typename T>
class IMetricCollector {
public:
    virtual ~IMetricCollector() = default;
    /**
     * @brief Reads current metric values.
     * @return Current metric values, or std::nullopt when the source is unavailable.
     */
    virtual std::optional<T> collect() = 0;
};

/**
 * @brief Temperature readings collected from hwmon sensors.
 */
struct TemperatureStats {
    /**
     * @brief Single hwmon temperature zone reading.
     */
    struct TemperatureMetric {
        std::string sensor;
        std::string label;
        /**
         * @brief Raw hwmon temperature value, usually in millidegrees Celsius.
         */
        int temperature;
    };
    std::vector<TemperatureMetric> zonesReadings;
};

/**
 * @brief Network interface counters read from /sys/class/net/<iface>/statistics.
 */
struct NetworkStats {
    uint64_t rx_packets = 0;
    uint64_t tx_packets = 0;
    uint64_t rx_dropped = 0;
    uint64_t tx_dropped = 0;
    uint64_t rx_errors = 0;
    uint64_t tx_errors = 0;
    uint64_t collisions = 0;
};

/**
 * @brief CPU counters and utilization read from /proc/stat.
 */
struct CpuStats {
    /**
     * @brief CPU usage percentage computed from the previous sample.
     */
    double usage_percent;
    uint64_t context_switches;
    uint64_t interrupts;
    uint64_t softirqs;
};

/**
 * @brief Memory counters read from /proc/meminfo.
 */
struct MemoryStats {
    uint64_t mem_available_kb = 0;
    uint64_t mem_free_kb = 0;
    uint64_t swap_total_kb = 0;
    uint64_t swap_free_kb = 0;
    uint64_t buffers_kb = 0;
};

/**
 * @brief Snapshot containing all system metric groups collected by the agent.
 */
struct SystemStats {
    /**
     * @brief Snapshot timestamp in microseconds since Unix epoch.
     */
    uint64_t timestamp_us;
    TemperatureStats temperatureStats;
    NetworkStats networkStats;
    CpuStats cpuStats;
    MemoryStats memoryStats;
};

/**
 * @brief Collects temperature readings for configured hwmon sensor names.
 */
class TemperatureCollector : public IMetricCollector<TemperatureStats> {
public:
    /**
     * @brief Builds the set of readable temperature zones for matching sensors.
     *
     * Missing sensors are logged and result in an empty reading set rather than
     * construction failure.
     *
     * @param sensors hwmon sensor names to include.
     * @param hwmonPath Base hwmon directory.
     */
    TemperatureCollector(const std::unordered_set<std::string>& sensors, const std::string& hwmonPath = "/sys/class/hwmon");
    ~TemperatureCollector();
    std::optional<TemperatureStats> collect() override;

private:
    struct TempZone {
        std::string sensor;
        std::string label;
        std::string inputPath;
        int fd = -1;
    };
    std::vector<TempZone> tempZones_;
};

/**
 * @brief Collects counters for one network interface.
 */
class NetworkCollector : public IMetricCollector<NetworkStats> {
public:
    /**
     * @brief Creates a network collector for a sysfs network statistics root.
     * @param netStatsBase Base directory containing network interface entries.
     */
    explicit NetworkCollector(const std::string& netStatsBase = "/sys/class/net");
    ~NetworkCollector();
    std::optional<NetworkStats> collect() override;
    /**
     * @brief Selects the interface and opens its statistics files.
     * @param interface Interface name to collect.
     */
    void setInterface(const std::string& interface);

private:
    struct MetricEntry {
        std::string path;
        int fd = -1;
        uint64_t NetworkStats::* field = nullptr;
    };

    std::string interface_{};
    std::string netStatsBase_{};
    std::array<MetricEntry, 7> metricEntries_{};

    void closeFds();
};

/**
 * @brief Collects CPU usage and aggregate kernel counters from /proc/stat.
 */
class CpuCollector : public IMetricCollector<CpuStats> {
public:
    /**
     * @brief Opens a proc stat file for CPU metric collection.
     * @param procStat Path to the proc stat file.
     */
    explicit CpuCollector(std::string procStat = "/proc/stat");
    ~CpuCollector();
    std::optional<CpuStats> collect() override;

private:
    std::string procStat_;
    int fd_ = -1;
    uint64_t prevIdle_ = 0;
    uint64_t prevTotal_ = 0;
    char buf_[16384];
};

/**
 * @brief Collects memory availability and swap counters from /proc/meminfo.
 */
class MemoryCollector : public IMetricCollector<MemoryStats> {
public:
    /**
     * @brief Opens a proc meminfo file for memory metric collection.
     * @param procMeminfo Path to the proc meminfo file.
     */
    explicit MemoryCollector(std::string procMeminfo = "/proc/meminfo");
    ~MemoryCollector();
    std::optional<MemoryStats> collect() override;

private:
    std::string procMeminfo_;
    int fd_ = -1;
    char buf_[4096];
};

/**
 * @brief Facade that gathers temperature, network, CPU, and memory metrics together.
 */
class SysMetricsCollector {
public:
    /**
     * @brief Creates low-level collectors using the agent config and optional test paths.
     * @param config Agent configuration containing system metric settings.
     * @param paths Filesystem paths used by the low-level collectors.
     */
    SysMetricsCollector(const AgentConfig& config, const SystemPaths& paths = SystemPaths{});
    /**
     * @brief Collects a best-effort system snapshot.
     * @return System snapshot where unavailable groups remain defaulted.
     */
    SystemStats collect();
    /**
     * @brief Updates the network interface watched by the network collector.
     * @param interface Interface name to collect.
     */
    void setInterface(const std::string& interface);

private:
    TemperatureCollector temperatureCollector_;
    NetworkCollector networkCollector_;
    CpuCollector cpuCollector_;
    MemoryCollector memoryCollector_;
};
