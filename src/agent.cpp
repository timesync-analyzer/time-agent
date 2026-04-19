#include "agent.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

#include "adapter.h"
#include "metrics.pb.h"
#include "timestamp_utils.h"

#include "network_adapter_resolver.h"

Agent::Agent(std::unordered_map<std::string, std::unique_ptr<ICollector>> collectors, std::unique_ptr<IAdapter> adapter,
                     const AgentConfig& config)
    : node(config.globalConfig.node),
      collectors(std::move(collectors)),
      adapter(std::move(adapter)),
      sysMetricsCollector(config),
      pollTimeoutMs(config.monitorConfig.poll_timeout_ms) {
    handlers_ = buildHandlers(*this->adapter, node);
}

Agent::HandlerMap Agent::buildHandlers(IAdapter& adapter, const std::string& node) {
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
                portEvent->adapterName = NetworkAdapterResolver::resolve(portEvent->portName);
                if (!portEvent->adapterName.empty()) {
                    spdlog::info("set new watching interface {} ({})", portEvent->portName, portEvent->adapterName);
                } else {
                    spdlog::info("set new watching interface {}", portEvent->portName);
                }
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

void Agent::readerLoop(ICollector& collector, Handler handler) {
    while (running) {
        auto event = collector.readEvent();

        if (!event) {
            break;
        }
        std::lock_guard lock(adapterMutex_);
        handler(*event);
    }
}

void Agent::sysMetricsLoop() {
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollTimeoutMs));
        if (!running) {
            break;
        }
        SystemStats sysMetrics = sysMetricsCollector.collect();
        sysMetrics.timestamp_us = timestamp_utils::now_us();
        std::lock_guard lock(adapterMutex_);
        adapter->send_sys_statistics(sysMetrics, node);
    }
}

void Agent::run() {
    spdlog::info("Event loop started");

    running = true;

    for (auto& [name, collector] : collectors) {
        auto it = handlers_.find(name);
        if (it == handlers_.end()) {
            spdlog::warn("No handler for collector: {}", name);
            continue;
        }
        readerThreads_.emplace_back([this, &collector = *collector, handler = it->second]() { readerLoop(collector, handler); });
    }

    sysMetricsThread_ = std::thread([this]() { sysMetricsLoop(); });

    for (auto& t : readerThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    if (sysMetricsThread_.joinable()) {
        sysMetricsThread_.join();
    }

    spdlog::info("Event loop stopped");
}

void Agent::stop() {
    running = false;
    for (auto& [name, collector] : collectors) {
        collector->stop();
    }
}