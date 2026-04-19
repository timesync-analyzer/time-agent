#pragma once

#include <string>

class NetworkAdapterResolver {
public:
    NetworkAdapterResolver() = delete;

    static std::string resolve(const std::string& interfaceName);
};
