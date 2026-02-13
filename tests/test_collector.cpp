#include <gtest/gtest.h>

#include "collector.h"

class ParserTest : public ::testing::Test {
protected:
    JournalMessageParser parser;
};

// === Тесты ptp4l парсера ===
TEST_F(ParserTest, ParsePtp4lValidMessage) {
    std::string msg = "ptp4l[192898.322]: master offset         21 s2 freq   +3212 path delay         7";
    auto result = parser.parse_ptp4l_msg("ptp4l@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->unit, "ptp4l@slave.service");
    EXPECT_EQ(result->offset, 21);
    EXPECT_EQ(result->state, 2);
    EXPECT_EQ(result->freq, 3212);
    EXPECT_EQ(result->path_delay, 7);
    EXPECT_DOUBLE_EQ(result->timestamp_ms, 192898322.0);
}

TEST_F(ParserTest, ParsePtp4lNegativeValues) {
    std::string msg = "ptp4l[100500.123]: master offset        -42 s3 freq  -15000 path delay        15";
    auto result = parser.parse_ptp4l_msg("ptp4l@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->offset, -42);
    EXPECT_EQ(result->state, 3);
    EXPECT_EQ(result->freq, -15000);
}

TEST_F(ParserTest, ParsePtp4lMinimalFields) {
    std::string msg = "ptp4l[1000.5]: master offset 10 s1 freq +100";
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
TEST_F(ParserTest, ParsePhc2sysValidMessage) {
    std::string msg = "phc2sys[192898.077]: CLOCK_REALTIME phc offset       -42 s2 freq   +1257 delay   1450";
    auto result = parser.parse_phc2sys_msg("phc2sys@slave.service", msg);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->unit, "phc2sys@slave.service");
    EXPECT_EQ(result->offset, -42);
    EXPECT_EQ(result->state, 2);
    EXPECT_EQ(result->freq, 1257);
    EXPECT_EQ(result->path_delay, 1450);
    EXPECT_DOUBLE_EQ(result->timestamp_ms, 192898077.0);
}

TEST_F(ParserTest, ParsePhc2sysNegativeFreq) {
    std::string msg = "phc2sys[5000.1]: CLOCK_REALTIME phc offset 100 s1 freq -9999 delay 500";
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
