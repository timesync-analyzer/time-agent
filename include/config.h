#pragma once
#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>
#include <unordered_set>

struct GlobalConfig {
    std::string node;
};

struct ServiceConfig {
    std::string unit;
    std::string parser;
};

struct MonitoringConfig {
    int poll_timeout_ms;
    int sys_metric_update_freq;
    std::string log_level;
};

struct TemperatureCollectorConfig {
    std::unordered_set<std::string> sensors;
};

struct NetworkConfig {
    std::string interface_name;
};

struct ZMQConfig {
    std::string endpoint;
    int queue_size;
    int timeout_after_close_ms;
};

struct AppConfig {
    GlobalConfig globalConfig;
    MonitoringConfig monitorConfig;
    std::vector<ServiceConfig> services;
    TemperatureCollectorConfig configTemperatureCollector;
    NetworkConfig configNetworkCollector;
    ZMQConfig configZMQ;
};

class ConfigLoader {
public:
    static std::optional<AppConfig> load(const std::string& path);
    static AppConfig loadOrDefault(const std::string& path);
    static AppConfig defaultConfig();
};