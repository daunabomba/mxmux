module;
#include "types.h"
#include <atomic>
#include <deque>
#include <memory>
#include <string>

import utils;
import socket;
import logger;
import engine;

export module arpsocket;

export class ArpSocket;

export class ArpSocketIf {
public:
    friend class ArpSocket;
    virtual ~ArpSocketIf() = default;
    virtual void update(IpAddr const &ipAddr, MacAddress const &macAddress) = 0;

protected:
    std::weak_ptr<ArpSocket> arpSocket;
};

export class ArpSocket final : virtual public Socket {
public:
    static void create(std::string const &ifname, std::shared_ptr<ArpSocketIf> const &newClient);
    ArpSocket(std::string const &ifName, std::shared_ptr<ArpSocketIf> const &newClient);

    void disconnect();
    virtual ~ArpSocket();
    virtual void handleRead() override;
    virtual void handleError() override;
    std::string getInterface() const { return interface; }

private:
    void bindAndAdd(std::shared_ptr<ArpSocket> const &me, std::shared_ptr<ArpSocketIf> const &newClient);

private:
    std::string interface;
    std::shared_ptr<ArpSocketIf> client;
};

void ArpSocket::create(std::string const &ifname, std::shared_ptr<ArpSocketIf> const &newClient) {
    std::shared_ptr<ArpSocket> ref = std::make_shared<ArpSocket>(ifname, newClient);
    ref->bindAndAdd(ref, newClient);
    logDebug("ArpSocket::create listerning on " + ifname);
}

void ArpSocket::bindAndAdd(std::shared_ptr<ArpSocket> const &me, std::shared_ptr<ArpSocketIf> const &newClient) {
    bind(interface);
    newClient->arpSocket = me;
    std::shared_ptr<Socket> sockRef = me;
    Engine::add(sockRef);
}

ArpSocket::ArpSocket(std::string const &ifName, std::shared_ptr<ArpSocketIf> const &newClient)
    : Runnable(Socket::createSocket(ARP)), Socket(ARP), interface(ifName), client(newClient) {
    pErrorThrow("ArpSocket::ArpSocket", fd);
}

void ArpSocket::disconnect() {
    logDebug("ArpSocket::disconnect() " + std::to_string(fd));
    Engine::remove(self);
}

ArpSocket::~ArpSocket() {}

void ArpSocket::handleRead() {
    IpAddr ipAddr;
    MacAddress macAddress;
    if (receiveArp(ipAddr, macAddress)) {
        logDebug("IP: " + ipAddr.to_string() + " MAC: " + macAddress.to_string());
        client->update(ipAddr, macAddress);
    }
}
void ArpSocket::handleError() {
    logDebug("ArpSocket::handleError() " + std::to_string(fd));
    assert(false && "Restart from init");
}
