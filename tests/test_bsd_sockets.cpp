#include "hal/impl/.mock/hal_mock.h"
#include "utils/unity.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

void setUp(void) {
  hal_mock_serial_reset();
  hal_mock_bsd_sockets_reset();
  hal_mock_net_reset();
  hal_mock_udp_reset();
  hal_mock_tcp_reset();
}

void tearDown(void) { hal_mock_bsd_sockets_reset(); }

static struct sockaddr_in make_sockaddr(const char *ip, uint16_t port) {
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  TEST_ASSERT_EQUAL_INT(1, inet_pton(AF_INET, ip, &addr.sin_addr));
  return addr;
}

static struct sockaddr_in make_any_sockaddr(uint16_t port) {
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  return addr;
}

static hal_net_endpoint_t make_endpoint(uint8_t a, uint8_t b, uint8_t c,
                                        uint8_t d, uint16_t port) {
  hal_net_endpoint_t endpoint = {};
  endpoint.family = HAL_NET_AF_INET;
  endpoint.addr[0] = a;
  endpoint.addr[1] = b;
  endpoint.addr[2] = c;
  endpoint.addr[3] = d;
  endpoint.port = port;
  return endpoint;
}

void test_inet_helpers_translate_ipv4_and_byte_order(void) {
  struct in_addr addr = {};
  char text[INET_ADDRSTRLEN] = {};

  TEST_ASSERT_EQUAL_UINT16(0x1234u, ntohs(htons(0x1234u)));
  TEST_ASSERT_EQUAL_UINT32(0x12345678u, ntohl(htonl(0x12345678u)));

  TEST_ASSERT_EQUAL_INT(1, inet_pton(AF_INET, "192.168.1.50", &addr));
  TEST_ASSERT_EQUAL_STRING("192.168.1.50",
                           inet_ntop(AF_INET, &addr, text, sizeof(text)));
  TEST_ASSERT_EQUAL_UINT32(addr.s_addr, inet_addr("192.168.1.50"));

  errno = 0;
  TEST_ASSERT_EQUAL_INT(0, inet_pton(AF_INET, "192.168.1.999", &addr));
  TEST_ASSERT_EQUAL_INT(0, errno);

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, inet_pton(AF_UNSPEC, "192.168.1.1", &addr));
  TEST_ASSERT_EQUAL_INT(EAFNOSUPPORT, errno);

  errno = 0;
  TEST_ASSERT_NULL(inet_ntop(AF_INET, &addr, text, 4u));
  TEST_ASSERT_EQUAL_INT(ENOSPC, errno);
}

void test_udp_sendto_autobinds_and_translates_remote_sockaddr(void) {
  const uint8_t payload[] = {'u', 'd', 'p'};
  hal_net_endpoint_t captured_remote = {};
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);

  struct sockaddr_in remote = make_sockaddr("203.0.113.9", 7777u);
  TEST_ASSERT_EQUAL_INT((int)sizeof(payload),
                        sendto(fd, payload, sizeof(payload), 0,
                               (const struct sockaddr *)&remote,
                               (socklen_t)sizeof(remote)));

  hal_udp_socket_t udp = hal_mock_bsd_socket_get_udp_handle(fd);
  TEST_ASSERT_NOT_NULL(udp);
  TEST_ASSERT_EQUAL_UINT16(49152u, hal_mock_udp_get_local_port_for(udp));
  TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(payload),
                           hal_mock_udp_get_last_tx_len_for(udp));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      payload, hal_mock_udp_get_last_tx_payload_for(udp), sizeof(payload));
  TEST_ASSERT_TRUE(hal_mock_udp_get_last_tx_remote_for(udp, &captured_remote));
  TEST_ASSERT_EQUAL_UINT8(203u, captured_remote.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(9u, captured_remote.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(7777u, captured_remote.port);

  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_udp_autobind_skips_ephemeral_port_already_bound(void) {
  const uint8_t payload[] = {'s', 'k', 'i', 'p'};
  struct sockaddr_in local = make_any_sockaddr(49152u);
  struct sockaddr_in remote = make_sockaddr("203.0.113.10", 7778u);

  int bound_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int auto_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, bound_fd);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, auto_fd);
  TEST_ASSERT_EQUAL_INT(0, bind(bound_fd, (const struct sockaddr *)&local,
                                (socklen_t)sizeof(local)));

  TEST_ASSERT_EQUAL_INT((int)sizeof(payload),
                        sendto(auto_fd, payload, sizeof(payload), 0,
                               (const struct sockaddr *)&remote,
                               (socklen_t)sizeof(remote)));

  hal_udp_socket_t bound_udp = hal_mock_bsd_socket_get_udp_handle(bound_fd);
  hal_udp_socket_t auto_udp = hal_mock_bsd_socket_get_udp_handle(auto_fd);
  TEST_ASSERT_NOT_NULL(bound_udp);
  TEST_ASSERT_NOT_NULL(auto_udp);
  TEST_ASSERT_EQUAL_UINT16(49152u, hal_mock_udp_get_local_port_for(bound_udp));
  TEST_ASSERT_EQUAL_UINT16(49153u, hal_mock_udp_get_local_port_for(auto_udp));

  TEST_ASSERT_EQUAL_INT(0, close(auto_fd));
  TEST_ASSERT_EQUAL_INT(0, close(bound_fd));
}

