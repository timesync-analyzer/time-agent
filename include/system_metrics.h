#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "config.h"

namespace fs = std::filesystem;

struct Ptp4lStats {
    std::string unit;
    int timestamp_ms;
    long long offset = 0;
    long long freq = 0;
    long long path_delay = 0;
    int state = 0;
};

struct Phc2SysStats {
    std::string unit;
    int timestamp_ms;
    long long offset = 0;
    long long freq = 0;
    long long path_delay = 0;
    int state = 0;
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
    int timestamp_ms;
    TemperatureStats temperatureStats;
    NetworkStats networkStats;
    CpuStats cpuStats;
    MemoryStats memoryStats;
};

class TemperatureCollector : public IMetricCollector<TemperatureStats> {
public:
    TemperatureCollector(const std::unordered_set<std::string>& sensors, const std::string& hwmon_path = "/sys/class/hwmon");
    std::optional<TemperatureStats> collect() override;

private:
    struct TempZone {
        std::string label;
        fs::path input_path;
    };
    std::unordered_map<std::string, fs::path> sensor2path_;
    std::unordered_map<std::string, std::vector<TempZone>> zones_;
    std::string hwmon_path_;
};

class NetworkCollector : public IMetricCollector<NetworkStats> {
public:
    explicit NetworkCollector(const std::string& interface);
    std::optional<NetworkStats> collect() override;

private:
    std::string interface;
    std::string path_to_statistics;
    std::unordered_map<std::string, std::string> metric2path;
};

class CpuCollector : public IMetricCollector<CpuStats> {
public:
    std::optional<CpuStats> collect() override;

private:
    uint64_t prev_idle_ = 0;
    uint64_t prev_total_ = 0;
};

class MemoryCollector : public IMetricCollector<MemoryStats> {
public:
    MemoryCollector() = default;
    std::optional<MemoryStats> collect() override;
};

class SysMetricsCollector {
public:
    SysMetricsCollector(const AppConfig& config);
    SystemStats collect();

private:
    TemperatureCollector temperatureCollector;
    NetworkCollector networkCollector;
    CpuCollector cpuCollector;
    MemoryCollector memoryCollector;
};
