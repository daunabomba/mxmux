module;
#include "types.h"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

import utils;
import socket;
import logger;
import engine;

export module udpsocket;

export class UdpSocket;

export class UdpSocketIf {
public:
    friend class UdpSocket;
    virtual ~UdpSocketIf() = default;
    virtual void connected(InetDest const &) = 0;
    virtual void disconnected() = 0;
    virtual void received(InetDest const &, Bytes const &) = 0;
    virtual void notSent(InetDest const &, Bytes const &) = 0;
    virtual void writeComplete() = 0;
    virtual void disconnect() = 0;

protected:
    std::weak_ptr<UdpSocket> udpSocket;
};

export class UdpSocket final : virtual public Socket {
public:
    static void create(uint16_t const localPort, std::shared_ptr<UdpSocketIf> const &newClient);
    static void create(InetDest const &dest, std::shared_ptr<UdpSocketIf> const &newClient);
    UdpSocket(std::shared_ptr<UdpSocketIf> const &newClient);
    void queueWrite(InetDest const &dest, Bytes const &data);
    void queueWrite(Bytes const &data);
    void disconnect();
    virtual ~UdpSocket();
    virtual void handleRead() override;
    virtual void handleWrite() override;
    virtual void handleError() override;
    void doWrite(const InetDest &dest, const Bytes &data);

private:
    void bindAndAdd(std::shared_ptr<UdpSocket> const &me, uint16_t const localPort,
                    std::shared_ptr<UdpSocketIf> const &newClient);
    void connectAndAdd(std::shared_ptr<UdpSocket> const &me, InetDest const &dest,
                       std::shared_ptr<UdpSocketIf> const &newClient);

private:
    std::shared_ptr<UdpSocketIf> client;
    std::mutex writeLock;
    std::mutex readLock;
    std::deque<std::pair<InetDest const, Bytes const>> writeQueue;
    InetDest destAddr;
};

void UdpSocket::create(uint16_t const localPort, std::shared_ptr<UdpSocketIf> const &newClient) {
    std::shared_ptr<UdpSocket> ref = std::make_shared<UdpSocket>(newClient);
    ref->bindAndAdd(ref, localPort, newClient);
    logDebug("UdpSocket::create listerning on " + std::to_string(localPort));
}

void UdpSocket::create(InetDest const &dest, std::shared_ptr<UdpSocketIf> const &newClient) {
    std::shared_ptr<UdpSocket> ref = std::make_shared<UdpSocket>(newClient);
    ref->connectAndAdd(ref, dest, newClient);
}

UdpSocket::UdpSocket(std::shared_ptr<UdpSocketIf> const &newClient)
    : Runnable(Socket::createSocket(UDP)), Socket(UDP), client(newClient) {
    pErrorThrow("UdpSocket::UdpSocket", fd);
}

UdpSocket::~UdpSocket() {}

void UdpSocket::connectAndAdd(std::shared_ptr<UdpSocket> const &me, InetDest const &dest,
                              std::shared_ptr<UdpSocketIf> const &newClient) {
    newClient->udpSocket = me;
    std::shared_ptr<Socket> sockRef = me;
    Engine::add(sockRef);
    destAddr = dest;
    connect(destAddr);
    newClient->connected(destAddr);
}

void UdpSocket::bindAndAdd(std::shared_ptr<UdpSocket> const &me, uint16_t const localPort,
                           std::shared_ptr<UdpSocketIf> const &newClient) {
    newClient->udpSocket = me;
    std::shared_ptr<Socket> sockRef = me;
    Engine::add(sockRef);
    bind(localPort);
}

void UdpSocket::handleRead() {
    std::lock_guard<std::mutex> sync(readLock);
    Bytes data(MAX_PACKET_SIZE);
    InetDest from{};
    const auto actuallyReceived = receiveDatagram(from, data);
    if (actuallyReceived < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
        logDebug("UdpClient::handleRead would block");
        return;
    }
    client->received(from, data);
}

void UdpSocket::queueWrite(const InetDest &dest, const Bytes &data) {
    std::lock_guard<std::mutex> sync(writeLock);
    writeQueue.push_back(std::make_pair<const InetDest, const Bytes>(InetDest(dest), Bytes(data)));
    handleWrite();
}

void UdpSocket::queueWrite(const Bytes &data) {
    std::lock_guard<std::mutex> sync(writeLock);
    writeQueue.push_back(std::make_pair<const InetDest, const Bytes>(InetDest(destAddr), Bytes(data)));
    handleWrite();
}

void UdpSocket::disconnect() {
    logDebug("UdpSocket::disconnect() " + std::to_string(fd));
    Engine::remove(self);
}

void UdpSocket::doWrite(InetDest const &dest, Bytes const &data) {
    auto const actuallySent = sendDatagram(dest, data);
    pErrorLog("UdpSocket::doWrite", actuallySent, fd);
    if (actuallySent == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            writeQueue.push_back(std::make_pair<const InetDest, const Bytes>(InetDest(dest), Bytes(data)));
            return;
        } else {
            logDebug(std::string("UdpSocket::doWrite failed completely"));
            client->notSent(dest, data);
        }
    }
    decltype(data.size()) dataLen = static_cast<decltype(data.size())>(actuallySent);
    if (data.size() != dataLen) {
        logDebug("Partial write of " + std::to_string(actuallySent) + " out of " + std::to_string(dataLen));
        writeQueue.push_back(std::make_pair<const InetDest, const Bytes>(
            InetDest(dest), Bytes(data.begin() + actuallySent, data.end())));
    }
}

void UdpSocket::handleWrite() {
    for (;;) {
        if (writeQueue.size() == 0) {
            client->writeComplete();
            return;
        }
        const auto data = writeQueue.front();
        writeQueue.pop_front();
        doWrite(std::get<0>(data), std::get<1>(data));
    }
}

void UdpSocket::handleError() {
    logDebug("UdpSocket::handleError() is closed");
    client->disconnected();
}
