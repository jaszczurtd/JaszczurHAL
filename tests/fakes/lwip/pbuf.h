#pragma once

#include "err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pbuf_layer {
  PBUF_TRANSPORT = 0,
  PBUF_RAW,
} pbuf_layer;

typedef enum pbuf_type {
  PBUF_RAM = 0,
} pbuf_type;

struct pbuf {
  struct pbuf *next;
  void *payload;
  uint16_t tot_len;
  uint16_t len;
  uint8_t *allocation;
};

struct pbuf *pbuf_alloc(pbuf_layer layer, uint16_t length, pbuf_type type);
uint8_t pbuf_free(struct pbuf *packet);
err_t pbuf_take(struct pbuf *packet, const void *source, size_t length);
void pbuf_cat(struct pbuf *head, struct pbuf *tail);
uint16_t pbuf_copy_partial(const struct pbuf *packet, void *destination,
                           uint16_t length, uint16_t offset);
struct pbuf *pbuf_free_header(struct pbuf *packet, uint16_t length);

#ifdef __cplusplus
}
#endif
