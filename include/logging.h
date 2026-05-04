#pragma once

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace logging {

/**
 * @brief Initializes the process-wide spdlog logger used by time-agent.
 * @param level Minimum log level name.
 */
inline void init(const std::string& level = "info") {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_st>();

    auto logger = std::make_shared<spdlog::logger>("time-agent", console_sink);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    if (level == "debug") {
        logger->set_level(spdlog::level::debug);
    } else if (level == "info") {
        logger->set_level(spdlog::level::info);
    } else if (level == "warn") {
        logger->set_level(spdlog::level::warn);
    } else if (level == "error") {
        logger->set_level(spdlog::level::err);
    } else {
        logger->set_level(spdlog::level::info);
    }

    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::warn);
}

/**
 * @brief Flushes and releases spdlog resources.
 */
inline void shutdown() { spdlog::shutdown(); }
}  // namespace logging
