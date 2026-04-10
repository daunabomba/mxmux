module;
#include "types.h"

import logger;
import udpsocket;

export module mxsrv;

export class MxSrv : public UdpSocketIf {
    void connected(InetDest const &) override;
    void disconnected() override;
    void received(InetDest const &, Bytes const &) override;
    void notSent(InetDest const &,  Bytes const &) override;
    void writeComplete() override;
    void disconnect() override;
};


void MxSrv::connected(InetDest const &dst) { (void)dst; }

void MxSrv::disconnected() {}

void MxSrv::received(InetDest const &addr, Bytes const &d) {
    logDebug("Received from " + addr.to_string());
    if(addr.addr.isIpv4Addr()) {
        logDebug("MxSrv is ipv4addr");
    }
    (void)d;
}

void MxSrv::notSent(InetDest const &dst, Bytes const &d) {
    (void)dst;
    (void)d;
}

void MxSrv::writeComplete() {}
void MxSrv::disconnect() {}