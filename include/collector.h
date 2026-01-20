#pragma once
#include <systemd/sd-journal.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct JournalEvent {
    uint64_t ts_usec;
    std::string unit;
    std::string msg;
};

const uint64_t TIMEOUT_USEC = 1'000'000;

class ICollector {
public:
    virtual ~ICollector() = default;
    virtual std::optional<JournalEvent> readEvent() = 0;
    virtual bool waitForData(int timeout_ms) = 0;
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