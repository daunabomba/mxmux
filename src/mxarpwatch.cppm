module;

#include "types.h"

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

export module mxsrv.mxarpwatch;

import utils;
import logger;
import arpsocket;

export struct ArpEntries {
    std::unordered_map<IpAddr, std::pair<MacAddress, std::string>, IpAddrHash> cache;
    std::mutex lock;
    void processNew(IpAddr const &ipAddr, MacAddress const &macAddress, std::string const &interface);
    std::optional<std::pair<MacAddress, std::string>> getMacaddressAndInterface(IpAddr const &ipAddr);
};

void ArpEntries::processNew(IpAddr const &ipAddr, MacAddress const &macAddress, std::string const &interface) {
    std::lock_guard<std::mutex> sync(lock);
    auto it = cache.find(ipAddr);
    if (it != cache.end()) {
        it->second = std::make_pair(macAddress, interface);
    } else {
        cache.emplace(ipAddr, std::make_pair(macAddress, interface));
    }
}

std::optional<std::pair<MacAddress, std::string>> ArpEntries::getMacaddressAndInterface(IpAddr const &ipAddr) {
    std::lock_guard<std::mutex> sync(lock);
    auto it = cache.find(ipAddr);
    if (it == cache.end()) {
        return std::nullopt;
    } else {
        return it->second;
    }
}

export class MxArpWatch : public ArpSocketIf {
public:
    MxArpWatch() = delete;
    MxArpWatch(ArpEntries &arpCache);
    void update(IpAddr const &ipAddr, MacAddress const &macAddress) override;

private:
    ArpEntries &macCache;
};

MxArpWatch::MxArpWatch(ArpEntries &arpCache) : macCache(arpCache) {}

void MxArpWatch::update(IpAddr const &ipAddr, MacAddress const &macAddress) {
    auto ref = arpSocket.lock();
    std::string interface;
    if (ref) {
        interface = ref->getInterface();
    }
    macCache.processNew(ipAddr, macAddress, interface);
}
