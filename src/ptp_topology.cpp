#include "ptp_topology.h"

#include <spdlog/spdlog.h>

#include <boost/process.hpp>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string_view>
#include <utility>

namespace bp = boost::process;

namespace {
std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::string firstToken(const std::string& value) {
    std::istringstream stream(value);
    std::string token;
    stream >> token;
    return token;
}

bool parseKeyValue(const std::string& line, std::string& key, std::string& value) {
    const auto trimmed = trim(line);
    if (trimmed.empty() || trimmed.rfind("sending:", 0) == 0) {
        return false;
    }

    std::istringstream stream(trimmed);
    if (!(stream >> key)) {
        return false;
    }

    std::string rest;
    std::getline(stream, rest);
    value = trim(rest);
    return !value.empty();
}

std::optional<int> parsePortNumber(const std::string& portIdentity) {
    const auto dash = portIdentity.rfind('-');
    if (dash == std::string::npos || dash + 1 >= portIdentity.size()) {
        return std::nullopt;
    }

    try {
        return std::stoi(portIdentity.substr(dash + 1));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string parseClockIdentity(const std::string& portIdentity) {
    const auto token = firstToken(portIdentity);
    const auto dash = token.rfind('-');
    if (dash == std::string::npos) {
        return token;
    }
    return token.substr(0, dash);
}

std::optional<int64_t> parseNanoseconds(const std::string& value) {
    const auto token = firstToken(value);
    if (token.empty()) {
        return std::nullopt;
    }

    try {
        return static_cast<int64_t>(std::llround(std::stod(token)));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

uint64_t nowUs() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}
}  // namespace

std::optional<PtpTopologySnapshot> PtpTopologyParser::parse(const std::string& defaultDataSet, const std::string& parentDataSet,
                                                            const std::string& currentDataSet,
                                                            const std::string& portDataSet) const {
    PtpTopologySnapshot snapshot;
    snapshot.timestamp_us = nowUs();

    std::string key;
    std::string value;

    std::istringstream defaultStream(defaultDataSet);
    for (std::string line; std::getline(defaultStream, line);) {
        if (parseKeyValue(line, key, value) && key == "clockIdentity") {
            snapshot.localClockIdentity = firstToken(value);
        }
    }

    std::istringstream parentStream(parentDataSet);
    for (std::string line; std::getline(parentStream, line);) {
        if (!parseKeyValue(line, key, value)) {
            continue;
        }
        if (key == "parentPortIdentity") {
            const auto identity = firstToken(value);
            snapshot.parentClockIdentity = parseClockIdentity(identity);
            if (const auto port = parsePortNumber(identity)) {
                snapshot.parentPortNumber = *port;
            }
        } else if (key == "grandmasterIdentity") {
            snapshot.grandmasterIdentity = firstToken(value);
        }
    }

    std::istringstream currentStream(currentDataSet);
    for (std::string line; std::getline(currentStream, line);) {
        if (!parseKeyValue(line, key, value)) {
            continue;
        }
        if (key == "stepsRemoved") {
            if (const auto steps = parseNanoseconds(value)) {
                snapshot.stepsRemoved = static_cast<int>(*steps);
            }
        } else if (key == "meanPathDelay") {
            if (const auto delay = parseNanoseconds(value)) {
                snapshot.meanPathDelayNs = *delay;
            }
        }
    }

    std::optional<PtpPortSnapshot> currentPort;
    std::istringstream portStream(portDataSet);
    for (std::string line; std::getline(portStream, line);) {
        if (!parseKeyValue(line, key, value)) {
            continue;
        }
        if (key == "portIdentity") {
            if (currentPort) {
                snapshot.ports.push_back(*currentPort);
            }
            currentPort = PtpPortSnapshot{};
            currentPort->portIdentity = firstToken(value);
            if (const auto port = parsePortNumber(currentPort->portIdentity)) {
                currentPort->portNumber = *port;
            }
        } else if (key == "portState" && currentPort) {
            currentPort->state = firstToken(value);
        }
    }
    if (currentPort) {
        snapshot.ports.push_back(*currentPort);
    }

    for (const auto& port : snapshot.ports) {
        if (port.state == "SLAVE") {
            snapshot.childPortNumber = port.portNumber;
            break;
        }
    }

    if (snapshot.localClockIdentity.empty()) {
        return std::nullopt;
    }
    return snapshot;
}

PtpTopologyCollector::PtpTopologyCollector(PtpTopologyConfig config) : config_(std::move(config)) {}

bool PtpTopologyCollector::enabled() const { return config_.on; }

std::optional<PtpTopologySnapshot> PtpTopologyCollector::collect() const {
    const auto defaultDataSet = runCommand("GET DEFAULT_DATA_SET");
    if (!defaultDataSet) {
        return std::nullopt;
    }

    const auto parentDataSet = runCommand("GET PARENT_DATA_SET");
    if (!parentDataSet) {
        return std::nullopt;
    }

    const auto currentDataSet = runCommand("GET CURRENT_DATA_SET");
    const auto portDataSet = runCommand("GET PORT_DATA_SET");
    return parser_.parse(*defaultDataSet, *parentDataSet, currentDataSet.value_or(""), portDataSet.value_or(""));
}

std::vector<std::string> PtpTopologyCollector::buildArgs(const std::string& command) const {
    std::vector<std::string> args;
    args.emplace_back("-u");
    args.emplace_back("-b");
    args.emplace_back(std::to_string(config_.boundary_hops));
    args.emplace_back("-d");
    args.emplace_back(std::to_string(config_.domain_number));

    if (!config_.config_path.empty()) {
        args.emplace_back("-f");
        args.emplace_back(config_.config_path);
    }
    if (!config_.uds_path.empty()) {
        args.emplace_back("-s");
        args.emplace_back(config_.uds_path);
    }

    args.emplace_back(command);
    return args;
}

std::optional<std::string> PtpTopologyCollector::runCommand(const std::string& command) const {
    try {
        const auto args = buildArgs(command);
        bp::ipstream output;
        bp::child child(config_.pmc_path, bp::args(args), bp::std_out > output);

        std::ostringstream result;
        std::string line;
        while (std::getline(output, line)) {
            result << line << '\n';
        }

        child.wait();
        if (child.exit_code() != 0) {
            spdlog::warn("pmc command failed: {} exit_code={}", command, child.exit_code());
            return std::nullopt;
        }
        return result.str();
    } catch (const std::exception& e) {
        spdlog::warn("failed to run pmc command {}: {}", command, e.what());
        return std::nullopt;
    }
}
