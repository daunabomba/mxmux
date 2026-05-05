module;
#include "types.h"
#include <string>
#include <vector>

export module dnspacket;

import logger;

export class DnsPacket {
public:
    DnsPacket(Bytes const &data) : data_(data) {}

    bool isQuery() const {
        if (data_.size() < 12)
            return false;
        // Flags are at bytes 2 and 3. QR bit is the most significant bit of byte 2.
        return (data_[2] & 0x80) == 0;
    }

    enum class QueryType { A = 1, UNKNOWN = 0 };

    void decode() {
        if (!isQuery()) {
            return;
        }
        if (data_.size() < 12) {
            return;
        }

        uint16_t qdcount = static_cast<uint16_t>((static_cast<uint16_t>(data_[4]) << 8) | data_[5]);
        if (qdcount == 0) {
            return;
        }

        // Skip header (12 bytes)
        size_t pos = 12;

        // Skip QNAME
        while (pos < data_.size()) {
            uint8_t len = data_[pos];
            if (len == 0) {
                pos++;
                break;
            }
            if ((len & 0xC0) == 0xC0) {
                // Compression pointer - skip 2 bytes
                pos += 2;
                break;
            }
            pos += 1 + len;
        }

        if (pos + 4 > data_.size()) {
            return;
        }
        uint16_t qtype = static_cast<uint16_t>((static_cast<uint16_t>(data_[pos]) << 8) | data_[pos + 1]);

        if (qtype == static_cast<uint16_t>(QueryType::A)) {
            logDebug("DNS Query Type A detected");
        } else {
            logDebug("DNS Query Type " + std::to_string(qtype) + " detected");
        }
    }

private:
    Bytes const &data_;
};
