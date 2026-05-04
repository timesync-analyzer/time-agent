#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include <charconv>
#include <chrono>
#include <fstream>
#include <optional>
#include <unordered_map>

#include "system_metrics.h"

namespace {

struct MetricDef {
    std::string_view name;
    uint64_t NetworkStats::* field;
};

constexpr std::array<MetricDef, 7> kNetworkMetrics = {{
    {"rx_packets", &NetworkStats::rx_packets},
    {"tx_packets", &NetworkStats::tx_packets},
    {"rx_dropped", &NetworkStats::rx_dropped},
    {"tx_dropped", &NetworkStats::tx_dropped},
    {"rx_errors", &NetworkStats::rx_errors},
    {"tx_errors", &NetworkStats::tx_errors},
    {"collisions", &NetworkStats::collisions},
}};

// Returns pointer past needle if needle is found at the start of any line in [buf, buf+len), else nullptr.
const char* findLinePrefix(const char* buf, size_t len, std::string_view needle) {
    const char* p = buf;
    const char* end = buf + len;
    while (p < end) {
        const size_t remaining = static_cast<size_t>(end - p);
        if (remaining >= needle.size() && std::memcmp(p, needle.data(), needle.size()) == 0) {
            return p + needle.size();
        }
        const char* nl = static_cast<const char*>(std::memchr(p, '\n', remaining));
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    return nullptr;
}

// Skips leading spaces, then parses one uint64_t at p, advancing p past the number. Returns false on failure.
bool parseNext(const char*& p, const char* end, uint64_t& out) {
    while (p < end && *p == ' ') {
        ++p;
    }
    const auto [ptr, ec] = std::from_chars(p, end, out);
    if (ec != std::errc{}) {
        return false;
    }
    p = ptr;
    return true;
}

}  // namespace

SysMetricsCollector::SysMetricsCollector(const AgentConfig& config, const SystemPaths& paths)
    : temperatureCollector(config.configTemperatureCollector.sensors, paths.hwmon),
      networkCollector(paths.netStats),
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

    std::unordered_map<std::string, fs::path> sensorToPath;
    try {
        for (const auto& entry : fs::directory_iterator(hwmonPath)) {
            if (!entry.is_directory()) {
                continue;
            }
            std::ifstream nameStream(entry.path() / "name");
            std::string name;
            if (std::getline(nameStream, name) && sensors.count(name)) {
                sensorToPath[name] = entry.path();
                spdlog::debug("Found sensor '{}' at {}", name, entry.path().string());
            }
        }
    } catch (const fs::filesystem_error& e) {
        spdlog::error("Failed to scan hwmon directory {}: {}", hwmonPath, e.what());
        return;
    }

    if (sensorToPath.empty()) {
        spdlog::warn("No matching sensors found in {}", hwmonPath);
    }

    for (const auto& [sensor, path] : sensorToPath) {
        const size_t zonesBefore = tempZones_.size();
        try {
            for (const auto& entry : fs::directory_iterator(path)) {
                const std::string filename = entry.path().filename().string();
                if (filename.find("temp") != 0 || filename.find("_label") == std::string::npos) {
                    continue;
                }
                const int num = std::stoi(filename.substr(4));

                std::ifstream labelStream(entry.path());
                std::string label;
                if (!std::getline(labelStream, label)) {
                    spdlog::warn("Failed to read label from {}", entry.path().string());
                    continue;
                }

                const fs::path inputPath = path / ("temp" + std::to_string(num) + "_input");
                if (!fs::exists(inputPath)) {
                    spdlog::warn("Input file {} does not exist for zone {}", inputPath.string(), label);
                    continue;
                }

                const int fd = ::open(inputPath.c_str(), O_RDONLY);
                if (fd == -1) {
                    spdlog::warn("Failed to open {} for zone {}", inputPath.string(), label);
                    continue;
                }

                tempZones_.push_back({sensor, std::move(label), inputPath.string(), fd});
                spdlog::debug("Registered zone '{}' for sensor '{}': {}", tempZones_.back().label, sensor, inputPath.string());
            }
        } catch (const fs::filesystem_error& e) {
            spdlog::error("Failed to scan zones for sensor '{}': {}", sensor, e.what());
        }

        spdlog::info("Sensor '{}' has {} temperature zones", sensor, tempZones_.size() - zonesBefore);
    }
}

TemperatureCollector::~TemperatureCollector() {
    for (const auto& zone : tempZones_) {
        if (zone.fd != -1) {
            ::close(zone.fd);
        }
    }
}

std::optional<TemperatureStats> TemperatureCollector::collect() {
    TemperatureStats metrics;
    metrics.zonesReadings.reserve(tempZones_.size());
    char buf[16];

    for (const auto& zone : tempZones_) {
        const ssize_t n = ::pread(zone.fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            spdlog::warn("Failed to read temperature from {}", zone.inputPath);
            continue;
        }
        int temp;
        const auto [ptr, ec] = std::from_chars(buf, buf + n, temp);
        if (ec != std::errc{}) {
            spdlog::warn("Failed to parse temperature value from {}", zone.inputPath);
            continue;
        }
        metrics.zonesReadings.push_back({zone.sensor, zone.label, temp});
    }

    return metrics;
}

NetworkCollector::NetworkCollector(const std::string& netStatsBase) : netStatsBase_(netStatsBase) {}

NetworkCollector::~NetworkCollector() { closeFds(); }

void NetworkCollector::closeFds() {
    for (auto& entry : metricEntries_) {
        if (entry.fd != -1) {
            ::close(entry.fd);
            entry.fd = -1;
        }
    }
}

