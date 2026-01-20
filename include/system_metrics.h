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

struct TemperatureMetricsTimestamped {
    struct TemperatureMetric {
        std::string sensor;
        std::string label;
        int temperature;
    };
    std::vector<TemperatureMetric> zonesReadings;
};

struct SystemMetrics {
    TemperatureMetricsTimestamped temperatureMetrics;
    int timestamp_ms;
};

class TemperatureCollector : public IMetricCollector<TemperatureMetricsTimestamped> {
public:
    explicit TemperatureCollector(const std::unordered_set<std::string>& cpu_sensors,
                                  const std::string& hwmon_path = "/sys/class/hwmon");
    std::optional<TemperatureMetricsTimestamped> collect() override;

private:
    struct TempZone {
        std::string label;
        fs::path input_path;
    };
    std::unordered_map<std::string, fs::path> sensor2path_;
    std::unordered_map<std::string, std::vector<TempZone>> zones_;
    std::string hwmon_path_;
};

class SysMetricsCollector {
public:
    SysMetricsCollector(const AppConfig& config);
    SystemMetrics collect();

private:
    std::string interface;
    TemperatureCollector temperatureCollector;
};
