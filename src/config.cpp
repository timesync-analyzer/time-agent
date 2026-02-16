#include "config.h"

#include <spdlog/spdlog.h>

std::optional<AppConfig> ConfigLoader::load(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        AppConfig config;

        if (root["global"]) {
            const auto& global = root["global"];
            if (global["node"]) {
                config.globalConfig.node = global["node"].as<std::string>();
            }
            if (global["regime"]) {
                config.globalConfig.sync_regime = global["sync_regime"].as<std::string>();
            }
        }

        if (root["services"]) {
            for (const auto& svc : root["services"]) {
                if (!svc["unit"] || !svc["parser"]) {
                    continue;
                }
                ServiceConfig sc;
                sc.unit = svc["unit"].as<std::string>();
                sc.parser = svc["parser"].as<std::string>();
                config.services.push_back(std::move(sc));
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

        if (root["network"] && root["network"]["interface_name"]) {
            config.configNetworkCollector.interface_name = root["network"]["interface_name"].as<std::string>();
        }

        if (root["zmq"]) {
            const auto& zmq = root["zmq"];
            if (zmq["endpoint"]) {
                config.configZMQ.endpoint = zmq["endpoint"].as<std::string>();
            }
            if (zmq["queue_size"]) {
                config.configZMQ.queue_size = zmq["queue_size"].as<int>();
            }
            if (zmq["node"]) {
                config.configZMQ.timeout_after_close_ms = zmq["timeout_after_close_ms"].as<int>();
            }
        }

        return config;

    } catch (const YAML::Exception& e) {
        return std::nullopt;
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

AppConfig ConfigLoader::defaultConfig() {
    AppConfig config;

    config.services = {{"ptp4l@slave.service", "ptp4l"}, {"phc2sys@slave.service", "phc2sys"}};

    config.monitorConfig.poll_timeout_ms = 1000;
    config.monitorConfig.log_level = "info";

    return config;
}

AppConfig ConfigLoader::loadOrDefault(const std::string& path) {
    auto config = load(path);
    if (config) {
        return *config;
    }
    spdlog::warn("Using default configuration");
    return defaultConfig();
}