#pragma once
// #1189: the setup view. An installer runs `opendartboard --setup` once on the board
// and points a phone at the screen: the view prints a QR code that encodes the
// score socket's address and the token, as the one URL docs/api.md describes -
//
//     ws://<address>:13520/scores?token=<token>
//
// - so an ordinary camera app reads it back exactly. The token is not written
// beside it in plain text; --show-token is the one place it is printed.
//
// The address is the board's own non-loopback IPv4 address, read from the
// interfaces the moment --setup runs (--setup-address overrides it, for a board
// with more than one). The socket answers there only when the detector is run
// with --listen, and the view says so; the QR is the same either way, because
// the URL a phone needs does not change with the flag.
//
// The QR is drawn as Unicode half-blocks, two module rows per text line, on a
// quiet zone of four modules, which is what a phone camera needs to lock on to a
// terminal. There is no image library behind it: the encoder is the vendored
// src/third_party/qrcodegen (MIT).
#include "third_party/qrcodegen/qrcodegen.hpp"
#include "communication/score_token.hpp"
#include "communication/announce.hpp"
#include <iostream>
#include <string>
#include <cstring>
#ifndef _WIN32
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace setup_view
{
    // The first non-loopback IPv4 address that is up; empty when there is none.
    inline std::string boardAddress()
    {
#ifdef _WIN32
        return "";
#else
        struct ifaddrs *list = nullptr;
        if (::getifaddrs(&list) != 0)
            return "";
        std::string found;
        for (struct ifaddrs *ifa = list; ifa != nullptr; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if ((ifa->ifa_flags & IFF_LOOPBACK) || !(ifa->ifa_flags & IFF_UP))
                continue;
            char buf[INET_ADDRSTRLEN] = {0};
            const auto *sin = reinterpret_cast<const struct sockaddr_in *>(ifa->ifa_addr);
            if (::inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf) != nullptr)
            {
                found = buf;
                break;
            }
        }
        ::freeifaddrs(list);
        return found;
#endif
    }

    inline std::string socketUrl(const std::string &address, int port, const std::string &token)
    {
        return "ws://" + address + ":" + std::to_string(port) + "/scores?token=" + token;
    }

    // The QR as text: '#' for a dark module, ' ' for light, one row per line,
    // with the quiet zone included. Kept separate from the drawing so a check can
    // read the modules without decoding the half-blocks.
    inline std::string modules(const std::string &text, int quiet = 4)
    {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        std::string out;
        int size = qr.getSize();
        for (int y = -quiet; y < size + quiet; y++)
        {
            for (int x = -quiet; x < size + quiet; x++)
                out += qr.getModule(x, y) ? '#' : ' ';
            out += '\n';
        }
        return out;
    }

    // Two module rows per line: U+2588 both dark, U+2580 upper dark, U+2584 lower dark.
    inline std::string halfBlocks(const std::string &text, int quiet = 4)
    {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        std::string out;
        int size = qr.getSize();
        for (int y = -quiet; y < size + quiet; y += 2)
        {
            for (int x = -quiet; x < size + quiet; x++)
            {
                bool upper = qr.getModule(x, y);
                bool lower = qr.getModule(x, y + 1);
                out += upper ? (lower ? "\xE2\x96\x88" : "\xE2\x96\x80") : (lower ? "\xE2\x96\x84" : " ");
            }
            out += '\n';
        }
        return out;
    }

    // Print the setup view and exit. The token is read from the same file the socket
    // reads it from, created on the first run like --show-token does, and reaches the
    // screen inside the QR only.
    inline void printAndExit(const std::string &token_path, const std::string &label, int port,
                             const std::string &address_override, bool listen)
    {
        score_token::Resolved token = score_token::loadOrCreate(token_path);
        if (token.token.empty())
        {
            std::cerr << "cannot read or create the score token at " << token_path << ": " << token.error << std::endl;
            exit(1);
        }
        std::string address = address_override.empty() ? boardAddress() : address_override;
        if (address.empty())
        {
            std::cerr << "no network address to put in the QR code: no non-loopback interface is up"
                      << " (--setup-address <addr> names one)" << std::endl;
            exit(1);
        }
        std::cout << "OpenDartboard setup\n";
        std::cout << "  Board:    " << label << "  (announced as " << announce::kServiceType << " while --listen is on)\n";
        std::cout << "  Socket:   ws://" << address << ":" << port << "/scores?token=...\n";
        std::cout << "  Token:    " << token_path << "  (inside the QR; opendartboard --show-token prints it)\n";
        std::cout << (listen ? "  The socket is open on the network with --listen.\n"
                             : "  The socket answers at that address only when the detector is run with --listen.\n");
        std::cout << "\nScan with a phone camera to subscribe:\n\n";
        std::cout << halfBlocks(socketUrl(address, port, token.token));
        std::cout << std::endl;
        exit(0);
    }
}
