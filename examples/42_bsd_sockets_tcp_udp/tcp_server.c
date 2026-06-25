#include "bsd_socket_example_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int server_fd = -1;

static int open_tcp_server(void) {
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons((uint16_t)BSD_EXAMPLE_TCP_PORT);

  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    derr("socket() failed errno=%d", errno);
    return -1;
  }

  if (bind(fd, (struct sockaddr *)&address, (socklen_t)sizeof(address)) < 0) {
    derr("bind() failed errno=%d", errno);
    close(fd);
    return -1;
  }

  if (listen(fd, 3) < 0) {
    derr("listen() failed errno=%d", errno);
    close(fd);
    return -1;
  }

  deb("TCP server listening on port %u", (unsigned)BSD_EXAMPLE_TCP_PORT);
  return fd;
}

static void serve_one_tcp_client(void) {
  struct sockaddr_in peer;
  socklen_t peer_len = (socklen_t)sizeof(peer);
  char buffer[128] = {};

  const int client_fd = accept(server_fd, (struct sockaddr *)&peer, &peer_len);
  if (client_fd < 0) {
    derr("accept() failed errno=%d", errno);
    return;
  }

  char peer_ip[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &peer.sin_addr, peer_ip, (socklen_t)sizeof(peer_ip));
  deb("TCP client connected: %s:%u", peer_ip, (unsigned)ntohs(peer.sin_port));

  const ssize_t received = read(client_fd, buffer, sizeof(buffer) - 1u);
  if (received > 0) {
    buffer[received] = '\0';
    deb("TCP RX: %s", buffer);
  }

  const char reply[] = "Hello from JaszczurHAL TCP server";
  (void)send(client_fd, reply, strlen(reply), 0);
  close(client_fd);
}

void app_start(void) {
  debugInit();
  setDebugPrefixWithColon("bsd-tcp-server");
}

void app_task0(void) {
  if (!bsd_example_wait_for_wifi("jaszczurhal-bsd-tcp-server")) {
    return;
  }

  if (server_fd < 0) {
    server_fd = open_tcp_server();
    if (server_fd < 0) {
      hal_delay_ms(1000u);
      return;
    }
  }

  serve_one_tcp_client();
}