void test_udp_bind_and_recvfrom_translate_sender_sockaddr(void) {
  const uint8_t payload[] = {'r', 'x'};
  uint8_t out[4] = {};
  char remote_text[INET_ADDRSTRLEN] = {};
  struct sockaddr_in local = make_any_sockaddr(9000u);
  struct sockaddr_in remote = {};
  socklen_t remote_len = (socklen_t)sizeof(remote);

  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);
  TEST_ASSERT_EQUAL_INT(
      0, bind(fd, (const struct sockaddr *)&local, (socklen_t)sizeof(local)));

  hal_udp_socket_t udp = hal_mock_bsd_socket_get_udp_handle(fd);
  TEST_ASSERT_NOT_NULL(udp);
  TEST_ASSERT_EQUAL_UINT16(9000u, hal_mock_udp_get_local_port_for(udp));

  hal_mock_udp_inject_packet_to(udp, "198.51.100.7", 5500u, payload,
                                (uint16_t)sizeof(payload));

  TEST_ASSERT_EQUAL_INT((int)sizeof(payload),
                        recvfrom(fd, out, sizeof(out), 0,
                                 (struct sockaddr *)&remote, &remote_len));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out, sizeof(payload));
  TEST_ASSERT_EQUAL_UINT32(sizeof(remote), remote_len);
  TEST_ASSERT_EQUAL_INT(AF_INET, remote.sin_family);
  TEST_ASSERT_EQUAL_UINT16(5500u, ntohs(remote.sin_port));
  TEST_ASSERT_EQUAL_STRING(
      "198.51.100.7",
      inet_ntop(AF_INET, &remote.sin_addr, remote_text, sizeof(remote_text)));

  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_connected_udp_send_write_recv_read_use_connected_peer(void) {
  const uint8_t first_tx[] = {'u', '1'};
  const uint8_t second_tx[] = {'u', '2', 'x'};
  const uint8_t first_rx[] = {'r', '1'};
  const uint8_t second_rx[] = {'r', '2', 'x'};
  uint8_t out[8] = {};
  hal_net_endpoint_t captured_remote = {};
  struct sockaddr_in local = {};
  struct sockaddr_in peer = {};
  socklen_t local_len = (socklen_t)sizeof(local);
  socklen_t peer_len = (socklen_t)sizeof(peer);

  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, send(fd, first_tx, sizeof(first_tx), 0));
  TEST_ASSERT_EQUAL_INT(ENOTCONN, errno);

  struct sockaddr_in remote = make_sockaddr("203.0.113.55", 9050u);
  TEST_ASSERT_EQUAL_INT(0, connect(fd, (const struct sockaddr *)&remote,
                                   (socklen_t)sizeof(remote)));

  hal_udp_socket_t udp = hal_mock_bsd_socket_get_udp_handle(fd);
  TEST_ASSERT_NOT_NULL(udp);
  TEST_ASSERT_EQUAL_UINT16(49152u, hal_mock_udp_get_local_port_for(udp));

  TEST_ASSERT_EQUAL_INT(0,
                        getsockname(fd, (struct sockaddr *)&local, &local_len));
  TEST_ASSERT_EQUAL_UINT16(49152u, ntohs(local.sin_port));

  TEST_ASSERT_EQUAL_INT(0,
                        getpeername(fd, (struct sockaddr *)&peer, &peer_len));
  TEST_ASSERT_EQUAL_UINT16(9050u, ntohs(peer.sin_port));

  TEST_ASSERT_EQUAL_INT((int)sizeof(first_tx),
                        send(fd, first_tx, sizeof(first_tx), 0));
  TEST_ASSERT_TRUE(hal_mock_udp_get_last_tx_remote_for(udp, &captured_remote));
  TEST_ASSERT_EQUAL_UINT8(203u, captured_remote.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(55u, captured_remote.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(9050u, captured_remote.port);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      first_tx, hal_mock_udp_get_last_tx_payload_for(udp), sizeof(first_tx));

  TEST_ASSERT_EQUAL_INT((int)sizeof(second_tx),
                        write(fd, second_tx, sizeof(second_tx)));
  TEST_ASSERT_TRUE(hal_mock_udp_get_last_tx_remote_for(udp, &captured_remote));
  TEST_ASSERT_EQUAL_UINT16(9050u, captured_remote.port);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      second_tx, hal_mock_udp_get_last_tx_payload_for(udp), sizeof(second_tx));

  hal_mock_udp_inject_packet_to(udp, "198.51.100.2", 7777u, first_rx,
                                (uint16_t)sizeof(first_rx));
  TEST_ASSERT_EQUAL_INT((int)sizeof(first_rx), recv(fd, out, sizeof(out), 0));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first_rx, out, sizeof(first_rx));

  memset(out, 0, sizeof(out));
  hal_mock_udp_inject_packet_to(udp, "198.51.100.3", 7778u, second_rx,
                                (uint16_t)sizeof(second_rx));
  TEST_ASSERT_EQUAL_INT((int)sizeof(second_rx), read(fd, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(second_rx, out, sizeof(second_rx));

  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_tcp_client_connect_send_recv_and_unistd_aliases(void) {
  const uint8_t first_tx[] = {'p', 'i', 'n', 'g'};
  const uint8_t second_tx[] = {'o', 'k'};
  const uint8_t first_rx[] = {'p', 'o', 'n', 'g'};
  const uint8_t second_rx[] = {'h', 'i'};
  uint8_t out[8] = {};
  hal_net_endpoint_t captured_remote = {};

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);

  struct sockaddr_in remote = make_sockaddr("10.20.30.40", 1883u);
  TEST_ASSERT_EQUAL_INT(0, connect(fd, (const struct sockaddr *)&remote,
                                   (socklen_t)sizeof(remote)));

  hal_tcp_socket_t tcp = hal_mock_bsd_socket_get_tcp_handle(fd);
  TEST_ASSERT_NOT_NULL(tcp);
  TEST_ASSERT_TRUE(hal_mock_tcp_get_remote_endpoint(tcp, &captured_remote));
  TEST_ASSERT_EQUAL_UINT8(10u, captured_remote.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(40u, captured_remote.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(1883u, captured_remote.port);

  TEST_ASSERT_EQUAL_INT((int)sizeof(first_tx),
                        send(fd, first_tx, sizeof(first_tx), 0));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first_tx, hal_mock_tcp_get_last_tx_payload(tcp),
                                sizeof(first_tx));

  TEST_ASSERT_EQUAL_INT((int)sizeof(second_tx),
                        write(fd, second_tx, sizeof(second_tx)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      second_tx, hal_mock_tcp_get_last_tx_payload(tcp), sizeof(second_tx));

  hal_mock_tcp_inject_rx(tcp, first_rx, (uint16_t)sizeof(first_rx));
  TEST_ASSERT_EQUAL_INT((int)sizeof(first_rx), recv(fd, out, sizeof(out), 0));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(first_rx, out, sizeof(first_rx));

  memset(out, 0, sizeof(out));
  hal_mock_tcp_inject_rx(tcp, second_rx, (uint16_t)sizeof(second_rx));
  TEST_ASSERT_EQUAL_INT((int)sizeof(second_rx), read(fd, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(second_rx, out, sizeof(second_rx));

  TEST_ASSERT_EQUAL_INT(0, shutdown(fd, SHUT_RDWR));
  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_getsockname_getpeername_and_so_error_for_tcp_client(void) {
  struct sockaddr_in local = {};
  struct sockaddr_in peer = {};
  socklen_t local_len = (socklen_t)sizeof(local);
  socklen_t peer_len = (socklen_t)sizeof(peer);
  socklen_t opt_len = (socklen_t)sizeof(int);
  int so_error = -1;

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1,
                        getpeername(fd, (struct sockaddr *)&peer, &peer_len));
  TEST_ASSERT_EQUAL_INT(ENOTCONN, errno);

  struct sockaddr_in remote = make_sockaddr("10.20.30.41", 1884u);
  TEST_ASSERT_EQUAL_INT(0, connect(fd, (const struct sockaddr *)&remote,
                                   (socklen_t)sizeof(remote)));

  TEST_ASSERT_EQUAL_INT(0,
                        getsockname(fd, (struct sockaddr *)&local, &local_len));
  TEST_ASSERT_EQUAL_UINT32(sizeof(local), local_len);
  TEST_ASSERT_EQUAL_INT(AF_INET, local.sin_family);
  TEST_ASSERT_EQUAL_UINT16(0u, ntohs(local.sin_port));

  TEST_ASSERT_EQUAL_INT(0,
                        getpeername(fd, (struct sockaddr *)&peer, &peer_len));
  TEST_ASSERT_EQUAL_UINT32(sizeof(peer), peer_len);
  TEST_ASSERT_EQUAL_INT(AF_INET, peer.sin_family);
  TEST_ASSERT_EQUAL_UINT16(1884u, ntohs(peer.sin_port));

  TEST_ASSERT_EQUAL_INT(
      0, getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &opt_len));
  TEST_ASSERT_EQUAL_INT(0, so_error);
  TEST_ASSERT_EQUAL_UINT32(sizeof(int), opt_len);

  TEST_ASSERT_EQUAL_INT(0, close(fd));

  fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);
  hal_mock_tcp_set_connect_result(false);
  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, connect(fd, (const struct sockaddr *)&remote,
                                    (socklen_t)sizeof(remote)));
  TEST_ASSERT_EQUAL_INT(ECONNREFUSED, errno);

  so_error = 0;
  opt_len = (socklen_t)sizeof(so_error);
  TEST_ASSERT_EQUAL_INT(
      0, getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &opt_len));
  TEST_ASSERT_EQUAL_INT(ECONNREFUSED, so_error);

  so_error = -1;
  opt_len = (socklen_t)sizeof(so_error);
  TEST_ASSERT_EQUAL_INT(
      0, getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &opt_len));
  TEST_ASSERT_EQUAL_INT(0, so_error);

  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_tcp_server_bind_listen_accept_returns_connected_socket_fd(void) {
  const uint8_t tx[] = {'s', 'v', 'r'};
  const uint8_t rx[] = {'c', 'l', 'i'};
  uint8_t out[4] = {};
  char remote_text[INET_ADDRSTRLEN] = {};
  struct sockaddr_in local = make_any_sockaddr(8080u);
  struct sockaddr_in accepted_addr = {};
  socklen_t accepted_len = (socklen_t)sizeof(accepted_addr);

  int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, listener_fd);
  TEST_ASSERT_EQUAL_INT(0, bind(listener_fd, (const struct sockaddr *)&local,
                                (socklen_t)sizeof(local)));
  TEST_ASSERT_EQUAL_INT(0, listen(listener_fd, 3));

  hal_tcp_listener_t listener =
      hal_mock_bsd_socket_get_tcp_listener(listener_fd);
  TEST_ASSERT_NOT_NULL(listener);
  TEST_ASSERT_EQUAL_UINT16(8080u,
                           hal_mock_tcp_listener_get_local_port(listener));
  TEST_ASSERT_EQUAL_UINT8(3u, hal_mock_tcp_listener_get_backlog(listener));

  hal_net_endpoint_t remote = make_endpoint(192u, 0u, 2u, 44u, 6000u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));

  int accepted_fd =
      accept(listener_fd, (struct sockaddr *)&accepted_addr, &accepted_len);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, accepted_fd);
  TEST_ASSERT_EQUAL_UINT32(sizeof(accepted_addr), accepted_len);
  TEST_ASSERT_EQUAL_INT(AF_INET, accepted_addr.sin_family);
  TEST_ASSERT_EQUAL_UINT16(6000u, ntohs(accepted_addr.sin_port));
  TEST_ASSERT_EQUAL_STRING("192.0.2.44",
                           inet_ntop(AF_INET, &accepted_addr.sin_addr,
                                     remote_text, sizeof(remote_text)));

  hal_tcp_socket_t accepted_tcp =
      hal_mock_bsd_socket_get_tcp_handle(accepted_fd);
  TEST_ASSERT_NOT_NULL(accepted_tcp);
  TEST_ASSERT_EQUAL_INT((int)sizeof(tx), send(accepted_fd, tx, sizeof(tx), 0));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      tx, hal_mock_tcp_get_last_tx_payload(accepted_tcp), sizeof(tx));

  hal_mock_tcp_inject_rx(accepted_tcp, rx, (uint16_t)sizeof(rx));
  TEST_ASSERT_EQUAL_INT((int)sizeof(rx),
                        recv(accepted_fd, out, sizeof(out), 0));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out, sizeof(rx));

  TEST_ASSERT_EQUAL_INT(0, close(accepted_fd));
  TEST_ASSERT_EQUAL_INT(0, close(listener_fd));
}

