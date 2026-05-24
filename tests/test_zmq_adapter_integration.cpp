#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <zmq.hpp>

#include "adapter.h"
#include "metrics.pb.h"

namespace {

MetricsWrapper receiveWrapper(zmq::socket_t& receiver) {
    zmq::message_t message;
    auto result = receiver.recv(message, zmq::recv_flags::none);
    EXPECT_TRUE(result.has_value());

    MetricsWrapper wrapper;
    EXPECT_TRUE(wrapper.ParseFromArray(message.data(), static_cast<int>(message.size())));
    return wrapper;
}

}  // namespace

TEST(ZMQAdapterIntegrationTest, SendsPtp4lMetricsOverPushPullSocket) {
    zmq::context_t context(1);
    zmq::socket_t receiver(context, zmq::socket_type::pull);
    receiver.set(zmq::sockopt::rcvtimeo, 1500);
    receiver.bind("tcp://127.0.0.1:*");
    const std::string endpoint = receiver.get(zmq::sockopt::last_endpoint);

    ZMQConfig config;
    config.endpoint = endpoint;
    config.queue_size = 10;
    config.timeout_after_close_ms = 0;
    ZMQAdapter adapter(config, "node-zmq");

    Ptp4lStats stats;
    stats.timestamp_us = 1'234'567;
    stats.offset = -12;
    stats.freq = 34;
    stats.path_delay = 56;
    stats.state = 2;

    ASSERT_TRUE(adapter.send_ptp_statistics(stats, "node-zmq"));

    auto wrapper = receiveWrapper(receiver);
    ASSERT_TRUE(wrapper.has_ptp4l());
    EXPECT_EQ(wrapper.type(), MESSAGE_TYPE_PTP4L);
    EXPECT_EQ(wrapper.node_name(), "node-zmq");
    EXPECT_EQ(wrapper.timestamp().seconds(), 1);
    EXPECT_EQ(wrapper.timestamp().nanos(), 234'567'000);
    EXPECT_EQ(wrapper.ptp4l().offset_ns(), -12);
    EXPECT_EQ(wrapper.ptp4l().frequency(), 34);
    EXPECT_EQ(wrapper.ptp4l().path_delay(), 56);
    EXPECT_EQ(wrapper.ptp4l().state(), 2);
}

TEST(ZMQAdapterIntegrationTest, SendsSystemMetricsOverPushPullSocket) {
    zmq::context_t context(1);
    zmq::socket_t receiver(context, zmq::socket_type::pull);
    receiver.set(zmq::sockopt::rcvtimeo, 1500);
    receiver.bind("tcp://127.0.0.1:*");
    const std::string endpoint = receiver.get(zmq::sockopt::last_endpoint);

    ZMQConfig config;
    config.endpoint = endpoint;
    config.queue_size = 10;
    config.timeout_after_close_ms = 0;
    ZMQAdapter adapter(config, "node-system");

    SystemStats stats;
    stats.timestamp_us = 9'876'543;
    stats.cpuStats.emplace();
    stats.cpuStats->usage_percent = 12.5;
    stats.memoryStats.emplace();
    stats.memoryStats->mem_available_kb = 2048;
    stats.networkStats.emplace();
    stats.networkStats->rx_packets = 99;
    stats.temperatureStats.emplace();
    stats.temperatureStats->zonesReadings.push_back({"coretemp", "Package id 0", 44000});

    ASSERT_TRUE(adapter.send_sys_statistics(stats, "node-system"));

    auto wrapper = receiveWrapper(receiver);
    ASSERT_TRUE(wrapper.has_system());
    EXPECT_EQ(wrapper.type(), MESSAGE_TYPE_SYSTEM);
    EXPECT_EQ(wrapper.node_name(), "node-system");
    EXPECT_DOUBLE_EQ(wrapper.system().cpu_stats().usage_percent(), 12.5);
    EXPECT_EQ(wrapper.system().memory_stats().mem_available_kb(), 2048);
    EXPECT_EQ(wrapper.system().network_stats().rx_packets(), 99);
    ASSERT_EQ(wrapper.system().temperature_stats().zones_readings_size(), 1);
    EXPECT_EQ(wrapper.system().temperature_stats().zones_readings(0).sensor(), "coretemp");
    EXPECT_EQ(wrapper.system().temperature_stats().zones_readings(0).temperature(), 44000);
}
