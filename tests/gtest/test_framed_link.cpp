#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "framed_link.h"
}

namespace {

struct CapturedFrame {
  uint8_t type = 0;
  uint8_t sequence = 0;
  std::vector<uint8_t> payload;
  size_t count = 0;
};

struct WireCapture {
  std::vector<uint8_t> bytes;
};

WireCapture *g_wire_capture = nullptr;

size_t CaptureWrite(uint8_t *data, size_t length)
{
  if (g_wire_capture == nullptr)
    return 0;
  g_wire_capture->bytes.assign(data, data + length);
  return length;
}

void CaptureFrame(void *context, uint8_t type, uint8_t sequence, const uint8_t *payload, size_t payload_length)
{
  auto *frame = static_cast<CapturedFrame *>(context);
  frame->type = type;
  frame->sequence = sequence;
  frame->payload.assign(payload, payload + payload_length);
  frame->count++;
}

void InitLink(framed_link_t *link, WireCapture *wire, CapturedFrame *frame)
{
  g_wire_capture = wire;
  framed_link_init(link, CaptureWrite, nullptr, CaptureFrame, frame);
}

uint16_t Crc16(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

std::vector<uint8_t> MakeRawFrame(uint8_t type, uint8_t sequence, const std::vector<uint8_t> &stuffed_payload)
{
  std::vector<uint8_t> frame(7 + stuffed_payload.size());
  frame[0] = 0xAA;
  frame[1] = type;
  frame[2] = sequence;
  frame[3] = static_cast<uint8_t>(stuffed_payload.size());
  frame[4] = static_cast<uint8_t>(stuffed_payload.size() >> 8);
  std::memcpy(frame.data() + 5, stuffed_payload.data(), stuffed_payload.size());
  const uint16_t crc = Crc16(frame.data() + 1, 4 + stuffed_payload.size());
  frame[5 + stuffed_payload.size()] = static_cast<uint8_t>(crc);
  frame[6 + stuffed_payload.size()] = static_cast<uint8_t>(crc >> 8);
  return frame;
}

CapturedFrame Decode(const std::vector<uint8_t> &wire)
{
  framed_link_t parser{};
  CapturedFrame frame;
  framed_link_init(&parser, nullptr, nullptr, CaptureFrame, &frame);
  std::vector<uint8_t> bytes = wire;
  framed_link_process(&parser, bytes.data(), bytes.size());
  return frame;
}

class FramedLinkTest : public ::testing::Test {
protected:
  void SetUp() override { InitLink(&link_, &wire_, &received_); }

  framed_link_t link_{};
  WireCapture wire_;
  CapturedFrame received_;
};

TEST_F(FramedLinkTest, SendsAndReceivesEscapedPayload)
{
  const std::vector<uint8_t> payload = {0x00, 0xAA, 0x1B, 0x55};

  ASSERT_NE(framed_link_send_frame(&link_, 0x42, 7, payload.data(), payload.size()), 0u);
  ASSERT_EQ(wire_.bytes.size(), 13u);
  EXPECT_EQ(wire_.bytes[0], 0xAA);
  EXPECT_EQ(wire_.bytes[5], 0x00);
  EXPECT_EQ(wire_.bytes[6], 0x1B);
  EXPECT_EQ(wire_.bytes[7], 0x8A);
  EXPECT_EQ(wire_.bytes[8], 0x1B);
  EXPECT_EQ(wire_.bytes[9], 0x3B);

  std::vector<uint8_t> input = wire_.bytes;
  framed_link_process(&link_, input.data(), input.size());

  EXPECT_EQ(received_.count, 1u);
  EXPECT_EQ(received_.type, 0x42);
  EXPECT_EQ(received_.sequence, 7);
  EXPECT_EQ(received_.payload, payload);
  EXPECT_EQ(link_.rx_seq, 7);
}

TEST_F(FramedLinkTest, AcceptsFragmentedAndConcatenatedFrames)
{
  framed_link_t sender{};
  WireCapture first_wire;
  InitLink(&sender, &first_wire, nullptr);
  const uint8_t first_payload[] = {1, 2, 3};
  ASSERT_NE(framed_link_send_frame(&sender, 0x10, 1, first_payload, sizeof(first_payload)), 0u);
  const std::vector<uint8_t> first = first_wire.bytes;

  first_wire.bytes.clear();
  const uint8_t second_payload[] = {4, 5};
  ASSERT_NE(framed_link_send_frame(&sender, 0x11, 2, second_payload, sizeof(second_payload)), 0u);
  std::vector<uint8_t> input = first;
  input.insert(input.end(), first_wire.bytes.begin(), first_wire.bytes.end());

  framed_link_process(&link_, input.data(), 3);
  EXPECT_EQ(received_.count, 0u);
  framed_link_process(&link_, input.data() + 3, input.size() - 3);

  EXPECT_EQ(received_.count, 2u);
  EXPECT_EQ(received_.type, 0x11);
  EXPECT_EQ(received_.sequence, 2);
  EXPECT_EQ(received_.payload, (std::vector<uint8_t>{4, 5}));
}

TEST_F(FramedLinkTest, KeepsNewestBytesAndSkipsNoiseWhenInputExceedsBuffer)
{
  const std::vector<uint8_t> frame = MakeRawFrame(0x12, 3, {});
  std::vector<uint8_t> input(sizeof(link_.parse_buf) + 1, 0x00);
  input.insert(input.end(), frame.begin(), frame.end());

  framed_link_process(&link_, input.data(), input.size());

  EXPECT_EQ(received_.count, 1u);
  EXPECT_EQ(received_.type, 0x12);
  EXPECT_EQ(received_.sequence, 3);
  EXPECT_TRUE(received_.payload.empty());
  EXPECT_EQ(link_.parse_len, 0u);
}

TEST_F(FramedLinkTest, RejectsBadCrcWithNack)
{
  const uint8_t payload[] = {1, 2};
  ASSERT_NE(framed_link_send_frame(&link_, 0x20, 9, payload, sizeof(payload)), 0u);
  wire_.bytes[5] ^= 0x01;

  std::vector<uint8_t> input = wire_.bytes;
  wire_.bytes.clear();
  framed_link_process(&link_, input.data(), input.size());

  EXPECT_EQ(received_.count, 0u);
  const CapturedFrame nack = Decode(wire_.bytes);
  EXPECT_EQ(nack.type, FRAMED_LINK_MSG_NACK);
  EXPECT_EQ(nack.sequence, 9);
  EXPECT_EQ(nack.payload, (std::vector<uint8_t>{9, FRAMED_LINK_ERR_CRC}));
}

TEST_F(FramedLinkTest, RejectsDanglingEscapeWithFormatNack)
{
  std::vector<uint8_t> input = MakeRawFrame(0x21, 10, {0x1B});
  wire_.bytes.clear();
  framed_link_process(&link_, input.data(), input.size());

  EXPECT_EQ(received_.count, 0u);
  const CapturedFrame nack = Decode(wire_.bytes);
  EXPECT_EQ(nack.type, FRAMED_LINK_MSG_NACK);
  EXPECT_EQ(nack.sequence, 10);
  EXPECT_EQ(nack.payload, (std::vector<uint8_t>{10, FRAMED_LINK_ERR_FORMAT}));
}

TEST_F(FramedLinkTest, RejectsOversizedStuffedPayloadWithLengthNack)
{
  std::vector<uint8_t> input = {0xAA, 0x22, 11, 0x01, 0x02, 0x00, 0x00};
  wire_.bytes.clear();
  framed_link_process(&link_, input.data(), input.size());

  const CapturedFrame nack = Decode(wire_.bytes);
  EXPECT_EQ(nack.type, FRAMED_LINK_MSG_NACK);
  EXPECT_EQ(nack.sequence, 11);
  EXPECT_EQ(nack.payload, (std::vector<uint8_t>{11, FRAMED_LINK_ERR_LEN}));
}

TEST_F(FramedLinkTest, SendsAckAndNackPayloads)
{
  ASSERT_NE(framed_link_send_ack(&link_, 12), 0u);
  CapturedFrame ack = Decode(wire_.bytes);
  EXPECT_EQ(ack.type, FRAMED_LINK_MSG_ACK);
  EXPECT_EQ(ack.sequence, 12);
  EXPECT_EQ(ack.payload, (std::vector<uint8_t>{12}));

  wire_.bytes.clear();
  ASSERT_NE(framed_link_send_nack(&link_, 13, FRAMED_LINK_ERR_FORMAT), 0u);
  CapturedFrame nack = Decode(wire_.bytes);
  EXPECT_EQ(nack.type, FRAMED_LINK_MSG_NACK);
  EXPECT_EQ(nack.sequence, 13);
  EXPECT_EQ(nack.payload, (std::vector<uint8_t>{13, FRAMED_LINK_ERR_FORMAT}));
}

TEST_F(FramedLinkTest, RejectsNullPayloadAndPayloadsLargerThanMaximum)
{
  EXPECT_EQ(framed_link_send_frame(&link_, 0x30, 1, nullptr, 1), 0u);

  std::vector<uint8_t> payload(FRAMED_LINK_MAX_PAYLOAD_LEN + 1, 0x55);
  EXPECT_EQ(framed_link_send_frame(&link_, 0x30, 1, payload.data(), payload.size()), 0u);
  EXPECT_TRUE(wire_.bytes.empty());
}

} // namespace
