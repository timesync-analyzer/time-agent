#include "config.h"

#include <spdlog/spdlog.h>

std::optional<AppConfig> ConfigLoader::load(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        AppConfig config;

        if (root["services"]) {
            for (const auto& svc : root["services"]) {
                if (!svc["unit"] || !svc["parser"]) {
                    spdlog::warn("Skipping incomplete service config");
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
        }

        if (root["temperature"] && root["temperature"]["sensors"]) {
            for (const auto& sensor : root["temperature"]["sensors"]) {
                if (sensor["name"]) {
                    config.configTemperatureCollector.sensors.emplace(sensor["name"].as<std::string>());
                }
            }
        }

        // Network
        if (root["network"] && root["network"]["interface_name"]) {
            config.configNetworkCollector.interface_name = root["network"]["interface_name"].as<std::string>();
        }

        return config;

    } catch (const YAML::Exception& e) {
        spdlog::error("Config parse error: {}", e.what());
        return std::nullopt;
    } catch (const std::exception& e) {
        spdlog::error("Config load error: {}", e.what());
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