void test_getsockname_getpeername_for_bound_udp_and_accepted_tcp(void) {
  struct sockaddr_in local = make_any_sockaddr(9020u);
  struct sockaddr_in out = {};
  socklen_t out_len = (socklen_t)sizeof(out);

  int udp_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, udp_fd);
  TEST_ASSERT_EQUAL_INT(0, bind(udp_fd, (const struct sockaddr *)&local,
                                (socklen_t)sizeof(local)));
  TEST_ASSERT_EQUAL_INT(0,
                        getsockname(udp_fd, (struct sockaddr *)&out, &out_len));
  TEST_ASSERT_EQUAL_INT(AF_INET, out.sin_family);
  TEST_ASSERT_EQUAL_UINT16(9020u, ntohs(out.sin_port));
  TEST_ASSERT_EQUAL_INT(0, close(udp_fd));

  int listener_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, listener_fd);
  local = make_any_sockaddr(8083u);
  TEST_ASSERT_EQUAL_INT(0, bind(listener_fd, (const struct sockaddr *)&local,
                                (socklen_t)sizeof(local)));
  TEST_ASSERT_EQUAL_INT(0, listen(listener_fd, 1));

  hal_tcp_listener_t listener =
      hal_mock_bsd_socket_get_tcp_listener(listener_fd);
  TEST_ASSERT_NOT_NULL(listener);
  hal_net_endpoint_t remote = make_endpoint(192u, 0u, 2u, 91u, 4445u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));

  int accepted_fd = accept(listener_fd, NULL, NULL);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, accepted_fd);

  out_len = (socklen_t)sizeof(out);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(
      0, getsockname(accepted_fd, (struct sockaddr *)&out, &out_len));
  TEST_ASSERT_EQUAL_UINT16(8083u, ntohs(out.sin_port));

  out_len = (socklen_t)sizeof(out);
  memset(&out, 0, sizeof(out));
  TEST_ASSERT_EQUAL_INT(
      0, getpeername(accepted_fd, (struct sockaddr *)&out, &out_len));
  TEST_ASSERT_EQUAL_UINT16(4445u, ntohs(out.sin_port));

  TEST_ASSERT_EQUAL_INT(0, close(accepted_fd));
  TEST_ASSERT_EQUAL_INT(0, close(listener_fd));
}

