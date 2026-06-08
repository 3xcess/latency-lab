#include "latency_lab/socket_utils.hpp"

#if defined(__linux__)
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace latency_lab {

bool set_tcp_no_delay(int fd, bool enabled) noexcept {
#if defined(__linux__)
    const int value = enabled ? 1 : 0;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) == 0;
#else
    (void)fd;
    (void)enabled;
    return false;
#endif
}

bool set_socket_send_buffer(int fd, int bytes) noexcept {
#if defined(__linux__)
    return bytes <= 0 || setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) == 0;
#else
    (void)fd;
    (void)bytes;
    return false;
#endif
}

bool set_socket_receive_buffer(int fd, int bytes) noexcept {
#if defined(__linux__)
    return bytes <= 0 || setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0;
#else
    (void)fd;
    (void)bytes;
    return false;
#endif
}

bool set_non_blocking(int fd, bool enabled) noexcept {
#if defined(__linux__)
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }

    const int updated_flags = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(fd, F_SETFL, updated_flags) == 0;
#else
    (void)fd;
    (void)enabled;
    return false;
#endif
}

bool send_all(int fd, const void* data, std::size_t bytes) noexcept {
#if defined(__linux__)
    const auto* cursor = static_cast<const std::byte*>(data);
    std::size_t sent = 0;

    while (sent < bytes) {
        const ssize_t result = send(fd, cursor + sent, bytes - sent, MSG_NOSIGNAL);
        if (result > 0) {
            sent += static_cast<std::size_t>(result);
            continue;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
#else
    (void)fd;
    (void)data;
    (void)bytes;
    return false;
#endif
}

bool recv_all(int fd, void* data, std::size_t bytes) noexcept {
#if defined(__linux__)
    auto* cursor = static_cast<std::byte*>(data);
    std::size_t received = 0;

    while (received < bytes) {
        const ssize_t result = recv(fd, cursor + received, bytes - received, 0);
        if (result > 0) {
            received += static_cast<std::size_t>(result);
            continue;
        }

        if (result < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
#else
    (void)fd;
    (void)data;
    (void)bytes;
    return false;
#endif
}

int get_bound_port(int fd) noexcept {
#if defined(__linux__)
    sockaddr_in address{};
    socklen_t address_length = sizeof(address);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
        return -1;
    }
    return ntohs(address.sin_port);
#else
    (void)fd;
    return -1;
#endif
}

void close_socket(int fd) noexcept {
#if defined(__linux__)
    if (fd >= 0) {
        close(fd);
    }
#else
    (void)fd;
#endif
}

}
