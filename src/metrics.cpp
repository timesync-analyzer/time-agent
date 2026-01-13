#include "metrics.h"

std::optional<PTPMetrics> Ptp4lMessageParser::parse(const JournalEvent& event) const {
    PTPMetrics res;
    res.unit = event.unit;
    double timestamp;
    // master offset -9 s2 freq +3204 path delay 7
    if (sscanf(event.msg.c_str(), "ptp4l[%lf]: master offset %lld s%d freq %lld",
                &timestamp, &res.offset, &res.state, &res.freq) >= 3) {
        return res;
    }
    res.timestamp = timestamp * 1000;
    return std::nullopt;
}

std::optional<PTPMetrics> Phc2SysMessageParser::parse(const JournalEvent& event) const {
    PTPMetrics res;
    res.unit = event.unit;
    double timestamp;
    // CLOCK_REALTIME phc offset 15 s2 freq +1425
    if (sscanf(event.msg.c_str(), "phc2sys[%lf]: %*s %*s offset %lld s%d freq %lld",
                &timestamp, &res.offset, &res.state, &res.freq) >= 3) {
        return res;
    }
    res.timestamp = timestamp * 1000;
    return std::nullopt;
}

SysMetricsCollector::SysMetricsCollector(const std::string& interface) : interface(interface) {}

void SysMetricsCollector::collect(PTPMetrics &metrics) {

}