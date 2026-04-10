module;
#include <memory>
#include <string>
#include <bits/std_function.h>

import logger;
import engine;
import socket;
import tcpstream;

export module tcplistener;

export class TcpListener final : virtual public Socket {
  public:
    static void create(uint16_t const port, std::function<std::shared_ptr<TcpStreamIf>()> const &clientFactory);
    virtual ~TcpListener() override;
    TcpListener(uint16_t const port, std::function<std::shared_ptr<TcpStreamIf>()> const &clientFactory);

  private:
    virtual void handleRead() override;

  private:
    void createStream(const int newFd);
    std::function<std::shared_ptr<TcpStreamIf>()> clientFactory;
    std::mutex lock;
};



void TcpListener::create(uint16_t const port, std::function<std::shared_ptr<TcpStreamIf>()> const &clientFactory) {
    std::shared_ptr<Socket> listenRef = std::make_shared<TcpListener>(port, clientFactory);
    Engine::add(listenRef);
    logDebug("TcpListener::create on port " + std::to_string(port));
}

TcpListener::TcpListener(uint16_t const port, std::function<std::shared_ptr<TcpStreamIf>()> const &cf)
    : Runnable( createSocket(TCP)), Socket(TCP), clientFactory(cf) {
    logDebug("TcpListener::TcpListener on port " + std::to_string(port));
    reuseAddress();
    bind(port);
    makeNonBlocking();
    listen();
}

TcpListener::~TcpListener() { logDebug("TcpListener::~TcpListener"); }

void TcpListener::handleRead() {
    logDebug("TcpListener::handleRead");
    for (;;) {
        int connFd = -1;
        {
            std::lock_guard<std::mutex> sync(lock);
            connFd = accept();
        }
        if (connFd >= 0) {
            createStream(connFd);
        } else {
            break;
        }
    }
}

void TcpListener::createStream(const int connFd) {
    auto client = clientFactory();
    TcpStream::create(client, connFd);
    client->writeComplete();
}
