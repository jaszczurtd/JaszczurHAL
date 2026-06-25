#include "bsd_socket_example_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int udp_fd = -1;

static int open_udp_server(void) {
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons((uint16_t)BSD_EXAMPLE_UDP_PORT);

  const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    derr("socket() failed errno=%d", errno);
    return -1;
  }

  if (bind(fd, (struct sockaddr *)&address, (socklen_t)sizeof(address)) < 0) {
    derr("bind() failed errno=%d", errno);
    close(fd);
    return -1;
  }

  deb("UDP server listening on port %u", (unsigned)BSD_EXAMPLE_UDP_PORT);
  return fd;
}

static void serve_one_udp_packet(void) {
  struct sockaddr_in peer;
  socklen_t peer_len = (socklen_t)sizeof(peer);
  char buffer[128] = {};

  const ssize_t received = recvfrom(udp_fd, buffer, sizeof(buffer) - 1u, 0,
                                    (struct sockaddr *)&peer, &peer_len);
  if (received < 0) {
    derr("recvfrom() failed errno=%d", errno);
    return;
  }

  buffer[received] = '\0';

  char peer_ip[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &peer.sin_addr, peer_ip, (socklen_t)sizeof(peer_ip));
  deb("UDP RX from %s:%u: %s", peer_ip, (unsigned)ntohs(peer.sin_port), buffer);

  const char reply[] = "Hello from JaszczurHAL UDP server";
  (void)sendto(udp_fd, reply, strlen(reply), 0, (struct sockaddr *)&peer,
               peer_len);
}

void app_start(void) {
  debugInit();
  setDebugPrefixWithColon("bsd-udp-server");
}

void app_task0(void) {
  if (!bsd_example_wait_for_wifi("jaszczurhal-bsd-udp-server")) {
    return;
  }

  if (udp_fd < 0) {
    udp_fd = open_udp_server();
    if (udp_fd < 0) {
      hal_delay_ms(1000u);
      return;
    }
  }

  serve_one_udp_packet();
}
