#include "loop.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

#include "adapter.h"
#include "metrics.pb.h"
#include "timestamp_utils.h"

EventLoop::EventLoop(std::unordered_map<std::string, std::unique_ptr<ICollector>> collectors, std::unique_ptr<IAdapter> adapter,
                     const AppConfig& config)
    : node(config.globalConfig.node),
      collectors(std::move(collectors)),
      adapter(std::move(adapter)),
      sysMetricsCollector(config),
      sysMetricUpdateFreq(config.monitorConfig.sys_metric_update_freq),
      pollTimeoutMs(config.monitorConfig.poll_timeout_ms) {
    parsers_ = buildHandlers(*this->adapter, node);
}

EventLoop::HandlerMap EventLoop::buildHandlers(IAdapter& adapter, const std::string& node) {
    HandlerMap handlers;

    handlers["ptp4l"] = [parser = std::make_shared<Ptp4lParser>(), &adapter, node, this](const CollectorEvent& event) {
        if (auto metrics = parser->parseMetrics(event.msg)) {
            metrics->timestamp_us = event.ts_usec;
            metrics->unit = event.unit;
            spdlog::debug("[{}] parsed {}: offset = {}, freq = {}, path_delay = {}", metrics->timestamp_us, metrics->unit,
                          metrics->offset, metrics->freq, metrics->path_delay);
            adapter.send_ptp_statistics(*metrics, node);
            return;
        }
        if (auto portEvent = parser->parsePortEvent(event.msg)) {
            portEvent->timestamp_us = event.ts_usec;
            portEvent->unit = event.unit;
            spdlog::debug("[ptp4l] port {} ({}) {} -> {} ({})", portEvent->portNumber, portEvent->portName, portEvent->fromState,
                         portEvent->toState, portEvent->trigger);
            if (portEvent->portName.find('/') == std::string::npos) {
                spdlog::info("set new watching interface {}", portEvent->portName);
                sysMetricsCollector.setInterface(portEvent->portName);
            }
            adapter.send_ptp4l_port_event(*portEvent, node);
            return;
        }
        spdlog::debug("[ptp4l] unhandled: {}", event.msg);
    };

    handlers["phc2sys"] = [parser = std::make_shared<Phc2SysParser>(), &adapter, node](const CollectorEvent& event) {
        if (auto metrics = parser->parseMetrics(event.msg)) {
            metrics->timestamp_us = event.ts_usec;
            metrics->unit = event.unit;
            spdlog::debug("[{}] parsed {}: offset = {}, freq = {}, path_delay = {}", metrics->timestamp_us, metrics->unit,
                          metrics->offset, metrics->freq, metrics->path_delay);
            adapter.send_phc2sys_statistics(*metrics, node);
            return;
        }
        if (parser->isWaiting(event.msg)) {
            spdlog::warn("[phc2sys] waiting for ptp4l synchronisation {}", event.msg);
            return;
        }
        spdlog::debug("[phc2sys] unhandled: {}", event.msg);
    };

    handlers["ppswatch"] = [parser = std::make_shared<PPSParser>(), &adapter, node](const CollectorEvent& event) {
        if (auto metrics = parser->parseMetrics(event.msg)) {
            metrics->timestamp_us = event.ts_usec;
            metrics->unit = event.unit;
            adapter.send_pps_statistics(*metrics, node);
            spdlog::debug("[{}] parsed {}: offset = {}", metrics->timestamp_us, metrics->unit, metrics->offset);
            return;
        }
        spdlog::debug("[ppswatch] unhandled: {}", event.msg);
    };

    return handlers;
}

void EventLoop::readerLoop(ICollector& collector) {
    while (running) {
        auto event = collector.readEvent();
        if (!event) {
            break;
        }
        eventQueue_.push(std::move(*event));
    }
}

void EventLoop::run() {
    spdlog::info("Event loop started");

    running = true;

    for (auto& [name, collector] : collectors) {
        readerThreads_.emplace_back([this, &collector = *collector]() { readerLoop(collector); });
    }

    SystemStats sysMetrics;
    int iterCounter = 0;

    while (running) {
        while (auto event = eventQueue_.pop(std::chrono::milliseconds(pollTimeoutMs))) {
            auto it = parsers_.find(event->unit);
            if (it == parsers_.end()) {
                spdlog::warn("No parser for unit: {}", event->unit);
                continue;
            }
            it->second(*event);
            ++iterCounter;
            if (iterCounter >= sysMetricUpdateFreq) {
                sysMetrics = sysMetricsCollector.collect();
                sysMetrics.timestamp_us = timestamp_utils::now_us();
                adapter->send_sys_statistics(sysMetrics, node);
                iterCounter = 0;
            }
        }
    }

    for (auto& t : readerThreads_) {
        if (t.joinable()) t.join();
    }

    spdlog::info("Event loop stopped");
}

void EventLoop::stop() {
    running = false;
    for (auto& [name, collector] : collectors) {
        collector->stop();
    }
}