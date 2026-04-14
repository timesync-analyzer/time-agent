#pragma once

#include <spdlog/spdlog.h>

#include <zmq.hpp>

#include "collector.h"
#include "metrics.pb.h"
#include "system_metrics.h"

namespace converters {

MetricsWrapper to_ptp4l_metrics(const Ptp4lStats& internal, const std::string& node_name);

MetricsWrapper to_phc2sys_metrics(const Phc2SysStats& internal, const std::string& node_name);

MetricsWrapper to_system_metrics(const SystemStats& internal, const std::string& node_name);

MetricsWrapper to_pps_metrics(const PPSStats& internal, const std::string& node);

MetricsWrapper to_port_event(const PortEvent& internal, const std::string& node);
}  // namespace converters

class IAdapter {
public:
    virtual ~IAdapter() = default;
    virtual bool send_ptp_statistics(const Ptp4lStats& ptp4lStats, const std::string& node) = 0;
    virtual bool send_phc2sys_statistics(const Phc2SysStats& phc2sysStats, const std::string& node) = 0;
    virtual bool send_pps_statistics(const PPSStats& ppsStats, const std::string& node) = 0;
    virtual bool send_sys_statistics(const SystemStats& sysStats, const std::string& node) = 0;
    virtual bool send_ptp4l_port_event(const PortEvent& event, const std::string& node) = 0;
};

class ZMQAdapter : public IAdapter {
public:
    ~ZMQAdapter() override;
    ZMQAdapter(const ZMQConfig& config, const std::string& node);
    bool send_ptp_statistics(const Ptp4lStats& ptp4lStats, const std::string& node) override;
    bool send_phc2sys_statistics(const Phc2SysStats& phc2sysStats, const std::string& node) override;
    bool send_pps_statistics(const PPSStats& ppsStats, const std::string& node) override;
    bool send_sys_statistics(const SystemStats& sysStats, const std::string& node) override;
    bool send_ptp4l_port_event(const PortEvent& event, const std::string& node) override;

private:
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> sender_;

private:
    template <typename T>
    bool send_impl(const T& msg);
};

template <typename T>
bool ZMQAdapter::send_impl(const T& msg) {
    static_assert(std::is_base_of<google::protobuf::Message, T>::value, "T must be a protobuf message");
    if (!sender_) {
        spdlog::error("Sender not initialized");
        return false;
    }

    std::string serialized;
    if (!msg.SerializeToString(&serialized)) {
        spdlog::error("Failed to serialize message");
        return false;
    }

    try {
        zmq::message_t zmq_msg(msg.ByteSizeLong());
        msg.SerializeToArray(zmq_msg.data(), zmq_msg.size());

        auto result = sender_->send(std::move(zmq_msg), zmq::send_flags::none);
        if (!result) {
            spdlog::warn("Queue full");
            return false;
        }

        return true;
    } catch (const zmq::error_t& e) {
        spdlog::error("Send error: {}", e.what());
        return false;
    }
}