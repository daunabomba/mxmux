module;
#include "types.h"

#include <cassert>
#include <memory>

import socket;
import tcpstream;
import logger;

export module smtpproxy;

export class RemoteSmtp;

export class SmtpProxy : public TcpStreamIf {
  public:
    SmtpProxy(const InetDest &dest) : mxAddr(dest) {}
    virtual ~SmtpProxy();
    virtual void received(const Bytes &x) override;
    virtual void writeComplete() override;
    virtual void disconnected() override;
    virtual void disconnect();
    virtual void queueWrite(Bytes const &x);
    virtual void disconnectRemoteSmtp() const;

  private:
    std::weak_ptr<RemoteSmtp> ep;
    bool didConnect = false;
    std::mutex lock;
    const InetDest &mxAddr;
};

export class RemoteSmtp : public TcpStreamIf {
  public:
    RemoteSmtp(std::weak_ptr<SmtpProxy> const &newEp) : ep(newEp) {}

    virtual ~RemoteSmtp() {}

    virtual void disconnect();
    virtual void received(const Bytes &x) override;
    virtual void disconnected() override;
    virtual void writeComplete() override;

    void doWrite(const Bytes &x);

  private:
    std::weak_ptr<SmtpProxy> ep;
    std::mutex lock;
    bool epDisconnected = false;
};



void RemoteSmtp::disconnect() {
    logDebug("RemoteSmtp::disconnect");
    assert(!epDisconnected && "Already disconnected");
    epDisconnected = true;
    auto ref = tcpStream.lock();
    if (ref) {
        ref->disconnect();
    }
}

void RemoteSmtp::received(const Bytes &x) {
    logDebug("RemoteSmtp::received");

    auto ref = ep.lock();
    if (ref) {
        ref->queueWrite(x);
    }
}

void RemoteSmtp::disconnected() {
    logDebug("RemoteSmtp::disconnected");
    auto ref = ep.lock();
    if (ref) {
        ref->disconnect();
    }
}

void RemoteSmtp::writeComplete() {
    logDebug("RemoteSmtp::writeComplete");
    std::lock_guard<std::mutex> sync(lock);
}

void RemoteSmtp::doWrite(const Bytes &x) {
    logDebug("RemoteSmtp::doWrite");
    std::lock_guard<std::mutex> sync(lock);
    auto ref = tcpStream.lock();
    if (ref) {
        ref->queueWrite(x);
    }
}

SmtpProxy::~SmtpProxy() {}

void SmtpProxy::received(const Bytes &x) {
    logDebug("SmtpProxy::received");
    auto ref = ep.lock();
    if (ref) {
        ref->doWrite(x);
        return;
    } else {
        logDebug("SmtpProxy::received but nowhere to write to.");
    }
}

void SmtpProxy::writeComplete() {
    logDebug("SmtpProxy::writeComplete");
    std::lock_guard<std::mutex> sync(lock);
    auto ref = tcpStream.lock();

    if (!didConnect) {
        didConnect = true;
        logDebug("SmtpProxy::writeComplete connected from " + ref->startPoint().to_string() + " to " +
                 ref->endPoint().to_string());
        auto sp = std::make_shared<RemoteSmtp>(std::dynamic_pointer_cast<SmtpProxy>(shared_from_this()));
        ep = sp;
        TcpStream::create(sp, mxAddr, ref->startPoint());
    }
}

void SmtpProxy::disconnected() {
    logDebug("SmtpProxy::disconnected");
    std::lock_guard<std::mutex> sync(lock);
    disconnectRemoteSmtp();
}

void SmtpProxy::disconnect() {
    logDebug("SmtpProxy::disconnect");
    auto ref = tcpStream.lock();
    if (ref) {
        ref->disconnect();
    }
}

void SmtpProxy::queueWrite(Bytes const &x) {
    logDebug("SmtpProxy::queueWrite");
    auto tcpRef = tcpStream.lock();
    if (tcpRef) {
        tcpRef->queueWrite(x);
    }
}

void SmtpProxy::disconnectRemoteSmtp() const {
    logDebug("SmtpProxy::disconnectRemoteSmtp");
    auto ref = ep.lock();
    if (ref) {
        ref->disconnect();
    }
}