void test_nonblocking_tcp_recv_and_msg_dontwait_report_eagain(void) {
  const uint8_t tx[] = {'n', 'b'};
  const uint8_t rx[] = {'o', 'k'};
  uint8_t out[4] = {};

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);

  struct sockaddr_in remote = make_sockaddr("10.0.0.5", 1234u);
  TEST_ASSERT_EQUAL_INT(0, connect(fd, (const struct sockaddr *)&remote,
                                   (socklen_t)sizeof(remote)));

  const int flags = fcntl(fd, F_GETFL);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(0, flags);
  TEST_ASSERT_EQUAL_INT(0, fcntl(fd, F_SETFL, flags | O_NONBLOCK));
  TEST_ASSERT_TRUE((fcntl(fd, F_GETFL) & O_NONBLOCK) != 0);

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, recv(fd, out, sizeof(out), 0));
  TEST_ASSERT_EQUAL_INT(EAGAIN, errno);

  hal_tcp_socket_t tcp = hal_mock_bsd_socket_get_tcp_handle(fd);
  TEST_ASSERT_NOT_NULL(tcp);
  hal_mock_tcp_inject_rx(tcp, rx, (uint16_t)sizeof(rx));
  TEST_ASSERT_EQUAL_INT((int)sizeof(rx), recv(fd, out, sizeof(out), 0));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out, sizeof(rx));

  TEST_ASSERT_EQUAL_INT(0, fcntl(fd, F_SETFL, 0));
  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, recv(fd, out, sizeof(out), MSG_DONTWAIT));
  TEST_ASSERT_EQUAL_INT(EAGAIN, errno);

  TEST_ASSERT_EQUAL_INT((int)sizeof(tx),
                        send(fd, tx, sizeof(tx), MSG_DONTWAIT));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, hal_mock_tcp_get_last_tx_payload(tcp),
                                sizeof(tx));

  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_nonblocking_udp_recvfrom_and_msg_dontwait(void) {
  const uint8_t tx[] = {'u', 'p'};
  const uint8_t rx[] = {'d', 'n'};
  uint8_t out[4] = {};
  struct sockaddr_in local = make_any_sockaddr(9010u);
  struct sockaddr_in remote = make_sockaddr("203.0.113.77", 9011u);
  struct sockaddr_in peer = {};
  socklen_t peer_len = (socklen_t)sizeof(peer);

  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);
  TEST_ASSERT_EQUAL_INT(
      0, bind(fd, (const struct sockaddr *)&local, (socklen_t)sizeof(local)));

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, recvfrom(fd, out, sizeof(out), MSG_DONTWAIT,
                                     (struct sockaddr *)&peer, &peer_len));
  TEST_ASSERT_EQUAL_INT(EAGAIN, errno);

  hal_udp_socket_t udp = hal_mock_bsd_socket_get_udp_handle(fd);
  TEST_ASSERT_NOT_NULL(udp);
  hal_mock_udp_inject_packet_to(udp, "198.51.100.9", 777u, rx,
                                (uint16_t)sizeof(rx));

  TEST_ASSERT_EQUAL_INT((int)sizeof(rx),
                        recvfrom(fd, out, sizeof(out), MSG_DONTWAIT,
                                 (struct sockaddr *)&peer, &peer_len));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, out, sizeof(rx));
  TEST_ASSERT_EQUAL_UINT16(777u, ntohs(peer.sin_port));

  TEST_ASSERT_EQUAL_INT((int)sizeof(tx),
                        sendto(fd, tx, sizeof(tx), MSG_DONTWAIT,
                               (const struct sockaddr *)&remote,
                               (socklen_t)sizeof(remote)));

  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_nonblocking_accept_returns_eagain_until_client_is_pending(void) {
  struct sockaddr_in local = make_any_sockaddr(8081u);
  struct sockaddr_in accepted_addr = {};
  socklen_t accepted_len = (socklen_t)sizeof(accepted_addr);

  int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, listener_fd);
  TEST_ASSERT_EQUAL_INT(0, bind(listener_fd, (const struct sockaddr *)&local,
                                (socklen_t)sizeof(local)));
  TEST_ASSERT_EQUAL_INT(0, listen(listener_fd, 1));
  TEST_ASSERT_EQUAL_INT(0, fcntl(listener_fd, F_SETFL, O_NONBLOCK));

  errno = 0;
  TEST_ASSERT_EQUAL_INT(
      -1,
      accept(listener_fd, (struct sockaddr *)&accepted_addr, &accepted_len));
  TEST_ASSERT_EQUAL_INT(EAGAIN, errno);

  hal_tcp_listener_t listener =
      hal_mock_bsd_socket_get_tcp_listener(listener_fd);
  TEST_ASSERT_NOT_NULL(listener);
  hal_net_endpoint_t remote = make_endpoint(192u, 0u, 2u, 90u, 4444u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));

  int accepted_fd =
      accept(listener_fd, (struct sockaddr *)&accepted_addr, &accepted_len);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, accepted_fd);
  TEST_ASSERT_EQUAL_UINT16(4444u, ntohs(accepted_addr.sin_port));

  TEST_ASSERT_EQUAL_INT(0, close(accepted_fd));
  TEST_ASSERT_EQUAL_INT(0, close(listener_fd));
}

