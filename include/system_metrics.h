#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config.h"

namespace fs = std::filesystem;
struct SystemPaths {
    std::string hwmon = "/sys/class/hwmon";
    std::string procStat = "/proc/stat";
    std::string procMeminfo = "/proc/meminfo";
    std::string netStats = "/sys/class/net";
};

struct Ptp4lStats {
    std::string unit;
    uint64_t timestamp_us;
    int64_t offset = 0;
    int64_t freq = 0;
    int64_t path_delay = 0;
    int state = 0;
};

struct Phc2SysStats {
    std::string unit;
    uint64_t timestamp_us;
    int64_t offset = 0;
    int64_t freq = 0;
    int64_t path_delay = 0;
    int state = 0;
};

struct PPSStats {
    std::string unit;
    uint64_t timestamp_us;
    int64_t offset = 0;
};

template <typename T>
class IMetricCollector {
public:
    virtual ~IMetricCollector() = default;
    virtual std::optional<T> collect() = 0;
};

struct TemperatureStats {
    struct TemperatureMetric {
        std::string sensor;
        std::string label;
        int temperature;
    };
    std::vector<TemperatureMetric> zonesReadings;
};

struct NetworkStats {
    uint64_t rx_packets = 0;
    uint64_t tx_packets = 0;
    uint64_t rx_dropped = 0;
    uint64_t tx_dropped = 0;
    uint64_t rx_errors = 0;
    uint64_t tx_errors = 0;
    uint64_t collisions = 0;
};

struct CpuStats {
    double usage_percent;
    uint64_t context_switches;
    uint64_t interrupts;
    uint64_t softirqs;
};

struct MemoryStats {
    uint64_t mem_available_kb = 0;
    uint64_t mem_free_kb = 0;
    uint64_t swap_total_kb = 0;
    uint64_t swap_free_kb = 0;
    uint64_t buffers_kb = 0;
};

struct SystemStats {
    uint64_t timestamp_us;
    TemperatureStats temperatureStats;
    NetworkStats networkStats;
    CpuStats cpuStats;
    MemoryStats memoryStats;
};

class TemperatureCollector : public IMetricCollector<TemperatureStats> {
public:
    TemperatureCollector(const std::unordered_set<std::string>& sensors, const std::string& hwmonPath = "/sys/class/hwmon");
    std::optional<TemperatureStats> collect() override;

private:
    struct TempZone {
        std::string label;
        fs::path inputPath;
    };
    std::unordered_map<std::string, fs::path> sensorToPath_;
    std::unordered_map<std::string, std::vector<TempZone>> tempZones_;
};

class NetworkCollector : public IMetricCollector<NetworkStats> {
public:
    NetworkCollector(const std::string& interface, const std::string& netStatsBase = "/sys/class/net");
    std::optional<NetworkStats> collect() override;

private:
    std::string interface_;
    std::string pathToStatistics_;
    std::unordered_map<std::string, std::string> metricToPath_;
};

class CpuCollector : public IMetricCollector<CpuStats> {
public:
    explicit CpuCollector(std::string procStat = "/proc/stat");
    std::optional<CpuStats> collect() override;

private:
    std::string procStat_;
    uint64_t prevIdle_ = 0;
    uint64_t prevTotal_ = 0;
};

class MemoryCollector : public IMetricCollector<MemoryStats> {
public:
    explicit MemoryCollector(std::string procMeminfo = "/proc/meminfo");
    std::optional<MemoryStats> collect() override;

private:
    std::string procMeminfo_;
};

class SysMetricsCollector {
public:
    SysMetricsCollector(const AppConfig& config, const SystemPaths& paths = SystemPaths{});
    SystemStats collect();

private:
    TemperatureCollector temperatureCollector;
    NetworkCollector networkCollector;
    CpuCollector cpuCollector;
    MemoryCollector memoryCollector;
};