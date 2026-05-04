#pragma once
#include <yaml-cpp/yaml.h>

#include <optional>
#include <string>
#include <unordered_set>

/**
 * @brief Global identity settings for this agent instance.
 */
struct GlobalConfig {
    std::string node;
};

/**
 * @brief Per-service collector configuration loaded from YAML.
 */
struct ServiceConfig {
    /**
     * @brief Enables or disables the service collector.
     */
    bool on;
    /**
     * @brief Logical collector name, for example ptp4l, phc2sys, or ppswatch.
     */
    std::string name;
    /**
     * @brief Source type: journal or subprocess.
     */
    std::string source;
    /**
     * @brief Device or interface argument used by subprocess collectors.
     */
    std::string dev;
};

/**
 * @brief Runtime settings that control polling and log verbosity.
 */
struct MonitoringConfig {
    /**
     * @brief Sleep interval for periodic system metric collection, in milliseconds.
     */
    int poll_timeout_ms;
    int sys_metric_update_freq;
    std::string log_level;
};

/**
 * @brief Temperature collector settings.
 */
struct TemperatureCollectorConfig {
    /**
     * @brief hwmon sensor names to include, for example coretemp.
     */
    std::unordered_set<std::string> sensors;
};

/**
 * @brief ZMQ PUSH socket settings.
 */
struct ZMQConfig {
    std::string endpoint;
    /**
     * @brief High-water mark for queued outbound messages.
     */
    int queue_size;
    /**
     * @brief Socket linger timeout used during shutdown, in milliseconds.
     */
    int timeout_after_close_ms;
};

/**
 * @brief Complete time-agent configuration assembled from config.yaml.
 */
struct AgentConfig {
    GlobalConfig globalConfig;
    MonitoringConfig monitorConfig;
    std::vector<ServiceConfig> service;
    TemperatureCollectorConfig configTemperatureCollector;
    ZMQConfig configZMQ;
};

/**
 * @brief YAML loader for AgentConfig.
 */
class ConfigLoader {
public:
    /**
     * @brief Loads configuration from path.
     * @param path Path to config.yaml.
     * @return Loaded config, or std::nullopt on parse or I/O errors.
     */
    static std::optional<AgentConfig> load(const std::string& path);
    /**
     * @brief Loads configuration from path or returns defaultConfig() on failure.
     * @param path Path to config.yaml.
     * @return Loaded config or default config.
     */
    static AgentConfig loadOrDefault(const std::string& path);
    /**
     * @brief Returns minimal defaults used when config loading fails.
     * @return Default agent configuration.
     */
    static AgentConfig defaultConfig();
};
