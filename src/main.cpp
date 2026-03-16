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

std::unique_ptr<ICollector> makeCollector(const ServiceConfig& cfg) {
    spdlog::info("Create {} {} collector", cfg.name, cfg.source);
    if (cfg.name == "ptp4l") {
        if (cfg.source == "subproccess") {
            return std::make_unique<SubproccessCollector>(
                "ptp4l", std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "/usr/local/sbin/ptp4l", "-i", cfg.dev, "-s", "-m",
                                                  "--free_running", "1"});
        } else if (cfg.source == "journal") {
            return std::make_unique<JournalCollector>("ptp4l");
        }
    } else if (cfg.name == "phc2sys") {
        if (cfg.source == "subproccess") {
            return std::make_unique<SubproccessCollector>(
                "phc2sys", std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "/usr/sbin/phc2sys", "-s", cfg.dev, "-c",
                                                    "CLOCK_REALTIME", "-w", "-m", "--free_running", "1"});
        } else if (cfg.source == "journal") {
            return std::make_unique<JournalCollector>("phc2sys");
        }
    } else if (cfg.name == "ppswatch") {
        if (cfg.source == "subproccess") {
            return std::make_unique<SubproccessCollector>(
                "ppswatch", std::vector<std::string>{"/usr/bin/stdbuf", "-oL", "/usr/bin/ppswatch", cfg.dev});
        } else if (cfg.source == "journal") {
            return std::make_unique<JournalCollector>("ppswatch");
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
    for (const auto& cfg : config.service) {
        if (cfg.on) {
            collectors[cfg.name] = makeCollector(cfg);
        }
    }
    spdlog::info("Connecting to endpoint: '{}'", config.configZMQ.endpoint);
    auto adapter = std::make_unique<ZMQAdapter>(config.configZMQ, config.globalConfig.node);
    EventLoop loop(std::move(collectors), std::move(adapter), config);

    g_loop = &loop;
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    loop.run();

    return 0;
}