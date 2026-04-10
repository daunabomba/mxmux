#pragma once
#include "endians.h"

#include <array>
#include <algorithm>
#include <cstring>
#include <linux/input.h>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <vector>
#include <netinet/in.h>
#include <arpa/inet.h>

using linux_input_event = struct input_event;
union SocketAddress {
    struct sockaddr_in6 addrV6;
    struct sockaddr_in addrV4;
    struct sockaddr addr;
};


using byte = std::uint8_t;
using Bytes = std::vector<byte>;

constexpr ssize_t MAX_PACKET_SIZE = 4096;
constexpr size_t NUM_BITS_PER_OCTET = 8;

// Network order IpAddr
struct IpAddr {
    static constexpr ssize_t IP_ADDRESS_BIT_LEN = 128;
    static constexpr ssize_t IPV4_ADDRESS_BIT_LEN = 32;

    static constexpr size_t ADDR_LEN_IPV6 = IP_ADDRESS_BIT_LEN / NUM_BITS_PER_OCTET;
    static constexpr size_t ADDR_LEN_IPV4_PREFIX = ADDR_LEN_IPV6 - IPV4_ADDRESS_BIT_LEN / NUM_BITS_PER_OCTET;
    IpAddr() : d8{} {}
    IpAddr(const IpAddr &other) = default;

    // using octets as this class is network endian
    std::array<std::uint8_t, ADDR_LEN_IPV4_PREFIX> const ip4Prefix{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff}};

    IpAddr &operator=(IpAddr const &rhs) {
        d8 = rhs.d8;
        return *this;
    }

    bool operator==(IpAddr const &rhs) const { return d8 == rhs.d8; }

    void setIpV4(in_addr_t& addr) {
        std::copy(ip4Prefix.begin(), ip4Prefix.end(), d8.begin());
        auto &d32Addr = *reinterpret_cast<uint32_t*>(&d8[ADDR_LEN_IPV4_PREFIX]);
        d32Addr = addr;
    };

    std::uint8_t const* get() const { return &d8[0]; };

    bool isIpv4Addr() const { return std::equal(d8.begin(), d8.begin() + ip4Prefix.size(), ip4Prefix.begin()); }

    void setFromNetwork(struct in6_addr& addr) {
        std::memcpy(&d8[0], &addr, d8.size());
    }

    std::string to_string() const {
        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &d8[0], ip_str, sizeof ip_str);
        if(isIpv4Addr()) {
            return   std::string(ip_str + strlen("::ffff:"));

        } else {
        return   std::string(ip_str);
        }
    }

private:
    std::array<std::uint8_t, ADDR_LEN_IPV6> d8;
};

struct InetDest {
    InetDest() : valid(false), port(0), ifIndex(0) {}
    InetDest(const InetDest &other) = default;
    std::string to_string() const { return addr.to_string() + ":" + std::to_string(networkEndian(port)); }
    InetDest &operator=(InetDest const &rhs) {
        addr = rhs.addr;
        valid = rhs.valid;
        port = rhs.port;
        ifIndex = rhs.ifIndex;
        return *this;
    }
    bool operator==(InetDest const &rhs) const { return port == rhs.port && addr == rhs.addr; }

    IpAddr addr;
    bool valid;

    std::uint16_t port;
    unsigned int ifIndex;
};
