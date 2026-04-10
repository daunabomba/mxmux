#pragma once
#include <bit>
#include <cstdint>
#include <vector>


constexpr inline bool is_little_endian() {
    return std::endian::native == std::endian::little;
}

inline std::uint16_t networkEndian(const std::uint16_t d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::uint32_t networkEndian(const std::uint32_t d0) {
    if constexpr (is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}

inline std::uint64_t networkEndian(const std::uint64_t d0) {
    if constexpr(is_little_endian()) {
        return std::byteswap(d0);
    } else {
        return d0;
    }
}


