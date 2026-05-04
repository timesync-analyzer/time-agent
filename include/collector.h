#pragma once
#include <systemd/sd-journal.h>

#include <atomic>
#include <boost/process.hpp>
#include <boost/process/detail/child_decl.hpp>
#include <boost/process/pipe.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "system_metrics.h"

namespace bp = boost::process;

/**
 * @brief Event read from an external metric source.
 *
 * Journal and subprocess collectors normalize source-specific records into this
 * structure before parser handlers convert them to protobuf metrics.
 */
struct CollectorEvent {
    /**
     * @brief Event timestamp in microseconds since Unix epoch.
     */
    uint64_t ts_usec;
    /**
     * @brief Logical source name, for example ptp4l, phc2sys, or ppswatch.
     */
    std::string_view unit;
    /**
     * @brief Raw log line or stdout line read from the source.
     */
    std::string msg;
};

/**
 * @brief Parsed ptp4l port state transition.
 *
 * The agent sends these events separately from periodic ptp4l metrics and also
 * uses the port name to update the network interface used by system metrics.
 */
struct PortEvent {
    std::string unit;
    /**
     * @brief Event timestamp in microseconds since Unix epoch.
     */
    uint64_t timestamp_us = 0;
    int portNumber = 0;
    std::string portName;
    std::string adapterName;
    std::string fromState;
    std::string toState;
    std::string trigger;
};

/**
 * @brief Parser for ptp4l metric and port transition log lines.
 */
class Ptp4lParser {
public:
    /**
     * @brief Parses a ptp4l offset, frequency, and path delay line.
     * @param msg Raw ptp4l log message.
     * @return Parsed statistics when msg matches a supported format.
     */
    std::optional<Ptp4lStats> parseMetrics(const std::string& msg) const;
    /**
     * @brief Parses a ptp4l port state transition line.
     * @param msg Raw ptp4l log message.
     * @return Parsed port event when msg contains a supported transition.
     */
    std::optional<PortEvent> parsePortEvent(const std::string& msg) const;
};

/**
 * @brief Parser for phc2sys metric and status log lines.
 */
class Phc2SysParser {
public:
    /**
     * @brief Parses a phc2sys offset, frequency, and delay line.
     * @param msg Raw phc2sys log message.
     * @return Parsed statistics when msg matches a supported format.
     */
    std::optional<Phc2SysStats> parseMetrics(const std::string& msg) const;
    /**
     * @brief Checks whether msg is a "Waiting for ptp4l" status message.
     * @param msg Raw phc2sys log message.
     * @return true when msg reports waiting for ptp4l synchronization.
     */
    bool isWaiting(const std::string& msg) const;
};

/**
 * @brief Parser for ppswatch stdout metric lines.
 */
class PPSParser {
public:
    /**
     * @brief Parses a ppswatch timestamp, sequence, and offset line.
     * @param msg Raw ppswatch stdout line.
     * @return Parsed statistics when msg matches the ppswatch format.
     */
    std::optional<PPSStats> parseMetrics(const std::string& msg) const;
};

/**
 * @brief Common blocking collector interface.
 *
 * Implementations own their source handles. readEvent() blocks until a record is
 * available or collection stops, returning std::nullopt on EOF, stop, or fatal
 * read failure.
 */
class ICollector {
public:
    virtual ~ICollector() = default;
    /**
     * @brief Blocks until the next source event is available.
     * @return Next event, or std::nullopt on EOF, stop, or fatal read failure.
     */
    virtual std::optional<CollectorEvent> readEvent() = 0;
    /**
     * @brief Requests shutdown for collectors that can be unblocked externally.
     */
    virtual void stop() {}
};

/**
 * @brief Collector that follows local systemd journal records for a service unit.
 */
class JournalCollector : public ICollector {
public:
    /**
     * @brief Opens the local journal and filters records by unit or identifier.
     * @param unit Service unit or syslog identifier to follow.
     */
    explicit JournalCollector(const std::string_view& unit);
    ~JournalCollector();
    std::optional<CollectorEvent> readEvent() override;
    void stop() override;

private:
    std::string unit_;
    sd_journal* journal_ = nullptr;
    std::atomic<bool> stop_requested_{false};
};

/**
 * @brief Collector that starts a subprocess and reads metric events from stdout.
 */
class SubprocessCollector : public ICollector {
public:
    /**
     * @brief Starts the subprocess with args and captures its stdout stream.
     * @param cmd Logical command name used as the collector unit.
     * @param args Arguments passed to the launched process.
     */
    explicit SubprocessCollector(const std::string& cmd, const std::vector<std::string>& args);
    ~SubprocessCollector();
    std::optional<CollectorEvent> readEvent() override;
    void stop() override;

private:
    bp::child child_;
    bp::ipstream stream_;
    std::string unit_;
};
