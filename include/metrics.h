#pragma once

#include "collector.h"
#include <cstdint>
#include <string>
#include <optional>

struct PTPMetrics {
    std::string unit;
    uint64_t timestamp = 0;
    long long offset = 0;
    long long freq = 0;
    long long path_delay = 0;
    int state = 0;
};

class IMessageParser {
public:
    virtual ~IMessageParser() = default;
    virtual std::optional<PTPMetrics> parse(const JournalEvent& event) const = 0;
};

class Ptp4lMessageParser : public IMessageParser {
public:
    std::optional<PTPMetrics> parse(const JournalEvent& event) const override;
};

class Phc2SysMessageParser : public IMessageParser {
public:
    std::optional<PTPMetrics> parse(const JournalEvent& event) const override;
};

class SysMetricsCollector {
public:
    SysMetricsCollector(const std::string& eth);
    void collect(PTPMetrics& metrics);
public:
    std::string interface;
};