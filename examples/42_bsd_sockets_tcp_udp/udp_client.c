#include "bsd_socket_example_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static uint32_t last_run_ms = 0u;

static void run_udp_client_once(void) {
  struct sockaddr_in serv_addr = {};
  if (!bsd_example_resolve_server(BSD_EXAMPLE_SERVER_HOST,
                                  (uint16_t)BSD_EXAMPLE_UDP_PORT, SOCK_DGRAM,
                                  &serv_addr)) {
    return;
  }

  const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    derr("socket() failed errno=%d", errno);
    return;
  }

  char hello[96] = {};
  snprintf(hello, sizeof(hello), "Hello from JaszczurHAL UDP client at %lu",
           (unsigned long)hal_millis());

  deb("UDP sendto %s:%u", BSD_EXAMPLE_SERVER_HOST,
      (unsigned)BSD_EXAMPLE_UDP_PORT);
  if (sendto(fd, hello, strlen(hello), 0, (struct sockaddr *)&serv_addr,
             (socklen_t)sizeof(serv_addr)) < 0) {
    derr("sendto() failed errno=%d", errno);
    close(fd);
    return;
  }

  struct sockaddr_in from;
  socklen_t from_len = (socklen_t)sizeof(from);
  char buffer[128] = {};
  const ssize_t received = recvfrom(fd, buffer, sizeof(buffer) - 1u, 0,
                                    (struct sockaddr *)&from, &from_len);
  if (received > 0) {
    buffer[received] = '\0';
    deb("UDP RX: %s", buffer);
  } else {
    derr("recvfrom() failed errno=%d", errno);
  }

  close(fd);
}

void app_start(void) {
  debugInit();
  setDebugPrefixWithColon("bsd-udp-client");
}

void app_task0(void) {
  if (!bsd_example_wait_for_wifi("jaszczurhal-bsd-udp-client")) {
    return;
  }

  const uint32_t now = hal_millis();
  if (last_run_ms != 0u && (uint32_t)(now - last_run_ms) < 5000u) {
    hal_delay_ms(50u);
    return;
  }
  last_run_ms = now;

  run_udp_client_once();
}