void test_select_reports_tcp_listener_readiness(void) {
  struct sockaddr_in local = make_any_sockaddr(8084u);

  int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, listener_fd);
  TEST_ASSERT_EQUAL_INT(0, bind(listener_fd, (const struct sockaddr *)&local,
                                (socklen_t)sizeof(local)));
  TEST_ASSERT_EQUAL_INT(0, listen(listener_fd, 1));

  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(listener_fd, &readfds);
  struct timeval timeout = {};
  TEST_ASSERT_EQUAL_INT(
      0, select(listener_fd + 1, &readfds, NULL, NULL, &timeout));
  TEST_ASSERT_FALSE(FD_ISSET(listener_fd, &readfds));

  hal_tcp_listener_t listener =
      hal_mock_bsd_socket_get_tcp_listener(listener_fd);
  TEST_ASSERT_NOT_NULL(listener);
  hal_net_endpoint_t remote = make_endpoint(192u, 0u, 2u, 92u, 4446u);
  TEST_ASSERT_TRUE(hal_mock_tcp_listener_inject_client(listener, &remote));

  FD_ZERO(&readfds);
  FD_SET(listener_fd, &readfds);
  timeout = {};
  TEST_ASSERT_EQUAL_INT(
      1, select(listener_fd + 1, &readfds, NULL, NULL, &timeout));
  TEST_ASSERT_TRUE(FD_ISSET(listener_fd, &readfds));

  int accepted_fd = accept(listener_fd, NULL, NULL);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, accepted_fd);
  TEST_ASSERT_EQUAL_INT(0, close(accepted_fd));

  FD_ZERO(&readfds);
  FD_SET(listener_fd, &readfds);
  timeout = {};
  TEST_ASSERT_EQUAL_INT(
      0, select(listener_fd + 1, &readfds, NULL, NULL, &timeout));
  TEST_ASSERT_FALSE(FD_ISSET(listener_fd, &readfds));

  TEST_ASSERT_EQUAL_INT(0, close(listener_fd));
}

