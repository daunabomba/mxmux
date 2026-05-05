module;

#include "types.h"
#include <atomic>
#include <chrono>

export module mxsrv.mxremote;
import logger;
import timer;

using namespace std::chrono_literals;

export class MxRemote;

export class MxRemoteIf {
public:
    virtual ~MxRemoteIf() = default;
    virtual void timedoutRemote(MxRemote const *remote) = 0;
};

export class MxRemote final : public TimerIf {
public:
    ~MxRemote();
    static std::shared_ptr<MxRemote> create(MxRemoteIf &srv, std::uint16_t const ri, InetDest const rd,
                                            bool const expires);
    MxRemote(MxRemoteIf &srv, std::uint16_t const ri, InetDest const rd);
    std::uint16_t getIndex() const { return remoteIndex; }
    InetDest getDest() const { return remoteDest; }
    IpAddr getDestIpAddr() const { return remoteDest.addr; }
    uint16_t getDestPort() const { return remoteDest.port; }
    uint32_t getSequence() const { return sequence; }
    uint32_t getAndIncSequence() { return sequence.fetch_add(1); }
    void storeSequence(std::uint32_t const seq) { sequence.store(seq); }
    uint32_t getAndIncStaleCount() { return stale_count.fetch_add(1); }
    void resetStaleCount() { stale_count.store(0); }
    void resetTimer();

protected:
    void timeout() override;

public:
    struct NonceOffset {
        int no;
        unsigned co;
    };
    static constexpr NonceOffset nonceOffsets[] = {{0, 0},  {-1, 0}, {1, 0},  {0, 1}, {0, 2}, {0, 3},
                                                   {-1, 1}, {1, 2},  {-1, 3}, {1, 1}, {1, 2}, {1, 3}};
    Bytes generateNonce(NonceOffset const to) const;
    void updateNonce(Bytes const &nonce) { std::memcpy(last_nonce.data(), nonce.data(), last_nonce.size()); }
    Bytes generateNewNonce();

private:
    static constexpr auto NONCE_LEN = 15;
    static constexpr auto NONCE_TIME_OFFSET = 0;
    static constexpr auto NONCE_COUNTER_OFFSET = sizeof(uint64_t);

private:
    static constexpr auto ConnectionTimeout = 300s;
    MxRemoteIf &mxSrv;

    std::uint16_t const remoteIndex;
    InetDest const remoteDest;
    std::atomic<std::uint32_t> sequence{0};
    std::atomic<std::uint32_t> stale_count{0};
    Bytes last_nonce = Bytes(NONCE_LEN, 0);
    std::shared_ptr<OneShotTimer> idleTimer;
    uint64_t last_nonce_gen_sec = 0;

};

MxRemote::~MxRemote() {}

MxRemote::MxRemote(MxRemoteIf &srv, std::uint16_t const ri, InetDest const rd)
    : mxSrv(srv), remoteIndex(ri), remoteDest(rd) {}

std::shared_ptr<MxRemote> MxRemote::create(MxRemoteIf &srv, std::uint16_t const ri, InetDest const rd,
                                           bool const expires) {
    auto ref = std::make_shared<MxRemote>(srv, ri, rd);
    if (expires) {
        ref->idleTimer = OneShotTimer::create(ConnectionTimeout, ref);
    }
    return ref;
}

void MxRemote::resetTimer() {
    auto ref = idleTimer.get();
    if (ref) {
        ref->reset(ConnectionTimeout);
    }
}

void MxRemote::timeout() {
    mxSrv.timedoutRemote(this);
    auto ref = idleTimer.get();
    ref->destroy();
    idleTimer.reset();
}

Bytes MxRemote::generateNonce(NonceOffset const offset) const {
    Bytes nonce(NONCE_LEN, 0);
    auto secs = static_cast<uint64_t>(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                               std::chrono::system_clock::now().time_since_epoch())
                                                               .count()) +
                                      offset.no);

    uint32_t nonce_seq = 0;

    // counter only increments if non-zero.
    if (offset.co != 0) {
        nonce_seq = toU32FromArray(last_nonce, NONCE_COUNTER_OFFSET) + offset.co;
    }
    toArray(secs, nonce, NONCE_TIME_OFFSET);
    toArray(nonce_seq, nonce, NONCE_COUNTER_OFFSET);

    return nonce;
}

Bytes MxRemote::generateNewNonce() {
    Bytes nonce(NONCE_LEN, 0);
    auto secs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());

    uint32_t nonce_seq = 0;
    if (last_nonce_gen_sec == secs) {
        nonce_seq = toU32FromArray(last_nonce, NONCE_COUNTER_OFFSET) + 1;
    } else {
        last_nonce_gen_sec = secs;
    }

    toArray(secs, nonce, NONCE_TIME_OFFSET);
    toArray(nonce_seq, nonce, NONCE_COUNTER_OFFSET);

    updateNonce(nonce);
    return nonce;
}
