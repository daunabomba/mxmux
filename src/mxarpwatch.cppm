module;

#include "types.h"

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    MxArpWatch(ArpEntries &arpCache, std::string const &ifName, std::vector<IpAddr> const &ips = {});
    MxArpWatch(ArpEntries &arpCache, std::string const &ifName, IpAddr const &ipAddr);

    void addWatchIp(IpAddr const &ipAddr);
    bool isWatching(IpAddr const &ipAddr) const;
    void scanExistingEntries();
    void update(IpAddr const &ipAddr, MacAddress const &macAddress) override;

private:
    void parseNeigh(const nlmsghdr *h, unsigned int targetIfIndex);

    ArpEntries &macCache;
    std::string interface;
    std::unordered_set<IpAddr, IpAddrHash> watchedIps;
    mutable std::mutex watchLock;
};

MxArpWatch::MxArpWatch(ArpEntries &arpCache) : macCache(arpCache) {}

MxArpWatch::MxArpWatch(ArpEntries &arpCache, std::string const &ifName, std::vector<IpAddr> const &ips)
    : macCache(arpCache), interface(ifName) {
    for (auto const &ip : ips) {
        watchedIps.insert(ip);
    }
    scanExistingEntries();
}

MxArpWatch::MxArpWatch(ArpEntries &arpCache, std::string const &ifName, IpAddr const &ipAddr)
    : macCache(arpCache), interface(ifName) {
    watchedIps.insert(ipAddr);
    scanExistingEntries();
}

void MxArpWatch::addWatchIp(IpAddr const &ipAddr) {
    bool isNew = false;
    {
        std::lock_guard<std::mutex> sync(watchLock);
        isNew = watchedIps.insert(ipAddr).second;
    }
    if (isNew) {
        scanExistingEntries();
    }
}

bool MxArpWatch::isWatching(IpAddr const &ipAddr) const {
    std::lock_guard<std::mutex> sync(watchLock);
    if (watchedIps.empty()) {
        return true;
    }
    return watchedIps.contains(ipAddr);
}

void MxArpWatch::scanExistingEntries() {
    std::string ifName = interface;
    if (ifName.empty()) {
        auto ref = arpSocket.lock();
        if (ref) {
            ifName = ref->getInterface();
            interface = ifName;
        }
    }

    unsigned int targetIfIndex = 0;
    if (!ifName.empty()) {
        targetIfIndex = if_nametoindex(ifName.c_str());
        if (targetIfIndex == 0) {
            logWarn("MxArpWatch: Interface " + ifName + " not found via if_nametoindex");
            return;
        }
    }

    int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (fd < 0) {
        logError("MxArpWatch: Failed to open netlink socket: " + std::string(strerror(errno)));
        return;
    }

    struct timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_nl local{};
    local.nl_family = AF_NETLINK;
    if (::bind(fd, reinterpret_cast<struct sockaddr *>(&local), sizeof(local)) < 0) {
        logError("MxArpWatch: Failed to bind netlink socket: " + std::string(strerror(errno)));
        ::close(fd);
        return;
    }

    struct {
        struct nlmsghdr nlh;
        struct ndmsg ndm;
    } req{};

    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ndmsg));
    req.nlh.nlmsg_type = RTM_GETNEIGH;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 1;
    req.ndm.ndm_family = AF_UNSPEC;

    struct sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    if (::sendto(fd, &req, req.nlh.nlmsg_len, 0,
                 reinterpret_cast<struct sockaddr *>(&kernel), sizeof(kernel)) < 0) {
        logError("MxArpWatch: Failed to send netlink request: " + std::string(strerror(errno)));
        ::close(fd);
        return;
    }

    alignas(struct nlmsghdr) char buf[32768];
    bool done = false;
    while (!done) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            logError("MxArpWatch: Failed to recv from netlink socket: " + std::string(strerror(errno)));
            break;
        }
        if (n == 0) {
            break;
        }

        auto msg_len = static_cast<unsigned int>(n);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
        for (auto *h = reinterpret_cast<struct nlmsghdr *>(buf);
             NLMSG_OK(h, msg_len);
             h = NLMSG_NEXT(h, msg_len)) {
            if (h->nlmsg_type == NLMSG_DONE) {
                done = true;
                break;
            }
            if (h->nlmsg_type == NLMSG_ERROR) {
                logError("MxArpWatch: Netlink returned error message");
                done = true;
                break;
            }
            if (h->nlmsg_type == RTM_NEWNEIGH) {
                parseNeigh(h, targetIfIndex);
            }
        }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }
    ::close(fd);
}

