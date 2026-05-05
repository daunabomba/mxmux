#include "exception.h"
#include "types.h"

#include <iostream>
#include <memory>
#include <fstream>
#include <array>

import mxsrv;
import engine;
import router;
import socket;
import smtpproxy;
import tcplistener;
import udpsocket;
import arpsocket;
import resolver;

int main(int const argc, char const *const *argv) {
    try {
        Engine::init();
#if defined(MX_ROUTER)
        auto constexpr absent = "absent";
        auto constexpr empty = "";

        char const *inKeyboardName = empty;
        char const *inMouseName = empty;
        if (argc != 4) {
            std::cerr << "Usage: " << argv[0] << " { keyfile } { keyboardDevName | 'absent' } { mouseDevName | 'absent' }"
                      << std::endl
                      << "   eg: " << argv[0] << " /dev/input/event2 /dev/input/event5" << std::endl;
            throw StringException("Arguments incorrect");
        }
        if (std::string(absent).compare(std::string(argv[2])) != 0) {
            inKeyboardName = argv[2];
        }
        if (std::string(absent).compare(std::string(argv[3])) != 0) {
            inMouseName = argv[3];
        }

        auto muxer = std::make_shared<Router>(inKeyboardName, inMouseName);

        std::array<byte, 32> ocbKey{};
        // Read OCB key from file
        std::string keyFilename = argv[1];
        std::ifstream keyFile(keyFilename, std::ios::binary);
        if (!keyFile.is_open()) {
            throw StringException("Failed to open key file: " + keyFilename);
        }
        keyFile.read(reinterpret_cast<char*>(ocbKey.data()), 32);
        if (!keyFile || keyFile.gcount() != 32) {
            throw StringException("Key file must contain exactly 32 bytes: " + keyFilename);
        }
        // Check if key is all zeros
        bool allZero = true;
        for (byte b : ocbKey) {
            if (b != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero) {
            throw StringException("OCB key is all zeros, invalid key");
        }
        std::shared_ptr<MxSrv> ref = std::make_shared<MxSrv>(muxer, ocbKey);
        UdpSocket::create(MXEV_PORT, ref);

        muxer->init(ref->shared_from_this());
#elif defined(MX_SMTPPROXY)
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
#endif
        Engine::start();
        return 0;
    } catch (StringException const &ex) {
        std::cerr << "Failed: " << ex.why() << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "Engine " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "exception" << std::endl;
    }
    return 1;
}
