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

inline Timestamp from_us(int64_t us) {
    Timestamp ts;
    ts.set_seconds(us / 1'000'000);
    ts.set_nanos((us % 1'000'000) * 1000);
    return ts;
}

}  // namespace timestamp_utils