#pragma once

#include <arpa/inet.h>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <linux/input.h>
#include <netinet/in.h>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using linux_input_event = struct input_event;
union SocketAddress {
    struct sockaddr_in6 addrV6;
    struct sockaddr_in addrV4;
    struct sockaddr addr;
};

using byte = std::uint8_t;
using Bytes = std::vector<byte>;

constexpr std::size_t MAX_PACKET_SIZE = 4096;
constexpr std::size_t NUM_BITS_PER_OCTET = 8;

constexpr inline bool is_little_endian() { return std::endian::native == std::endian::little; }

inline std::uint16_t toNetworkEndian(std::uint16_t const d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::uint32_t toNetworkEndian(std::uint32_t const d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::int32_t toNetworkEndian(std::int32_t const d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::uint64_t toNetworkEndian(std::uint64_t const d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::uint16_t fromNetworkEndian(std::uint16_t const d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::uint32_t fromNetworkEndian(std::uint32_t const d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::int32_t fromNetworkEndian(std::int32_t const d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::uint64_t fromNetworkEndian(std::uint64_t const d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::uint64_t toU64FromArray(Bytes const &b, size_t const &startOffset) {
    std::uint64_t d0 = (static_cast<std::uint64_t>(b[startOffset + 0]) << 56) |
                       (static_cast<std::uint64_t>(b[startOffset + 1]) << 48) |
                       (static_cast<std::uint64_t>(b[startOffset + 2]) << 40) |
                       (static_cast<std::uint64_t>(b[startOffset + 3]) << 32) |
                       (static_cast<std::uint64_t>(b[startOffset + 4]) << 24) |
                       (static_cast<std::uint64_t>(b[startOffset + 5]) << 16) |
                       (static_cast<std::uint64_t>(b[startOffset + 6]) << 8) |
                       (static_cast<std::uint64_t>(b[startOffset + 7]) << 0);

    return d0;
}

inline void toArray(std::uint64_t const value, Bytes &b, size_t const &startOffset) {
    b[startOffset + 0] = static_cast<std::uint8_t>(value >> 56);
    b[startOffset + 1] = static_cast<std::uint8_t>(value >> 48);
    b[startOffset + 2] = static_cast<std::uint8_t>(value >> 40);
    b[startOffset + 3] = static_cast<std::uint8_t>(value >> 32);
    b[startOffset + 4] = static_cast<std::uint8_t>(value >> 24);
    b[startOffset + 5] = static_cast<std::uint8_t>(value >> 16);
    b[startOffset + 6] = static_cast<std::uint8_t>(value >> 8);
    b[startOffset + 7] = static_cast<std::uint8_t>(value >> 0);
}

inline std::uint32_t toU32FromArray(Bytes const &b, size_t const &startOffset) {
    std::uint32_t d0 = (static_cast<std::uint32_t>(b[startOffset + 0]) << 24) |
                       (static_cast<std::uint32_t>(b[startOffset + 1]) << 16) |
                       (static_cast<std::uint32_t>(b[startOffset + 2]) << 8) |
                       (static_cast<std::uint32_t>(b[startOffset + 3]) << 0);
    return d0;
}

inline void toArray(std::uint32_t const value, Bytes &b, size_t const &startOffset) {
    b[startOffset + 0] = static_cast<std::uint8_t>(value >> 24);
    b[startOffset + 1] = static_cast<std::uint8_t>(value >> 16);
    b[startOffset + 2] = static_cast<std::uint8_t>(value >> 8);
    b[startOffset + 3] = static_cast<std::uint8_t>(value >> 0);
}

template <typename T>
inline std::optional<T> extractNetOrderValueFromBytesAndUpdatePos(Bytes const &b, size_t &startPos) {
    static_assert(std::is_trivially_copyable<T>::value, "Type must be trivially copyable");
    auto const size_of_t = sizeof(T);
    if (startPos + size_of_t <= b.size()) {
        T value = 0;
        std::memcpy(&value, b.data() + startPos, size_of_t);
        startPos += size_of_t;
        return toNetworkEndian(value);
    } else {
        return std::nullopt;
    }
}

template <typename T, typename Bytes> void appendNetworkEndianToPacket(T const &value, Bytes &packet) {
    static_assert(std::is_trivially_copyable<T>::value, "Type must be trivially copyable");

    auto netOrder = toNetworkEndian(value);
    auto ptr = reinterpret_cast<const byte *>(&netOrder);
    packet.insert(packet.end(), ptr, ptr + sizeof(netOrder));
}

// Network order IpAddr
struct IpAddr {
    static constexpr std::size_t IP_ADDRESS_BIT_LEN = 128;
    static constexpr std::size_t IPV4_ADDRESS_BIT_LEN = 32;

    static constexpr std::size_t ADDR_LEN_IPV6 = IP_ADDRESS_BIT_LEN / NUM_BITS_PER_OCTET;
    static constexpr std::size_t ADDR_LEN_IPV4_PREFIX = ADDR_LEN_IPV6 - IPV4_ADDRESS_BIT_LEN / NUM_BITS_PER_OCTET;
    IpAddr() : d8{} {}
    IpAddr(const IpAddr &other) = default;

    // using octets as this class is network endian
    std::array<std::uint8_t, ADDR_LEN_IPV4_PREFIX> const ip4Prefix{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff}};

    IpAddr &operator=(IpAddr const &rhs) {
        d8 = rhs.d8;
        return *this;
    }

    bool operator==(IpAddr const &rhs) const { return d8 == rhs.d8; }

    void setIpV4(in_addr_t &addr) {
        std::memcpy(d8.data(), ip4Prefix.data(), ip4Prefix.size());
        auto &d32Addr = *reinterpret_cast<in_addr_t *>(&d8[ADDR_LEN_IPV4_PREFIX]);
        d32Addr = addr;
    };

    // How is this used and why is this network endian?
    in_addr_t getIpV4() const {
        assert(isIpv4Addr() && "Not a valid IPv4 address");
        return fromNetworkEndian(*reinterpret_cast<in_addr_t const *>(&d8[ADDR_LEN_IPV4_PREFIX]));
    }

    std::uint8_t const *get() const { return &d8[0]; };

    bool isIpv4Addr() const { return std::equal(d8.begin(), d8.begin() + ip4Prefix.size(), ip4Prefix.begin()); }

    void setFromNetwork(struct in6_addr &addr) { std::memcpy(&d8[0], &addr, d8.size()); }

    std::string to_string() const {
        char ip_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &d8[0], ip_str, sizeof ip_str);
        if (isIpv4Addr()) {
            return std::string(ip_str + strlen("::ffff:"));

        } else {
            return std::string(ip_str);
        }
    }

    std::array<byte, ADDR_LEN_IPV6> d8{};
};

struct IpAddrHash {

    std::size_t operator()(const IpAddr &ip) const noexcept {
        const std::size_t prime = 0x9e3779b9; // hash magic constant
        std::size_t result = 0;
        for (auto b : ip.d8) {
            result ^= std::hash<uint8_t>{}(b) + prime + (result << 6) + (result >> 2);
        }
        return result;
    }
};

struct InetDest {
    InetDest() : valid(false), port(0), ifIndex(0) {}
    InetDest(InetDest const &other) = default;
    std::string to_string() const { return addr.to_string() + ":" + std::to_string(fromNetworkEndian(port)); }
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

struct InetDestHash {
    std::size_t hashAddrVector(std::array<byte, IpAddr::ADDR_LEN_IPV6> const &v) const {
        std::size_t hash = 0;
        for (auto const b : v) {
            hash ^= std::hash<byte>{}(b) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
    std::size_t operator()(const InetDest &id) const {
        std::size_t h1 = hashAddrVector(id.addr.d8);
        std::size_t h2 = std::hash<std::uint16_t>{}(id.port);
        // combine with bit manipulation for better distribution
        return h1 ^ (h2 << 1);
    }
};

struct MacAddress {
    static constexpr std::size_t MAC_ADDR_LEN = 6;

    std::string to_string() const {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (int i = 0; i < 6; ++i) {
            ss << std::setw(2) << static_cast<int>(mac[i]);
            if (i != 5)
                ss << ":";
        }
        return ss.str();
    }

    std::uint8_t mac[MAC_ADDR_LEN];
};
