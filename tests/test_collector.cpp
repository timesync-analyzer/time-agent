#include <gtest/gtest.h>

#include "collector.h"

class ParserTest : public ::testing::Test {
protected:
    JournalMessageParser parser;
};

// === Тесты ptp4l парсера ===
// Формат MESSAGE journald: [TIMESTAMP] master offset X sN freq Y path delay Z
TEST_F(ParserTest, ParsePtp4lValidMessage) {
    std::string msg = "[192898.322] master offset         21 s2 freq   +3212 path delay         7";
    auto result = parser.parse_ptp4l_msg("ptp4l@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->unit, "ptp4l@slave.service");
    EXPECT_EQ(result->offset, 21);
    EXPECT_EQ(result->state, 2);
    EXPECT_EQ(result->freq, 3212);
    EXPECT_EQ(result->path_delay, 7);
    EXPECT_EQ(result->timestamp_us, 192898322ULL);
}

TEST_F(ParserTest, ParsePtp4lNegativeValues) {
    std::string msg = "[100500.123] master offset        -42 s3 freq  -15000 path delay        15";
    auto result = parser.parse_ptp4l_msg("ptp4l@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->offset, -42);
    EXPECT_EQ(result->state, 3);
    EXPECT_EQ(result->freq, -15000);
}

TEST_F(ParserTest, ParsePtp4lMinimalFields) {
    std::string msg = "[1000.5] master offset 10 s1 freq +100";
    auto result = parser.parse_ptp4l_msg("ptp4l@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->offset, 10);
    EXPECT_EQ(result->state, 1);
    EXPECT_EQ(result->freq, 100);
}

TEST_F(ParserTest, ParsePtp4lInvalidMessage) {
    std::string msg = "invalid message format";
    auto result = parser.parse_ptp4l_msg("ptp4l@slave.service", msg);

    EXPECT_FALSE(result.has_value());
}

TEST_F(ParserTest, ParsePtp4lEmptyMessage) {
    auto result = parser.parse_ptp4l_msg("ptp4l@slave.service", "");
    EXPECT_FALSE(result.has_value());
}

// === Тесты phc2sys парсера ===
// Формат MESSAGE journald: [TIMESTAMP] CLOCK_REALTIME phc offset X sN freq Y delay Z
TEST_F(ParserTest, ParsePhc2sysValidMessage) {
    std::string msg = "[192898.077] CLOCK_REALTIME phc offset       -42 s2 freq   +1257 delay   1450";
    auto result = parser.parse_phc2sys_msg("phc2sys@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->unit, "phc2sys@slave.service");
    EXPECT_EQ(result->offset, -42);
    EXPECT_EQ(result->state, 2);
    EXPECT_EQ(result->freq, 1257);
    EXPECT_EQ(result->path_delay, 1450);
    EXPECT_EQ(result->timestamp_us, 192898077ULL);
}

TEST_F(ParserTest, ParsePhc2sysNegativeFreq) {
    std::string msg = "[5000.1] CLOCK_REALTIME phc offset 100 s1 freq -9999 delay 500";
    auto result = parser.parse_phc2sys_msg("phc2sys@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->freq, -9999);
}

TEST_F(ParserTest, ParsePhc2sysInvalidMessage) {
    std::string msg = "not a phc2sys message";
    auto result = parser.parse_phc2sys_msg("phc2sys@slave.service", msg);

    EXPECT_FALSE(result.has_value());
}

TEST_F(ParserTest, ParsePhc2sysEmptyMessage) {
    auto result = parser.parse_phc2sys_msg("phc2sys@slave.service", "");
    EXPECT_FALSE(result.has_value());
}

// === Тесты парсера port state событий ===
// Формат MESSAGE journald: [TIMESTAMP] port N (name): FROM to TO on EVENT
TEST_F(ParserTest, ParsePortEventValidEthInterface) {
    std::string msg = "[1878447.505] port 1 (eth1): UNCALIBRATED to SLAVE on MASTER_CLOCK_SELECTED";
    auto result = parser.parse_ptp4l_port_event("ptp4l@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->unit, "ptp4l@slave.service");
    EXPECT_EQ(result->portNumber, 1);
    EXPECT_EQ(result->portName, "eth1");
    EXPECT_EQ(result->fromState, "UNCALIBRATED");
    EXPECT_EQ(result->toState, "SLAVE");
    EXPECT_EQ(result->trigger, "MASTER_CLOCK_SELECTED");
    EXPECT_EQ(result->timestamp_us, 1878447505ULL);
}

TEST_F(ParserTest, ParsePortEventSocketInterface) {
    std::string msg = "[1878437.557] port 0 (/var/run/ptp/ptp4l): INITIALIZING to LISTENING on INIT_COMPLETE";
    auto result = parser.parse_ptp4l_port_event("ptp4l@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->portNumber, 0);
    EXPECT_EQ(result->portName, "/var/run/ptp/ptp4l");
    EXPECT_EQ(result->fromState, "INITIALIZING");
    EXPECT_EQ(result->toState, "LISTENING");
    EXPECT_EQ(result->trigger, "INIT_COMPLETE");
}

TEST_F(ParserTest, ParsePortEventMetricsLineNotMatched) {
    // Метрики не должны матчиться как port event
    std::string msg = "[192898.322] master offset 21 s2 freq +3212 path delay 7";
    auto result = parser.parse_ptp4l_port_event("ptp4l@slave.service", msg);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ParserTest, ParsePortEventForeignMasterNotMatched) {
    // "new foreign master" не соответствует формату "FROM to TO on EVENT"
    std::string msg = "[1878439.505] port 1 (eth1): new foreign master e0d4e8.fffe.f3965e-1";
    auto result = parser.parse_ptp4l_port_event("ptp4l@slave.service", msg);
    EXPECT_FALSE(result.has_value());
}

// === Тесты is_phc2sys_waiting ===
TEST_F(ParserTest, IsPhc2sysWaitingTrue) {
    std::string msg = "[1878438.561] Waiting for ptp4l...";
    EXPECT_TRUE(parser.is_phc2sys_waiting(msg));
}

TEST_F(ParserTest, IsPhc2sysWaitingFalseForMetrics) {
    std::string msg = "[192898.077] CLOCK_REALTIME phc offset -42 s2 freq +1257 delay 1450";
    EXPECT_FALSE(parser.is_phc2sys_waiting(msg));
}

TEST_F(ParserTest, IsPhc2sysWaitingFalseForEmpty) { EXPECT_FALSE(parser.is_phc2sys_waiting("")); }
