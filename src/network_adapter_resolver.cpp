#include "network_adapter_resolver.h"

#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

extern "C" {
#include <pci/pci.h>
}

namespace {

class FileDescriptor {
public:
    explicit FileDescriptor(int fd) : fd_(fd) {}

    ~FileDescriptor() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    int get() const { return fd_; }

private:
    int fd_ = -1;
};

struct PciAddress {
    unsigned int domain = 0;
    unsigned int bus = 0;
    unsigned int device = 0;
    unsigned int function = 0;
};

std::string getBusInfo(const std::string& interfaceName) {
    FileDescriptor socketFd(::socket(AF_INET, SOCK_DGRAM, 0));
    if (socketFd.get() < 0) {
        throw std::runtime_error("socket() failed: " + std::string(std::strerror(errno)));
    }

    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, interfaceName.c_str(), IFNAMSIZ - 1);

    struct ethtool_drvinfo drvinfo {};
    drvinfo.cmd = ETHTOOL_GDRVINFO;
    ifr.ifr_data = reinterpret_cast<char*>(&drvinfo);

    if (::ioctl(socketFd.get(), SIOCETHTOOL, &ifr) < 0) {
        throw std::runtime_error("ioctl(SIOCETHTOOL) failed for '" + interfaceName + "': " + std::string(std::strerror(errno)));
    }

    std::string busInfo = drvinfo.bus_info;
    if (busInfo.empty() || busInfo == "N/A") {
        throw std::runtime_error("bus-info is empty or unsupported for interface '" + interfaceName + "'");
    }

    return busInfo;
}

PciAddress parsePciAddress(const std::string& busInfo) {
    PciAddress address;
    if (std::sscanf(busInfo.c_str(), "%x:%x:%x.%x", &address.domain, &address.bus, &address.device, &address.function) != 4) {
        throw std::runtime_error("failed to parse PCI address from bus-info '" + busInfo + "'");
    }
    return address;
}

std::string lookupAdapterName(const std::string& busInfo) {
    const PciAddress address = parsePciAddress(busInfo);

    using PciAccessPtr = std::unique_ptr<pci_access, decltype(&pci_cleanup)>;
    PciAccessPtr pciAccess(pci_alloc(), &pci_cleanup);
    if (!pciAccess) {
        throw std::runtime_error("pci_alloc() failed");
    }

    pci_init(pciAccess.get());

    pci_dev* device = pci_get_dev(pciAccess.get(), address.domain, address.bus, address.device, address.function);
    if (device == nullptr) {
        throw std::runtime_error("pci_get_dev() failed for '" + busInfo + "'");
    }

    const int fillResult = pci_fill_info(device, PCI_FILL_IDENT);
    if ((fillResult & PCI_FILL_IDENT) == 0) {
        pci_free_dev(device);
        throw std::runtime_error("pci_fill_info() failed for '" + busInfo + "'");
    }

    std::array<char, 512> vendorName {};
    std::array<char, 512> deviceName {};

    pci_lookup_name(pciAccess.get(), vendorName.data(), vendorName.size(), PCI_LOOKUP_VENDOR, device->vendor_id);
    pci_lookup_name(pciAccess.get(), deviceName.data(), deviceName.size(), PCI_LOOKUP_DEVICE, device->vendor_id, device->device_id);

    std::ostringstream result;
    result << vendorName.data() << ' ' << deviceName.data() << " (rev " << std::hex << std::nouppercase << std::setw(2)
           << std::setfill('0') << static_cast<unsigned int>(device->rev_id) << ')';

    pci_free_dev(device);
    return result.str();
}

}  // namespace

std::string NetworkAdapterResolver::resolve(const std::string& interfaceName) {
    try {
        return lookupAdapterName(getBusInfo(interfaceName));
    } catch (const std::exception& e) {
        spdlog::warn("Failed to resolve adapter for '{}': {}", interfaceName, e.what());
        return "";
    }
}
