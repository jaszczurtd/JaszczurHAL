#define HAL_TARGET_MOCK 1
#define HAL_ENABLE_BSD_SOCKETS 1

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

_Static_assert(AF_INET == 2, "AF_INET value");
_Static_assert(AF_INET6 == 10, "AF_INET6 value");
_Static_assert(PF_INET6 == AF_INET6, "PF_INET6 alias");
_Static_assert(sizeof(struct sockaddr_in) >= 16u, "IPv4 socket address size");
_Static_assert(sizeof(((struct sockaddr_in6 *)0)->sin6_addr.s6_addr) == 16u,
               "IPv6 address size");
_Static_assert(sizeof(socklen_t) >= sizeof(uint32_t), "socket length type");

int main(void) {
  struct addrinfo hints = {0};
  struct sockaddr_in endpoint = {0};
  struct timeval timeout = {0};
  fd_set readfds;

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  endpoint.sin_family = AF_INET;
  endpoint.sin_port = htons(8266u);
  timeout.tv_sec = 1;
  FD_ZERO(&readfds);
  return hints.ai_family + endpoint.sin_family + (int)timeout.tv_sec == 3 ? 0
                                                                          : 1;
}
