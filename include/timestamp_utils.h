#pragma once

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>

#include <chrono>

namespace timestamp_utils {

using google::protobuf::Timestamp;
using google::protobuf::util::TimeUtil;

inline int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline Timestamp from_ms(int64_t ms) {
    Timestamp ts;
    ts.set_seconds(ms / 1000);
    ts.set_nanos((ms % 1000) * 1000000);
    return ts;
}

}  // namespace timestamp_utils