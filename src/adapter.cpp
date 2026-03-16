#include "adapter.h"

#include <spdlog/spdlog.h>

#include <thread>

#include "metrics.pb.h"
#include "system_metrics.h"
#include "timestamp_utils.h"

namespace converters {
MetricsWrapper to_ptp4l_metrics(const Ptp4lStats& internal, const std::string& node) {
    MetricsWrapper metrics_wrapper;
    metrics_wrapper.set_type(MessageType::MESSAGE_TYPE_PTP4L);
    metrics_wrapper.set_node_name(node);
    *metrics_wrapper.mutable_timestamp() = timestamp_utils::from_us(internal.timestamp_us);
    auto* metrics = metrics_wrapper.mutable_ptp4l();

    metrics->set_path_delay(internal.path_delay);
    metrics->set_frequency(internal.freq);
    metrics->set_offset_ns(internal.offset);
    metrics->set_state(internal.state);
    return metrics_wrapper;
}

MetricsWrapper to_phc2sys_metrics(const Phc2SysStats& internal, const std::string& node) {
    MetricsWrapper metrics_wrapper;
    metrics_wrapper.set_type(MessageType::MESSAGE_TYPE_PHC2SYS);
    metrics_wrapper.set_node_name(node);
    *metrics_wrapper.mutable_timestamp() = timestamp_utils::from_us(internal.timestamp_us);

    auto* metrics = metrics_wrapper.mutable_phc2sys();

    metrics->set_path_delay(internal.path_delay);
    metrics->set_frequency(internal.freq);
    metrics->set_offset_ns(internal.offset);
    metrics->set_state(internal.state);
    return metrics_wrapper;
}

MetricsWrapper to_system_metrics(const SystemStats& internal, const std::string& nodeName) {
    MetricsWrapper metrics_wrapper;
    metrics_wrapper.set_type(MessageType::MESSAGE_TYPE_SYSTEM);
    metrics_wrapper.set_node_name(nodeName);
    *metrics_wrapper.mutable_timestamp() = timestamp_utils::from_us(internal.timestamp_us);

    auto* metrics = metrics_wrapper.mutable_system();

    auto* cpu = metrics->mutable_cpustats();
    cpu->set_usage_percent(internal.cpuStats.usage_percent);
    cpu->set_context_switches(internal.cpuStats.context_switches);
    cpu->set_interrupts(internal.cpuStats.interrupts);
    cpu->set_softirqs(internal.cpuStats.softirqs);

    auto* mem = metrics->mutable_memorystats();
    mem->set_mem_available_kb(internal.memoryStats.mem_available_kb);
    mem->set_mem_free_kb(internal.memoryStats.mem_free_kb);
    mem->set_swap_total_kb(internal.memoryStats.swap_total_kb);
    mem->set_swap_free_kb(internal.memoryStats.swap_free_kb);
    mem->set_buffers_kb(internal.memoryStats.buffers_kb);

    auto* net = metrics->mutable_networkstats();
    net->set_rx_packets(internal.networkStats.rx_packets);
    net->set_tx_packets(internal.networkStats.tx_packets);
    net->set_rx_dropped(internal.networkStats.rx_dropped);
    net->set_tx_dropped(internal.networkStats.tx_dropped);
    net->set_rx_errors(internal.networkStats.rx_errors);
    net->set_tx_errors(internal.networkStats.tx_errors);
    net->set_collisions(internal.networkStats.collisions);

    auto* temp = metrics->mutable_temperaturestats();
    for (const auto& zone : internal.temperatureStats.zonesReadings) {
        auto* reading = temp->add_zones_readings();
        reading->set_sensor(zone.sensor);
        reading->set_label(zone.label);
        reading->set_temperature(zone.temperature);
    }

    return metrics_wrapper;
}

MetricsWrapper to_node_info(const NodeInfo& internal, const std::string& node) {
    MetricsWrapper metrics_wrapper;
    metrics_wrapper.set_type(MessageType::MESSAGE_TYPE_NODE_INFO);
    metrics_wrapper.set_node_name(node);

    NodeInfo* node_info = metrics_wrapper.mutable_node_info();

    node_info->set_net_interface(internal.net_interface());
    node_info->set_ip_address(internal.ip_address());
    node_info->set_node_type(internal.node_type());

    return metrics_wrapper;
}

MetricsWrapper to_pps_metrics(const PPSStats& internal, const std::string& node) {
    MetricsWrapper metrics_wrapper;
    metrics_wrapper.set_type(MessageType::MESSAGE_TYPE_PTP4L);
    metrics_wrapper.set_node_name(node);
    *metrics_wrapper.mutable_timestamp() = timestamp_utils::from_us(internal.timestamp_us);
    auto* metrics = metrics_wrapper.mutable_ptp4l();

    metrics->set_offset_ns(internal.offset);
    return metrics_wrapper;
}

}  // namespace converters

ZMQAdapter::ZMQAdapter(const ZMQConfig& config, const std::string& node)
    : context_(std::make_unique<zmq::context_t>(1)), sender_(std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::push)) {
    sender_->set(zmq::sockopt::sndhwm, config.queue_size);
    sender_->set(zmq::sockopt::linger, config.timeout_after_close_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    spdlog::info("Connecting to endpoint: '{}'", config.endpoint);
    sender_->connect(config.endpoint);

    spdlog::info("MetricsSender connected to {} (node: {})", config.endpoint, node);
}

bool ZMQAdapter::send_ptp_statistics(const Ptp4lStats& ptp4l, const std::string& node) {
    auto metrics = converters::to_ptp4l_metrics(ptp4l, node);
    return send_impl(metrics);
}

bool ZMQAdapter::send_phc2sys_statistics(const Phc2SysStats& phc2sys, const std::string& node) {
    auto metrics = converters::to_phc2sys_metrics(phc2sys, node);
    return send_impl(metrics);
}

bool ZMQAdapter::send_pps_statistics(const PPSStats& ppsStats, const std::string& node) {
    auto metrics = converters::to_pps_metrics(ppsStats, node);
    return send_impl(metrics);
}

bool ZMQAdapter::send_node_info(const NodeInfo& info, const std::string& node) {
    auto metrics = converters::to_node_info(info, node);
    return send_impl(metrics);
}

bool ZMQAdapter::send_sys_statistics(const SystemStats& sysStats, const std::string& node) {
    auto metrics = converters::to_system_metrics(sysStats, node);
    return send_impl(metrics);
}

ZMQAdapter::~ZMQAdapter() {
    try {
        spdlog::info("Closing ZMQAdapter...");

        if (sender_) {
            try {
                sender_->close();
            } catch (const zmq::error_t& e) {
                spdlog::warn("Error closing socket: {}", e.what());
            }
            sender_.reset();
        }

        if (context_) {
            try {
                context_->shutdown();
            } catch (const zmq::error_t& e) {
                spdlog::warn("Error shutting down context: {}", e.what());
            }
            context_.reset();
        }

        spdlog::info("ZMQAdapter closed successfully");

    } catch (const std::exception& e) {
        spdlog::error("Error during ZMQAdapter cleanup: {}", e.what());
    } catch (...) {
        spdlog::error("Unknown error during ZMQAdapter cleanup");
    }
}