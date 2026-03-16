#include "collector.h"

#include <spdlog/spdlog.h>
#include <systemd/sd-journal.h>

#include <optional>
#include <stdexcept>

#include "config.h"
#include "system_metrics.h"

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

std::optional<PPSStats> ICollector::parse_pps_msg(const JournalEvent& event) {
    auto result = msgParser_->parse_pps_msg(event.unit, event.msg);
    if (result) {
        result->timestamp_us = event.ts_usec;
    }
    return result;
}

bool ICollector::is_phc2sys_waiting(const JournalEvent& event) { return msgParser_->is_phc2sys_waiting(event.msg); }

JournalCollector::JournalCollector(const std::string_view& unit) : ICollector(std::make_unique<JournalMessageParser>()) {
    spdlog::debug("use unit {}", unit);

    int ret = sd_journal_open(&journal_, SD_JOURNAL_LOCAL_ONLY);
    if (ret < 0) {
        throw std::runtime_error("sd_journal_open failed: " + std::to_string(-ret));
    }

    sd_journal_flush_matches(journal_);

    std::string matchComm = "_COMM=" + std::string(unit);
    sd_journal_add_match(journal_, matchComm.c_str(), 0);

    sd_journal_add_disjunction(journal_);

    std::string matchIdent = "SYSLOG_IDENTIFIER=" + std::string(unit);
    sd_journal_add_match(journal_, matchIdent.c_str(), 0);
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
    while (true) {
        int ret = sd_journal_next(journal_);
        if (ret > 0) {
            break;
        }
        if (ret < 0) {
            spdlog::error("sd_journal_next failed: {}", ret);
            return std::nullopt;
        }
        ret = sd_journal_wait(journal_, 1'000'000);
        if (ret < 0) {
            spdlog::error("sd_journal_wait failed: {}", ret);
            return std::nullopt;
        }
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

std::optional<Ptp4lStats> JournalMessageParser::parse_ptp4l_msg(const std::string& unit, const std::string& msg) const {
    Ptp4lStats res;
    res.unit = unit;
    double timestamp;
    if (sscanf(msg.c_str(), "ptp4l[%lf]: master offset %ld s%d freq %ld path delay %ld", &timestamp, &res.offset, &res.state,
               &res.freq, &res.path_delay) >= 3) {
        return res;
    }
    if (sscanf(msg.c_str(), "[%lf] master offset %ld s%d freq %ld path delay %ld", &timestamp, &res.offset, &res.state, &res.freq,
               &res.path_delay) >= 3) {
        return res;
    }
    return std::nullopt;
}

std::optional<Phc2SysStats> JournalMessageParser::parse_phc2sys_msg(const std::string& unit, const std::string& msg) const {
    Phc2SysStats res;
    res.unit = unit;
    double timestamp;
    if (sscanf(msg.c_str(), "phc2sys[%lf]: %*s %*s offset %ld s%d freq %ld delay %ld", &timestamp, &res.offset, &res.state,
               &res.freq, &res.path_delay) >= 3) {
        return res;
    }
    if (sscanf(msg.c_str(), "[%lf] %*s %*s offset %ld s%d freq %ld delay %ld", &timestamp, &res.offset, &res.state, &res.freq,
               &res.path_delay) >= 3) {
        return res;
    }
    return std::nullopt;
}

std::optional<PPSStats> JournalMessageParser::parse_pps_msg(const std::string& unit, const std::string& msg) const {
    PPSStats res;
    res.unit = unit;
    int64_t timestamp_val, sequence_val;
    if (sscanf(msg.c_str(), "timestamp: %ld, sequence: %ld, offset: %ld", &timestamp_val, &sequence_val, &res.offset) == 3) {
        return res;
    }
    spdlog::debug("return nullopt {}", msg);
    return std::nullopt;
}

// пример: ptp4l[1878437.557]: port 1 (eth1): INITIALIZING to LISTENING on INIT_COMPLETE
// пример: ptp4l[1878437.557]: port 0 (/var/run/ptp/ptp4l): INITIALIZING to LISTENING on INIT_COMPLETE
std::optional<Ptp4lPortEvent> JournalMessageParser::parse_ptp4l_port_event(const std::string& unit,
                                                                           const std::string& msg) const {
    Ptp4lPortEvent res;
    res.unit = unit;
    double timestamp;
    char portName[128], fromState[64], toState[64], trigger[128];

    if (sscanf(msg.c_str(), "ptp4l[%lf]: port %d (%127[^)]): %63s to %63s on %127[^\n]", &timestamp, &res.portNumber, portName,
               fromState, toState, trigger) == 6 ||
        sscanf(msg.c_str(), "[%lf] port %d (%127[^)]): %63s to %63s on %127[^\n]", &timestamp, &res.portNumber, portName,
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
    return msg.find("Waiting for ptp4l") != std::string::npos;  // этот и так работает
}

SubproccessCollector::SubproccessCollector(const std::string& cmd, const std::vector<std::string>& args)
    : ICollector(std::make_unique<JournalMessageParser>()), unit(cmd) {
    std::vector<std::string> fullArgs(args);  // args уже содержит все флаги
    child = bp::child("/usr/bin/sudo", bp::args(fullArgs), bp::std_out > stream);
    if (!child.running()) {
        spdlog::error("Failed to start process: {}", cmd);
        throw std::runtime_error("Failed to start process: " + cmd);
    }
    spdlog::info("Started process: {} (pid={})", cmd, child.id());
}

std::optional<JournalEvent> SubproccessCollector::readEvent() {
    JournalEvent event{};
    auto now = std::chrono::system_clock::now();
    event.ts_usec = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    event.unit = unit;
    if (!getline(stream, event.msg)) {
        return std::nullopt;
    }
    if (event.msg.empty()) {
        return std::nullopt;
    }
    return event;
}

SubproccessCollector::~SubproccessCollector() {
    if (child.running()) {
        child.terminate();
        child.wait();
    }
}