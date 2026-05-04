#pragma once

#include <string>

/**
 * @brief Resolves a Linux network interface to a human-readable PCI adapter name.
 */
class NetworkAdapterResolver {
public:
    NetworkAdapterResolver() = delete;

    /**
     * @brief Returns the adapter name for interfaceName.
     * @param interfaceName Linux network interface name.
     * @return Human-readable PCI adapter name, or an empty string on failure.
     */
    static std::string resolve(const std::string& interfaceName);
};
