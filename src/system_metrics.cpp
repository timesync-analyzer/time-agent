#include "system_metrics.h"

#include <spdlog/spdlog.h>

#include <fstream>

SysMetricsCollector::SysMetricsCollector(const AppConfig& config)
    : temperatureCollector(config.configTemperatureCollector.sensors),
      networkCollector(config.configNetworkCollector.interface_name) {
    spdlog::debug("SysMetricsCollector initialized");
}

SystemMetrics SysMetricsCollector::collect() {
    SystemMetrics metrics;

    metrics.timestamp_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    auto temp_metrics = temperatureCollector.collect();
    if (temp_metrics) {
        metrics.temperatureStats = *temp_metrics;
        spdlog::debug("Collected {} temperature readings", metrics.temperatureStats.zonesReadings.size());
    } else {
        spdlog::warn("Failed to collect temperature metrics");
    }

    auto network_metrics = networkCollector.collect();
    if (network_metrics) {
        metrics.networkStats = *network_metrics;
        spdlog::debug("Collected {} temperature readings", metrics.temperatureStats.zonesReadings.size());
    } else {
        spdlog::warn("Failed to collect temperature metrics");
    }

    return metrics;
}

TemperatureCollector::TemperatureCollector(const std::unordered_set<std::string>& sensors, const std::string& hwmon_path) {
    spdlog::info("Initializing TemperatureCollector with {} sensors from {}", sensors.size(), hwmon_path);

    try {
        for (const auto& entry : fs::directory_iterator(hwmon_path)) {
            if (!entry.is_directory()) continue;

            std::ifstream name_stream(entry.path() / "name");
            std::string name;
            if (std::getline(name_stream, name) && sensors.count(name)) {
                sensor2path_[name] = entry.path();
                spdlog::debug("Found sensor '{}' at {}", name, entry.path().string());
            }
        }
    } catch (const fs::filesystem_error& e) {
        spdlog::error("Failed to scan hwmon directory {}: {}", hwmon_path, e.what());
        return;
    }

    if (sensor2path_.empty()) {
        spdlog::warn("No matching sensors found in {}", hwmon_path);
    }

    for (const auto& [sensor, path] : sensor2path_) {
        zones_[sensor] = std::vector<TempZone>();

        try {
            for (const auto& entry : fs::directory_iterator(path)) {
                std::string filename = entry.path().filename().string();

                if (filename.find("temp") == 0 && filename.find("_label") != std::string::npos) {
                    int num = std::stoi(filename.substr(4));

                    std::ifstream label_stream(entry.path());
                    std::string label;
                    if (!std::getline(label_stream, label)) {
                        spdlog::warn("Failed to read label from {}", entry.path().string());
                        continue;
                    }

                    fs::path input_path = path / ("temp" + std::to_string(num) + "_input");
                    if (!fs::exists(input_path)) {
                        spdlog::warn("Input file {} does not exist for zone {}", input_path.string(), label);
                        continue;
                    }

                    zones_[sensor].emplace_back(TempZone{label, input_path});
                    spdlog::debug("Registered zone '{}' for sensor '{}': {}", label, sensor, input_path.string());
                }
            }
        } catch (const fs::filesystem_error& e) {
            spdlog::error("Failed to scan zones for sensor '{}': {}", sensor, e.what());
        }

        spdlog::info("Sensor '{}' has {} temperature zones", sensor, zones_[sensor].size());
    }
}

std::optional<TemperatureStats> TemperatureCollector::collect() {
    TemperatureStats metrics;

    for (const auto& [sensor, zones] : zones_) {
        for (const auto& zone : zones) {
            std::ifstream f(zone.input_path);
            int temp;
            if (f >> temp) {
                metrics.zonesReadings.push_back({sensor, zone.label, temp});
            } else {
                spdlog::warn("Failed to read temperature from {}", zone.input_path.string());
            }
        }
    }

    return metrics;
}

NetworkCollector::NetworkCollector(const std::string& interface)
    : interface(interface), path_to_statistics("/sys/class/net/" + interface + "/statistics/") {
    metric2path["rx_packets"] = path_to_statistics + "/rx_packets";
    metric2path["tx_packets"] = path_to_statistics + "/tx_packets";
    metric2path["rx_dropped"] = path_to_statistics + "/rx_dropped";
    metric2path["tx_dropped"] = path_to_statistics + "/tx_dropped";
    metric2path["rx_errors"] = path_to_statistics + "/rx_errors";
    metric2path["tx_errors"] = path_to_statistics + "/tx_errors";
    metric2path["collisions"] = path_to_statistics + "/collisions";
}

std::optional<NetworkStats> NetworkCollector::collect() {  // maybe need optimize by opening files all time
    NetworkStats metrics;
    std::unordered_map<std::string, uint64_t*> metric_map = {
        {"rx_packets", &metrics.rx_packets}, {"tx_packets", &metrics.tx_packets}, {"rx_dropped", &metrics.rx_dropped},
        {"tx_dropped", &metrics.tx_dropped}, {"rx_errors", &metrics.rx_errors},   {"tx_errors", &metrics.tx_errors},
        {"collisions", &metrics.collisions}};

    for (const auto& [name, value_ptr] : metric_map) {
        std::ifstream f(metric2path[name]);
        if (!f.is_open() || !(f >> *value_ptr)) {
            spdlog::error("Failed to read {} from {}", name, metric2path[name]);
            return std::nullopt;
        }
    }

    spdlog::debug("Network stats for '{}': rx={}, tx={}, drops={}+{}", interface, metrics.rx_packets, metrics.tx_packets,
                  metrics.rx_dropped, metrics.tx_dropped);

    return metrics;
}