#include "config.h"

#include <spdlog/spdlog.h>

std::optional<AgentConfig> ConfigLoader::load(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        AgentConfig config;

        if (root["global"]) {
            const auto& global = root["global"];
            if (global["node"]) {
                config.globalConfig.node = global["node"].as<std::string>();
            }
        }

        if (root["settings"]) {
            const auto& settings = root["settings"];
            if (settings["poll_timeout_ms"]) {
                config.monitorConfig.poll_timeout_ms = settings["poll_timeout_ms"].as<int>();
            }
            if (settings["log_level"]) {
                config.monitorConfig.log_level = settings["log_level"].as<std::string>();
            }
            if (settings["sys_metric_update_freq"]) {
                config.monitorConfig.sys_metric_update_freq = settings["sys_metric_update_freq"].as<int>();
            }
        }

        if (root["temperature"] && root["temperature"]["sensors"]) {
            for (const auto& sensor : root["temperature"]["sensors"]) {
                if (sensor["name"]) {
                    config.configTemperatureCollector.sensors.emplace(sensor["name"].as<std::string>());
                }
            }
        }

        if (root["zmq"]) {
            const auto& zmq = root["zmq"];
            if (zmq["endpoint"]) {
                config.configZMQ.endpoint = zmq["endpoint"].as<std::string>();
            }
            if (zmq["queue_size"]) {
                config.configZMQ.queue_size = zmq["queue_size"].as<int>();
            }
            if (zmq["timeout_after_close_ms"]) {
                config.configZMQ.timeout_after_close_ms = zmq["timeout_after_close_ms"].as<int>();
            }
        }

        if (root["services"]) {
            const auto& services = root["services"];
            if (services["ptp4l"]) {
                ServiceConfig ptp4lConfig;
                ptp4lConfig.name = "ptp4l";
                ptp4lConfig.on = services["ptp4l"]["on"].as<bool>(false);
                ptp4lConfig.source = services["ptp4l"]["source"].as<std::string>();
                config.service.push_back(ptp4lConfig);
            }
            if (services["phc2sys"]) {
                ServiceConfig phc2sysConfig;
                phc2sysConfig.name = "phc2sys";
                phc2sysConfig.on = services["phc2sys"]["on"].as<bool>(false);
                phc2sysConfig.source = services["phc2sys"]["source"].as<std::string>();
                config.service.push_back(phc2sysConfig);
            }
            if (services["ppswatch"]) {
                ServiceConfig ppsConfig;
                ppsConfig.name = "ppswatch";
                ppsConfig.on = services["ppswatch"]["on"].as<bool>(false);
                ppsConfig.dev = services["ppswatch"]["dev"].as<std::string>();
                ppsConfig.source = services["ppswatch"]["source"].as<std::string>();
                config.service.push_back(ppsConfig);
            }
        }
        return config;

    } catch (const YAML::Exception& e) {
        spdlog::error("YAML parsing error in '{}': {}", path, e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::error("Failed to load config '{}': {}", path, e.what());
        return std::nullopt;
    }
}

AgentConfig ConfigLoader::defaultConfig() {
    AgentConfig config;

    config.monitorConfig.poll_timeout_ms = 1000;
    config.monitorConfig.log_level = "info";

    return config;
}

AgentConfig ConfigLoader::loadOrDefault(const std::string& path) {
    auto config = load(path);
    if (config) {
        return *config;
    }
    spdlog::warn("Using default configuration");
    return defaultConfig();
}