#include "framed_link.h"

#include <stdbool.h>
#include <string.h>

#define FRAMED_LINK_SOF 0xAA
#define FRAMED_LINK_ESCAPE 0x1B
#define FRAMED_LINK_ESCAPE_XOR 0x20

static uint16_t framed_link_crc16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
      crc &= 0xFFFF;
    }
  }

  return crc;
}

static bool framed_link_unstuff(const uint8_t *stuffed, size_t len, uint8_t *unstuffed, size_t unstuffed_size,
                                size_t *out_len)
{
  size_t decoded_len = 0;

  for (size_t i = 0; i < len; i++) {
    uint8_t value = stuffed[i];

    if (value == FRAMED_LINK_ESCAPE) {
      i++;
      if (i >= len)
        return false;
      value = stuffed[i] ^ FRAMED_LINK_ESCAPE_XOR;
    }

    if (decoded_len >= unstuffed_size)
      return false;
    unstuffed[decoded_len++] = value;
  }

  *out_len = decoded_len;
  return true;
}

static bool framed_link_stuff(const uint8_t *data, size_t len, uint8_t *stuffed, size_t stuffed_size, size_t *out_len)
{
  size_t stuffed_len = 0;

  for (size_t i = 0; i < len; i++) {
    if (data[i] == FRAMED_LINK_SOF || data[i] == FRAMED_LINK_ESCAPE) {
      if (stuffed_len + 2 > stuffed_size)
        return false;
      stuffed[stuffed_len++] = FRAMED_LINK_ESCAPE;
      stuffed[stuffed_len++] = data[i] ^ FRAMED_LINK_ESCAPE_XOR;
    } else {
      if (stuffed_len + 1 > stuffed_size)
        return false;
      stuffed[stuffed_len++] = data[i];
    }
  }

  *out_len = stuffed_len;
  return true;
}

void framed_link_init(framed_link_t *link, framed_link_write_fn_t write, void *write_ctx, framed_link_rx_fn_t on_frame,
                      void *on_frame_ctx)
{
  memset(link, 0, sizeof(*link));
  link->write = write;
  link->write_ctx = write_ctx;
  link->on_frame = on_frame;
  link->on_frame_ctx = on_frame_ctx;
}

size_t framed_link_send_frame(framed_link_t *link, uint8_t msg_type, uint8_t seq, const uint8_t *payload,
                              size_t payload_len)
{
  if (payload_len > FRAMED_LINK_MAX_PAYLOAD_LEN || (payload_len > 0 && payload == NULL))
    return 0;

  uint8_t stuffed[FRAMED_LINK_MAX_STUFFED_LEN];
  size_t stuffed_len = 0;
  if (!framed_link_stuff(payload, payload_len, stuffed, sizeof(stuffed), &stuffed_len))
    return 0;

  uint8_t frame[1 + 4 + sizeof(stuffed) + 2];
  frame[0] = FRAMED_LINK_SOF;
  frame[1] = msg_type;
  frame[2] = seq;
  frame[3] = stuffed_len & 0xFF;
  frame[4] = (stuffed_len >> 8) & 0xFF;
  memcpy(frame + 5, stuffed, stuffed_len);

  const uint16_t crc = framed_link_crc16(frame + 1, 4 + stuffed_len);
  frame[5 + stuffed_len] = crc & 0xFF;
  frame[6 + stuffed_len] = (crc >> 8) & 0xFF;

  if (link->write != NULL) {
    const size_t frame_len = 7 + stuffed_len;
    return link->write(frame, frame_len) == frame_len ? frame_len : 0;
  }

  return 0;
}

size_t framed_link_send_ack(framed_link_t *link, uint8_t seq)
{
  return framed_link_send_frame(link, FRAMED_LINK_MSG_ACK, seq, &seq, 1);
}

size_t framed_link_send_nack(framed_link_t *link, uint8_t seq, uint8_t err)
{
  uint8_t payload[2] = {seq, err};
  return framed_link_send_frame(link, FRAMED_LINK_MSG_NACK, seq, payload, sizeof(payload));
}

void framed_link_process(framed_link_t *link, uint8_t *data, size_t len)
{
  if (len == 0) {
    return;
  }

  if (link->parse_len + len > sizeof(link->parse_buf)) {
    link->parse_len = 0;
  }

  if (len > sizeof(link->parse_buf)) {
    data += len - sizeof(link->parse_buf);
    len = sizeof(link->parse_buf);
  }

  memcpy(link->parse_buf + link->parse_len, data, len);
  link->parse_len += len;

  size_t offset = 0;
  while (offset + 7 <= link->parse_len) {
    if (link->parse_buf[offset] != FRAMED_LINK_SOF) {
      offset++;
      continue;
    }

    const uint8_t msg_type = link->parse_buf[offset + 1];
    const uint8_t seq = link->parse_buf[offset + 2];
    const uint16_t length = link->parse_buf[offset + 3] | (link->parse_buf[offset + 4] << 8);
    const size_t frame_len = length + 7;

    link->rx_seq = seq;

    if (length > FRAMED_LINK_MAX_STUFFED_LEN) {
      framed_link_send_nack(link, seq, FRAMED_LINK_ERR_LEN);
      offset++;
      continue;
    }

    if (offset + frame_len > link->parse_len) {
      break;
    }

    const uint16_t crc_received = link->parse_buf[offset + 5 + length] | (link->parse_buf[offset + 6 + length] << 8);
    const uint16_t crc_computed = framed_link_crc16(link->parse_buf + offset + 1, 4 + length);

    if (crc_computed != crc_received) {
      framed_link_send_nack(link, seq, FRAMED_LINK_ERR_CRC);
      offset += frame_len;
      continue;
    }

    uint8_t payload[FRAMED_LINK_MAX_PAYLOAD_LEN];
    const uint8_t *stuffed_payload = link->parse_buf + offset + 5;
    size_t payload_len = 0;
    if (!framed_link_unstuff(stuffed_payload, length, payload, sizeof(payload), &payload_len)) {
      framed_link_send_nack(link, seq, FRAMED_LINK_ERR_FORMAT);
      offset += frame_len;
      continue;
    }

    if (link->on_frame != NULL) {
      link->on_frame(link->on_frame_ctx, msg_type, seq, payload, payload_len);
    }
    offset += frame_len;
  }

  if (offset > 0) {
    memmove(link->parse_buf, link->parse_buf + offset, link->parse_len - offset);
    link->parse_len -= offset;
  }
}
