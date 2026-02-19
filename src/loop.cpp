#include "loop.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>

#include "adapter.h"
#include "metrics.pb.h"
#include "timestamp_utils.h"

namespace {

std::string resolveInterfaceIP(const std::string& iface) {
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) {
        spdlog::warn("getifaddrs failed: could not enumerate network interfaces");
        return "";
    }
    std::unique_ptr<struct ifaddrs, decltype(&freeifaddrs)> guard(ifap, freeifaddrs);

    for (auto* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (iface == ifa->ifa_name) {
            char buf[INET_ADDRSTRLEN];
            auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
            spdlog::debug("Resolved IP for interface '{}': {}", iface, buf);
            return buf;
        }
    }

    spdlog::warn("Interface '{}' not found or has no IPv4 address", iface);
    return "";
}

}  // namespace

EventLoop::EventLoop(std::unique_ptr<ICollector> collector, std::unique_ptr<IAdapter> adapter, const AppConfig& config)
    : node(config.globalConfig.node),
      ip(resolveInterfaceIP(config.configNetworkCollector.interface_name)),
      node_type(config.globalConfig.sync_regime == "master" ? NODE_TYPE_MASTER : NODE_TYPE_SLAVE),
      net_interface(config.configNetworkCollector.interface_name),
      collector(std::move(collector)),
      adapter(std::move(adapter)),
      sysMetricsCollector(config),
      sysMetricUpdateFreq(config.monitorConfig.sys_metric_update_freq),
      pollTimeoutMs(config.monitorConfig.poll_timeout_ms) {
    parsers_ = buildHandlers(*this->collector, *this->adapter, node);
}

EventLoop::HandlerMap EventLoop::buildHandlers(ICollector& collector, IAdapter& adapter, const std::string& node) {
    HandlerMap handlers;

    handlers["ptp4l"] = [&collector, &adapter, node](const JournalEvent& event) {
        if (auto metrics = collector.parse_ptp4l_msg(event)) {
            adapter.send_ptp_statistics(*metrics, node);
            return;
        }
        if (auto portEvent = collector.parse_ptp4l_port_event(event)) {
            spdlog::info("[ptp4l] port {} ({}) {} -> {} ({})", portEvent->portNumber, portEvent->portName, portEvent->fromState,
                         portEvent->toState, portEvent->trigger);
            return;
        }
        spdlog::debug("[ptp4l] unhandled: {}", event.msg);
    };

    handlers["phc2sys"] = [&collector, &adapter, node](const JournalEvent& event) {
        if (auto metrics = collector.parse_phc2sys_msg(event)) {
            adapter.send_phc2sys_statistics(*metrics, node);
            return;
        }
        if (collector.is_phc2sys_waiting(event)) {
            spdlog::warn("[phc2sys] waiting for ptp4l synchronisation");
            return;
        }
        spdlog::debug("[phc2sys] unhandled: {}", event.msg);
    };

    return handlers;
}

void EventLoop::run() {
    spdlog::info("Event loop started");
    adapter->send_node_info(getNodeInfo(), node);

    SystemStats sysMetrics;
    running = true;
    int iterCounter = 0;

    while (running) {
        if (collector->waitForData(pollTimeoutMs)) {
            while (auto event = collector->readEvent()) {
                auto it = parsers_.find(event->unit);
                if (it == parsers_.end()) {
                    spdlog::warn("No parser for unit: {}", event->unit);
                    continue;
                }

                it->second(*event);

                spdlog::debug("Processed metrics from {}, {}", event->unit, event->ts_usec);
            }
        }

        if (++iterCounter >= sysMetricUpdateFreq) {
            sysMetrics = sysMetricsCollector.collect();
            sysMetrics.timestamp_us = timestamp_utils::now_us();
            adapter->send_sys_statistics(sysMetrics, node);
            iterCounter = 0;
        }
    }

    spdlog::info("Event loop stopped");
}

void EventLoop::stop() { running = false; }

NodeInfo EventLoop::getNodeInfo() const {
    NodeInfo info;
    info.set_node_type(node_type);
    info.set_ip_address(ip);
    info.set_net_interface(net_interface);
    return info;
}
