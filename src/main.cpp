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
    std::vector<std::string_view> units;
    for (const auto& svc : config.services) {
        units.emplace_back(svc.unit);
        spdlog::debug("Monitoring unit: {}", svc.unit);
    }
    auto collector = std::make_unique<JournalCollector>(units);
    auto adapter = std::make_unique<ZMQAdapter>(config.configZMQ, config.globalConfig.node);
    EventLoop loop(std::move(collector), std::move(adapter), config);

    g_loop = &loop;
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    loop.run();

    return 0;
}