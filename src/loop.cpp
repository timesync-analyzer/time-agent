#include "loop.h"

#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>

#include "metrics.h"

EventLoop::EventLoop(std::unique_ptr<ICollector> collector, const AppConfig& config)
    : collector(std::move(collector)), poll_timeout_ms(config.monitorConfig.poll_timeout_ms) {
    for (const auto& svc : config.services) {
        if (svc.parser == "ptp4l") {
            unit2parser[svc.unit] = std::make_unique<Ptp4lMessageParser>();
        } else if (svc.parser == "phc2sys") {
            unit2parser[svc.unit] = std::make_unique<Phc2SysMessageParser>();
        } else {
            spdlog::warn("Unknown parser type: {}", svc.parser);
        }
        spdlog::debug("Registered parser '{}' for unit '{}'", svc.parser, svc.unit);
    }
}

void EventLoop::run() {
    spdlog::info("Event loop started");
    running = true;
    while (running) {
        if (collector->waitForData(poll_timeout_ms)) {
            while (auto event = collector->readEvent()) {

                auto it = unit2parser.find(event->unit);
                if (it == unit2parser.end()) {
                    spdlog::warn("No parser for unit: {}", event->unit);
                    continue;
                }

                auto result = it->second->parse(event.value());
                if (!result) {
                    spdlog::debug("Failed to parse message from {}: {}", event->unit, event->msg);
                    continue;
                }

                auto& metrics = *result;
                spdlog::info("{} offset={} freq={} state={}", metrics.unit, metrics.offset, metrics.freq, metrics.state);
            }
        }
    }

    spdlog::info("Event loop stopped");
}

void EventLoop::stop() { running = false; }