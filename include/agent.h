#pragma once

#include "collector.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>

#include "adapter.h"
#include "config.h"
#include "metrics.pb.h"
#include "system_metrics.h"

/**
 * @brief Coordinates collectors, parsers, system metrics, and outbound transport.
 *
 * Agent owns all collectors and the adapter. run() starts one reader thread per
 * service collector plus a periodic system metrics thread.
 */
class Agent {
public:
    /**
     * @brief Takes ownership of collectors and adapter and builds parser handlers.
     * @param collector Service collectors keyed by logical service name.
     * @param adapter Outbound metric adapter.
     * @param config Agent runtime configuration.
     */
    Agent(std::unordered_map<std::string, std::unique_ptr<ICollector>> collector, std::unique_ptr<IAdapter> adapter,
              const AgentConfig& config);
    /**
     * @brief Runs until collectors finish or stop() is requested.
     */
    void run();
    /**
     * @brief Requests all collector threads to stop.
     */
    void stop();

private:
    using ParserFunc = std::function<void(const CollectorEvent&)>;
    using Handler = std::function<void(const CollectorEvent&)>;
    using HandlerMap = std::unordered_map<std::string, Handler>;

    HandlerMap buildHandlers(IAdapter& adapter, const std::string& node);
    void readerLoop(ICollector& collector, Handler handler);
    void sysMetricsLoop();

    std::string node_{""};

    std::unordered_map<std::string, std::unique_ptr<ICollector>> collectors_;
    std::unique_ptr<IAdapter> adapter_;
    SysMetricsCollector sysMetricsCollector_;
    std::mutex adapterMutex_;
    int pollTimeoutMs_;
    std::atomic<bool> running_{false};
    std::vector<std::thread> readerThreads_;
    std::thread sysMetricsThread_;
    HandlerMap handlers_;
};
