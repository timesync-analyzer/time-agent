#pragma once
#include <collector.h>

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
    void initParsers();
    NodeInfo getNodeInfo() const;

    std::string node;
    std::string ip;
    NodeType node_type;
    std::string net_interface;

    std::unique_ptr<ICollector> collector;
    std::unique_ptr<IAdapter> adapter;
    SysMetricsCollector sys_metrics_collector;
    int sys_metric_update_freq;
    int poll_timeout_ms;
    bool running;
    using ParserFunc = std::function<void(const JournalEvent&)>;
    std::unordered_map<std::string, ParserFunc> parsers_;
};