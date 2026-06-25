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

static void run_tcp_client_once(void) {
  struct sockaddr_in serv_addr = {};
  if (!bsd_example_resolve_server(BSD_EXAMPLE_SERVER_HOST,
                                  (uint16_t)BSD_EXAMPLE_TCP_PORT, SOCK_STREAM,
                                  &serv_addr)) {
    return;
  }

  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    derr("socket() failed errno=%d", errno);
    return;
  }

  deb("TCP connect %s:%u", BSD_EXAMPLE_SERVER_HOST,
      (unsigned)BSD_EXAMPLE_TCP_PORT);
  if (connect(fd, (struct sockaddr *)&serv_addr, (socklen_t)sizeof(serv_addr)) <
      0) {
    derr("connect() failed errno=%d", errno);
    close(fd);
    return;
  }

  char hello[96] = {};
  snprintf(hello, sizeof(hello), "Hello from JaszczurHAL TCP client at %lu",
           (unsigned long)hal_millis());

  (void)send(fd, hello, strlen(hello), 0);

  char buffer[128] = {};
  const ssize_t received = read(fd, buffer, sizeof(buffer) - 1u);
  if (received > 0) {
    buffer[received] = '\0';
    deb("TCP RX: %s", buffer);
  } else {
    derr("read() failed errno=%d", errno);
  }

  shutdown(fd, SHUT_RDWR);
  close(fd);
}

void app_start(void) {
  debugInit();
  setDebugPrefixWithColon("bsd-tcp-client");
}

void app_task0(void) {
  if (!bsd_example_wait_for_wifi("jaszczurhal-bsd-tcp-client")) {
    return;
  }

  const uint32_t now = hal_millis();
  if (last_run_ms != 0u && (uint32_t)(now - last_run_ms) < 5000u) {
    hal_delay_ms(50u);
    return;
  }
  last_run_ms = now;

  run_tcp_client_once();
}
