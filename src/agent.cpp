#include "agent.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>

#include "adapter.h"
#include "metrics.pb.h"
#include "network_adapter_resolver.h"
#include "timestamp_utils.h"

Agent::Agent(std::unordered_map<std::string, std::unique_ptr<ICollector>> collectors, std::unique_ptr<IAdapter> adapter,
             const AgentConfig& config)
    : node_(config.globalConfig.node),
      collectors_(std::move(collectors)),
      adapter_(std::move(adapter)),
      sysMetricsCollector_(config),
      ptpTopologyCollector_(config.configPtpTopology),
      pollTimeoutMs_(config.monitorConfig.poll_timeout_ms) {
    handlers_ = buildHandlers(*this->adapter_, node_);
}

Agent::HandlerMap Agent::buildHandlers(IAdapter& adapter, const std::string& node) {
    HandlerMap handlers;

    handlers["ptp4l"] = [parser = std::make_shared<Ptp4lParser>(), &adapter, node, this](const CollectorEvent& event) {
        if (auto metrics = parser->parseMetrics(event.msg)) {
            metrics->timestamp_us = event.ts_usec;
            metrics->unit = event.unit;
            spdlog::debug("[{}] parsed {}: offset = {}, freq = {}, path_delay = {}", metrics->timestamp_us, metrics->unit,
                          metrics->offset, metrics->freq, metrics->path_delay);
            {
                std::lock_guard lock(adapterMutex_);
                adapter.send_ptp_statistics(*metrics, node);
            }
            return;
        }
        if (auto portEvent = parser->parsePortEvent(event.msg)) {
            portEvent->timestamp_us = event.ts_usec;
            portEvent->unit = event.unit;
            spdlog::debug("[ptp4l] port {} ({}) {} -> {} ({})", portEvent->portNumber, portEvent->portName, portEvent->fromState,
                          portEvent->toState, portEvent->trigger);
            if (portEvent->portName.find('/') == std::string::npos) {
                spdlog::info("set new watching interface {}", portEvent->portName);
                sysMetricsCollector_.setInterface(portEvent->portName);
                portEvent->adapterName = NetworkAdapterResolver::resolve(portEvent->portName);
                if (!portEvent->adapterName.empty()) {
                    spdlog::info("set new watching interface {} ({})", portEvent->portName, portEvent->adapterName);
                } else {
                    spdlog::info("set new watching interface {}", portEvent->portName);
                }
            }
            {
                std::lock_guard lock(adapterMutex_);
                adapter.send_ptp4l_port_event(*portEvent, node);
            }
            return;
        }
        if (auto foreignMaster = parser->parseForeignMasterEvent(event.msg)) {
            foreignMaster->timestamp_us = event.ts_usec;
            foreignMaster->unit = event.unit;
            spdlog::info("[ptp4l] new foreign master {}-{} on port {} ({})", foreignMaster->masterClockIdentity,
                         foreignMaster->masterPortNumber, foreignMaster->portNumber, foreignMaster->portName);
            return;
        }
        if (auto bestMaster = parser->parseBestMasterEvent(event.msg)) {
            bestMaster->timestamp_us = event.ts_usec;
            bestMaster->unit = event.unit;
            spdlog::info("[ptp4l] selected best master clock {}", bestMaster->masterClockIdentity);
            if (ptpTopologyCollector_.enabled()) {
                if (const auto snapshot = ptpTopologyCollector_.collect()) {
                    spdlog::info(
                        "PTP topology snapshot selected_best_master={} local_clock={} parent_clock={} grandmaster_clock={} "
                        "steps_removed={} mean_path_delay_ns={} child_port={}",
                        bestMaster->masterClockIdentity, snapshot->localClockIdentity, snapshot->parentClockIdentity,
                        snapshot->grandmasterIdentity, snapshot->stepsRemoved, snapshot->meanPathDelayNs,
                        snapshot->childPortNumber);
                    {
                        std::lock_guard lock(adapterMutex_);
                        adapter.send_ptp_topology_snapshot(*snapshot, node);
                    }
                } else {
                    spdlog::warn("failed to collect PTP topology snapshot after best master {}", bestMaster->masterClockIdentity);
                }
            }
            return;
        }
        spdlog::debug("[ptp4l] unhandled: {}", event.msg);
    };

    handlers["phc2sys"] = [parser = std::make_shared<Phc2SysParser>(), &adapter, node, this](const CollectorEvent& event) {
        if (auto metrics = parser->parseMetrics(event.msg)) {
            metrics->timestamp_us = event.ts_usec;
            metrics->unit = event.unit;
            spdlog::debug("[{}] parsed {}: offset = {}, freq = {}, path_delay = {}", metrics->timestamp_us, metrics->unit,
                          metrics->offset, metrics->freq, metrics->path_delay);
            {
                std::lock_guard lock(adapterMutex_);
                adapter.send_phc2sys_statistics(*metrics, node);
            }
            return;
        }
        if (parser->isWaiting(event.msg)) {
            spdlog::warn("[phc2sys] waiting for ptp4l synchronisation {}", event.msg);
            return;
        }
        spdlog::debug("[phc2sys] unhandled: {}", event.msg);
    };

    handlers["ppswatch"] = [parser = std::make_shared<PPSParser>(), &adapter, node, this](const CollectorEvent& event) {
        if (auto metrics = parser->parseMetrics(event.msg)) {
            metrics->timestamp_us = event.ts_usec;
            metrics->unit = event.unit;
            {
                std::lock_guard lock(adapterMutex_);
                adapter.send_pps_statistics(*metrics, node);
            }
            spdlog::debug("[{}] parsed {}: offset = {}", metrics->timestamp_us, metrics->unit, metrics->offset);
            return;
        }
        spdlog::debug("[ppswatch] unhandled: {}", event.msg);
    };

    return handlers;
}

void Agent::readerLoop(ICollector& collector, Handler handler) {
    while (running_) {
        auto event = collector.readEvent();

        if (!event) {
            break;
        }
        handler(*event);
    }
}

void Agent::sysMetricsLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollTimeoutMs_));
        if (!running_) {
            break;
        }

        SystemStats sysMetrics = sysMetricsCollector_.collect();
        sysMetrics.timestamp_us = timestamp_utils::now_us();
        {
            std::lock_guard lock(adapterMutex_);
            adapter_->send_sys_statistics(sysMetrics, node_);
        }
    }
}

void Agent::run() {
    spdlog::info("Event loop started");

    running_ = true;

    for (auto& [name, collector] : collectors_) {
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
    running_ = false;
    if (sysMetricsThread_.joinable()) {
        sysMetricsThread_.join();
    }

    spdlog::info("Event loop stopped");
}

void Agent::stop() {
    running_ = false;
    for (auto& [name, collector] : collectors_) {
        collector->stop();
    }
}