void test_nonblocking_tcp_connect_is_immediate_best_effort(void) {
  struct sockaddr_in remote = make_sockaddr("10.1.2.5", 1885u);
  int so_error = 0;
  socklen_t opt_len = (socklen_t)sizeof(so_error);

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);
  TEST_ASSERT_EQUAL_INT(0, fcntl(fd, F_SETFL, O_NONBLOCK));

  hal_mock_tcp_set_connect_result(false);
  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, connect(fd, (const struct sockaddr *)&remote,
                                    (socklen_t)sizeof(remote)));
  TEST_ASSERT_EQUAL_INT(EINPROGRESS, errno);

  TEST_ASSERT_EQUAL_INT(
      0, getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &opt_len));
  TEST_ASSERT_EQUAL_INT(EINPROGRESS, so_error);

  fd_set writefds;
  FD_ZERO(&writefds);
  FD_SET(fd, &writefds);
  struct timeval timeout = {};
  TEST_ASSERT_EQUAL_INT(0, select(fd + 1, NULL, &writefds, NULL, &timeout));
  TEST_ASSERT_FALSE(FD_ISSET(fd, &writefds));

  hal_mock_tcp_set_connect_result(true);
  TEST_ASSERT_EQUAL_INT(0, connect(fd, (const struct sockaddr *)&remote,
                                   (socklen_t)sizeof(remote)));

  FD_ZERO(&writefds);
  FD_SET(fd, &writefds);
  timeout = {};
  TEST_ASSERT_EQUAL_INT(1, select(fd + 1, NULL, &writefds, NULL, &timeout));
  TEST_ASSERT_TRUE(FD_ISSET(fd, &writefds));

  so_error = -1;
  opt_len = (socklen_t)sizeof(so_error);
  TEST_ASSERT_EQUAL_INT(
      0, getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &opt_len));
  TEST_ASSERT_EQUAL_INT(0, so_error);

  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_blocking_accept_backend_listener_error_reports_einval(void) {
  struct sockaddr_in local = make_any_sockaddr(8082u);

  int listener_fd = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, listener_fd);
  TEST_ASSERT_EQUAL_INT(0, bind(listener_fd, (const struct sockaddr *)&local,
                                (socklen_t)sizeof(local)));
  TEST_ASSERT_EQUAL_INT(0, listen(listener_fd, 1));

  hal_tcp_listener_t listener =
      hal_mock_bsd_socket_get_tcp_listener(listener_fd);
  TEST_ASSERT_NOT_NULL(listener);
  hal_tcp_listener_close(listener);

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, accept(listener_fd, NULL, NULL));
  TEST_ASSERT_EQUAL_INT(EINVAL, errno);

  TEST_ASSERT_EQUAL_INT(0, close(listener_fd));
}

void test_select_reports_read_and_write_ready_sets(void) {
  const uint8_t rx[] = {'s', 'e', 'l'};
  struct sockaddr_in local_a = make_any_sockaddr(9100u);
  struct sockaddr_in local_b = make_any_sockaddr(9101u);
  struct sockaddr_in remote = make_sockaddr("10.1.2.3", 1883u);

  int udp_idle_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int udp_ready_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int tcp_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, udp_idle_fd);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, udp_ready_fd);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, tcp_fd);

  TEST_ASSERT_EQUAL_INT(0, bind(udp_idle_fd, (const struct sockaddr *)&local_a,
                                (socklen_t)sizeof(local_a)));
  TEST_ASSERT_EQUAL_INT(0, bind(udp_ready_fd, (const struct sockaddr *)&local_b,
                                (socklen_t)sizeof(local_b)));
  TEST_ASSERT_EQUAL_INT(0, connect(tcp_fd, (const struct sockaddr *)&remote,
                                   (socklen_t)sizeof(remote)));

  hal_udp_socket_t udp_ready = hal_mock_bsd_socket_get_udp_handle(udp_ready_fd);
  TEST_ASSERT_NOT_NULL(udp_ready);
  hal_mock_udp_inject_packet_to(udp_ready, "198.51.100.10", 5000u, rx,
                                (uint16_t)sizeof(rx));

  fd_set readfds;
  fd_set writefds;
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_SET(udp_idle_fd, &readfds);
  FD_SET(udp_ready_fd, &readfds);
  FD_SET(tcp_fd, &writefds);

  struct timeval timeout = {};
  const int max_fd = tcp_fd > udp_ready_fd ? tcp_fd : udp_ready_fd;
  TEST_ASSERT_EQUAL_INT(
      2, select(max_fd + 1, &readfds, &writefds, NULL, &timeout));
  TEST_ASSERT_FALSE(FD_ISSET(udp_idle_fd, &readfds));
  TEST_ASSERT_TRUE(FD_ISSET(udp_ready_fd, &readfds));
  TEST_ASSERT_TRUE(FD_ISSET(tcp_fd, &writefds));

  TEST_ASSERT_EQUAL_INT(0, close(tcp_fd));
  TEST_ASSERT_EQUAL_INT(0, close(udp_ready_fd));
  TEST_ASSERT_EQUAL_INT(0, close(udp_idle_fd));
}

