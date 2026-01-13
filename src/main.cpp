#include <string>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>
#include "collector.h"
#include "loop.h"
#include "metrics.h"
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

int main() {
    std::unordered_map<std::string_view, std::unique_ptr<IMessageParser>> unit2parser;
    unit2parser["ptp4l@slave.service"] = std::make_unique<Ptp4lMessageParser>();
    unit2parser["phc2sys@slave.service"] = std::make_unique<Phc2SysMessageParser>();

    std::vector<std::string_view> units;
    for (const auto& [unit, parser]: unit2parser) {
        units.push_back(unit);
    }

    auto collector = std::make_unique<JournalCollector>(units);
    EventLoop loop(std::move(collector));
    loop.run();
}