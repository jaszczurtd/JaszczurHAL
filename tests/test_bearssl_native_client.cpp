#include "hal/network/tls/BearSSL/jh_bearssl_bsd_io.h"
#include "hal/network/tls/BearSSL/jh_bearssl_engine.h"
#include "hal/network/tls/BearSSL/jh_bearssl_provider.h"
#include "hal/network/tls/hal_tls.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C" uint32_t hal_millis(void) {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
extern "C" void hal_delay_ms(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
extern "C" void hal_idle(void) {}

struct dn_collector_t {
  std::vector<uint8_t> bytes;
};

static void collect_dn(void *context, const void *data, size_t length) {
  dn_collector_t *collector = static_cast<dn_collector_t *>(context);
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  collector->bytes.insert(collector->bytes.end(), bytes, bytes + length);
}

int main(int argc, char **argv) {
  if (argc != 6) {
    return 2;
  }
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> der((std::istreambuf_iterator<char>(input)), {});
  if (der.empty()) {
    return 3;
  }
  dn_collector_t dn;
  br_x509_decoder_context decoder = {};
  br_x509_decoder_init(&decoder, collect_dn, &dn, nullptr, nullptr);
  br_x509_decoder_push(&decoder, der.data(), der.size());
  br_x509_pkey *key = br_x509_decoder_get_pkey(&decoder);
  if (key == nullptr || dn.bytes.empty() || key->key_type != BR_KEYTYPE_RSA) {
    return 4;
  }
  hal_tls_trust_anchor_t anchor = {};
  anchor.subject_dn = dn.bytes.data();
  anchor.subject_dn_length = dn.bytes.size();
  anchor.key_type = HAL_TLS_TRUST_KEY_RSA;
  anchor.key.rsa.modulus = key->key.rsa.n;
  anchor.key.rsa.modulus_length = key->key.rsa.nlen;
  anchor.key.rsa.exponent = key->key.rsa.e;
  anchor.key.rsa.exponent_length = key->key.rsa.elen;

  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in remote = {};
  remote.sin_family = AF_INET;
  remote.sin_port = htons((uint16_t)strtoul(argv[3], nullptr, 10));
  inet_pton(AF_INET, "127.0.0.1", &remote.sin_addr);
  if (fd < 0 ||
      connect(fd, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)) != 0) {
    return 5;
  }
  jh_bearssl_bsd_transport_t transport = {};
  if (jh_bearssl_bsd_transport_init(&transport, fd) != HAL_OK) {
    close(fd);
    return 5;
  }

  uint8_t entropy[JH_BEARSSL_ENTROPY_SIZE];
  for (size_t index = 0; index < sizeof(entropy); ++index) {
    entropy[index] = (uint8_t)(index * 17u + 3u);
  }
  jh_bearssl_client_t provider = {};
  hal_status_t status = jh_bearssl_client_init(&provider, &anchor, 1u, argv[2],
                                               strtoull(argv[4], nullptr, 10),
                                               entropy, sizeof(entropy));
  if (status != HAL_OK) {
    std::fprintf(stderr, "client init failed: %d\n", (int)status);
  }
  const bool expect_success = argv[5][0] == '1';
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (status == HAL_OK || status == HAL_EAGAIN) {
    jh_bearssl_poll_result_t result = {};
    status = jh_bearssl_engine_poll(&provider.client.eng, &transport.transport,
                                    8u, &result);
    if (status == HAL_OK &&
        (result.event == JH_BEARSSL_EVENT_APPLICATION_READABLE ||
         result.event == JH_BEARSSL_EVENT_APPLICATION_WRITABLE)) {
      close(fd);
      return expect_success ? 0 : 6;
    }
    if (status != HAL_OK && status != HAL_EAGAIN) {
      if (result.engine_error != 0) {
        status = jh_bearssl_error_to_hal(result.engine_error);
      }
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      close(fd);
      return 7;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  close(fd);
  std::fprintf(stderr, "handshake failed: status=%d engine=%d\n", (int)status,
               br_ssl_engine_last_error(&provider.client.eng));
  return !expect_success && status == HAL_EAUTH ? 0 : 8;
}
