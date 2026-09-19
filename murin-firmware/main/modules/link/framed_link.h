#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FRAMED_LINK_MSG_ACK = 0x7E,
  FRAMED_LINK_MSG_NACK = 0x7F,
} framed_link_msg_type_t;

typedef enum {
  FRAMED_LINK_ERR_CRC = 0x01,
  FRAMED_LINK_ERR_LEN = 0x02,
  FRAMED_LINK_ERR_FORMAT = 0x03,
} framed_link_error_code_t;

#define FRAMED_LINK_MAX_PAYLOAD_LEN 256
#define FRAMED_LINK_MAX_STUFFED_LEN 512

typedef size_t (*framed_link_write_fn_t)(uint8_t *data, size_t len);
typedef void (*framed_link_rx_fn_t)(void *ctx, uint8_t msg_type, uint8_t seq, const uint8_t *payload,
                                    size_t payload_len);

typedef struct {
  uint8_t parse_buf[1024];
  size_t parse_len;
  uint8_t rx_seq;
  framed_link_write_fn_t write;
  void *write_ctx;
  framed_link_rx_fn_t on_frame;
  void *on_frame_ctx;
} framed_link_t;

void framed_link_init(framed_link_t *link, framed_link_write_fn_t write, void *write_ctx, framed_link_rx_fn_t on_frame,
                      void *on_frame_ctx);
void framed_link_process(framed_link_t *link, uint8_t *data, size_t len);
size_t framed_link_send_frame(framed_link_t *link, uint8_t msg_type, uint8_t seq, const uint8_t *payload,
                              size_t payload_len);
size_t framed_link_send_ack(framed_link_t *link, uint8_t seq);
size_t framed_link_send_nack(framed_link_t *link, uint8_t seq, uint8_t err);

#ifdef __cplusplus
}
#endif
