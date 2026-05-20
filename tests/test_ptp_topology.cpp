#include <gtest/gtest.h>

#include "ptp_topology.h"

namespace {
const char* kDefaultDataSet = R"(sending: GET DEFAULT_DATA_SET
        c46237.fffe.0d2070-0 seq 0 RESPONSE MANAGEMENT DEFAULT_DATA_SET
                twoStepFlag             1
                slaveOnly               0
                numberPorts             1
                clockIdentity           c46237.fffe.0d2070
                domainNumber            0
)";

const char* kParentDataSet = R"(sending: GET PARENT_DATA_SET
        c46237.fffe.0d2070-0 seq 0 RESPONSE MANAGEMENT PARENT_DATA_SET
                parentPortIdentity                    000bab.fffe.df7a52-1
                parentStats                           0
                grandmasterIdentity                   000bab.fffe.df7a52
)";

const char* kCurrentDataSet = R"(sending: GET CURRENT_DATA_SET
        c46237.fffe.0d2070-0 seq 0 RESPONSE MANAGEMENT CURRENT_DATA_SET
                stepsRemoved     1
                offsetFromMaster -14.0
                meanPathDelay    83.0
)";

const char* kPortDataSet = R"(sending: GET PORT_DATA_SET
        c46237.fffe.0d2070-1 seq 0 RESPONSE MANAGEMENT PORT_DATA_SET
                portIdentity            c46237.fffe.0d2070-1
                portState               SLAVE
                logMinDelayReqInterval  0
)";
}  // namespace

TEST(PtpTopologyParserTest, ParsesObservedParentEdge) {
    PtpTopologyParser parser;

    auto snapshot = parser.parse(kDefaultDataSet, kParentDataSet, kCurrentDataSet, kPortDataSet);

    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->localClockIdentity, "c46237.fffe.0d2070");
    EXPECT_EQ(snapshot->parentClockIdentity, "000bab.fffe.df7a52");
    EXPECT_EQ(snapshot->parentPortNumber, 1);
    EXPECT_EQ(snapshot->grandmasterIdentity, "000bab.fffe.df7a52");
    EXPECT_EQ(snapshot->stepsRemoved, 1);
    EXPECT_EQ(snapshot->meanPathDelayNs, 83);
    EXPECT_EQ(snapshot->childPortNumber, 1);
    ASSERT_EQ(snapshot->ports.size(), 1);
    EXPECT_EQ(snapshot->ports[0].portIdentity, "c46237.fffe.0d2070-1");
    EXPECT_EQ(snapshot->ports[0].state, "SLAVE");
}

TEST(PtpTopologyParserTest, ReturnsNulloptWithoutLocalClockIdentity) {
    PtpTopologyParser parser;

    auto snapshot = parser.parse("sending: GET DEFAULT_DATA_SET\n", kParentDataSet, kCurrentDataSet, kPortDataSet);

    EXPECT_FALSE(snapshot.has_value());
}

TEST(PtpTopologyCollectorTest, DisabledByDefault) {
    PtpTopologyCollector collector(PtpTopologyConfig{});

    EXPECT_FALSE(collector.enabled());
}
