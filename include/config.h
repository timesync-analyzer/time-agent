#pragma once
#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>
#include <unordered_set>

struct ServiceConfig {
    std::string unit;
    std::string parser;
};

struct MonitoringConfig {
    int poll_timeout_ms;
    std::string log_level;
};

struct TemperatureCollectorConfig {
    std::unordered_set<std::string> sensors;
};

struct NetworkConfig {
    std::string interface_name;
};

struct AppConfig {
    MonitoringConfig monitorConfig;
    std::vector<ServiceConfig> services;
    TemperatureCollectorConfig configTemperatureCollector;
    NetworkConfig configNetworkCollector;
};

class ConfigLoader {
public:
    static std::optional<AppConfig> load(const std::string& path);
    static AppConfig loadOrDefault(const std::string& path);
    static AppConfig defaultConfig();
};