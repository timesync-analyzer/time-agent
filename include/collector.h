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

struct CollectorEvent {
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

class Ptp4lParser {
public:
    std::optional<Ptp4lStats> parseMetrics(const std::string& msg) const;
    std::optional<Ptp4lPortEvent> parsePortEvent(const std::string& msg) const;
};

class Phc2SysParser {
public:
    std::optional<Phc2SysStats> parseMetrics(const std::string& msg) const;
    bool isWaiting(const std::string& msg) const;
};

class PPSParser {
public:
    std::optional<PPSStats> parseMetrics(const std::string& msg) const;
};

class ICollector {
public:
    virtual ~ICollector() = default;
    virtual std::optional<CollectorEvent> readEvent() = 0;
    virtual void stop() {}
};

class JournalCollector : public ICollector {
public:
    explicit JournalCollector(const std::string_view& unit);
    ~JournalCollector();
    std::optional<CollectorEvent> readEvent() override;
    void stop() override;

private:
    sd_journal* journal_ = nullptr;
    int stop_pipe_[2] = {-1, -1};
};

class SubproccessCollector : public ICollector {
public:
    explicit SubproccessCollector(const std::string& cmd, const std::vector<std::string>& args);
    ~SubproccessCollector();
    std::optional<CollectorEvent> readEvent() override;
    void stop() override;

private:
    bp::child child;
    bp::ipstream stream;
    std::string unit;
};
