#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static struct sockaddr_in endpoint(const char *ip, uint16_t port) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  (void)inet_pton(AF_INET, ip, &addr.sin_addr);
  return addr;
}

static struct sockaddr_in any_endpoint(uint16_t port) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  return addr;
}

static void compile_tcp_client_shape(void) {
  char rx[8];
  const char tx[] = "GET";
  struct addrinfo hints;
  struct addrinfo *resolved = NULL;
  struct sockaddr_in remote = endpoint("192.0.2.10", 80u);
  struct sockaddr_in local_name;
  struct sockaddr_in peer_name;
  socklen_t name_len = (socklen_t)sizeof(local_name);
  struct timeval timeout;
  int so_error = 0;
  socklen_t opt_len = (socklen_t)sizeof(so_error);
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd >= 0) {
    int opt = 1;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    if (getaddrinfo("example.com", "80", &hints, &resolved) == 0) {
      (void)connect(fd, resolved->ai_addr, resolved->ai_addrlen);
      freeaddrinfo(resolved);
    }
    (void)connect(fd, (const struct sockaddr *)&remote,
                  (socklen_t)sizeof(remote));
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                     (socklen_t)sizeof(opt));
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                     (socklen_t)sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                     (socklen_t)sizeof(timeout));
    (void)getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &opt_len);
    opt_len = (socklen_t)sizeof(timeout);
    (void)getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, &opt_len);
    (void)getsockname(fd, (struct sockaddr *)&local_name, &name_len);
    name_len = (socklen_t)sizeof(peer_name);
    (void)getpeername(fd, (struct sockaddr *)&peer_name, &name_len);
    (void)fcntl(fd, F_SETFL, O_NONBLOCK);
    (void)send(fd, tx, sizeof(tx) - 1u, 0);
    (void)send(fd, tx, sizeof(tx) - 1u, MSG_DONTWAIT);
    (void)write(fd, tx, sizeof(tx) - 1u);
    (void)recv(fd, rx, sizeof(rx), MSG_DONTWAIT);
    (void)read(fd, rx, sizeof(rx));
    (void)shutdown(fd, SHUT_RDWR);
    (void)close(fd);
  }
}

static void compile_tcp_server_shape(void) {
  struct sockaddr_in local = any_endpoint(8080u);
  struct sockaddr_in peer;
  socklen_t peer_len = (socklen_t)sizeof(peer);
  int opt = 1;
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd >= 0) {
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                     (socklen_t)sizeof(opt));
    (void)bind(fd, (const struct sockaddr *)&local, (socklen_t)sizeof(local));
    (void)listen(fd, 1);
    (void)fcntl(fd, F_SETFL, O_NONBLOCK);
    (void)accept(fd, (struct sockaddr *)&peer, &peer_len);
    (void)close(fd);
  }
}

static void compile_udp_client_shape(void) {
  char rx[8];
  const char tx[] = "ping";
  struct sockaddr_in remote = endpoint("198.51.100.20", 9000u);
  struct sockaddr_in local_name;
  struct sockaddr_in peer_name;
  socklen_t name_len = (socklen_t)sizeof(local_name);
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd >= 0) {
    (void)sendto(fd, tx, sizeof(tx) - 1u, MSG_DONTWAIT,
                 (const struct sockaddr *)&remote, (socklen_t)sizeof(remote));
    (void)connect(fd, (const struct sockaddr *)&remote,
                  (socklen_t)sizeof(remote));
    (void)send(fd, tx, sizeof(tx) - 1u, 0);
    (void)write(fd, tx, sizeof(tx) - 1u);
    (void)recv(fd, rx, sizeof(rx), MSG_DONTWAIT);
    (void)read(fd, rx, sizeof(rx));
    (void)getsockname(fd, (struct sockaddr *)&local_name, &name_len);
    name_len = (socklen_t)sizeof(peer_name);
    (void)getpeername(fd, (struct sockaddr *)&peer_name, &name_len);
    (void)close(fd);
  }
}

static void compile_udp_server_shape(void) {
  char rx[8];
  struct sockaddr_in local = any_endpoint(9000u);
  struct sockaddr_in peer;
  socklen_t peer_len = (socklen_t)sizeof(peer);
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd >= 0) {
    (void)bind(fd, (const struct sockaddr *)&local, (socklen_t)sizeof(local));
    (void)recvfrom(fd, rx, sizeof(rx), MSG_DONTWAIT, (struct sockaddr *)&peer,
                   &peer_len);
    (void)close(fd);
  }
}

static void compile_select_shape(void) {
  struct timeval timeout;
  fd_set readfds;
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd >= 0) {
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    (void)select(fd + 1, &readfds, NULL, NULL, &timeout);
    (void)close(fd);
  }
}

int main(void) {
  compile_tcp_client_shape();
  compile_tcp_server_shape();
  compile_udp_client_shape();
  compile_udp_server_shape();
  compile_select_shape();
  return 0;
}
