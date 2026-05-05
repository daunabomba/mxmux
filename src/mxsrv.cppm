module;
#include "exception.h"
#include "ocb.h"
#include "types.h"

#include <map>
#include <memory>
#include <mutex>
#include <algorithm>
#include <unordered_map>

import logger;
import socket;
import udpsocket;
import evdev;
import arpsocket;

export module mxsrv;
import mxsrv.mxremote;
import mxsrv.mxarpwatch;

export constexpr std::uint16_t MXEV_PORT =
#if defined(MXEVPORT)
    MXEVPORT
#else
    3344
#endif
    ;

export class MxSrvIf : public std::enable_shared_from_this<MxSrvIf>, public MxRemoteIf {
public:
    virtual ~MxSrvIf() = default;
    virtual void sendToRemote(RouterIf::DeviceType const type, std::uint16_t const dest, linux_input_event const ev[],
                              std::size_t const count) = 0;
    virtual void addRemote(std::size_t const index, std::uint16_t const remoteIndex, InetDest const &addr) = 0;
    virtual void wakeUpRemote(std::size_t const index) = 0;

protected:
    enum PacketType : std::uint32_t { RouterEvent = 0xdeadbeef, RouterControl = 0xfeedbeef };
};

export class MxSrv : public UdpSocketIf, public MxSrvIf {
public:
    MxSrv() = delete;
    MxSrv(std::shared_ptr<RouterIf> const &rt, std::array<byte, 32> const &key);
    virtual ~MxSrv();

public:
    // from MxSrvIf
    void sendToRemote(RouterIf::DeviceType const type, std::uint16_t const dest, linux_input_event const ev[],
                      std::size_t const count) override;
    void addRemote(std::size_t const index, std::uint16_t const remoteIndex, InetDest const &addr) override;
    void wakeUpRemote(std::size_t const index) override;
    void timedoutRemote(MxRemote const *remote) override;

private:
    // from UdpSocketIf
    void connected(InetDest const &) override;
    void disconnected() override;
    void received(InetDest const &, Bytes const &) override;
    void notSent(InetDest const &, Bytes const &) override;
    void writeComplete() override;
    void disconnect() override;

private:
    union DestType {
        uint32_t d32;
        struct {
            std::uint16_t dest;
            std::uint8_t type;
            std::uint8_t count;
        } vals;
    };
    union EventTypeCode {
        std::uint32_t d32;
        struct {
            std::uint16_t type;
            std::uint16_t code;
        } vals;
    };

    std::weak_ptr<RouterIf> router;
    std::unordered_map<InetDest, std::shared_ptr<MxRemote>, InetDestHash> inRemotes;
    std::map<std::size_t, std::shared_ptr<MxRemote>> outRemotes;
    std::unordered_map<std::string, std::shared_ptr<MxArpWatch>> arpWatchers;
    ArpEntries arpCache;
    bool arpWatchFailed = false;

    std::array<byte, 32> ocbKey{};
};

MxSrv::MxSrv(std::shared_ptr<RouterIf> const &rt, std::array<byte, 32> const &key) : router(rt), ocbKey(key) {}

MxSrv::~MxSrv() {}

void MxSrv::wakeUpRemote(std::size_t const index) {
    auto const &remoteAddr = outRemotes[index]->getDestIpAddr();

    auto iFaceAndmac = arpCache.getMacaddressAndInterface(remoteAddr);
    if (iFaceAndmac) {
        auto const &remotePort = fromNetworkEndian(outRemotes[index]->getDestPort());
        auto const &macAddress = std::get<0>(*iFaceAndmac);
        auto const &iFaceName = std::get<1>(*iFaceAndmac);
        logError("Entry for " + remoteAddr.to_string() + " to port " + std::to_string(remotePort) + " is on " +
                 iFaceName + " with mac " + macAddress.to_string());
        Socket::sendWolPacket(iFaceName, macAddress, remotePort);
    } else {
        logError("No entry for " + remoteAddr.to_string() + " in arp cahce");
    }
}

void MxSrv::timedoutRemote(MxRemote const *remote) {
    auto it = std::find_if(inRemotes.begin(), inRemotes.end(),
                           [remote](const auto &pair) { return pair.second.get() == remote; });
    if (it != inRemotes.end()) {
        inRemotes.erase(it);
    } else {
        logError("MxRemote inbound timed out yet not found");
    }
}

void MxSrv::addRemote(std::size_t const index, std::uint16_t const remoteIndex, InetDest const &addr) {
    outRemotes.emplace(index, MxRemote::create(*this, remoteIndex, addr, false));
    auto const interface = Socket::findArpInterface(addr);
    logError("Arpwatch interface check for " + std::string(interface));
    if (!arpWatchFailed && interface.size() != 0 && !arpWatchers.contains(interface)) {
        try {
            auto ref = std::make_shared<MxArpWatch>(arpCache);
            ArpSocket::create(interface, ref);
            arpWatchers.emplace(interface, std::move(ref));
        } catch (StringException const &ex) {
            logError("Disabling arpwatch because " + std::string(ex.why()) + " and not root or cap_net_raw is not set");
            arpWatchFailed = true;
        }
    }
}

