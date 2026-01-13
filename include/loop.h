#pragma once
#include "metrics.h"
#include <collector.h>
#include <memory>
#include <unordered_map>

class EventLoop {
public:
    EventLoop(std::unique_ptr<ICollector> collector);
    void run();
private:
    std::unique_ptr<ICollector> collector;
    std::unordered_map<std::string, std::unique_ptr<IMessageParser>> unit2parser;
};