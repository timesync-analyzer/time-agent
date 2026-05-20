#pragma once
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

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
 * @brief PTP topology collection settings.
 */
struct PtpTopologyConfig {
    /**
     * @brief Enables periodic pmc topology snapshots.
     */
    bool on = false;
    /**
     * @brief Path to the linuxptp pmc binary.
     */
    std::string pmc_path = "/usr/sbin/pmc";
    /**
     * @brief Unix-domain socket used by pmc to query ptp4l.
     */
    std::string uds_path = "/var/run/ptp4lro";
    /**
     * @brief Optional ptp4l config path passed to pmc.
     */
    std::string config_path;
    /**
     * @brief PTP domain number.
     */
    int domain_number = 0;
    /**
     * @brief Boundary hops for pmc management messages.
     */
    int boundary_hops = 0;
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
    PtpTopologyConfig configPtpTopology;
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
