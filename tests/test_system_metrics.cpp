#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <unistd.h>

#include "system_metrics.h"

namespace {

class TempDir {
public:
    explicit TempDir(const std::string& prefix) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / (prefix + "-" + std::to_string(::getpid()) + "-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TempDir() { std::filesystem::remove_all(path_); }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    EXPECT_TRUE(file.good()) << path;
    file << text;
}

void writeNetworkMetric(const std::filesystem::path& statsDir, const std::string& name, uint64_t value) {
    writeText(statsDir / name, std::to_string(value) + "\n");
}

void createNetworkStats(const std::filesystem::path& root, const std::string& interface) {
    const auto statsDir = root / interface / "statistics";
    writeNetworkMetric(statsDir, "rx_packets", 101);
    writeNetworkMetric(statsDir, "tx_packets", 202);
    writeNetworkMetric(statsDir, "rx_dropped", 3);
    writeNetworkMetric(statsDir, "tx_dropped", 4);
    writeNetworkMetric(statsDir, "rx_errors", 5);
    writeNetworkMetric(statsDir, "tx_errors", 6);
    writeNetworkMetric(statsDir, "collisions", 7);
}

void createHwmonSensor(const std::filesystem::path& hwmonRoot, const std::string& dir, const std::string& sensor,
                       const std::string& label, int temperature) {
    const auto sensorDir = hwmonRoot / dir;
    writeText(sensorDir / "name", sensor + "\n");
    writeText(sensorDir / "temp1_label", label + "\n");
    writeText(sensorDir / "temp1_input", std::to_string(temperature) + "\n");
}

std::string firstCpuSample() {
    return R"(cpu 100 0 100 800 0 0 0 0
intr 1000
ctxt 2000
softirq 3000
)";
}

std::string secondCpuSample() {
    return R"(cpu 150 0 150 850 0 0 0 0
intr 1100
ctxt 2300
softirq 3300
)";
}

std::string meminfoSample() {
    return R"(MemTotal:       8000000 kB
MemFree:         1200000 kB
MemAvailable:    4500000 kB
Buffers:          123456 kB
SwapTotal:       2000000 kB
SwapFree:        1500000 kB
)";
}

}  // namespace

TEST(SystemMetricsTest, MemoryCollectorParsesMeminfoFields) {
    TempDir temp("time-agent-memory-test");
    const auto meminfo = temp.path() / "meminfo";
    writeText(meminfo, meminfoSample());

    MemoryCollector collector(meminfo.string());
    auto stats = collector.collect();

    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->mem_available_kb, 4'500'000);
    EXPECT_EQ(stats->mem_free_kb, 1'200'000);
    EXPECT_EQ(stats->swap_total_kb, 2'000'000);
    EXPECT_EQ(stats->swap_free_kb, 1'500'000);
    EXPECT_EQ(stats->buffers_kb, 123'456);
}

TEST(SystemMetricsTest, MemoryCollectorReturnsNulloptForMissingFile) {
    TempDir temp("time-agent-memory-missing-test");

    MemoryCollector collector((temp.path() / "missing-meminfo").string());
    auto stats = collector.collect();

    EXPECT_FALSE(stats.has_value());
}

TEST(SystemMetricsTest, MemoryCollectorLeavesMissingFieldsAtZero) {
    TempDir temp("time-agent-memory-partial-test");
    const auto meminfo = temp.path() / "meminfo";
    writeText(meminfo, "MemFree: 64 kB\n");

    MemoryCollector collector(meminfo.string());
    auto stats = collector.collect();

    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->mem_free_kb, 64);
    EXPECT_EQ(stats->mem_available_kb, 0);
    EXPECT_EQ(stats->swap_total_kb, 0);
    EXPECT_EQ(stats->swap_free_kb, 0);
    EXPECT_EQ(stats->buffers_kb, 0);
}

TEST(SystemMetricsTest, CpuCollectorParsesCountersAndCalculatesUsageFromSecondSample) {
    TempDir temp("time-agent-cpu-test");
    const auto procStat = temp.path() / "stat";
    writeText(procStat, firstCpuSample());

    CpuCollector collector(procStat.string());
    auto first = collector.collect();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->context_switches, 2000);
    EXPECT_EQ(first->interrupts, 1000);
    EXPECT_EQ(first->softirqs, 3000);

    writeText(procStat, secondCpuSample());
    auto second = collector.collect();

    ASSERT_TRUE(second.has_value());
    EXPECT_NEAR(second->usage_percent, 66.666, 0.01);
    EXPECT_EQ(second->context_switches, 2300);
    EXPECT_EQ(second->interrupts, 1100);
    EXPECT_EQ(second->softirqs, 3300);
}

TEST(SystemMetricsTest, CpuCollectorReturnsNulloptForMissingFile) {
    TempDir temp("time-agent-cpu-missing-test");

    CpuCollector collector((temp.path() / "missing-stat").string());
    auto stats = collector.collect();

    EXPECT_FALSE(stats.has_value());
}

TEST(SystemMetricsTest, NetworkCollectorReadsSelectedInterfaceStatistics) {
    TempDir temp("time-agent-network-test");
    const auto netRoot = temp.path() / "net";
    createNetworkStats(netRoot, "eth-test");

    NetworkCollector collector(netRoot.string());
    EXPECT_FALSE(collector.collect().has_value());

    collector.setInterface("eth-test");
    auto stats = collector.collect();

    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->rx_packets, 101);
    EXPECT_EQ(stats->tx_packets, 202);
    EXPECT_EQ(stats->rx_dropped, 3);
    EXPECT_EQ(stats->tx_dropped, 4);
    EXPECT_EQ(stats->rx_errors, 5);
    EXPECT_EQ(stats->tx_errors, 6);
    EXPECT_EQ(stats->collisions, 7);
}

