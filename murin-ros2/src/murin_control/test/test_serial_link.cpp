#include <pty.h>
#include <stdexcept>
#include "murin_control/serial_link.hpp"
using namespace murin_control;
void check(bool value)
{
  if (!value)
  {
    throw std::runtime_error("Serial protocol assertion failed");
  }
}
int main()
{
  check(frame(0, 0, {}) == Bytes({0xaa, 0, 0, 0, 0, 0xc0, 0x84}));
  const Bytes payload{0xaa, 0x1b, 0, 255};
  const auto encoded = frame(3, 255, payload);
  FrameParser parser;
  std::vector<Bytes> decoded;
  for (auto byte : encoded)
  {
    auto frames = parser.feed({byte});
    decoded.insert(decoded.end(), frames.begin(), frames.end());
  }
  check(decoded == std::vector<Bytes>({{3, 255, 0xaa, 0x1b, 0, 255}}));
  auto bad = encoded;
  bad.back() ^= 1;
  check(parser.feed(bad).empty());
  Bytes combined{4, 3, 0xaa, 1, 0, 255, 255};
  combined.insert(combined.end(), encoded.begin(), encoded.end());
  combined.insert(combined.end(), encoded.begin(), encoded.end());
  check(parser.feed(combined).size() == 2);
  int master, slave;
  char name[128];
  check(openpty(&master, &slave, name, nullptr, nullptr) == 0);
  SerialLink serial;
  serial.open(name, 2000000);
  for (int i = 0; i < 258; ++i)
  {
    check(serial.motor(0.25, -0.25));
    uint8_t buffer[64];
    const auto size = ::read(master, buffer, sizeof(buffer));
    const auto frames = parser.feed(Bytes(buffer, buffer + size));
    check(frames.size() == 1 && frames[0][1] == static_cast<uint8_t>(i));
    check(Bytes(frames[0].begin() + 2, frames[0].end()) == Bytes({0, 0, 128, 62, 0, 0, 128, 190}));
  }
  serial.close();
  ::close(master);
  ::close(slave);
}
