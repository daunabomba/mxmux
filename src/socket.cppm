module;

#include "exception.h"
#include "types.h"

#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

import utils;
import logger;
import event;

export module socket;

export class Socket : virtual public Runnable {
public:
    static InetDest destFromString(std::string const &where, const uint16_t port);
    static std::string findArpInterface(InetDest const &ipv4Dest);
    static bool sendWolPacket(std::string const &interface, MacAddress const &macaddr, uint16_t const port);

protected:
    enum SockType { ARP = 1, UDP = 2, TCP = 4 };

    explicit Socket(SockType const newType);
    virtual ~Socket() = 0;
    void makeNonBlocking() const;
    void makeTransparent(InetDest const &fauxBind) const;
    void keepAlive() const;
    void onReadComplete();
    void onWriteComplete();
    void reuseAddress() const;
    ssize_t read(Bytes &data) const;
    ssize_t write(Bytes const &data) const;
    void bind(uint16_t const port) const;
    void bind(std::string const &ifName) const;
    int connect(InetDest const &whereTo) const;
    void listen() const;
    int accept() const;
    InetDest originSource() const;
    InetDest originalDestination() const;
    int receiveDatagram(InetDest &whereFrom, Bytes &data) const;
    int sendDatagram(InetDest const &whereTo, Bytes const &data) const;
    bool receiveArp(IpAddr &ipAddr, MacAddress &macAddress) const;

protected:
    virtual void handleError() override;
    virtual void handleRead() override;
    virtual void handleWrite() override;
    virtual bool hasPolledOut() const override;
    virtual void preShutdown() override;

    bool hasRead() const override { return true; }
    virtual int getLastError() const override;
    static int createSocket(SockType const type);

private:
    int convertFromStdError(ssize_t const error) const;

protected:
    SockType const type;

private:
    const int LISTEN_MAX_PENDING = 256;
};

static constexpr int SO_ORIGINAL_DST = 80;

Socket::Socket(SockType const newType) : type(newType) { makeNonBlocking(); }

Socket::~Socket() {}

bool Socket::hasPolledOut() const { return false; }

void Socket::handleRead() { assert(false && "Socket::handleRead()"); }

void Socket::handleWrite() { assert(false && "Socket::handleWrite()"); }

void Socket::handleError() { assert(false && "Socket::handleError()"); }

void Socket::preShutdown() {}

void Socket::makeNonBlocking() const {
    auto flags = ::fcntl(fd, F_GETFL, 0);
    pErrorThrow("makeNonBlocking flags", flags, fd);
    flags |= O_NONBLOCK;
    pErrorThrow("makeNonBlocking fnctl", ::fcntl(fd, F_SETFL, flags), fd);
}

int Socket::createSocket(Socket::SockType const type) {
    int fd = -1;
    switch (type) {
    case ARP:
        fd = ::socket(AF_PACKET, SOCK_RAW | SOCK_NONBLOCK, htons(ETH_P_ARP));
        break;
    case UDP:
        fd = ::socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        break;
    case TCP:
        fd = ::socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
        break;
    }
    throwIf(fd < 0, StringException(std::string("createSocket failed")));
    int on = 1;
    switch (type) {
    case ARP:
        break;
    case UDP:
        pErrorLog("Socket::createSocket UDP", ::setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on)), fd);
        break;
    case TCP:
        pErrorLog("Socket::createSocket TCP", ::setsockopt(fd, SOL_TCP, TCP_NODELAY, &on, sizeof(on)), fd);
        break;
    }
    return fd;
}

void Socket::reuseAddress() const {
    const int yes = 1;
    pErrorThrow("reuseAddress", ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes), fd);
}

int Socket::convertFromStdError(ssize_t const error) const {
    if (error >= 0) {
        return static_cast<int>(error);
    } else if (error == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS)) {
        logDebug("Socket::convertFromStdError " + std::to_string(errno));
        return -1;
    } else {
        return -2;
    }
}

