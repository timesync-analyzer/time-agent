#include <gtest/gtest.h>

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "agent.h"

namespace {

class FakeCollector : public ICollector {
public:
    FakeCollector(std::string unit, std::vector<std::string> messages) : unit_(std::move(unit)), messages_(std::move(messages)) {}

    std::optional<CollectorEvent> readEvent() override {
        if (stopped_ || next_ >= messages_.size()) {
            return std::nullopt;
        }

        CollectorEvent event;
        event.ts_usec = 1'234'567 + next_;
        event.unit = unit_;
        event.msg = messages_[next_];
        ++next_;
        return event;
    }

    void stop() override { stopped_ = true; }

private:
    std::string unit_;
    std::vector<std::string> messages_;
    size_t next_ = 0;
    bool stopped_ = false;
};

class RecordingAdapter : public IAdapter {
public:
    bool send_ptp_statistics(const Ptp4lStats& stats, const std::string& node) override {
        std::lock_guard lock(mutex_);
        ptpStats.push_back(stats);
        nodes.push_back(node);
        return true;
    }

    bool send_phc2sys_statistics(const Phc2SysStats& stats, const std::string& node) override {
        std::lock_guard lock(mutex_);
        phcStats.push_back(stats);
        nodes.push_back(node);
        return true;
    }

    bool send_pps_statistics(const PPSStats& stats, const std::string& node) override {
        std::lock_guard lock(mutex_);
        ppsStats.push_back(stats);
        nodes.push_back(node);
        return true;
    }

    bool send_sys_statistics(const SystemStats& stats, const std::string& node) override {
        std::lock_guard lock(mutex_);
        sysStats.push_back(stats);
        nodes.push_back(node);
        return true;
    }

    bool send_ptp4l_port_event(const PortEvent& event, const std::string& node) override {
        std::lock_guard lock(mutex_);
        portEvents.push_back(event);
        nodes.push_back(node);
        return true;
    }

    std::vector<Ptp4lStats> ptpStats;
    std::vector<Phc2SysStats> phcStats;
    std::vector<PPSStats> ppsStats;
    std::vector<SystemStats> sysStats;
    std::vector<PortEvent> portEvents;
    std::vector<std::string> nodes;

private:
    std::mutex mutex_;
};

AgentConfig makeAgentConfig() {
    AgentConfig config;
    config.globalConfig.node = "test-node";
    config.monitorConfig.poll_timeout_ms = 1;
    return config;
}

std::unordered_map<std::string, std::unique_ptr<ICollector>> makeCollectors(std::string name, std::string unit,
                                                                            std::vector<std::string> messages) {
    std::unordered_map<std::string, std::unique_ptr<ICollector>> collectors;
    collectors.emplace(std::move(name), std::make_unique<FakeCollector>(std::move(unit), std::move(messages)));
    return collectors;
}

}  // namespace

TEST(AgentTest, RoutesPtp4lMetricsToAdapter) {
    auto adapter = std::make_unique<RecordingAdapter>();
    auto* adapterPtr = adapter.get();
    Agent agent(makeCollectors("ptp4l", "ptp4l", {"[192898.322] master offset 21 s2 freq +3212 path delay 7"}),
                std::move(adapter), makeAgentConfig());

    agent.run();

    ASSERT_EQ(adapterPtr->ptpStats.size(), 1);
    EXPECT_EQ(adapterPtr->ptpStats[0].unit, "ptp4l");
    EXPECT_EQ(adapterPtr->ptpStats[0].timestamp_us, 1'234'567);
    EXPECT_EQ(adapterPtr->ptpStats[0].offset, 21);
    EXPECT_EQ(adapterPtr->ptpStats[0].state, 2);
    EXPECT_EQ(adapterPtr->ptpStats[0].freq, 3212);
    EXPECT_EQ(adapterPtr->ptpStats[0].path_delay, 7);
    ASSERT_FALSE(adapterPtr->nodes.empty());
    EXPECT_EQ(adapterPtr->nodes[0], "test-node");
}