void NetworkCollector::setInterface(const std::string& interface) {
    closeFds();
    interface_ = interface;
    const std::string statsPath = netStatsBase_ + "/" + interface + "/statistics";

    for (size_t i = 0; i < kNetworkMetrics.size(); ++i) {
        metricEntries_[i].path = statsPath + "/" + std::string(kNetworkMetrics[i].name);
        metricEntries_[i].field = kNetworkMetrics[i].field;
        metricEntries_[i].fd = ::open(metricEntries_[i].path.c_str(), O_RDONLY);
        if (metricEntries_[i].fd == -1) {
            spdlog::error("Failed to open {}", metricEntries_[i].path);
        }
    }
}

std::optional<NetworkStats> NetworkCollector::collect() {
    if (interface_.empty()) {
        return std::nullopt;
    }

    NetworkStats metrics;
    char buf[32];

    for (const auto& entry : metricEntries_) {
        if (entry.fd == -1) {
            spdlog::error("Network metric fd not open: {}", entry.path);
            return std::nullopt;
        }
        const ssize_t n = ::pread(entry.fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            spdlog::error("Failed to read network metric from {}", entry.path);
            return std::nullopt;
        }
        const auto [ptr, ec] = std::from_chars(buf, buf + n, metrics.*entry.field);
        if (ec != std::errc{}) {
            spdlog::error("Failed to parse network metric value from {}", entry.path);
            return std::nullopt;
        }
    }

    spdlog::debug("Network stats for '{}': rx={}, tx={}, drops={}+{}", interface_, metrics.rx_packets, metrics.tx_packets,
                  metrics.rx_dropped, metrics.tx_dropped);

    return metrics;
}

CpuCollector::CpuCollector(std::string procStat) : procStat_(std::move(procStat)) {
    fd_ = ::open(procStat_.c_str(), O_RDONLY);
    if (fd_ == -1) {
        spdlog::error("Failed to open {}", procStat_);
    }
}

CpuCollector::~CpuCollector() {
    if (fd_ != -1) {
        ::close(fd_);
    }
}

std::optional<CpuStats> CpuCollector::collect() {
    if (fd_ == -1) {
        return std::nullopt;
    }

    const ssize_t n = ::pread(fd_, buf_, sizeof(buf_) - 1, 0);
    if (n <= 0) {
        spdlog::error("Failed to read {}", procStat_);
        return std::nullopt;
    }

    CpuStats stats;
    const char* const end = buf_ + n;
    const size_t len = static_cast<size_t>(n);

    const char* p = findLinePrefix(buf_, len, "cpu ");
    if (p) {
        uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
        if (parseNext(p, end, user) && parseNext(p, end, nice) && parseNext(p, end, system) && parseNext(p, end, idle) &&
            parseNext(p, end, iowait) && parseNext(p, end, irq) && parseNext(p, end, softirq) && parseNext(p, end, steal)) {
            const uint64_t idleTime = idle + iowait;
            const uint64_t totalTime = user + nice + system + idle + iowait + irq + softirq + steal;
            if (prevTotal_ > 0) {
                const uint64_t totalDelta = totalTime - prevTotal_;
                const uint64_t idleDelta = idleTime - prevIdle_;
                if (totalDelta > 0) {
                    stats.usage_percent = 100.0 * (1.0 - static_cast<double>(idleDelta) / static_cast<double>(totalDelta));
                }
            }
            prevIdle_ = idleTime;
            prevTotal_ = totalTime;
        }
    }

    p = findLinePrefix(buf_, len, "ctxt ");
    if (p) {
        parseNext(p, end, stats.context_switches);
    }

    p = findLinePrefix(buf_, len, "intr ");
    if (p) {
        parseNext(p, end, stats.interrupts);
    }

    p = findLinePrefix(buf_, len, "softirq ");
    if (p) {
        parseNext(p, end, stats.softirqs);
    }

    spdlog::debug("CPU stats: usage={:.1f}%, ctxt={}, intr={}, softirq={}", stats.usage_percent, stats.context_switches,
                  stats.interrupts, stats.softirqs);

    return stats;
}

MemoryCollector::MemoryCollector(std::string procMeminfo) : procMeminfo_(std::move(procMeminfo)) {
    fd_ = ::open(procMeminfo_.c_str(), O_RDONLY);
    if (fd_ == -1) {
        spdlog::error("Failed to open {}", procMeminfo_);
    }
}

MemoryCollector::~MemoryCollector() {
    if (fd_ != -1) {
        ::close(fd_);
    }
}

std::optional<MemoryStats> MemoryCollector::collect() {
    if (fd_ == -1) {
        return std::nullopt;
    }

    const ssize_t n = ::pread(fd_, buf_, sizeof(buf_) - 1, 0);
    if (n <= 0) {
        spdlog::error("Failed to read {}", procMeminfo_);
        return std::nullopt;
    }

    MemoryStats stats;
    const char* const end = buf_ + n;
    const size_t len = static_cast<size_t>(n);

    const auto readField = [&](std::string_view key, uint64_t& field) {
        const char* p = findLinePrefix(buf_, len, key);
        if (p) {
            parseNext(p, end, field);
        }
    };

    readField("MemAvailable:", stats.mem_available_kb);
    readField("MemFree:", stats.mem_free_kb);
    readField("SwapTotal:", stats.swap_total_kb);
    readField("SwapFree:", stats.swap_free_kb);
    readField("Buffers:", stats.buffers_kb);

    spdlog::debug("Memory stats: available={}KB, free={}KB, swap_used={}KB", stats.mem_available_kb, stats.mem_free_kb,
                  stats.swap_total_kb - stats.swap_free_kb);

    return stats;
}