ssize_t Socket::read(Bytes &data) const {
    auto numRead = ::read(fd, &data[0], data.size());
    if (numRead >= 0 && data.size() > static_cast<decltype(data.size())>(numRead)) {
        data.resize(static_cast<decltype(data.size())>(numRead));
    }
    return convertFromStdError(numRead);
}

ssize_t Socket::write(Bytes const &data) const { return convertFromStdError(::write(fd, &data[0], data.size())); }

void Socket::listen() const {
    logDebug("Socket::listen " + std::to_string(fd));
    pErrorThrow("listen", ::listen(fd, LISTEN_MAX_PENDING), fd);
}

int Socket::accept() const {
    SocketAddress addr;
    socklen_t addrLen = sizeof addr.addrV6;
    ssize_t aFd = ::accept(fd, &addr.addr, &addrLen);
    return convertFromStdError(aFd);
}

int Socket::receiveDatagram(InetDest &whereFrom, Bytes &data) const {
    struct iovec iovec[]{{&data[0], data.size()}};
    uint8_t msgHeader[1024];
    SocketAddress addr;
    struct msghdr msg{&addr,
                      sizeof(addr),
                      &iovec[0],
                      sizeof(iovec) / sizeof(iovec[0]),
                      &msgHeader[0],
                      sizeof(msgHeader) / sizeof(msgHeader[0]),
                      0};
    const auto numReceived = ::recvmsg(fd, &msg, 0);
    if (numReceived >= 0 && (data.size() > static_cast<decltype(data.size())>(numReceived))) {
        data.resize(static_cast<decltype(data.size())>(numReceived));
    }
    pErrorLog("Socket::receiveDatagram", static_cast<int>(numReceived), fd);

    struct cmsghdr *cmsg;
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr && cmsg->cmsg_level >= 0; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO) {
            // assert(false);
            // auto const &pktInfo = *reinterpret_cast<struct in6_pktinfo *>(CMSG_DATA(cmsg));
            // InetDest x;
            // x.addr.setFromNetwork(pktInfo.ipi6_addr);
            // x.ifIndex = pktInfo.ipi6_ifindex;
        }
    }

    whereFrom.addr.setFromNetwork(addr.addrV6.sin6_addr);
    whereFrom.port = addr.addrV6.sin6_port;
    return static_cast<int>(numReceived);
}

int Socket::sendDatagram(const InetDest &whereTo, const Bytes &data) const {
    SocketAddress addr;
    addr.addrV6.sin6_family = AF_INET6;
    addr.addrV6.sin6_port = whereTo.port;
    std::memcpy(&addr.addrV6.sin6_addr, whereTo.addr.get(), sizeof addr.addrV6.sin6_addr);
    const auto numSent = ::sendto(fd, &data[0], data.size(), 0, &addr.addr, sizeof addr.addrV6);
    pErrorLog("Socket::sendDatagram", static_cast<int>(numSent), fd);
    return static_cast<int>(numSent);
}

bool Socket::receiveArp(IpAddr &ipAddr, MacAddress &macAddress) const {
    uint8_t buf[1024];

    ssize_t len = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
    if (len < static_cast<decltype(len)>(sizeof(struct ethhdr) + sizeof(struct arphdr))) {
        return false;
    }

    struct ethhdr *eth = reinterpret_cast<struct ethhdr *>(buf);
    if (ntohs(eth->h_proto) != ETH_P_ARP)
        return false;

    uint8_t *arp_ptr = buf + sizeof(struct ethhdr);
    // ARP header layout per RFC: htype(2), ptype(2), hlen(1), plen(1), oper(2)
    auto oper = fromNetworkEndian(*reinterpret_cast<std::uint16_t *>(arp_ptr + 6));
    if (oper != ARPOP_REQUEST && oper != ARPOP_REPLY) {
        return false;
    }

    auto ipv4Addr = *reinterpret_cast<in_addr_t *>(arp_ptr + 14);
    ipAddr.setIpV4(ipv4Addr);
    std::memcpy(&macAddress.mac[0], arp_ptr + 8, sizeof macAddress.mac);
    return true;
}