TEST(AgentTest, RoutesPhc2SysMetricsToAdapter) {
    auto adapter = std::make_unique<RecordingAdapter>();
    auto* adapterPtr = adapter.get();
    Agent agent(makeCollectors("phc2sys", "phc2sys", {"[192898.077] CLOCK_REALTIME phc offset -42 s2 freq +1257 delay 1450"}),
                std::move(adapter), makeAgentConfig());

    agent.run();

    ASSERT_EQ(adapterPtr->phcStats.size(), 1);
    EXPECT_EQ(adapterPtr->phcStats[0].unit, "phc2sys");
    EXPECT_EQ(adapterPtr->phcStats[0].timestamp_us, 1'234'567);
    EXPECT_EQ(adapterPtr->phcStats[0].offset, -42);
    EXPECT_EQ(adapterPtr->phcStats[0].state, 2);
    EXPECT_EQ(adapterPtr->phcStats[0].freq, 1257);
    EXPECT_EQ(adapterPtr->phcStats[0].path_delay, 1450);
}

TEST(AgentTest, IgnoresPhc2SysWaitingMessage) {
    auto adapter = std::make_unique<RecordingAdapter>();
    auto* adapterPtr = adapter.get();
    Agent agent(makeCollectors("phc2sys", "phc2sys", {"[1878438.561] Waiting for ptp4l..."}), std::move(adapter),
                makeAgentConfig());

    agent.run();

    EXPECT_TRUE(adapterPtr->phcStats.empty());
    EXPECT_TRUE(adapterPtr->ptpStats.empty());
    EXPECT_TRUE(adapterPtr->ppsStats.empty());
    EXPECT_TRUE(adapterPtr->portEvents.empty());
}

TEST(AgentTest, RoutesPpswatchMetricsToAdapter) {
    auto adapter = std::make_unique<RecordingAdapter>();
    auto* adapterPtr = adapter.get();
    Agent agent(makeCollectors("ppswatch", "ppswatch", {"timestamp: 1712600030, sequence: 42, offset: -314"}),
                std::move(adapter), makeAgentConfig());

    agent.run();

    ASSERT_EQ(adapterPtr->ppsStats.size(), 1);
    EXPECT_EQ(adapterPtr->ppsStats[0].unit, "ppswatch");
    EXPECT_EQ(adapterPtr->ppsStats[0].timestamp_us, 1'234'567);
    EXPECT_EQ(adapterPtr->ppsStats[0].offset, -314);
}

TEST(AgentTest, RoutesPtp4lPortEventToAdapter) {
    auto adapter = std::make_unique<RecordingAdapter>();
    auto* adapterPtr = adapter.get();
    Agent agent(makeCollectors("ptp4l", "ptp4l",
                               {"[1878437.557] port 0 (/var/run/ptp/ptp4l): INITIALIZING to LISTENING on INIT_COMPLETE"}),
                std::move(adapter), makeAgentConfig());

    agent.run();

    ASSERT_EQ(adapterPtr->portEvents.size(), 1);
    EXPECT_EQ(adapterPtr->portEvents[0].unit, "ptp4l");
    EXPECT_EQ(adapterPtr->portEvents[0].timestamp_us, 1'234'567);
    EXPECT_EQ(adapterPtr->portEvents[0].portNumber, 0);
    EXPECT_EQ(adapterPtr->portEvents[0].portName, "/var/run/ptp/ptp4l");
    EXPECT_EQ(adapterPtr->portEvents[0].fromState, "INITIALIZING");
    EXPECT_EQ(adapterPtr->portEvents[0].toState, "LISTENING");
    EXPECT_EQ(adapterPtr->portEvents[0].trigger, "INIT_COMPLETE");
}

TEST(AgentTest, SkipsCollectorWithoutHandler) {
    auto adapter = std::make_unique<RecordingAdapter>();
    auto* adapterPtr = adapter.get();
    Agent agent(makeCollectors("unknown", "unknown", {"message"}), std::move(adapter), makeAgentConfig());

    agent.run();

    EXPECT_TRUE(adapterPtr->ptpStats.empty());
    EXPECT_TRUE(adapterPtr->phcStats.empty());
    EXPECT_TRUE(adapterPtr->ppsStats.empty());
    EXPECT_TRUE(adapterPtr->portEvents.empty());
}
