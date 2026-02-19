#include "collector.h"

#include <spdlog/spdlog.h>
#include <systemd/sd-journal.h>

#include <stdexcept>

// не забыть включить ntpd service

ICollector::ICollector(std::unique_ptr<IMessageParser> msgParser) : msgParser_(std::move(msgParser)) {}

std::optional<Ptp4lStats> ICollector::parse_ptp4l_msg(const JournalEvent& event) {
    auto result = msgParser_->parse_ptp4l_msg(event.unit, event.msg);
    if (result) {
        result->timestamp_us = event.ts_usec;
    }
    return result;
}
std::optional<Phc2SysStats> ICollector::parse_phc2sys_msg(const JournalEvent& event) {
    auto result = msgParser_->parse_phc2sys_msg(event.unit, event.msg);
    if (result) {
        result->timestamp_us = event.ts_usec;
    }
    return result;
}

std::optional<Ptp4lPortEvent> ICollector::parse_ptp4l_port_event(const JournalEvent& event) {
    auto result = msgParser_->parse_ptp4l_port_event(event.unit, event.msg);
    if (result) {
        result->timestamp_us = event.ts_usec;
    }
    return result;
}

bool ICollector::is_phc2sys_waiting(const JournalEvent& event) { return msgParser_->is_phc2sys_waiting(event.msg); }

JournalCollector::JournalCollector(const std::vector<std::string_view>& units)
    : ICollector(std::make_unique<JournalMessageParser>()) {
    int ret = sd_journal_open(&journal_, SD_JOURNAL_LOCAL_ONLY);
    if (ret < 0) {
        throw std::runtime_error("sd_journal_open failed: " + std::to_string(-ret));
    }

    sd_journal_flush_matches(journal_);

    for (const auto& unit : units) {
        std::string matchSystemd = "_COMM=" + std::string(unit);
        sd_journal_add_match(journal_, matchSystemd.c_str(), 0);
        sd_journal_add_disjunction(journal_);
    }
    sd_journal_add_disjunction(journal_);
    sd_journal_add_match(journal_, "_TRANSPORT=syslog", 0);

    sd_journal_seek_tail(journal_);
    sd_journal_previous(journal_);
}

JournalCollector::~JournalCollector() {
    if (journal_) {
        sd_journal_close(journal_);
    }
}

std::optional<JournalEvent> JournalCollector::readEvent() {
    if (sd_journal_next(journal_) <= 0) {
        return std::nullopt;
    }
    JournalEvent event;
    sd_journal_get_realtime_usec(journal_, &event.ts_usec);

    const void* data;
    size_t len;

    if (sd_journal_get_data(journal_, "_COMM", &data, &len) == 0) {
        const char* raw = static_cast<const char*>(data);
        event.unit = std::string(raw + sizeof("_COMM=") - 1, len - (sizeof("_COMM=") - 1));
    }

    if (sd_journal_get_data(journal_, "MESSAGE", &data, &len) == 0) {
        const char* raw = static_cast<const char*>(data);
        event.msg = std::string(raw + sizeof("MESSAGE=") - 1, len - (sizeof("MESSAGE=") - 1));
    }

    return event;
}

bool JournalCollector::waitForData(int timeoutMs) {
    int result = sd_journal_wait(journal_, static_cast<uint64_t>(timeoutMs) * 1000);
    return (result == SD_JOURNAL_APPEND || result == SD_JOURNAL_INVALIDATE);
}

// пример: ptp4l[192898.322]: master offset         21 s2 freq   +3212 path delay         7
std::optional<Ptp4lStats> JournalMessageParser::parse_ptp4l_msg(const std::string& unit, const std::string& msg) const {
    Ptp4lStats res;
    res.unit = unit;
    double timestamp;
    if (sscanf(msg.c_str(), "[%lf] master offset %ld s%d freq %ld path delay %ld", &timestamp, &res.offset, &res.state, &res.freq,
               &res.path_delay) >= 3) {
        return res;
    }
    return std::nullopt;
}

// пример: phc2sys[192898.077]: CLOCK_REALTIME phc offset       -42 s2 freq   +1257 delay   1450
std::optional<Phc2SysStats> JournalMessageParser::parse_phc2sys_msg(const std::string& unit, const std::string& msg) const {
    Phc2SysStats res;
    res.unit = unit;
    double timestamp;
    if (sscanf(msg.c_str(), "[%lf] %*s %*s offset %ld s%d freq %ld delay %ld", &timestamp, &res.offset, &res.state, &res.freq,
               &res.path_delay) >= 3) {
        return res;
    }
    return std::nullopt;
}

// пример: ptp4l[1878437.557]: port 1 (eth1): INITIALIZING to LISTENING on INIT_COMPLETE
// пример: ptp4l[1878437.557]: port 0 (/var/run/ptp/ptp4l): INITIALIZING to LISTENING on INIT_COMPLETE
std::optional<Ptp4lPortEvent> JournalMessageParser::parse_ptp4l_port_event(const std::string& unit,
                                                                           const std::string& msg) const {
    Ptp4lPortEvent res;
    res.unit = unit;

    double timestamp;
    char portName[128];
    char fromState[64];
    char toState[64];
    char trigger[128];

    if (sscanf(msg.c_str(), "[%lf] port %d (%127[^)]): %63s to %63s on %127[^\n]", &timestamp, &res.portNumber, portName,
               fromState, toState, trigger) == 6) {
        res.portName = portName;
        res.fromState = fromState;
        res.toState = toState;
        res.trigger = trigger;
        return res;
    }

    return std::nullopt;
}

// пример: phc2sys[1878438.561]: Waiting for ptp4l...
bool JournalMessageParser::is_phc2sys_waiting(const std::string& msg) const {
    return msg.find("Waiting for ptp4l") != std::string::npos;
}
