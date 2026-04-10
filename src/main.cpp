#include "exception.h"

#include <unistd.h>

#include <iostream>
#include <memory>
#include <functional>

import mxsrv;
import engine;
import router;
import socket;
import smtpproxy;
import tcplistener;
import udpsocket;

static void runUnit(const std::string id, std::function<void()> what) {
    try {
        Engine::init();
        what();
        Engine::start();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
    } catch (...) {
        std::cerr << id << " " << "exception" << std::endl;
    }
}

static uint16_t to_port_no(const std::string &str) {
    int val = std::stoi(str);
    if (val < 0 || val > USHRT_MAX) {
        val = 0;
    }
    return static_cast<uint16_t>(val);
}

int main(int const argc, char const *const *argv) {
    try {
	    uint16_t listeningPort = 25;
	    uint16_t destinationPort = 25;
	    if (argc != 2 && argc != 4) {
		std::cerr << "Usage: " << argv[0] << " desintaion_ip4_addr [destination_port_no listen_port_no]" << std::endl
		          << "Default port no is 25 both for listening and for destination" << std::endl;
		return 1;
	    }
	    if (argc == 4) {
		destinationPort = to_port_no(argv[2]);
		listeningPort = to_port_no(argv[3]);
	    }
	    ::close(0);

	    auto mxDest = Socket::destFromString(argv[1], destinationPort);

	    runUnit("SmtpProxy", [listeningPort, &mxDest]() {
		TcpListener::create(listeningPort, [&mxDest]() { return std::make_shared<SmtpProxy>(mxDest); });
	    });
	    return 0;
    } catch (StringException const &ex) {
        std::cerr << "Failed: " << ex.why() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
    } catch (...) {
        std::cerr << "exception" << std::endl;
    }
    return 1;
}
