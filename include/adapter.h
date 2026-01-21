#pragma once

#include <zmq.hpp>

#include "metrics.pb.h"
#include "system_metrics.h"

namespace converters {

Ptp4lMetrics to_ptp4l_metrics(const Ptp4lStats& internal, const std::string& node_id);

Phc2SysMetrics to_phc2sys_metrics(const Ptp4lStats& internal, const std::string& node_id);

SystemMetrics to_system_metrics(const SystemStats& internal, const std::string& node_id);

}  // namespace converters

class IAdapter {
public:
    ~IAdapter() = default;
    virtual bool send_ptp_statistics(const Ptp4lStats& ptp4lStats) = 0;
    virtual bool send_phc2sys_statistics(const Ptp4lStats& phc2sysStats) = 0;
    virtual bool send_sys_statistics(const SystemStats& sysStats) = 0;
};

class ZMQAdapter : public IAdapter {
public:
    ZMQAdapter(const ZMQConfig& config, const std::string& node);
    bool send_ptp_statistics(const Ptp4lStats& ptp4lStats) override;
    bool send_phc2sys_statistics(const Ptp4lStats& phc2sysStats) override;
    bool send_sys_statistics(const SystemStats& sysStats) override;

private:
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> sender_;
};