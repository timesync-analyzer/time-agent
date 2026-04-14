#pragma once

#include <collector.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>

#include "adapter.h"
#include "config.h"
#include "metrics.pb.h"
#include "system_metrics.h"

class Agent {
public:
    Agent(std::unordered_map<std::string, std::unique_ptr<ICollector>> collector, std::unique_ptr<IAdapter> adapter,
              const AgentConfig& config);
    void run();
    void stop();

private:
    using ParserFunc = std::function<void(const CollectorEvent&)>;
    using Handler = std::function<void(const CollectorEvent&)>;
    using HandlerMap = std::unordered_map<std::string, Handler>;

    HandlerMap buildHandlers(IAdapter& adapter, const std::string& node);
    void readerLoop(ICollector& collector, Handler handler);
    void sysMetricsLoop();

    std::string node{""};
    std::string ip{""};
    std::string node_type{""};
    std::string net_interface{""};

    std::unordered_map<std::string, std::unique_ptr<ICollector>> collectors;
    std::unique_ptr<IAdapter> adapter;
    SysMetricsCollector sysMetricsCollector;
    std::mutex adapterMutex_;
    int pollTimeoutMs;
    std::atomic<bool> running{false};
    std::vector<std::thread> readerThreads_;
    std::thread sysMetricsThread_;
    HandlerMap handlers_;
};