void test_select_reports_tcp_exception_after_connected_socket_shutdown(void) {
  struct sockaddr_in remote = make_sockaddr("10.1.2.4", 1884u);

  int fresh_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int connected_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fresh_fd);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, connected_fd);
  TEST_ASSERT_EQUAL_INT(0,
                        connect(connected_fd, (const struct sockaddr *)&remote,
                                (socklen_t)sizeof(remote)));
  TEST_ASSERT_EQUAL_INT(0, shutdown(connected_fd, SHUT_RDWR));

  fd_set exceptfds;
  FD_ZERO(&exceptfds);
  FD_SET(fresh_fd, &exceptfds);
  FD_SET(connected_fd, &exceptfds);

  struct timeval timeout = {};
  const int max_fd = connected_fd > fresh_fd ? connected_fd : fresh_fd;
  TEST_ASSERT_EQUAL_INT(1,
                        select(max_fd + 1, NULL, NULL, &exceptfds, &timeout));
  TEST_ASSERT_FALSE(FD_ISSET(fresh_fd, &exceptfds));
  TEST_ASSERT_TRUE(FD_ISSET(connected_fd, &exceptfds));

  TEST_ASSERT_EQUAL_INT(0, close(connected_fd));
  TEST_ASSERT_EQUAL_INT(0, close(fresh_fd));
}

void test_getaddrinfo_hostname_result_connects_tcp_client(void) {
  struct addrinfo hints = {};
  struct addrinfo *resolved = NULL;
  hal_net_endpoint_t captured_remote = {};

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_CANONNAME;

  TEST_ASSERT_TRUE(
      hal_mock_net_set_dns_entry("broker.example", "203.0.113.45"));
  TEST_ASSERT_EQUAL_INT(
      0, getaddrinfo("broker.example", "1883", &hints, &resolved));
  TEST_ASSERT_NOT_NULL(resolved);
  TEST_ASSERT_EQUAL_INT(AF_INET, resolved->ai_family);
  TEST_ASSERT_EQUAL_INT(SOCK_STREAM, resolved->ai_socktype);
  TEST_ASSERT_EQUAL_INT(IPPROTO_TCP, resolved->ai_protocol);
  TEST_ASSERT_EQUAL_UINT32(sizeof(struct sockaddr_in), resolved->ai_addrlen);
  TEST_ASSERT_EQUAL_STRING("broker.example", resolved->ai_canonname);

  const struct sockaddr_in *addr =
      (const struct sockaddr_in *)resolved->ai_addr;
  TEST_ASSERT_EQUAL_UINT16(1883u, ntohs(addr->sin_port));

  int fd =
      socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);
  TEST_ASSERT_EQUAL_INT(0,
                        connect(fd, resolved->ai_addr, resolved->ai_addrlen));

  hal_tcp_socket_t tcp = hal_mock_bsd_socket_get_tcp_handle(fd);
  TEST_ASSERT_NOT_NULL(tcp);
  TEST_ASSERT_TRUE(hal_mock_tcp_get_remote_endpoint(tcp, &captured_remote));
  TEST_ASSERT_EQUAL_UINT8(203u, captured_remote.addr[0]);
  TEST_ASSERT_EQUAL_UINT8(45u, captured_remote.addr[3]);
  TEST_ASSERT_EQUAL_UINT16(1883u, captured_remote.port);

  TEST_ASSERT_EQUAL_INT(0, close(fd));
  freeaddrinfo(resolved);
}

void test_getaddrinfo_passive_numeric_and_errors(void) {
  struct addrinfo hints = {};
  struct addrinfo *resolved = NULL;
  char text[INET_ADDRSTRLEN] = {};

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  TEST_ASSERT_EQUAL_INT(0, getaddrinfo(NULL, "9000", &hints, &resolved));
  TEST_ASSERT_NOT_NULL(resolved);

  const struct sockaddr_in *addr =
      (const struct sockaddr_in *)resolved->ai_addr;
  TEST_ASSERT_EQUAL_UINT16(9000u, ntohs(addr->sin_port));
  TEST_ASSERT_EQUAL_STRING(
      "0.0.0.0", inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text)));
  freeaddrinfo(resolved);
  resolved = NULL;

  hints.ai_flags = AI_NUMERICHOST;
  TEST_ASSERT_EQUAL_INT(
      EAI_NONAME, getaddrinfo("example.invalid", "80", &hints, &resolved));
  TEST_ASSERT_NULL(resolved);

  TEST_ASSERT_EQUAL_INT(EAI_SERVICE,
                        getaddrinfo("127.0.0.1", "http", NULL, &resolved));
  TEST_ASSERT_NULL(resolved);

  hints.ai_family = 10;
  TEST_ASSERT_EQUAL_INT(EAI_FAMILY,
                        getaddrinfo("127.0.0.1", "80", &hints, &resolved));
  TEST_ASSERT_NULL(resolved);
}

