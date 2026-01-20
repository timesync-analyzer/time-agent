#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "config.h"

namespace fs = std::filesystem;

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

struct SystemMetrics {
    TemperatureStats temperatureStats;
    NetworkStats networkStats;
    int timestamp_ms;
};

class TemperatureCollector : public IMetricCollector<TemperatureStats> {
public:
    explicit TemperatureCollector(const std::unordered_set<std::string>& sensors,
                                  const std::string& hwmon_path = "/sys/class/hwmon");
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

class SysMetricsCollector {
public:
    SysMetricsCollector(const AppConfig& config);
    SystemMetrics collect();

private:
    TemperatureCollector temperatureCollector;
    NetworkCollector networkCollector;
};
