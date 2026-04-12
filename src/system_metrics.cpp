#include <spdlog/spdlog.h>

#include <chrono>
#include <fstream>
#include <optional>

#include "system_metrics.h"

SysMetricsCollector::SysMetricsCollector(const AppConfig& config, const SystemPaths& paths)
    : temperatureCollector(config.configTemperatureCollector.sensors, paths.hwmon),
      cpuCollector(paths.procStat),
      memoryCollector(paths.procMeminfo) {
    spdlog::debug("SysMetricsCollector initialized");
}

void SysMetricsCollector::setInterface(const std::string& interface) { networkCollector.setInterface(interface); }

SystemStats SysMetricsCollector::collect() {
    SystemStats metrics;

    metrics.timestamp_us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    auto tempMetrics = temperatureCollector.collect();
    if (tempMetrics) {
        metrics.temperatureStats = *tempMetrics;
    } else {
        spdlog::warn("Failed to collect temperature metrics");
    }

    auto networkMetrics = networkCollector.collect();
    if (networkMetrics) {
        metrics.networkStats = *networkMetrics;
    } else {
        spdlog::warn("Failed to collect network metrics");
    }

    auto cpuMetrics = cpuCollector.collect();
    if (cpuMetrics) {
        metrics.cpuStats = *cpuMetrics;
    } else {
        spdlog::warn("Failed to collect cpu metrics");
    }

    auto memoryMetrics = memoryCollector.collect();
    if (memoryMetrics) {
        metrics.memoryStats = *memoryMetrics;
    } else {
        spdlog::warn("Failed to collect memory metrics");
    }

    return metrics;
}

TemperatureCollector::TemperatureCollector(const std::unordered_set<std::string>& sensors, const std::string& hwmonPath) {
    spdlog::info("Initializing TemperatureCollector with {} sensors from {}", sensors.size(), hwmonPath);

    try {
        for (const auto& entry : fs::directory_iterator(hwmonPath)) {
            if (!entry.is_directory()) continue;

            std::ifstream nameStream(entry.path() / "name");
            std::string name;
            if (std::getline(nameStream, name) && sensors.count(name)) {
                sensorToPath_[name] = entry.path();
                spdlog::debug("Found sensor '{}' at {}", name, entry.path().string());
            }
        }
    } catch (const fs::filesystem_error& e) {
        spdlog::error("Failed to scan hwmon directory {}: {}", hwmonPath, e.what());
        return;
    }

    if (sensorToPath_.empty()) {
        spdlog::warn("No matching sensors found in {}", hwmonPath);
    }

    for (const auto& [sensor, path] : sensorToPath_) {
        try {
            for (const auto& entry : fs::directory_iterator(path)) {
                std::string filename = entry.path().filename().string();

                if (filename.find("temp") == 0 && filename.find("_label") != std::string::npos) {
                    int num = std::stoi(filename.substr(4));

                    std::ifstream labelStream(entry.path());
                    std::string label;
                    if (!std::getline(labelStream, label)) {
                        spdlog::warn("Failed to read label from {}", entry.path().string());
                        continue;
                    }

                    fs::path inputPath = path / ("temp" + std::to_string(num) + "_input");
                    if (!fs::exists(inputPath)) {
                        spdlog::warn("Input file {} does not exist for zone {}", inputPath.string(), label);
                        continue;
                    }

                    tempZones_[sensor].emplace_back(TempZone{label, inputPath});
                    spdlog::debug("Registered zone '{}' for sensor '{}': {}", label, sensor, inputPath.string());
                }
            }
        } catch (const fs::filesystem_error& e) {
            spdlog::error("Failed to scan zones for sensor '{}': {}", sensor, e.what());
        }

        spdlog::info("Sensor '{}' has {} temperature zones", sensor, tempZones_[sensor].size());
    }
}

std::optional<TemperatureStats> TemperatureCollector::collect() {
    TemperatureStats metrics;

    for (const auto& [sensor, zones] : tempZones_) {
        for (const auto& zone : zones) {
            std::ifstream f(zone.inputPath);
            int temp;
            if (f >> temp) {
                metrics.zonesReadings.push_back({sensor, zone.label, temp});
            } else {
                spdlog::warn("Failed to read temperature from {}", zone.inputPath.string());
            }
        }
    }

    return metrics;
}

NetworkCollector::NetworkCollector(const std::string& netStatsBase) : netStatsBase_(netStatsBase) {}

