#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "config.h"

/**
 * @brief State of one local ptp4l port reported by pmc PORT_DATA_SET.
 */
struct PtpPortSnapshot {
    int portNumber = 0;
    std::string portIdentity;
    std::string state;
};

/**
 * @brief PTP topology snapshot from local ptp4l management data.
 *
 * parentClockIdentity -> localClockIdentity is the observed graph edge when
 * the local clock is synchronized through a SLAVE port.
 */
struct PtpTopologySnapshotData {
    uint64_t timestamp_us = 0;
    std::string localClockIdentity;
    std::string parentClockIdentity;
    int parentPortNumber = 0;
    std::string grandmasterIdentity;
    int stepsRemoved = 0;
    int64_t meanPathDelayNs = 0;
    int childPortNumber = 0;
    std::vector<PtpPortSnapshot> ports;
};

/**
 * @brief Parser for linuxptp pmc management command output.
 */
class PtpTopologyParser {
public:
    /**
     * @brief Builds a topology snapshot from pmc command outputs.
     */
    std::optional<PtpTopologySnapshotData> parse(const std::string& defaultDataSet, const std::string& parentDataSet,
                                                 const std::string& currentDataSet, const std::string& portDataSet) const;
};

/**
 * @brief Collects topology snapshots by invoking linuxptp pmc.
 */
class PtpTopologyCollector {
public:
    explicit PtpTopologyCollector(PtpTopologyConfig config);

    bool enabled() const;
    std::optional<PtpTopologySnapshotData> collect() const;

private:
    std::vector<std::string> buildArgs(const std::string& command) const;
    std::optional<std::string> runCommand(const std::string& command) const;

    PtpTopologyConfig config_;
    PtpTopologyParser parser_;
};
