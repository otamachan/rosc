// Socket helpers (IPv4 only).
#pragma once

#include <sys/types.h>

#include <string>

namespace rosc {

int tcp_listen(int* port);  // non-blocking, all interfaces, ephemeral port
// Non-blocking connect; completion is signaled by POLLOUT. Returns -1 and sets *err on failure.
int tcp_connect(const std::string& host, int port, std::string* err);
void setup_socket(int fd);  // non-blocking, close-on-exec, no SIGPIPE
int socket_error(int fd);  // SO_ERROR
void set_nodelay(int fd);
ssize_t send_some(int fd, const void* data, size_t n);  // never raises SIGPIPE
int make_wake_pipe(int fds[2]);

}  // namespace rosc
