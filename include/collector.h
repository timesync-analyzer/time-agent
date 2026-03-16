#pragma once
#include <systemd/sd-journal.h>

#include <boost/process.hpp>
#include <boost/process/detail/child_decl.hpp>
#include <boost/process/pipe.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "system_metrics.h"

namespace bp = boost::process;

struct JournalEvent {
    uint64_t ts_usec;
    std::string unit;
    std::string msg;
};

struct Ptp4lPortEvent {
    std::string unit;
    uint64_t timestamp_us = 0;
    int portNumber = 0;
    std::string portName;
    std::string fromState;
    std::string toState;
    std::string trigger;
};

class IMessageParser {
public:
    virtual ~IMessageParser() = default;

    // Metrics lines: "[TS]: master offset X sN freq Y path delay Z"
    virtual std::optional<Ptp4lStats> parse_ptp4l_msg(const std::string& label, const std::string& msg) const = 0;

    // Metrics lines: "[TS]: CLOCK_REALTIME phc offset X sN freq Y delay Z"
    virtual std::optional<Phc2SysStats> parse_phc2sys_msg(const std::string& label, const std::string& msg) const = 0;

    // Port state transition: "[TS]: port N (name): FROM to TO on EVENT"
    virtual std::optional<Ptp4lPortEvent> parse_ptp4l_port_event(const std::string& label, const std::string& msg) const = 0;

    virtual std::optional<PPSStats> parse_pps_msg(const std::string& unit, const std::string& msg) const = 0;

    // Returns true for "[TS]: Waiting for ptp4l..."
    virtual bool is_phc2sys_waiting(const std::string& msg) const = 0;
};

class JournalMessageParser : public IMessageParser {
public:
    std::optional<Ptp4lStats> parse_ptp4l_msg(const std::string& label, const std::string& msg) const override;
    std::optional<Phc2SysStats> parse_phc2sys_msg(const std::string& label, const std::string& msg) const override;
    std::optional<Ptp4lPortEvent> parse_ptp4l_port_event(const std::string& label, const std::string& msg) const override;
    std::optional<PPSStats> parse_pps_msg(const std::string& unit, const std::string& msg) const override;
    bool is_phc2sys_waiting(const std::string& msg) const override;
};

class ICollector {
public:
    explicit ICollector(std::unique_ptr<IMessageParser> msgParser);
    virtual ~ICollector() = default;
    virtual std::optional<JournalEvent> readEvent() = 0;
    virtual std::optional<Ptp4lStats> parse_ptp4l_msg(const JournalEvent& event);
    virtual std::optional<Phc2SysStats> parse_phc2sys_msg(const JournalEvent& event);
    virtual std::optional<Ptp4lPortEvent> parse_ptp4l_port_event(const JournalEvent& event);
    virtual std::optional<PPSStats> parse_pps_msg(const JournalEvent& event);
    virtual bool is_phc2sys_waiting(const JournalEvent& event);

private:
    std::unique_ptr<IMessageParser> msgParser_;
};

class JournalCollector : public ICollector {
public:
    explicit JournalCollector(const std::string_view& unit);
    ~JournalCollector();
    std::optional<JournalEvent> readEvent() override;

private:
    sd_journal* journal_ = nullptr;
};

class SubproccessCollector : public ICollector {
public:
    explicit SubproccessCollector(const std::string& cmd, const std::vector<std::string>& args);
    ~SubproccessCollector();
    std::optional<JournalEvent> readEvent() override;

private:
    bp::child child;
    bp::ipstream stream;
    std::string unit;
};