void Socket::bind(std::string const &ifName) const {
    // Bind to the interface to receive ARP frames only on that interface
    struct sockaddr_ll saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sll_family = AF_PACKET;
    saddr.sll_protocol = toNetworkEndian(static_cast<decltype(saddr.sll_protocol)>(ETH_P_ARP));
    saddr.sll_ifindex = static_cast<decltype(saddr.sll_ifindex)>(if_nametoindex(ifName.c_str()));
    throwIf(saddr.sll_ifindex == 0, "Interface " + ifName + " not found.");
    pErrorThrow("bind interface", ::bind(fd, reinterpret_cast<struct sockaddr *>(&saddr), sizeof(saddr)));
}

void Socket::bind(uint16_t const port) const {
    makeNonBlocking();
    SocketAddress addr;
    addr.addrV6.sin6_family = AF_INET6;
    addr.addrV6.sin6_port = toNetworkEndian(port);
    addr.addrV6.sin6_addr = IN6ADDR_ANY_INIT;
    logError("Socket::bind " + std::to_string(fd) + " to port " + std::to_string(port));
    pErrorThrow("bind", ::bind(fd, &addr.addr, sizeof addr.addrV6), fd);
}

int Socket::connect(InetDest const &whereTo) const {
    makeNonBlocking();
    SocketAddress addr{};
    addr.addrV6.sin6_family = AF_INET6;
    addr.addrV6.sin6_port = whereTo.port;
    std::memcpy(&addr.addrV6.sin6_addr, whereTo.addr.get(), sizeof addr.addrV6.sin6_addr);
    logError("Socket::connect " + whereTo.to_string());
    char ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &addr.addrV6.sin6_addr, ip_str, sizeof ip_str);

    logDebug("len " + std::to_string(sizeof addr.addrV6.sin6_addr) + " " + std::string(ip_str));
    return convertFromStdError(::connect(fd, &addr.addr, sizeof addr.addrV6));
}

int Socket::getLastError() const {
    socklen_t len = sizeof(errno);
    auto ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &errno, &len);
    pErrorLog("Socket::getLastError", errno, fd);
    return ret;
}

InetDest Socket::destFromString(std::string const &where, uint16_t const port) {
    InetDest dest;
    dest.port = toNetworkEndian(port);
    dest.valid = false;
    SocketAddress addr;
    if (inet_pton(AF_INET, where.c_str(), &addr.addrV4.sin_addr) == 1) {
        dest.addr.setIpV4(addr.addrV4.sin_addr.s_addr);
        dest.valid = true;
    } else if (inet_pton(AF_INET6, where.c_str(), &addr) == 1) {
        dest.addr.setFromNetwork(addr.addrV6.sin6_addr);
        dest.valid = true;
    }
    return dest;
}

// Finds an ipv4 address that is on the same broadcast network
std::string Socket::findArpInterface(InetDest const &ipv4Dest) {
    in_addr_t input_ip = ipv4Dest.addr.getIpV4();
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        return "";
    }

    bool found = false;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            uint32_t if_ip = fromNetworkEndian(reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr)->sin_addr.s_addr);
            uint32_t if_netmask =
                fromNetworkEndian(reinterpret_cast<struct sockaddr_in *>(ifa->ifa_netmask)->sin_addr.s_addr);

            uint32_t if_network = if_ip & if_netmask;
            uint32_t input_network = input_ip & if_netmask;

            if (if_network == input_network) {
                found = true;
                break;
            }
        }
    }
    if (found) {
        return ifa->ifa_name;
    } else {
        return "";
    }
}