void test_setsockopt_accepts_reuseaddr_and_reports_unsupported_options(void) {
  int opt = 1;
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);

  TEST_ASSERT_EQUAL_INT(0, setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                                      (socklen_t)sizeof(opt)));
  TEST_ASSERT_EQUAL_INT(0, setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt,
                                      (socklen_t)sizeof(opt)));

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt,
                                       (socklen_t)sizeof(opt)));
  TEST_ASSERT_EQUAL_INT(ENOPROTOOPT, errno);

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, NULL,
                                       (socklen_t)sizeof(opt)));
  TEST_ASSERT_EQUAL_INT(EINVAL, errno);

  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_socket_timeouts_round_trip_through_sockopts(void) {
  struct timeval timeout = {};
  struct timeval out_timeout = {};
  socklen_t out_len = (socklen_t)sizeof(out_timeout);

  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, fd);

  timeout.tv_sec = 1;
  timeout.tv_usec = 250000;
  TEST_ASSERT_EQUAL_INT(0, setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                      (socklen_t)sizeof(timeout)));

  timeout.tv_sec = 0;
  timeout.tv_usec = 1500;
  TEST_ASSERT_EQUAL_INT(0, setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                                      (socklen_t)sizeof(timeout)));

  TEST_ASSERT_EQUAL_INT(
      0, getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &out_timeout, &out_len));
  TEST_ASSERT_EQUAL_UINT32(sizeof(out_timeout), out_len);
  TEST_ASSERT_EQUAL_INT(1, out_timeout.tv_sec);
  TEST_ASSERT_EQUAL_INT(250000, out_timeout.tv_usec);

  memset(&out_timeout, 0, sizeof(out_timeout));
  out_len = (socklen_t)sizeof(out_timeout);
  TEST_ASSERT_EQUAL_INT(
      0, getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &out_timeout, &out_len));
  TEST_ASSERT_EQUAL_INT(0, out_timeout.tv_sec);
  TEST_ASSERT_EQUAL_INT(2000, out_timeout.tv_usec);

  timeout.tv_sec = -1;
  timeout.tv_usec = 0;
  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                       (socklen_t)sizeof(timeout)));
  TEST_ASSERT_EQUAL_INT(EINVAL, errno);

  TEST_ASSERT_EQUAL_INT(0, close(fd));
}

void test_errors_set_errno_for_invalid_and_unsupported_operations(void) {
  struct sockaddr_in remote = make_sockaddr("203.0.113.8", 443u);
  const uint8_t payload[] = {0x42u};

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, socket(AF_UNSPEC, SOCK_STREAM, 0));
  TEST_ASSERT_EQUAL_INT(EAFNOSUPPORT, errno);

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, socket(AF_INET, SOCK_STREAM, IPPROTO_UDP));
  TEST_ASSERT_EQUAL_INT(EPROTONOSUPPORT, errno);

  int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, udp_fd);
  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, send(udp_fd, payload, sizeof(payload), 0));
  TEST_ASSERT_EQUAL_INT(ENOTCONN, errno);
  TEST_ASSERT_EQUAL_INT(0, close(udp_fd));

  int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_GREATER_OR_EQUAL_INT(HAL_BSD_SOCKET_FD_BASE, tcp_fd);
  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, connect(tcp_fd, (const struct sockaddr *)&remote,
                                    (socklen_t)(sizeof(remote) - 1u)));
  TEST_ASSERT_EQUAL_INT(EINVAL, errno);

  hal_mock_tcp_set_connect_result(false);
  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, connect(tcp_fd, (const struct sockaddr *)&remote,
                                    (socklen_t)sizeof(remote)));
  TEST_ASSERT_EQUAL_INT(ECONNREFUSED, errno);
  TEST_ASSERT_EQUAL_INT(0, close(tcp_fd));

  errno = 0;
  TEST_ASSERT_EQUAL_INT(-1, close(tcp_fd));
  TEST_ASSERT_EQUAL_INT(EBADF, errno);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_inet_helpers_translate_ipv4_and_byte_order);
  RUN_TEST(test_udp_sendto_autobinds_and_translates_remote_sockaddr);
  RUN_TEST(test_udp_autobind_skips_ephemeral_port_already_bound);
  RUN_TEST(test_udp_bind_and_recvfrom_translate_sender_sockaddr);
  RUN_TEST(test_connected_udp_send_write_recv_read_use_connected_peer);
  RUN_TEST(test_tcp_client_connect_send_recv_and_unistd_aliases);
  RUN_TEST(test_getsockname_getpeername_and_so_error_for_tcp_client);
  RUN_TEST(test_tcp_server_bind_listen_accept_returns_connected_socket_fd);
  RUN_TEST(test_getsockname_getpeername_for_bound_udp_and_accepted_tcp);
  RUN_TEST(test_nonblocking_tcp_recv_and_msg_dontwait_report_eagain);
  RUN_TEST(test_nonblocking_udp_recvfrom_and_msg_dontwait);
  RUN_TEST(test_nonblocking_accept_returns_eagain_until_client_is_pending);
  RUN_TEST(test_select_reports_tcp_listener_readiness);
  RUN_TEST(test_nonblocking_tcp_connect_is_immediate_best_effort);
  RUN_TEST(test_blocking_accept_backend_listener_error_reports_einval);
  RUN_TEST(test_select_reports_read_and_write_ready_sets);
  RUN_TEST(test_select_reports_tcp_exception_after_connected_socket_shutdown);
  RUN_TEST(test_getaddrinfo_hostname_result_connects_tcp_client);
  RUN_TEST(test_getaddrinfo_passive_numeric_and_errors);
  RUN_TEST(test_setsockopt_accepts_reuseaddr_and_reports_unsupported_options);
  RUN_TEST(test_socket_timeouts_round_trip_through_sockopts);
  RUN_TEST(test_errors_set_errno_for_invalid_and_unsupported_operations);
  return UNITY_END();
}
