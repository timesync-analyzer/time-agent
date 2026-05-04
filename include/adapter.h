#pragma once

#include <spdlog/spdlog.h>

#include <zmq.hpp>

#include "collector.h"
#include "metrics.pb.h"
#include "system_metrics.h"

namespace converters {

/**
 * @brief Converts internal ptp4l stats into a MetricsWrapper protobuf message.
 * @param internal Internal ptp4l statistics.
 * @param node_name Node name attached to the protobuf wrapper.
 * @return Serialized-ready protobuf wrapper.
 */
MetricsWrapper to_ptp4l_metrics(const Ptp4lStats& internal, const std::string& node_name);

/**
 * @brief Converts internal phc2sys stats into a MetricsWrapper protobuf message.
 * @param internal Internal phc2sys statistics.
 * @param node_name Node name attached to the protobuf wrapper.
 * @return Serialized-ready protobuf wrapper.
 */
MetricsWrapper to_phc2sys_metrics(const Phc2SysStats& internal, const std::string& node_name);

/**
 * @brief Converts a system metrics snapshot into a MetricsWrapper protobuf message.
 * @param internal Internal system metrics snapshot.
 * @param node_name Node name attached to the protobuf wrapper.
 * @return Serialized-ready protobuf wrapper.
 */
MetricsWrapper to_system_metrics(const SystemStats& internal, const std::string& node_name);

/**
 * @brief Converts internal ppswatch stats into a MetricsWrapper protobuf message.
 * @param internal Internal ppswatch statistics.
 * @param node Node name attached to the protobuf wrapper.
 * @return Serialized-ready protobuf wrapper.
 */
MetricsWrapper to_pps_metrics(const PPSStats& internal, const std::string& node);

/**
 * @brief Converts a ptp4l port transition into a MetricsWrapper protobuf message.
 * @param internal Internal ptp4l port event.
 * @param node Node name attached to the protobuf wrapper.
 * @return Serialized-ready protobuf wrapper.
 */
MetricsWrapper to_port_event(const PortEvent& internal, const std::string& node);
}  // namespace converters

/**
 * @brief Transport boundary for sending collected metrics to the analyzer.
 *
 * Concrete adapters are responsible for serialization and delivery. Methods
 * return false when a message could not be sent.
 */
class IAdapter {
public:
    virtual ~IAdapter() = default;
    /**
     * @brief Sends a ptp4l timing sample for node.
     * @param ptp4lStats Parsed ptp4l statistics.
     * @param node Node name attached to the outgoing metric.
     * @return true when the metric was accepted by the transport.
     */
    virtual bool send_ptp_statistics(const Ptp4lStats& ptp4lStats, const std::string& node) = 0;
    /**
     * @brief Sends a phc2sys timing sample for node.
     * @param phc2sysStats Parsed phc2sys statistics.
     * @param node Node name attached to the outgoing metric.
     * @return true when the metric was accepted by the transport.
     */
    virtual bool send_phc2sys_statistics(const Phc2SysStats& phc2sysStats, const std::string& node) = 0;
    /**
     * @brief Sends a ppswatch offset sample for node.
     * @param ppsStats Parsed ppswatch statistics.
     * @param node Node name attached to the outgoing metric.
     * @return true when the metric was accepted by the transport.
     */
    virtual bool send_pps_statistics(const PPSStats& ppsStats, const std::string& node) = 0;
    /**
     * @brief Sends a system metrics snapshot for node.
     * @param sysStats System metrics snapshot.
     * @param node Node name attached to the outgoing metric.
     * @return true when the metric was accepted by the transport.
     */
    virtual bool send_sys_statistics(const SystemStats& sysStats, const std::string& node) = 0;
    /**
     * @brief Sends a ptp4l port transition event for node.
     * @param event Parsed ptp4l port event.
     * @param node Node name attached to the outgoing metric.
     * @return true when the event was accepted by the transport.
     */
    virtual bool send_ptp4l_port_event(const PortEvent& event, const std::string& node) = 0;
};

/**
 * @brief ZMQ PUSH adapter that serializes MetricsWrapper protobuf messages.
 */
class ZMQAdapter : public IAdapter {
public:
    ~ZMQAdapter() override;
    /**
     * @brief Connects a PUSH socket to the configured analyzer endpoint.
     * @param config ZMQ socket configuration.
     * @param node Node name used for connection logging.
     */
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
    /**
     * @brief Serializes a protobuf message and sends it through the PUSH socket.
     * @tparam T Protobuf message type.
     * @param msg Message to serialize and send.
     * @return true when the message was sent successfully.
     */
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
