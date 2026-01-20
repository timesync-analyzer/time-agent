#pragma once
#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>

struct ServiceConfig {
    std::string unit;
    std::string parser;
};

struct MonitoringConfig {
    int poll_timeout_ms;
    std::string log_level;
};

struct AppConfig {
    MonitoringConfig monitorConfig;
    std::vector<ServiceConfig> services;
};

class ConfigLoader {
public:
    static std::optional<AppConfig> load(const std::string& path);
    static AppConfig loadOrDefault(const std::string& path);
    static AppConfig defaultConfig();
};