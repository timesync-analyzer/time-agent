#include <csignal>
#include <memory>
#include <string_view>
#include <vector>

#include "adapter.h"
#include "collector.h"
#include "config.h"
#include "logging.h"
#include "agent.h"

namespace {
Agent* g_loop = nullptr;

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

std::unique_ptr<ICollector> makeCollector(const ServiceConfig& cfg) {
    spdlog::info("Create {} {} collector interface {}", cfg.name, cfg.source, cfg.dev);

    if (cfg.name == "ptp4l") {
        if (cfg.source == "subproccess") {
                return std::make_unique<SubproccessCollector>(
                    "ptp4l", std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "ptp4l", "-i", cfg.dev, "-s", "-m"});
        } else if (cfg.source == "journal") {
            return std::make_unique<JournalCollector>("ptp4l");
        }
    } else if (cfg.name == "phc2sys") {
        if (cfg.source == "subproccess") {
            return std::make_unique<SubproccessCollector>(
                "phc2sys", std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "/usr/local/sbin/phc2sys", "-a", "-r", "-r", "-m",
                                                    "--free_running", "1", "-l 6"});
        } else if (cfg.source == "journal") {
            return std::make_unique<JournalCollector>("phc2sys");
        }
    } else if (cfg.name == "ppswatch") {
        if (cfg.source == "subproccess") {
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
                collectors[cfg.name] = makeCollector(cfg);
            } catch (...) {
                continue;
            }
        }
    }
    auto adapter = std::make_unique<ZMQAdapter>(config.configZMQ, config.globalConfig.node);
    Agent loop(std::move(collectors), std::move(adapter), config);

    g_loop = &loop;
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    loop.run();

    return 0;
}