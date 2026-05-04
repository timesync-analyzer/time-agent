#pragma once

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/util/time_util.h>

#include <chrono>

namespace timestamp_utils {

using google::protobuf::Timestamp;

/**
 * @brief Returns current system time in microseconds since Unix epoch.
 * @return Current Unix epoch timestamp in microseconds.
 */
inline int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

/**
 * @brief Converts a Unix epoch microsecond timestamp to google.protobuf.Timestamp.
 * @param us Unix epoch timestamp in microseconds.
 * @return Protobuf timestamp with second and nanosecond fields populated.
 */
inline Timestamp from_us(int64_t us) {
    Timestamp ts;
    ts.set_seconds(us / 1'000'000);
    ts.set_nanos((us % 1'000'000) * 1000);
    return ts;
}

}  // namespace timestamp_utils
