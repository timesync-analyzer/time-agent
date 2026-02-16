#include "loop.h"

#include <spdlog/spdlog.h>

#include "adapter.h"
#include "metrics.pb.h"
#include "timestamp_utils.h"

EventLoop::EventLoop(std::unique_ptr<ICollector> collector, std::unique_ptr<IAdapter> adapter, const AppConfig& config)
    : node(config.globalConfig.node),
      ip(""),  // need specify
      node_type(config.globalConfig.sync_regime == "master" ? NODE_TYPE_MASTER : NODE_TYPE_SLAVE),
      net_interface(config.configNetworkCollector.interface_name),
      collector(std::move(collector)),
      adapter(std::move(adapter)),
      sys_metrics_collector(config),
      sys_metric_update_freq(config.monitorConfig.sys_metric_update_freq),
      poll_timeout_ms(config.monitorConfig.poll_timeout_ms) {
    initParsers();
}

void EventLoop::initParsers() {
    parsers_["ptp4l"] = [this](const JournalEvent& event) {
        auto result = collector->parse_ptp4l_msg(event);
        if (result) {
            adapter->send_ptp_statistics(*result, node);
        }
    };

    parsers_["phc2sys"] = [this](const JournalEvent& event) {
        auto result = collector->parse_phc2sys_msg(event);
        if (result) {
            auto pb_metrics = converters::to_phc2sys_metrics(*result, node);
            adapter->send_phc2sys_statistics(*result, node);
        }
    };
}

void EventLoop::run() {
    spdlog::info("Event loop started");
    adapter->send_node_info(getNodeInfo(), node);
    SystemStats sysMetrics;
    running = true;
    int iter_counter = 0;
    while (running) {
        if (collector->waitForData(poll_timeout_ms)) {
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
        if (++iter_counter >= sys_metric_update_freq) {
            sysMetrics = sys_metrics_collector.collect();
            sysMetrics.timestamp_ms = timestamp_utils::now_us();
            adapter->send_sys_statistics(sysMetrics, node);
            iter_counter = 0;
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