void Socket::keepAlive() const {
    int const on = 1;
    pErrorLog("keepAlive", setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)), fd);
}

void Socket::makeTransparent(InetDest const &fauxBind) const {
    int const on = 1;
    pErrorLog("maketransparent", setsockopt(fd, SOL_IP, IP_TRANSPARENT, &on, sizeof(on)), fd);
    SocketAddress addr;
    addr.addrV6.sin6_family = AF_INET6;
    addr.addrV6.sin6_port = fauxBind.port;
    std::memcpy(&addr.addrV6.sin6_addr, fauxBind.addr.get(), sizeof addr.addrV6.sin6_addr);
    pErrorLog("bind", ::bind(fd, &addr.addr, sizeof addr.addrV6), fd);
}

InetDest Socket::originSource() const {
    assert(type == TCP && "Only valid for TCP or connected UDP");
    InetDest from;
    SocketAddress addr;
    socklen_t addrLen = sizeof(addr.addrV6);
    auto ret = getpeername(fd, &addr.addr, &addrLen);
    if (ret == 0) {
        assert(addr.addrV6.sin6_family == AF_INET6 && "Wrong family");
        from.addr.setFromNetwork(addr.addrV6.sin6_addr);
        from.port = addr.addrV6.sin6_port;
        from.valid = true;
    }
    return from;
}

InetDest Socket::originalDestination() const {
    assert(type == TCP && "Only valid for TCP");
    InetDest from;
    SocketAddress addr;

    socklen_t addrLen = sizeof(addr.addr);
    auto ret = ::getsockopt(fd, SOL_IP, SO_ORIGINAL_DST, &addr.addr, &addrLen);
    if (ret == 0) {
        assert(addrLen == sizeof(addr.addrV4) && "Buggy address len");
        from.addr.setIpV4(addr.addrV4.sin_addr.s_addr);
        from.port = addr.addrV4.sin_port;
        from.valid = true;
    } else {
        socklen_t addrLen6 = sizeof addr.addrV6;
        ret = ::getsockopt(fd, SOL_IPV6, SO_ORIGINAL_DST, &addr, &addrLen6);
        if (ret == 0) {
            assert(addrLen6 == sizeof(addr.addrV6) && "Buggy address len");
            from.addr.setFromNetwork(addr.addrV6.sin6_addr);
            from.port = addr.addrV6.sin6_port;
            from.valid = true;
        }
    }
    if (ret < 0) {
        pErrorLog("originalDestination", ret, fd);
        from.valid = false;
    }
    return from;
}

bool Socket::sendWolPacket(std::string const &interface, MacAddress const &macaddr, uint16_t const port) {
    auto sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        pErrorLog("sendWolPacket socket", sock, sock);
        return false;
    }

    int ret;
    if ((ret = setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, interface.c_str(),
                          static_cast<socklen_t>(interface.size()))) < 0) {
        pErrorLog("sendWolPacket SO_BINDTODEVICE", ret, sock);
        close(sock);
        return false;
    }

    int broadcast = 1;
    if ((ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast))) < 0) {
        pErrorLog("sendWolPacket SO_BROADCAST", ret, sock);
        close(sock);
        return false;
    }

    std::vector<uint8_t> packet(102, 0xFF);
    size_t mac_bytes_start = 6;

    // write the macaddress 16 times
    for (size_t i = 0; i < 16; ++i) {
        for (size_t j = 0; j < 6; ++j) {
            packet[mac_bytes_start++] = macaddr.mac[j];
        }
    }

    sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = toNetworkEndian(port);
    dest_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    ssize_t sent_bytes =
        sendto(sock, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr *>(&dest_addr), sizeof dest_addr);
    close(sock);

    return sent_bytes == static_cast<decltype(sent_bytes)>(packet.size());
}

void Socket::onReadComplete() { assert(false && "Implement onReadComplete"); }

void Socket::onWriteComplete() { assert(false && "Implement onWriteComplete"); }
