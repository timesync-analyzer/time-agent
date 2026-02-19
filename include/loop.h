#pragma once

#include <collector.h>

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>

#include "adapter.h"
#include "config.h"
#include "metrics.pb.h"
#include "system_metrics.h"

class EventLoop {
public:
    EventLoop(std::unique_ptr<ICollector> collector, std::unique_ptr<IAdapter> adapter, const AppConfig& config);
    void run();
    void stop();

private:
    using ParserFunc = std::function<void(const JournalEvent&)>;
    using HandlerMap = std::unordered_map<std::string, ParserFunc>;

    static HandlerMap buildHandlers(ICollector& collector, IAdapter& adapter, const std::string& node);
    NodeInfo getNodeInfo() const;

    std::string node;
    std::string ip;
    NodeType node_type;
    std::string net_interface;

    std::unique_ptr<ICollector> collector;
    std::unique_ptr<IAdapter> adapter;
    SysMetricsCollector sysMetricsCollector;
    int sysMetricUpdateFreq;
    int pollTimeoutMs;
    std::atomic<bool> running{false};
    HandlerMap parsers_;
};
