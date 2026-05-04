#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "config.h"

class ConfigTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir;
    std::filesystem::path validConfigPath;
    std::filesystem::path invalidConfigPath;

    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / ("time-agent-config-test-" + std::to_string(::getpid()));
        std::filesystem::create_directories(tempDir);
        validConfigPath = tempDir / "test_config.yaml";
        invalidConfigPath = tempDir / "test_config_invalid.yaml";

        std::ofstream file(validConfigPath);
        file << R"(global:
  node: "test-node-01"

services:
  ptp4l:
    on: true
    source: "journal"
    dev: "eth0"
  phc2sys:
    on: false
    source: "subproccess"
    dev: "eth0"
  ppswatch:
    on: true
    source: "subproccess"
    dev: "/dev/pps0"

settings:
  poll_timeout_ms: 2000
  log_level: "debug"
  sys_metric_update_freq: 5

temperature:
  sensors:
    - name: "coretemp-isa-0000"
    - name: "nvme-pci-0100"

network:
  interface_name: "enp0s3"

zmq:
  endpoint: "tcp://localhost:5555"
  queue_size: 100
  timeout_after_close_ms: 1000
)";
        file.close();

        std::ofstream invalid(invalidConfigPath);
        invalid << "invalid: yaml: [unclosed\n";
        invalid.close();
    }

    void TearDown() override { std::filesystem::remove_all(tempDir); }
};

TEST_F(ConfigTest, LoadValidConfig) {
    auto config = ConfigLoader::load(validConfigPath.string());

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->globalConfig.node, "test-node-01");
    EXPECT_EQ(config->monitorConfig.poll_timeout_ms, 2000);
    EXPECT_EQ(config->monitorConfig.log_level, "debug");
    EXPECT_EQ(config->monitorConfig.sys_metric_update_freq, 5);
    EXPECT_EQ(config->configZMQ.endpoint, "tcp://localhost:5555");
    EXPECT_EQ(config->configZMQ.queue_size, 100);
    EXPECT_EQ(config->configZMQ.timeout_after_close_ms, 1000);
}

TEST_F(ConfigTest, LoadInvalidYaml) {
    auto config = ConfigLoader::load(invalidConfigPath.string());
    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, LoadNonexistentFile) {
    auto config = ConfigLoader::load((tempDir / "nonexistent.yaml").string());
    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, LoadServices) {
    auto config = ConfigLoader::load(validConfigPath.string());

    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(config->service.size(), 3);
    EXPECT_EQ(config->service[0].name, "ptp4l");
    EXPECT_TRUE(config->service[0].on);
    EXPECT_EQ(config->service[0].source, "journal");
    EXPECT_EQ(config->service[1].name, "phc2sys");
    EXPECT_FALSE(config->service[1].on);
    EXPECT_EQ(config->service[1].source, "subproccess");
    EXPECT_EQ(config->service[2].name, "ppswatch");
    EXPECT_TRUE(config->service[2].on);
    EXPECT_EQ(config->service[2].source, "subproccess");
    EXPECT_EQ(config->service[2].dev, "/dev/pps0");
}

TEST_F(ConfigTest, DefaultConfig) {
    auto config = ConfigLoader::defaultConfig();

    EXPECT_EQ(config.monitorConfig.poll_timeout_ms, 1000);
    EXPECT_EQ(config.monitorConfig.log_level, "info");
}

TEST_F(ConfigTest, LoadOrDefaultFallback) {
    auto config = ConfigLoader::loadOrDefault((tempDir / "nonexistent.yaml").string());

    EXPECT_EQ(config.monitorConfig.poll_timeout_ms, 1000);
    EXPECT_EQ(config.monitorConfig.log_level, "info");
}

TEST_F(ConfigTest, TemperatureSensorsLoading) {
    auto config = ConfigLoader::load(validConfigPath.string());

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->configTemperatureCollector.sensors.size(), 2);
    EXPECT_TRUE(config->configTemperatureCollector.sensors.count("coretemp-isa-0000") > 0);
    EXPECT_TRUE(config->configTemperatureCollector.sensors.count("nvme-pci-0100") > 0);
}
