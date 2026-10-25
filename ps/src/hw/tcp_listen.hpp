// tcp_listen.hpp — the listener half shared by the board's two TCP servers (hw/modbus_tcp, part_log's NetFeed):
// a non-blocking listener bound to want_port (0 = any free port) plus the wake pipe that unblocks the server
// thread's poll for a stop. Header-only static inline, like can_link.h.
#pragma once
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
namespace des {
struct TcpListen {
    int lfd = -1, port = 0, wake[2] = {-1, -1};
    // "" on success; on failure everything is closed and left invalid. Error text: "socket: ..." / "port N: ..."
    std::string open(int want_port, int backlog)
    {
        lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (lfd < 0) return std::string("socket: ") + strerror(errno);
        int one = 1;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(uint16_t(want_port));
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(lfd, reinterpret_cast<sockaddr *>(&a), sizeof a) < 0 || listen(lfd, backlog) < 0) {
            std::string e = "port " + std::to_string(want_port) + ": " + strerror(errno);
            close();
            return e;
        }
        socklen_t al = sizeof a;
        getsockname(lfd, reinterpret_cast<sockaddr *>(&a), &al);
        port = ntohs(a.sin_port);
        if (pipe2(wake, O_NONBLOCK | O_CLOEXEC) < 0) wake[0] = wake[1] = -1;
        return "";
    }
    void close()
    {
        if (lfd >= 0) ::close(lfd);
        lfd = -1;
        for (int &w : wake) {
            if (w >= 0) ::close(w);
            w = -1;
        }
    }
    void kick() const { if (wake[1] >= 0) (void)!write(wake[1], "q", 1); }   // unblock the server thread's poll
};
}  // namespace des
