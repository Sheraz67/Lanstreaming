#pragma once

#ifdef _WIN32

#include <winsock2.h>
#include <cstdio>

namespace lancast {

// RAII wrapper for WSAStartup/WSACleanup. Instantiate once in main().
class WinsockInit {
public:
    WinsockInit() {
        WSADATA wsa_data;
        int err = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (err != 0) {
            fprintf(stderr, "WSAStartup failed: %d\n", err);
            ok_ = false;
        } else {
            ok_ = true;
        }
    }

    ~WinsockInit() {
        if (ok_) {
            WSACleanup();
        }
    }

    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;

    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

} // namespace lancast

#endif // _WIN32