void MxArpWatch::parseNeigh(const nlmsghdr *h, unsigned int targetIfIndex) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
    auto const *ndm = reinterpret_cast<const struct ndmsg *>(NLMSG_DATA(h));
    if (ndm->ndm_state & (NUD_FAILED | NUD_INCOMPLETE)) {
        return;
    }
    if (targetIfIndex != 0 && static_cast<unsigned int>(ndm->ndm_ifindex) != targetIfIndex) {
        return;
    }

    if (h->nlmsg_len < NLMSG_LENGTH(sizeof(struct ndmsg))) {
        return;
    }

    int len = static_cast<int>(h->nlmsg_len - NLMSG_LENGTH(sizeof(struct ndmsg)));
    auto *a = reinterpret_cast<struct rtattr *>(
        reinterpret_cast<char *>(const_cast<struct ndmsg *>(ndm)) + NLMSG_ALIGN(sizeof(struct ndmsg)));

    const void *ip = nullptr;
    const void *mac = nullptr;
    int ip_len = 0;
    int mac_len = 0;

    for (; RTA_OK(a, len); a = RTA_NEXT(a, len)) {
        if (a->rta_type == NDA_DST) {
            ip = RTA_DATA(a);
            ip_len = RTA_PAYLOAD(a);
        } else if (a->rta_type == NDA_LLADDR) {
            mac = RTA_DATA(a);
            mac_len = RTA_PAYLOAD(a);
        }
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    if (!ip || !mac || mac_len != static_cast<int>(MacAddress::MAC_ADDR_LEN)) {
        return;
    }

    bool allZero = true;
    for (int i = 0; i < static_cast<int>(MacAddress::MAC_ADDR_LEN); ++i) {
        if (static_cast<const uint8_t *>(mac)[i] != 0) {
            allZero = false;
            break;
        }
    }
    if (allZero) {
        return;
    }

    IpAddr parsedIp;
    if (ndm->ndm_family == AF_INET && ip_len == static_cast<int>(sizeof(in_addr_t))) {
        in_addr_t v4 = *reinterpret_cast<const in_addr_t *>(ip);
        parsedIp.setIpV4(v4);
    } else if (ndm->ndm_family == AF_INET6 && ip_len == static_cast<int>(sizeof(struct in6_addr))) {
        struct in6_addr v6{};
        std::memcpy(&v6, ip, sizeof(v6));
        parsedIp.setFromNetwork(v6);
    } else {
        return;
    }

    {
        std::lock_guard<std::mutex> sync(watchLock);
        if (!watchedIps.empty() && !watchedIps.contains(parsedIp)) {
            return;
        }
    }

    MacAddress parsedMac{};
    std::memcpy(parsedMac.mac, mac, sizeof(parsedMac.mac));

    std::string ifName = interface;
    if (ifName.empty()) {
        char ifname_buf[IF_NAMESIZE]{};
        if (if_indextoname(static_cast<unsigned int>(ndm->ndm_ifindex), ifname_buf)) {
            ifName = ifname_buf;
        }
    }

    logDebug("MxArpWatch found existing ARP entry: IP " + parsedIp.to_string() + " MAC " + parsedMac.to_string() + " on " + ifName);
    macCache.processNew(parsedIp, parsedMac, ifName);
}

void MxArpWatch::update(IpAddr const &ipAddr, MacAddress const &macAddress) {
    if (!isWatching(ipAddr)) {
        return;
    }
    auto ref = arpSocket.lock();
    std::string ifName = interface;
    if (ref) {
        ifName = ref->getInterface();
    }
    macCache.processNew(ipAddr, macAddress, ifName);
}