void MxSrv::sendToRemote(RouterIf::DeviceType const type, std::uint16_t const dest, linux_input_event const ev[],
                         std::size_t const count) {

    Bytes packet;
    packet.reserve(1024);
    auto const &remote = outRemotes[dest];
    appendNetworkEndianToPacket(PacketType::RouterEvent, packet);

    appendNetworkEndianToPacket(remote->getAndIncSequence(), packet);

    DestType dt{.vals = {.dest = static_cast<uint16_t>(remote->getIndex()),
                         .type = static_cast<uint8_t>(type),
                         .count = static_cast<uint8_t>(count)}};

    appendNetworkEndianToPacket(dt.d32, packet);

    for (std::size_t i = 0; i < count; ++i) {
        EventTypeCode tc{.vals = {.type = ev[i].type, .code = ev[i].code}};
        appendNetworkEndianToPacket(tc.d32, packet);
        appendNetworkEndianToPacket(ev[i].value, packet);
    }
    // encrypt entire packet
    Bytes enc_packet(packet.size() + 16, 0);
    auto nonce = remote->generateNewNonce();
    ocb_encrypt(ocbKey.data(), nonce.data(), nonce.size(), packet.data(), packet.size(), enc_packet.data());

    auto ref = udpSocket.lock();

    if (ref) {
        ref->queueWrite(remote->getDest(), enc_packet);
    }
}

void MxSrv::connected(InetDest const &dst) { (void)dst; }

void MxSrv::disconnected() {}

void MxSrv::received(InetDest const &addr, Bytes const &enc) {
    if (fromNetworkEndian(addr.port) != MXEV_PORT) {
        logError("Ignoring packet on port " + std::to_string(addr.port) + " probably WOL");
        return;
    }
    auto it = inRemotes.find(addr);
    if (it == inRemotes.end()) {
        auto emp = inRemotes.emplace(addr, MxRemote::create(*this, 0, addr, true));
        if (emp.second) {
            it = emp.first;
            // set the seq
            it->second->storeSequence(-1u);
        } else {
            logError("Emplace failed from " + addr.to_string());
            return;
        }
    }

    auto &remote = it->second;
    remote->resetTimer();
    Bytes decrypted(enc.size(), 0);

    bool decrypt_ok = false;

    for (auto const offset : MxRemote::nonceOffsets) {
        auto nonce = remote->generateNonce(offset);
        if (ocb_decrypt(ocbKey.data(), nonce.data(), nonce.size(), enc.data(), enc.size() - 16, decrypted.data())) {
            remote->updateNonce(nonce);
            decrypt_ok = true;
            break;
        }
    }

    if (!decrypt_ok) {
        logError("ocb_decrypt failed");
        return;
    }

    size_t pos = 0;
    auto const packType = extractNetOrderValueFromBytesAndUpdatePos<std::uint32_t>(decrypted, pos);
    if (!packType.has_value()) {
        logError("Maligned received packType");
        return;
    }
    if (packType.value() != PacketType::RouterEvent) {
        logError("Currently unhandled packet type");
        return;
    }
    auto const seqNumRecv = extractNetOrderValueFromBytesAndUpdatePos<std::uint32_t>(decrypted, pos);
    if (!seqNumRecv.has_value()) {
        logError("Maligned received seq No");
        return;
    }

    auto const seqDiff = seqNumRecv.value() - remote->getSequence();

    if (seqDiff == 0 || (seqDiff & 0x80000000u) != 0) {
        logError("Sequence diff is " + std::to_string(seqDiff) + " recv " + std::to_string(seqNumRecv.value()) +
                 " prev " + std::to_string(remote->getSequence()));
        logDebug("Recv packet is identical or stale, ignoring");
        auto stale_count = remote->getAndIncStaleCount();
        if (stale_count < 8) {
            return;
        } else {
            logDebug("Resetting sequence number.");
            remote->storeSequence(seqNumRecv.value());
        }
    }

    // decode packet header
    auto const destTypeCount = extractNetOrderValueFromBytesAndUpdatePos<std::uint32_t>(decrypted, pos);
    if (!destTypeCount.has_value()) {
        logError("Bad destTypeCount");
        return;
    }
    DestType const dt{.d32 = destTypeCount.value()};

    std::size_t const numEvents = dt.vals.count;
    auto events = std::make_unique<linux_input_event[]>(numEvents);
    for (auto i = 0u; i < numEvents; ++i) {
        auto decodedTc = extractNetOrderValueFromBytesAndUpdatePos<std::uint32_t>(decrypted, pos);
        if (!decodedTc.has_value()) {
            logError("Bad EventTypeCode at " + std::to_string(i));
            return;
        }
        EventTypeCode const tc{.d32 = decodedTc.value()};

        auto const decodedValue = extractNetOrderValueFromBytesAndUpdatePos<std::int32_t>(decrypted, pos);
        if (!decodedValue.has_value()) {
            logError("Bad DecodedValue at " + std::to_string(i));
            return;
        }
        events[i].type = tc.vals.type;
        events[i].code = tc.vals.code;
        events[i].value = decodedValue.value();
    }
    // update sequence after valid packet
    remote->storeSequence(seqNumRecv.value());
    remote->resetStaleCount();
    auto const type = static_cast<RouterIf::DeviceType>(dt.vals.type);
    auto ref = router.lock();

    if (ref) {
        ref->fromNetwork(type, dt.vals.dest, events.get(), numEvents);
    }
}

void MxSrv::notSent(InetDest const &dst, Bytes const &d) {
    (void)dst;
    (void)d;
}

void MxSrv::writeComplete() {}
void MxSrv::disconnect() {}
