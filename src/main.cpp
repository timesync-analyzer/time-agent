#include <csignal>
#include <memory>
#include <string_view>
#include <vector>

#include "adapter.h"
#include "collector.h"
#include "config.h"
#include "logging.h"
#include "loop.h"

namespace {
EventLoop* g_loop = nullptr;

void signalHandler(int sig) {
    spdlog::info("Received signal {}, shutting down...", sig);
    if (g_loop) {
        g_loop->stop();
    }
}

void checkJournalAccess() {
    if (geteuid() != 0) {
        spdlog::warn(
            "Not running as root. Journal access may be limited. "
            "Run with sudo or add user to 'systemd-journal' group.");
    }
}
}  // namespace

std::unique_ptr<ICollector> makeCollector(const ServiceConfig& cfg, const std::string& regime) {
    spdlog::info("Create {} {} collector (regime: {}), interface {}", cfg.name, cfg.source, regime, cfg.dev);

    if (cfg.name == "ptp4l") {
        if (cfg.source == "subproccess") {
            if (regime == "slave") {
                return std::make_unique<SubproccessCollector>(
                    "ptp4l", std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "ptp4l", "-i", cfg.dev, "-s", "-m",
                                                      "--step_threshold", "0.000005"});
            } else if (regime == "master") {
                return std::make_unique<SubproccessCollector>(
                    "ptp4l", std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "ptp4l", "-i", cfg.dev, "-m", "--step_threshold",
                                                      "0.000005"});
            }
        } else if (cfg.source == "journal") {
            return std::make_unique<JournalCollector>("ptp4l");
        }
    } else if (cfg.name == "phc2sys") {
        if (cfg.source == "subproccess") {
            if (regime == "slave") {
                spdlog::debug("create slave phc2sys");
                return std::make_unique<SubproccessCollector>(
                    "phc2sys",
                    std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "phc2sys", "-s", cfg.dev, "-c", "CLOCK_REALTIME", "-w",
                                             "--free_running", "1", "-m", "--step_threshold", "0.000005"});
            } else if (regime == "master") {
                spdlog::debug("create master phc2sys");
                return std::make_unique<SubproccessCollector>(
                    "phc2sys", std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "phc2sys", "-s", "CLOCK_REALTIME", "-c",
                                                        cfg.dev, "-w", "-m", "--step_threshold", "0.000005"});
            }
        } else if (cfg.source == "journal") {
            return std::make_unique<JournalCollector>("phc2sys");
        }
    } else if (cfg.name == "ppswatch") {
        if (cfg.source == "subproccess" && regime == "slave") {
            spdlog::debug("why?");
            return std::make_unique<SubproccessCollector>(
                "ppswatch", std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "/usr/bin/ppswatch", cfg.dev});
        }
    }

    throw std::runtime_error("Unknown service: " + cfg.name);
}

int main(int argc, char* argv[]) {
    std::string config_path = "../config.yaml";
    if (argc > 1) {
        config_path = argv[1];
    }
    checkJournalAccess();

    auto config = ConfigLoader::loadOrDefault(config_path);
    logging::init(config.monitorConfig.log_level);

    spdlog::info("Starting time-agent");
    spdlog::info("Config loaded from: {}", config_path);
    std::unordered_map<std::string, std::unique_ptr<ICollector>> collectors;
    spdlog::debug("config.service.size {}", config.service.size());
    for (const auto& cfg : config.service) {
        spdlog::debug("on {}", cfg.on);
        if (cfg.on) {
            try {
                collectors[cfg.name] = makeCollector(cfg, config.globalConfig.sync_regime);
            } catch (...) {
                continue;
            }
        }
    }
    auto adapter = std::make_unique<ZMQAdapter>(config.configZMQ, config.globalConfig.node);
    EventLoop loop(std::move(collectors), std::move(adapter), config);

    g_loop = &loop;
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    loop.run();

    return 0;
}