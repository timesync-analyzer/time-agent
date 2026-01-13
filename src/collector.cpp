#include "collector.h"
#include <initializer_list>
#include <stdexcept>
#include <systemd/sd-journal.h>

// не забыть включить ntpd service

JournalCollector::JournalCollector(const std::vector<std::string_view>& units) {
    int ret = sd_journal_open(&journal, SD_JOURNAL_LOCAL_ONLY);
    if (ret < 0) {
        throw std::runtime_error("sd_journal_open failed: " + std::to_string(-ret));
    }

    sd_journal_flush_matches(journal);

    for (const auto& unit: units) {
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
        event.unit = std::string(raw + sizeof("_SYSTEMD_UNIT=") - 1,
                              len - (sizeof("_SYSTEMD_UNIT=") - 1));
    }

    if (sd_journal_get_data(journal, "MESSAGE", &data, &len) == 0) {
        const char* raw = static_cast<const char*>(data);
        event.msg = std::string(raw + sizeof("MESSAGE=") - 1,
                                 len - (sizeof("MESSAGE=") - 1));
    }

    return event;
}

bool JournalCollector::waitForData(int timeout_ms) {
    int result = sd_journal_wait(journal, static_cast<uint64_t>(timeout_ms) * 1000);
    return (result == SD_JOURNAL_APPEND || result == SD_JOURNAL_INVALIDATE);
}