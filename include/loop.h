#pragma once
#include <collector.h>

#include <memory>
#include <unordered_map>

#include "config.h"
#include "metrics.h"
#include "system_metrics.h"

class EventLoop {
public:
    EventLoop(std::unique_ptr<ICollector> collector, const AppConfig& config);
    void run();
    void stop();

private:
    std::unique_ptr<ICollector> collector;
    SysMetricsCollector sys_metrics_collector;
    int poll_timeout_ms;
    std::unordered_map<std::string, std::unique_ptr<IMessageParser>> unit2parser;
    bool running;
};