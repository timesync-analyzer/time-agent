#include "loop.h"
#include "metrics.h"
#include <memory>

EventLoop::EventLoop(std::unique_ptr<ICollector> collector) : collector(std::move(collector)) {
    unit2parser["ptp4l@slave.service"] = std::make_unique<Ptp4lMessageParser>();
    unit2parser["phc2sys@slave.service"] = std::make_unique<Phc2SysMessageParser>();
}

void EventLoop::run() {
    while (true) {
        if (collector->waitForData(1000)) {
            while (auto event = collector->readEvent()) {
                auto ptpMetric = unit2parser[event->unit]->parse(event.value()).value();
                std::cout << event->ts_usec << ' ' << event->unit << ' ' << ptpMetric.freq << ' ' << ptpMetric.offset << std::endl;
            }
        }
    }
}