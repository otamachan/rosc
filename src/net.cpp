#include "net.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace rosc {

void setup_socket(int fd) {
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

int tcp_listen(int* port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  socklen_t len = sizeof(a);
  if (bind(fd, (sockaddr*)&a, sizeof(a)) < 0 || listen(fd, 64) < 0 || getsockname(fd, (sockaddr*)&a, &len) < 0) {
    close(fd);
    return -1;
  }
  setup_socket(fd);
  *port = ntohs(a.sin_port);
  return fd;
}

// ponytail: getaddrinfo blocks (also on the I/O thread); fine for /etc/hosts and IP literals.
int tcp_connect(const std::string& host, int port, std::string* err) {
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
  if (rc != 0) {
    *err = "cannot resolve " + host + ": " + gai_strerror(rc);
    return -1;
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd >= 0) {
    setup_socket(fd);
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0 && errno != EINPROGRESS) {
      *err = "connect " + host + ":" + std::to_string(port) + ": " + strerror(errno);
      close(fd);
      fd = -1;
    }
  } else {
    *err = std::string("socket: ") + strerror(errno);
  }
  freeaddrinfo(res);
  return fd;
}

int socket_error(int fd) {
  int e = 0;
  socklen_t len = sizeof(e);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &len) < 0) return errno;
  return e;
}

void set_nodelay(int fd) {
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

ssize_t send_some(int fd, const void* data, size_t n) {
#ifdef MSG_NOSIGNAL
  return send(fd, data, n, MSG_NOSIGNAL);
#else
  return send(fd, data, n, 0);
#endif
}

int make_wake_pipe(int fds[2]) {
  if (pipe(fds) < 0) return -1;
  for (int i = 0; i < 2; i++) {
    fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
    fcntl(fds[i], F_SETFD, FD_CLOEXEC);
  }
  return 0;
}

}  // namespace rosc
