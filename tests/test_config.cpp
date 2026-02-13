#include <gtest/gtest.h>

#include <fstream>

#include "config.h"

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Создаём валидный тестовый конфиг
        std::ofstream file("fixtures/test_config.yaml");
        file << R"(global:
  node: "test-node-01"

services:
  - unit: "ptp4l@test.service"
    parser: "ptp4l"
  - unit: "phc2sys@test.service"
    parser: "phc2sys"

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

        // Создаём невалидный конфиг
        std::ofstream invalid("fixtures/test_config_invalid.yaml");
        invalid << "invalid: yaml: [unclosed\n";
        invalid.close();
    }
};

TEST_F(ConfigTest, LoadValidConfig) {
    auto config = ConfigLoader::load("fixtures/test_config.yaml");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->globalConfig.node, "test-node-01");
    EXPECT_EQ(config->services.size(), 2);
    EXPECT_EQ(config->services[0].unit, "ptp4l@test.service");
    EXPECT_EQ(config->services[1].parser, "phc2sys");
    EXPECT_EQ(config->monitorConfig.poll_timeout_ms, 2000);
    EXPECT_EQ(config->monitorConfig.log_level, "debug");
    EXPECT_EQ(config->monitorConfig.sys_metric_update_freq, 5);
    EXPECT_EQ(config->configNetworkCollector.interface_name, "enp0s3");
    EXPECT_EQ(config->configZMQ.endpoint, "tcp://localhost:5555");
    EXPECT_EQ(config->configZMQ.queue_size, 100);
}

TEST_F(ConfigTest, LoadInvalidYaml) {
    auto config = ConfigLoader::load("fixtures/test_config_invalid.yaml");
    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, LoadNonexistentFile) {
    auto config = ConfigLoader::load("fixtures/nonexistent.yaml");
    EXPECT_FALSE(config.has_value());
}

TEST_F(ConfigTest, DefaultConfig) {
    auto config = ConfigLoader::defaultConfig();

    EXPECT_EQ(config.services.size(), 2);
    EXPECT_EQ(config.services[0].unit, "ptp4l@slave.service");
    EXPECT_EQ(config.services[1].unit, "phc2sys@slave.service");
    EXPECT_EQ(config.monitorConfig.poll_timeout_ms, 1000);
    EXPECT_EQ(config.monitorConfig.log_level, "info");
}

TEST_F(ConfigTest, LoadOrDefaultFallback) {
    auto config = ConfigLoader::loadOrDefault("nonexistent.yaml");

    EXPECT_EQ(config.services.size(), 2);
    EXPECT_EQ(config.monitorConfig.log_level, "info");
}

TEST_F(ConfigTest, TemperatureSensorsLoading) {
    auto config = ConfigLoader::load("fixtures/test_config.yaml");

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->configTemperatureCollector.sensors.size(), 2);
    EXPECT_TRUE(config->configTemperatureCollector.sensors.count("coretemp-isa-0000") > 0);
    EXPECT_TRUE(config->configTemperatureCollector.sensors.count("nvme-pci-0100") > 0);
}