TEST(SystemMetricsTest, NetworkCollectorReturnsNulloptWhenMetricFileIsMissing) {
    TempDir temp("time-agent-network-missing-metric-test");
    const auto netRoot = temp.path() / "net";
    createNetworkStats(netRoot, "eth-test");
    std::filesystem::remove(netRoot / "eth-test" / "statistics" / "collisions");

    NetworkCollector collector(netRoot.string());
    collector.setInterface("eth-test");
    auto stats = collector.collect();

    EXPECT_FALSE(stats.has_value());
}

TEST(SystemMetricsTest, NetworkCollectorReturnsNulloptWhenMetricValueIsInvalid) {
    TempDir temp("time-agent-network-invalid-metric-test");
    const auto netRoot = temp.path() / "net";
    createNetworkStats(netRoot, "eth-test");
    writeText(netRoot / "eth-test" / "statistics" / "rx_packets", "not-a-number\n");

    NetworkCollector collector(netRoot.string());
    collector.setInterface("eth-test");
    auto stats = collector.collect();

    EXPECT_FALSE(stats.has_value());
}

TEST(SystemMetricsTest, TemperatureCollectorReadsOnlyConfiguredSensors) {
    TempDir temp("time-agent-temperature-test");
    const auto hwmonRoot = temp.path() / "hwmon";
    createHwmonSensor(hwmonRoot, "hwmon0", "coretemp", "Package id 0", 44500);
    createHwmonSensor(hwmonRoot, "hwmon1", "nvme", "Composite", 39000);
    createHwmonSensor(hwmonRoot, "hwmon2", "ignored", "Ignored", 99000);

    TemperatureCollector collector({"coretemp", "nvme"}, hwmonRoot.string());
    auto stats = collector.collect();

    ASSERT_TRUE(stats.has_value());
    ASSERT_EQ(stats->zonesReadings.size(), 2);

    std::map<std::string, TemperatureStats::TemperatureMetric> byLabel;
    for (const auto& reading : stats->zonesReadings) {
        byLabel.emplace(reading.label, reading);
    }

    ASSERT_TRUE(byLabel.count("Package id 0") > 0);
    EXPECT_EQ(byLabel["Package id 0"].sensor, "coretemp");
    EXPECT_EQ(byLabel["Package id 0"].temperature, 44500);
    ASSERT_TRUE(byLabel.count("Composite") > 0);
    EXPECT_EQ(byLabel["Composite"].sensor, "nvme");
    EXPECT_EQ(byLabel["Composite"].temperature, 39000);
    EXPECT_EQ(byLabel.count("Ignored"), 0);
}

TEST(SystemMetricsTest, TemperatureCollectorSkipsZoneWithoutInputFile) {
    TempDir temp("time-agent-temperature-missing-input-test");
    const auto sensorDir = temp.path() / "hwmon" / "hwmon0";
    writeText(sensorDir / "name", "coretemp\n");
    writeText(sensorDir / "temp1_label", "Package id 0\n");

    TemperatureCollector collector({"coretemp"}, (temp.path() / "hwmon").string());
    auto stats = collector.collect();

    ASSERT_TRUE(stats.has_value());
    EXPECT_TRUE(stats->zonesReadings.empty());
}

TEST(SystemMetricsTest, TemperatureCollectorSkipsInvalidTemperatureValue) {
    TempDir temp("time-agent-temperature-invalid-input-test");
    const auto sensorDir = temp.path() / "hwmon" / "hwmon0";
    writeText(sensorDir / "name", "coretemp\n");
    writeText(sensorDir / "temp1_label", "Package id 0\n");
    writeText(sensorDir / "temp1_input", "invalid\n");

    TemperatureCollector collector({"coretemp"}, (temp.path() / "hwmon").string());
    auto stats = collector.collect();

    ASSERT_TRUE(stats.has_value());
    EXPECT_TRUE(stats->zonesReadings.empty());
}

TEST(SystemMetricsTest, SysMetricsCollectorAggregatesFakeSystemPaths) {
    TempDir temp("time-agent-sysmetrics-test");
    const auto hwmonRoot = temp.path() / "hwmon";
    const auto netRoot = temp.path() / "net";
    const auto procStat = temp.path() / "stat";
    const auto procMeminfo = temp.path() / "meminfo";

    createHwmonSensor(hwmonRoot, "hwmon0", "coretemp", "Package id 0", 44500);
    createNetworkStats(netRoot, "eth-test");
    writeText(procStat, firstCpuSample());
    writeText(procMeminfo, meminfoSample());

    AgentConfig config;
    config.configTemperatureCollector.sensors.emplace("coretemp");

    SystemPaths paths;
    paths.hwmon = hwmonRoot.string();
    paths.netStats = netRoot.string();
    paths.procStat = procStat.string();
    paths.procMeminfo = procMeminfo.string();

    SysMetricsCollector collector(config, paths);
    collector.setInterface("eth-test");
    auto first = collector.collect();
    EXPECT_GT(first.timestamp_us, 0);

    writeText(procStat, secondCpuSample());
    auto stats = collector.collect();

    EXPECT_GT(stats.timestamp_us, 0);
    EXPECT_EQ(stats.networkStats.rx_packets, 101);
    EXPECT_EQ(stats.memoryStats.mem_available_kb, 4'500'000);
    EXPECT_NEAR(stats.cpuStats.usage_percent, 66.666, 0.01);
    ASSERT_EQ(stats.temperatureStats.zonesReadings.size(), 1);
    EXPECT_EQ(stats.temperatureStats.zonesReadings[0].sensor, "coretemp");
    EXPECT_EQ(stats.temperatureStats.zonesReadings[0].temperature, 44500);
}
