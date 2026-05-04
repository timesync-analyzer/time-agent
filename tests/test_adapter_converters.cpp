#include <gtest/gtest.h>

#include "adapter.h"

TEST(AdapterConvertersTest, ConvertsPtp4lStatsToMetricsWrapper) {
    Ptp4lStats stats;
    stats.timestamp_us = 1'700'000'123'456'789;
    stats.offset = -42;
    stats.freq = 1257;
    stats.path_delay = 88;
    stats.state = 3;

    auto wrapper = converters::to_ptp4l_metrics(stats, "node-a");

    EXPECT_EQ(wrapper.type(), MESSAGE_TYPE_PTP4L);
    EXPECT_EQ(wrapper.node_name(), "node-a");
    EXPECT_EQ(wrapper.timestamp().seconds(), 1'700'000'123);
    EXPECT_EQ(wrapper.timestamp().nanos(), 456'789'000);
    ASSERT_TRUE(wrapper.has_ptp4l());
    EXPECT_EQ(wrapper.ptp4l().offset_ns(), -42);
    EXPECT_EQ(wrapper.ptp4l().frequency(), 1257);
    EXPECT_EQ(wrapper.ptp4l().path_delay(), 88);
    EXPECT_EQ(wrapper.ptp4l().state(), 3);
}

TEST(AdapterConvertersTest, ConvertsPhc2SysStatsToMetricsWrapper) {
    Phc2SysStats stats;
    stats.timestamp_us = 987'654'321;
    stats.offset = 22;
    stats.freq = -13;
    stats.path_delay = 1440;
    stats.state = 2;

    auto wrapper = converters::to_phc2sys_metrics(stats, "node-b");

    EXPECT_EQ(wrapper.type(), MESSAGE_TYPE_PHC2SYS);
    EXPECT_EQ(wrapper.node_name(), "node-b");
    EXPECT_EQ(wrapper.timestamp().seconds(), 987);
    EXPECT_EQ(wrapper.timestamp().nanos(), 654'321'000);
    ASSERT_TRUE(wrapper.has_phc2sys());
    EXPECT_EQ(wrapper.phc2sys().offset_ns(), 22);
    EXPECT_EQ(wrapper.phc2sys().frequency(), -13);
    EXPECT_EQ(wrapper.phc2sys().path_delay(), 1440);
    EXPECT_EQ(wrapper.phc2sys().state(), 2);
}

TEST(AdapterConvertersTest, ConvertsPpsStatsToMetricsWrapper) {
    PPSStats stats;
    stats.timestamp_us = 42'000'001;
    stats.offset = -777;

    auto wrapper = converters::to_pps_metrics(stats, "node-c");

    EXPECT_EQ(wrapper.type(), MESSAGE_TYPE_PPS);
    EXPECT_EQ(wrapper.node_name(), "node-c");
    EXPECT_EQ(wrapper.timestamp().seconds(), 42);
    EXPECT_EQ(wrapper.timestamp().nanos(), 1'000);
    ASSERT_TRUE(wrapper.has_pps());
    EXPECT_EQ(wrapper.pps().offset_ns(), -777);
}

TEST(AdapterConvertersTest, ConvertsPortEventToMetricsWrapper) {
    PortEvent event;
    event.timestamp_us = 123'456'000;
    event.portNumber = 1;
    event.portName = "eth1";
    event.adapterName = "Intel I210";
    event.fromState = "UNCALIBRATED";
    event.toState = "SLAVE";
    event.trigger = "MASTER_CLOCK_SELECTED";

    auto wrapper = converters::to_port_event(event, "node-d");

    EXPECT_EQ(wrapper.type(), MESSAGE_TYPE_PTP4L_PORT_EVENT);
    EXPECT_EQ(wrapper.node_name(), "node-d");
    EXPECT_EQ(wrapper.timestamp().seconds(), 123);
    EXPECT_EQ(wrapper.timestamp().nanos(), 456'000'000);
    ASSERT_TRUE(wrapper.has_ptp4l_port_event());
    EXPECT_EQ(wrapper.ptp4l_port_event().port(), 1);
    EXPECT_EQ(wrapper.ptp4l_port_event().interface(), "eth1");
    EXPECT_EQ(wrapper.ptp4l_port_event().adapter_name(), "Intel I210");
    EXPECT_EQ(wrapper.ptp4l_port_event().from_state(), "UNCALIBRATED");
    EXPECT_EQ(wrapper.ptp4l_port_event().to_state(), "SLAVE");
    EXPECT_EQ(wrapper.ptp4l_port_event().event_trigger(), "MASTER_CLOCK_SELECTED");
}

TEST(AdapterConvertersTest, ConvertsSystemStatsToMetricsWrapper) {
    SystemStats stats;
    stats.timestamp_us = 2'000'003;
    stats.cpuStats.usage_percent = 73.5;
    stats.cpuStats.context_switches = 10;
    stats.cpuStats.interrupts = 20;
    stats.cpuStats.softirqs = 30;
    stats.memoryStats.mem_available_kb = 1000;
    stats.memoryStats.mem_free_kb = 400;
    stats.memoryStats.swap_total_kb = 200;
    stats.memoryStats.swap_free_kb = 50;
    stats.memoryStats.buffers_kb = 64;
    stats.networkStats.rx_packets = 1;
    stats.networkStats.tx_packets = 2;
    stats.networkStats.rx_dropped = 3;
    stats.networkStats.tx_dropped = 4;
    stats.networkStats.rx_errors = 5;
    stats.networkStats.tx_errors = 6;
    stats.networkStats.collisions = 7;
    stats.temperatureStats.zonesReadings.push_back({"coretemp", "Package id 0", 44500});
    stats.temperatureStats.zonesReadings.push_back({"nvme", "Composite", 39000});

    auto wrapper = converters::to_system_metrics(stats, "node-e");

    EXPECT_EQ(wrapper.type(), MESSAGE_TYPE_SYSTEM);
    EXPECT_EQ(wrapper.node_name(), "node-e");
    EXPECT_EQ(wrapper.timestamp().seconds(), 2);
    EXPECT_EQ(wrapper.timestamp().nanos(), 3'000);
    ASSERT_TRUE(wrapper.has_system());
    EXPECT_DOUBLE_EQ(wrapper.system().cpu_stats().usage_percent(), 73.5);
    EXPECT_EQ(wrapper.system().cpu_stats().context_switches(), 10);
    EXPECT_EQ(wrapper.system().memory_stats().mem_available_kb(), 1000);
    EXPECT_EQ(wrapper.system().network_stats().rx_packets(), 1);
    ASSERT_EQ(wrapper.system().temperature_stats().zones_readings_size(), 2);
    EXPECT_EQ(wrapper.system().temperature_stats().zones_readings(0).sensor(), "coretemp");
    EXPECT_EQ(wrapper.system().temperature_stats().zones_readings(0).label(), "Package id 0");
    EXPECT_EQ(wrapper.system().temperature_stats().zones_readings(0).temperature(), 44500);
    EXPECT_EQ(wrapper.system().temperature_stats().zones_readings(1).sensor(), "nvme");
}