void NetworkCollector::setInterface(const std::string& interface) {
    interface_ = interface;
    pathToStatistics_ = netStatsBase_ + "/" + interface + "/statistics";
    metricToPath_["rx_packets"] = pathToStatistics_ + "/rx_packets";
    metricToPath_["tx_packets"] = pathToStatistics_ + "/tx_packets";
    metricToPath_["rx_dropped"] = pathToStatistics_ + "/rx_dropped";
    metricToPath_["tx_dropped"] = pathToStatistics_ + "/tx_dropped";
    metricToPath_["rx_errors"] = pathToStatistics_ + "/rx_errors";
    metricToPath_["tx_errors"] = pathToStatistics_ + "/tx_errors";
    metricToPath_["collisions"] = pathToStatistics_ + "/collisions";
}

std::optional<NetworkStats> NetworkCollector::collect() {
    if (interface_.empty()) {
        return std::nullopt;
    }

    NetworkStats metrics;
    std::unordered_map<std::string, uint64_t*> metricMap = {
        {"rx_packets", &metrics.rx_packets}, {"tx_packets", &metrics.tx_packets}, {"rx_dropped", &metrics.rx_dropped},
        {"tx_dropped", &metrics.tx_dropped}, {"rx_errors", &metrics.rx_errors},   {"tx_errors", &metrics.tx_errors},
        {"collisions", &metrics.collisions}};

    for (const auto& [name, valuePtr] : metricMap) {
        std::ifstream f(metricToPath_[name]);
        if (!f.is_open() || !(f >> *valuePtr)) {
            spdlog::error("Failed to read {} from {}", name, metricToPath_[name]);
            return std::nullopt;
        }
    }

    spdlog::debug("Network stats for '{}': rx={}, tx={}, drops={}+{}", interface_, metrics.rx_packets, metrics.tx_packets,
                  metrics.rx_dropped, metrics.tx_dropped);

    return metrics;
}

CpuCollector::CpuCollector(std::string procStat) : procStat_(std::move(procStat)) {}

std::optional<CpuStats> CpuCollector::collect() {
    CpuStats stats;

    std::ifstream statFile(procStat_);
    if (!statFile.is_open()) {
        spdlog::error("Failed to open {}", procStat_);
        return std::nullopt;
    }

    std::string line;
    while (std::getline(statFile, line)) {
        if (line.substr(0, 4) == "cpu ") {
            std::istringstream iss(line);
            std::string cpuLabel;
            uint64_t user, nice, system, idle, iowait, irq, softirq, steal;

            iss >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

            uint64_t idleTime = idle + iowait;
            uint64_t totalTime = user + nice + system + idle + iowait + irq + softirq + steal;

            if (prevTotal_ > 0) {
                uint64_t totalDelta = totalTime - prevTotal_;
                uint64_t idleDelta = idleTime - prevIdle_;
                if (totalDelta > 0) {
                    stats.usage_percent = 100.0 * (1.0 - static_cast<double>(idleDelta) / static_cast<double>(totalDelta));
                }
            }

            prevIdle_ = idleTime;
            prevTotal_ = totalTime;
        } else if (line.substr(0, 4) == "ctxt") {
            std::istringstream iss(line);
            std::string label;
            iss >> label >> stats.context_switches;
        } else if (line.substr(0, 4) == "intr") {
            std::istringstream iss(line);
            std::string label;
            iss >> label >> stats.interrupts;
        } else if (line.substr(0, 7) == "softirq") {
            std::istringstream iss(line);
            std::string label;
            iss >> label >> stats.softirqs;
        }
    }

    spdlog::debug("CPU stats: usage={:.1f}%, ctxt={}, intr={}, softirq={}", stats.usage_percent, stats.context_switches,
                  stats.interrupts, stats.softirqs);

    return stats;
}

MemoryCollector::MemoryCollector(std::string procMeminfo) : procMeminfo_(std::move(procMeminfo)) {}

std::optional<MemoryStats> MemoryCollector::collect() {
    MemoryStats stats;

    std::ifstream meminfo(procMeminfo_);
    if (!meminfo.is_open()) {
        spdlog::error("Failed to open {}", procMeminfo_);
        return std::nullopt;
    }

    std::string line;
    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        uint64_t value;

        iss >> key >> value;

        if (key == "MemAvailable:") {
            stats.mem_available_kb = value;
        } else if (key == "MemFree:") {
            stats.mem_free_kb = value;
        } else if (key == "SwapTotal:") {
            stats.swap_total_kb = value;
        } else if (key == "SwapFree:") {
            stats.swap_free_kb = value;
        } else if (key == "Buffers:") {
            stats.buffers_kb = value;
        }
    }

    spdlog::debug("Memory stats: available={}KB, free={}KB, swap_used={}KB", stats.mem_available_kb, stats.mem_free_kb,
                  stats.swap_total_kb - stats.swap_free_kb);

    return stats;
}
