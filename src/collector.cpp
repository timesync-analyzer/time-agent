#include "collector.h"

#include <spdlog/spdlog.h>
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
        std::string matchSystemd = "_COMM=" + std::string(unit);
        sd_journal_add_match(journal, matchSystemd.c_str(), 0);

        sd_journal_add_disjunction(journal);
    }
    sd_journal_add_disjunction(journal);
    sd_journal_add_match(journal, "_TRANSPORT=syslog", 0);

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

    if (sd_journal_get_data(journal, "_COMM", &data, &len) == 0) {
        const char* raw = static_cast<const char*>(data);
        event.unit = std::string(raw + sizeof("_COMM=") - 1, len - (sizeof("_COMM=") - 1));
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

// янв 22 18:57:25 hawk-d12 ptp4l[46823]: ptp4l[192898.322]: master offset         21 s2 freq   +3212 path delay         7
std::optional<Ptp4lStats> JournalMessageParser::parse_ptp4l_msg(const std::string& unit, const std::string& msg) const {
    Ptp4lStats res;
    res.unit = unit;
    double timestamp;
    if (sscanf(msg.c_str(), "[%lf]: master offset %ld s%d freq %ld path delay %ld", &timestamp, &res.offset, &res.state,
               &res.freq, &res.path_delay) >= 3) {
        res.timestamp_ms = timestamp * 1000;
        return res;
    }
    return std::nullopt;
}

// янв 22 18:57:25 hawk-d12 phc2sys[42767]: phc2sys[192898.077]: CLOCK_REALTIME phc offset       -42 s2 freq   +1257 delay   1450
std::optional<Phc2SysStats> JournalMessageParser::parse_phc2sys_msg(const std::string& unit, const std::string& msg) const {
    Phc2SysStats res;
    res.unit = unit;
    double timestamp;
    if (sscanf(msg.c_str(), "[%lf]: %*s %*s offset %ld s%d freq %ld delay %ld", &timestamp, &res.offset, &res.state, &res.freq,
               &res.path_delay) >= 3) {
        res.timestamp_ms = timestamp * 1000;
        return res;
    }
    return std::nullopt;
}