#pragma once

#include <cstddef>

namespace latency_lab {

constexpr int loopback_port_auto = 0;

bool set_tcp_no_delay(int fd, bool enabled) noexcept;
bool set_socket_send_buffer(int fd, int bytes) noexcept;
bool set_socket_receive_buffer(int fd, int bytes) noexcept;
bool set_non_blocking(int fd, bool enabled) noexcept;
bool send_all(int fd, const void* data, std::size_t bytes) noexcept;
bool recv_all(int fd, void* data, std::size_t bytes) noexcept;
int get_bound_port(int fd) noexcept;
void close_socket(int fd) noexcept;

} 
