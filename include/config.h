#pragma once
#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>
#include <unordered_set>

struct GlobalConfig {
    std::string node;
    bool need_pps;
};

struct ServiceConfig {
    bool on;
    std::string name;
    std::string source;
    std::string dev;
};

struct MonitoringConfig {
    int poll_timeout_ms;
    int sys_metric_update_freq;
    std::string log_level;
};

struct TemperatureCollectorConfig {
    std::unordered_set<std::string> sensors;
};

struct ZMQConfig {
    std::string endpoint;
    int queue_size;
    int timeout_after_close_ms;
};

struct AppConfig {
    GlobalConfig globalConfig;
    MonitoringConfig monitorConfig;
    std::vector<ServiceConfig> service;
    TemperatureCollectorConfig configTemperatureCollector;
    ZMQConfig configZMQ;
};

class ConfigLoader {
public:
    static std::optional<AppConfig> load(const std::string& path);
    static AppConfig loadOrDefault(const std::string& path);
    static AppConfig defaultConfig();
};