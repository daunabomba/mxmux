module;
#include "types.h"
#include <deque>
#include <memory>

import utils;
import socket;
import logger;
import engine;

export module tcpstream;

export class TcpStream;

export class TcpStreamIf : public std::enable_shared_from_this<TcpStreamIf> {
  public:
    virtual ~TcpStreamIf() {}
    friend class TcpStream;
    virtual void received(Bytes const &) = 0;
    virtual void writeComplete() = 0;
    virtual void disconnected() = 0;

  protected:
    std::weak_ptr<TcpStream> tcpStream;
};

export class TcpStream : public Socket {
    friend class TcpStreamIf;

  public:
    static void create(std::shared_ptr<TcpStreamIf> const &newClient, int const newFd);
    static void create(std::shared_ptr<TcpStreamIf> const &newClient, InetDest const &dest);
    static void create(std::shared_ptr<TcpStreamIf> const &newClient, InetDest const &dest, InetDest const &fauxOrigin);

    void queueWrite(const Bytes &data);
    void disconnect();
    TcpStream(std::shared_ptr<TcpStreamIf> const &newClient);
    TcpStream(std::shared_ptr<TcpStreamIf> const &newClient, int const newFd);
    virtual ~TcpStream() override;
    bool didConnect() const;
    InetDest startPoint() const;
    InetDest endPoint() const;
    bool writeQueueEmpty();

  protected:
    virtual void handleRead() override;
    virtual void handleWrite() override;
    virtual void handleError() override;
    virtual bool waitingOutEvent() override;

  private:
    std::shared_ptr<TcpStreamIf> client;
    std::mutex writeLock;
    std::mutex readLock;
    std::deque<Bytes> writeQueue;
    bool blocked = false;
    bool once = false;
    bool writeTriggered = false;
    bool connected = false;
    bool disconnecting = false;
};

void TcpStream::create(std::shared_ptr<TcpStreamIf> const &client, int const fd) {
    auto tcpRef = std::make_shared<TcpStream>(client, fd);
    client->tcpStream = tcpRef;
    tcpRef->connected = true;
    tcpRef->originalDestination();
    std::shared_ptr<Socket> sockRef = tcpRef;
    Engine::add(sockRef);
}

void TcpStream::create(std::shared_ptr<TcpStreamIf> const &client, InetDest const &dest, InetDest const &fauxOrigin) {
    auto ref = std::make_shared<TcpStream>(client);
    client->tcpStream = ref;
    ref->makeTransparent(fauxOrigin);
    ref->keepAlive();
    auto const err = ref->connect(dest);
    if (err >= 0) {
        ref->connected = true;
    } else if (err == -1) {
        ref->connected = false;
    } else {
        pErrorLog("tcpstream::create faux", err, ref->fd);
        return;
    }
    std::shared_ptr<Socket> sockRef = ref;
    Engine::add(sockRef);
}

void TcpStream::create(std::shared_ptr<TcpStreamIf> const &client, InetDest const &dest) {
    auto ref = std::make_shared<TcpStream>(client);
    client->tcpStream = ref;
    auto const err = ref->connect(dest);
    if (err >= 0) {
        ref->connected = true;
    } else if (err == -1) {
        ref->connected = false;
    } else {
        pErrorLog("tcpstream::create normal", err, ref->fd);
        return;
    }
    std::shared_ptr<Socket> sockRef = ref;
    Engine::add(sockRef);
}

TcpStream::TcpStream(std::shared_ptr<TcpStreamIf> const &newClient) : Runnable(Socket::createSocket(TCP)), Socket(TCP), client(newClient) {}

TcpStream::TcpStream(std::shared_ptr<TcpStreamIf> const &newClient, int const newFd) : Runnable(newFd),
                  Socket(TCP) , client(newClient) {
    makeNonBlocking();
    keepAlive();
}

TcpStream::~TcpStream() {
    std::lock_guard<std::mutex> syncWrite(writeLock);
    std::lock_guard<std::mutex> syncRead(readLock);
    client->disconnected();
    client.reset();
    client = nullptr;
}

void TcpStream::handleRead() {
    std::lock_guard<std::mutex> sync(readLock);
    for (;;) {
        Bytes data(MAX_PACKET_SIZE);
        auto const actuallyRead = read(data);
        logDebug("TcpStream::handleRead " + std::to_string(actuallyRead) + " bytes");
        if (actuallyRead > 0) {
            if (client) {
                client->received(Bytes(data.data(), data.data() + actuallyRead));
            }
            continue;
        } else if (actuallyRead == -2) {
            if (client) {
                client->disconnected();
            }
        }
        break;
    }
}

void TcpStream::handleWrite() {
    std::lock_guard<std::mutex> sync(writeLock);
    writeTriggered = false;
    blocked = false;
    if (connected) {
        bool wasEmpty = (writeQueue.size() == 0);
        for (;;) {
            if (writeQueue.size() == 0) {
                break;
            } else {
                auto const data = writeQueue.front();
                writeQueue.pop_front();
                auto const actuallySent = write(data);
                if (actuallySent >= 0) {
                    auto dSize = data.size();
                    if (dSize != static_cast<decltype(dSize)>(actuallySent)) {
                        writeQueue.push_front(Bytes(data.begin() + actuallySent, data.end()));
                    }
                } else if (actuallySent == -1) {
                    writeQueue.push_front(data);
                    blocked = true;
                    break;
                } else {
                    break;
                }
            }
        }
        bool isEmpty = (writeQueue.size() == 0);
        if (!once || (!wasEmpty && isEmpty)) {
            once = true;
        }
    } else {
        connected = true;
        if (writeQueue.size() > 0) {
            writeTriggered = true;
            Engine::triggerWrites(this);
        }
    }
}

bool TcpStream::waitingOutEvent() {
    std::lock_guard<std::mutex> sync(writeLock);
    return (blocked || !once || !connected) && !disconnecting;
}

void TcpStream::handleError() {
    getLastError();
    disconnect();
}

void TcpStream::disconnect() {
    std::lock_guard<std::mutex> sync(writeLock);
    if (!disconnecting) {
        disconnecting = true;
        Engine::remove(self);
    }
}

void TcpStream::queueWrite(Bytes const &data) {
    std::lock_guard<std::mutex> sync(writeLock);
    if (data.size() == 0) {
        return;
    }
    writeQueue.push_back(data);
    if (connected && !blocked && !writeTriggered) {
        writeTriggered = true;
        Engine::triggerWrites(this);
    }
}

bool TcpStream::writeQueueEmpty() {
    std::lock_guard<std::mutex> sync(writeLock);
    return writeQueue.size() == 0;
}

bool TcpStream::didConnect() const { return connected; }

InetDest TcpStream::startPoint() const { return originSource(); }
InetDest TcpStream::endPoint() const { return originalDestination(); }
