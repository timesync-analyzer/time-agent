#include "adapter.h"

#include <spdlog/spdlog.h>

#include <thread>

ZMQAdapter::ZMQAdapter(const ZMQConfig& config, const std::string& node)
    : context_(std::make_unique<zmq::context_t>(1)), sender_(std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::push)) {
    sender_->connect(config.endpoint);
    sender_->set(zmq::sockopt::sndhwm, config.queue_size);
    sender_->set(zmq::sockopt::linger, config.timeout_after_close_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    spdlog::info("MetricsSender connected to {} (node: {})", config.endpoint, node);
}

bool ZMQAdapter::send_ptp_statistics(const Ptp4lStats& ptp4l) {}
bool ZMQAdapter::send_phc2sys_statistics(const Ptp4lStats& ptp4l) {}
bool ZMQAdapter::send_sys_statistics(const SystemStats& sysStats) {}
