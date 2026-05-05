module;
#include "types.h"

import udpsocket;
import logger;
import engine;

import dnspacket;

export module resolver;

export class Resolver : public UdpSocketIf {
public:
    virtual ~Resolver() = default;
    void connected(InetDest const &) override;
    void disconnected() override;
    void received(InetDest const &, Bytes const &) override;
    void notSent(InetDest const &, Bytes const &) override;
    void writeComplete() override;
    void disconnect() override;
};

void Resolver::connected(InetDest const &) {}
void Resolver::disconnected() {}

void Resolver::received(InetDest const &, Bytes const &data) {
    DnsPacket packet(data);
    if (packet.isQuery()) {
        packet.decode();
    }
}

void Resolver::notSent(InetDest const &, Bytes const &) {}
void Resolver::writeComplete() {}
void Resolver::disconnect() {}
