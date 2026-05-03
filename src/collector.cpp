#include <spdlog/spdlog.h>
#include <systemd/sd-journal.h>

#include <chrono>
#include <optional>
#include <stdexcept>

#include "collector.h"

std::optional<Ptp4lStats> Ptp4lParser::parseMetrics(const std::string& msg) const {
    Ptp4lStats res{};
    double timestamp;
    if (sscanf(msg.c_str(), "[%lf] %*s %*s offset %ld s%d freq %ld delay %ld",
            &timestamp, &res.offset, &res.state, &res.freq, &res.path_delay) == 5) {
        return res;
    }
    if (sscanf(msg.c_str(), "[%lf] rms %*ld max %ld freq %ld +/- %*ld delay %ld +/- %*ld", &timestamp, &res.offset, &res.freq,
                &res.path_delay) == 4) {
        return res;
    }
    return std::nullopt;
}

std::optional<Phc2SysStats> Phc2SysParser::parseMetrics(const std::string& msg) const {
    Phc2SysStats res{};
    double timestamp;
    // rms summary line: timestamp + 3 data fields (offset mapped to max, freq, path_delay)
    if (sscanf(msg.c_str(), "[%lf] %*s rms %*ld max %ld freq %ld +/- %*ld delay %ld +/- %*ld", &timestamp, &res.offset, &res.freq,
               &res.path_delay) == 4) {
        return res;
    }
    // Regular offset line: timestamp + 4 data fields (offset, state, freq, path_delay)
    if (sscanf(msg.c_str(), "[%lf] %*s %*s offset %ld s%d freq %ld delay %ld", &timestamp, &res.offset, &res.state, &res.freq,
               &res.path_delay) == 5) {
        return res;
    }
    return std::nullopt;
}

std::optional<PortEvent> Ptp4lParser::parsePortEvent(const std::string& msg) const {
    if (msg.find("port ") == std::string::npos || msg.find(" to ") == std::string::npos) {
        return std::nullopt;
    }
    PortEvent res;
    double timestamp;
    char portName[128], fromState[64], toState[64], trigger[128];
    if (sscanf(msg.c_str(), "[%lf] port %d (%127[^)]): %63s to %63s on %127[^\n]", &timestamp, &res.portNumber, portName,
               fromState, toState, trigger) == 6 ||
        sscanf(msg.c_str(), "[%lf] [%*[^]]] port %d (%127[^)]): %63s to %63s on %127[^\n]", &timestamp, &res.portNumber, portName,
               fromState, toState, trigger) == 6) {
        res.portName = portName;
        res.fromState = fromState;
        res.toState = toState;
        res.trigger = trigger;
        return res;
    }
    return std::nullopt;
}

bool Phc2SysParser::isWaiting(const std::string& msg) const { return msg.find("Waiting for ptp4l") != std::string::npos; }

std::optional<PPSStats> PPSParser::parseMetrics(const std::string& msg) const {
    PPSStats res;
    int64_t timestamp_val, sequence_val;
    if (sscanf(msg.c_str(), "timestamp: %ld, sequence: %ld, offset: %ld", &timestamp_val, &sequence_val, &res.offset) == 3) {
        return res;
    }
    spdlog::debug("return nullopt {}", msg);
    return std::nullopt;
}

JournalCollector::JournalCollector(const std::string_view& unit) : unit_(unit) {
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

void JournalCollector::stop() {
    stop_requested_ = true;
}

std::optional<CollectorEvent> JournalCollector::readEvent() {
    while (true) {
        int ret = sd_journal_next(journal_);
        if (ret > 0) {
            break;
        }
        if (ret < 0) {
            spdlog::error("sd_journal_next failed: {}", ret);
            return std::nullopt;
        }

        if (stop_requested_) {
            return std::nullopt;
        }

        ret = sd_journal_wait(journal_, 1'000'000);
        if (ret < 0) {
            spdlog::error("sd_journal_wait failed: {}", ret);
            return std::nullopt;
        }
    }
    CollectorEvent event;
    sd_journal_get_realtime_usec(journal_, &event.ts_usec);
    event.unit = std::string_view(unit_);

    const void* data;
    size_t len;

    if (sd_journal_get_data(journal_, "MESSAGE", &data, &len) == 0) {
        const char* raw = static_cast<const char*>(data);
        event.msg = std::string(raw + sizeof("MESSAGE=") - 1, len - (sizeof("MESSAGE=") - 1));
    }

    return event;
}

SubprocessCollector::SubprocessCollector(const std::string& cmd, const std::vector<std::string>& args) : unit(cmd) {
    std::vector<std::string> fullArgs(args);
    child = bp::child("/usr/bin/sudo", bp::args(fullArgs), bp::std_out > stream);
    if (!child.running()) {
        spdlog::error("Failed to start process: {}", cmd);
        throw std::runtime_error("Failed to start process: " + cmd);
    }
    spdlog::info("Started process: {} (pid={})", cmd, child.id());
}

std::optional<CollectorEvent> SubprocessCollector::readEvent() {
    CollectorEvent event{};
    auto now = std::chrono::system_clock::now();
    event.ts_usec = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    event.unit = std::string_view(unit);
    if (!getline(stream, event.msg)) {
        return std::nullopt;
    }
    if (event.msg.empty()) {
        return std::nullopt;
    }
    return event;
}

void SubprocessCollector::stop() {
    if (child.running()) {
        child.terminate();
    }
}

SubprocessCollector::~SubprocessCollector() {
    if (child.running()) {
        child.terminate();
        child.wait();
    }
}