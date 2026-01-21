#include "loop.h"

#include <spdlog/spdlog.h>

#include "adapter.h"

EventLoop::EventLoop(std::unique_ptr<ICollector> collector, std::unique_ptr<IAdapter> adapter, const AppConfig& config)
    : collector(std::move(collector)),
      adapter(std::move(adapter)),
      sys_metrics_collector(config),
      sys_metric_update_freq(config.monitorConfig.sys_metric_update_freq),
      poll_timeout_ms(config.monitorConfig.poll_timeout_ms) {
    initParsers();
}

void EventLoop::initParsers() {
    parsers_["ptp4l@slave.service"] = [this](const JournalEvent& event) {
        auto result = collector->parse_ptp4l_msg(event);
        // if (result) {
        //     auto pb_metrics = converters::to_ptp4l_metrics(*result);
        //     // sender_.send(pb_metrics);
        // }
    };

    parsers_["phc2sys@slave.service"] = [this](const JournalEvent& event) {
        auto result = collector->parse_phc2sys_msg(event);
        // if (result) {
        //     auto pb_metrics = converters::to_phc2sys_metrics(*result, node_id_);
        //     sender_.send(pb_metrics);
        // }
    };
}

void EventLoop::run() {
    spdlog::info("Event loop started");
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

                spdlog::debug("Processed metrics from {}", event->unit);
            }
        }
        if (++iter_counter >= sys_metric_update_freq) {
            sysMetrics = sys_metrics_collector.collect();
            // adapter->send_sys_metrics(sysMetrics);
            iter_counter = 0;
        }
    }

    spdlog::info("Event loop stopped");
}

void EventLoop::stop() { running = false; }