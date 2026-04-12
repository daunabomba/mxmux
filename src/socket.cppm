module;

#include "types.h"

#include <cassert>
#include <cstring>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

import utils;
import logger;
import event;

export module socket;



export class Socket : virtual public Runnable {
public:
    static InetDest destFromString(const std::string &where, const uint16_t port);

protected:
    enum SockType { UDP = 1, TCP = 2 };

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
    int connect(InetDest const &whereTo) const;
    void listen() const;
    int accept() const;
    InetDest originSource() const;
    InetDest originalDestination() const;
    int receiveDatagram(InetDest &whereFrom, Bytes &data) const;
    int sendDatagram(InetDest const &whereTo, Bytes const &data) const;

protected:
    virtual void handleError();
    virtual void handleRead();
    virtual void handleWrite();
    virtual bool waitingOutEvent();
    virtual int getLastError() const;

    static int createSocket(SockType const type);

private:
    int convertFromStdError(ssize_t const error) const;

protected:
    SockType const type;

private:
    const int LISTEN_MAX_PENDING = 256;
};


static constexpr int SO_ORIGINAL_DST = 80;

Socket::Socket(SockType const newType) : type(newType) {
    makeNonBlocking();
}

Socket::~Socket() {
    logDebug("Socket::~Socket " + std::to_string(fd));
}

bool Socket::waitingOutEvent() { return false; }

void Socket::handleRead() { assert(false && "Socket::handleRead()"); }

void Socket::handleWrite() { assert(false && "Socket::handleWrite()"); }

void Socket::handleError() { assert(false && "Socket::handleError()"); }

void Socket::makeNonBlocking() const {
    auto flags = ::fcntl(fd, F_GETFL, 0);
    pErrorThrow("makeNonBlocking flags", flags, fd);
    flags |= O_NONBLOCK;
    pErrorThrow("makeNonBlocking fnctl", ::fcntl(fd, F_SETFL, flags), fd);
}

int Socket::createSocket(Socket::SockType const type) {
    int fd = ::socket(AF_INET6, type == UDP ? SOCK_DGRAM : SOCK_STREAM | SOCK_NONBLOCK, 0);
    pErrorThrow("createSocket", fd, fd);
    int on = 1;
    switch (type) {
    case TCP:
        pErrorLog("Socket::createSocket TCP", ::setsockopt(fd, SOL_TCP, TCP_NODELAY, &on, sizeof(on)), fd);
        break;
    case UDP:
        pErrorLog("Socket::createSocket UDP", ::setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on)), fd);
        //                       pErrorLog(::setsockopt(fd, SOL_TCP,
        //                       SO_ORIGINAL_DST, &on, sizeof(on)), fd);
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
    struct msghdr msg = {
        .msg_name = &addr,
        .msg_namelen = sizeof(addr),
        .msg_iov = &iovec[0],
        .msg_iovlen = sizeof(iovec) / sizeof(iovec[0]),
        .msg_control = &msgHeader[0],
        .msg_controllen = sizeof(msgHeader) / sizeof(msgHeader[0]),
        .msg_flags = 0
    };
    const auto numReceived = ::recvmsg(fd, &msg, 0);
    if (numReceived >= 0 && (data.size() > static_cast<decltype(data.size())>(numReceived))) {
        data.resize(static_cast<decltype(data.size())>(numReceived));
    }
    pErrorLog("Socket::receiveDatagram", static_cast<int>(numReceived), fd);

    struct cmsghdr *cmsg;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr && cmsg->cmsg_level >= 0; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_PKTINFO) {
            // assert(false);
            // auto const &pktInfo = *reinterpret_cast<struct in6_pktinfo *>(CMSG_DATA(cmsg));
            // InetDest x;
            // x.addr.setFromNetwork(pktInfo.ipi6_addr);
            // x.ifIndex = pktInfo.ipi6_ifindex;
        }
    }
#pragma clang diagnostic pop

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

void Socket::bind(uint16_t const port) const {
    makeNonBlocking();
    SocketAddress addr;
    addr.addrV6.sin6_family = AF_INET6;
    addr.addrV6.sin6_port = networkEndian(port);
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

InetDest Socket::destFromString(const std::string &where, uint16_t const port) {
    InetDest dest;
    dest.port = networkEndian(port);
    dest.valid = false;
    SocketAddress addr;
    if (inet_pton(AF_INET, where.c_str(), &addr.addrV4.sin_addr) == 1) {
        dest.addr.setIpV4(addr.addrV4.sin_addr.s_addr);
        dest.valid = true;
    } else if(inet_pton(AF_INET6, where.c_str(), &addr) == 1) {
        dest.addr.setFromNetwork(addr.addrV6.sin6_addr);
        dest.valid = true;
    }
    return dest;
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

void Socket::onReadComplete() { assert(false && "Implement onReadComplete"); }

void Socket::onWriteComplete() { assert(false && "Implement onWriteComplete"); }
