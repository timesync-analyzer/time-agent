#include "collector.h"

#include <systemd/sd-journal.h>

#include <stdexcept>

// не забыть включить ntpd service

ICollector::ICollector(std::unique_ptr<IMessageParser> msgParser) : msgParser(std::move(msgParser)) {}

std::optional<Ptp4lStats> ICollector::parse_ptp4l_msg(const JournalEvent& event) {
    return msgParser->parse_ptp4l_msg(event.unit, event.msg);
}
std::optional<Phc2SysStats> ICollector::parse_phc2sys_msg(const JournalEvent& event) {
    return msgParser->parse_phc2sys_msg(event.unit, event.msg);
}

JournalCollector::JournalCollector(const std::vector<std::string_view>& units)
    : ICollector(std::make_unique<JournalMessageParser>()) {
    int ret = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);
    if (ret < 0) {
        throw std::runtime_error("sd_journal_open failed: " + std::to_string(-ret));
    }

    sd_journal_flush_matches(journal);

    for (const auto& unit : units) {
        std::string matchSystemd = "_SYSTEMD_UNIT=" + std::string(unit);
        sd_journal_add_match(journal, matchSystemd.c_str(), 0);

        sd_journal_add_disjunction(journal);
    }
    sd_journal_add_conjunction(journal);
    sd_journal_add_match(journal, "_TRANSPORT=stdout", 0);

    sd_journal_seek_tail(journal);
    sd_journal_previous(journal);
}

JournalCollector::~JournalCollector() {
    if (journal) {
        sd_journal_close(journal);
    }
}

std::optional<JournalEvent> JournalCollector::readEvent() {
    if (sd_journal_next(journal) <= 0) {
        return std::nullopt;
    }
    JournalEvent event;
    sd_journal_get_realtime_usec(journal, &event.ts_usec);
    const void* data;
    size_t len;

    if (sd_journal_get_data(journal, "_SYSTEMD_UNIT", &data, &len) == 0) {
        const char* raw = static_cast<const char*>(data);
        event.unit = std::string(raw + sizeof("_SYSTEMD_UNIT=") - 1, len - (sizeof("_SYSTEMD_UNIT=") - 1));
    }

    if (sd_journal_get_data(journal, "MESSAGE", &data, &len) == 0) {
        const char* raw = static_cast<const char*>(data);
        event.msg = std::string(raw + sizeof("MESSAGE=") - 1, len - (sizeof("MESSAGE=") - 1));
    }

    return event;
}

bool JournalCollector::waitForData(int timeout_ms) {
    int result = sd_journal_wait(journal, static_cast<uint64_t>(timeout_ms) * 1000);
    return (result == SD_JOURNAL_APPEND || result == SD_JOURNAL_INVALIDATE);
}

std::optional<Ptp4lStats> JournalMessageParser::parse_ptp4l_msg(const std::string& unit, const std::string& msg) const {
    Ptp4lStats res;
    res.unit = unit;
    double timestamp;
    // master offset -9 s2 freq +3204 path delay 7
    if (sscanf(msg.c_str(), "ptp4l[%lf]: master offset %lld s%d freq %lld", &timestamp, &res.offset, &res.state, &res.freq) >=
        3) {
        return res;
    }
    res.timestamp_ms = timestamp * 1000;
    return std::nullopt;
}

// phc2sys
std::optional<Phc2SysStats> JournalMessageParser::parse_phc2sys_msg(const std::string& unit, const std::string& msg) const {
    Phc2SysStats res;
    res.unit = unit;
    double timestamp;
    // CLOCK_REALTIME phc offset 15 s2 freq +1425
    if (sscanf(msg.c_str(), "phc2sys[%lf]: %*s %*s offset %lld s%d freq %lld", &timestamp, &res.offset, &res.state, &res.freq) >=
        3) {
        return res;
    }
    res.timestamp_ms = timestamp * 1000;
    return std::nullopt;
}