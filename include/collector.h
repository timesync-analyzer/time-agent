#pragma once
#include <systemd/sd-journal.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "system_metrics.h"

struct JournalEvent {
    uint64_t ts_usec;
    std::string unit;
    std::string msg;
};

class IMessageParser {
public:
    virtual ~IMessageParser() = default;
    virtual std::optional<Ptp4lStats> parse_ptp4l_msg(const std::string& label, const std::string& event) const = 0;
    virtual std::optional<Phc2SysStats> parse_phc2sys_msg(const std::string& label, const std::string& event) const = 0;
};

class JournalMessageParser : public IMessageParser {
public:
    std::optional<Ptp4lStats> parse_ptp4l_msg(const std::string& label, const std::string& msg) const;
    std::optional<Phc2SysStats> parse_phc2sys_msg(const std::string& label, const std::string& msg) const;
};

class ICollector {
public:
    explicit ICollector(std::unique_ptr<IMessageParser> msgParser);
    virtual ~ICollector() = default;
    virtual std::optional<JournalEvent> readEvent() = 0;
    virtual bool waitForData(int timeout_ms) = 0;
    virtual std::optional<Ptp4lStats> parse_ptp4l_msg(const JournalEvent& event);
    virtual std::optional<Phc2SysStats> parse_phc2sys_msg(const JournalEvent& event);

private:
    std::unique_ptr<IMessageParser> msgParser;
};

class JournalCollector : public ICollector {
public:
    JournalCollector(const std::vector<std::string_view>& units);
    ~JournalCollector();
    std::optional<JournalEvent> readEvent() override;
    bool waitForData(int timeout_ms) override;

private:
    sd_journal* journal = nullptr;
};