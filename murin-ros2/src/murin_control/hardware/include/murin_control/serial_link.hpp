#pragma once

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace murin_control
{
using Bytes = std::vector<uint8_t>;
inline uint16_t crc16(const Bytes & data)
{
  uint16_t crc = 0xffff;
  for (auto byte : data)
  {
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (int bit = 0; bit < 8; ++bit)
    {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
  }
  return crc;
}
inline Bytes frame(uint8_t type, uint8_t sequence, const Bytes & payload)
{
  Bytes stuffed;
  for (auto byte : payload)
  {
    if (byte == 0xaa || byte == 0x1b)
    {
      stuffed.push_back(0x1b);
      stuffed.push_back(byte ^ 0x20);
    }
    else
    {
      stuffed.push_back(byte);
    }
  }
  if (stuffed.size() > 4096)
  {
    throw std::invalid_argument("Payload too large");
  }
  Bytes data{
    type, sequence, static_cast<uint8_t>(stuffed.size()),
    static_cast<uint8_t>(stuffed.size() >> 8)};
  data.insert(data.end(), stuffed.begin(), stuffed.end());
  const auto crc = crc16(data);
  data.insert(data.begin(), 0xaa);
  data.push_back(static_cast<uint8_t>(crc & 0xff));
  data.push_back(static_cast<uint8_t>(crc >> 8));
  return data;
}
class FrameParser
{
  Bytes buffer_;

public:
  // Returned records contain type, sequence, then the unstuffed payload.
  std::vector<Bytes> feed(const Bytes & data)
  {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
    std::vector<Bytes> result;
    while (!buffer_.empty())
    {
      buffer_.erase(buffer_.begin(), std::find(buffer_.begin(), buffer_.end(), 0xaa));
      if (buffer_.size() < 7)
      {
        break;
      }
      const size_t length = buffer_[3] | (buffer_[4] << 8);
      if (length > 4096)
      {
        buffer_.erase(buffer_.begin());
        continue;
      }
      if (buffer_.size() < length + 7)
      {
        break;
      }
      Bytes encoded(buffer_.begin() + 1, buffer_.begin() + 5 + length);
      const auto crc = buffer_[5 + length] | (buffer_[6 + length] << 8);
      if (crc16(encoded) == crc)
      {
        Bytes decoded{buffer_[1], buffer_[2]};
        bool valid = true;
        for (size_t i = 5; i < 5 + length; ++i)
        {
          auto byte = buffer_[i];
          if (byte == 0x1b)
          {
            if (++i == 5 + length)
            {
              valid = false;
              break;
            }
            byte = buffer_[i] ^ 0x20;
          }
          decoded.push_back(byte);
        }
        if (valid)
        {
          result.push_back(decoded);
        }
      }
      buffer_.erase(buffer_.begin(), buffer_.begin() + length + 7);
    }
    return result;
  }
};
class SerialLink
{
  int fd_ = -1;
  uint8_t sequence_ = 0;

public:
  FrameParser parser;
  ~SerialLink() { close(); }
  bool is_open() const { return fd_ >= 0; }
  void close()
  {
    if (fd_ >= 0)
    {
      ::close(fd_);
      fd_ = -1;
    }
    parser = FrameParser{};
  }
  void open(const std::string & port, int baud)
  {
    close();
    speed_t speed;
    switch (baud)
    {
      case 115200:
        speed = B115200;
        break;
      case 921600:
        speed = B921600;
        break;
      case 1000000:
        speed = B1000000;
        break;
      case 2000000:
        speed = B2000000;
        break;
      default:
        throw std::invalid_argument("Unsupported serial baud rate");
    }
    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0)
    {
      throw std::runtime_error("Cannot open robot serial port: " + std::string(strerror(errno)));
    }
    termios tty{};
    if (ioctl(fd_, TIOCEXCL) < 0 || tcgetattr(fd_, &tty) < 0)
    {
      close();
      throw std::runtime_error("Cannot exclusively configure serial port");
    }
    cfmakeraw(&tty);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CRTSCTS;
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    if (tcsetattr(fd_, TCSANOW, &tty) < 0)
    {
      close();
      throw std::runtime_error("Cannot set serial attributes");
    }
    tcflush(fd_, TCIOFLUSH);
  }
  bool send(uint8_t type, const Bytes & payload)
  {
    if (!is_open())
    {
      return false;
    }
    const auto data = frame(type, sequence_++, payload);
    size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    while (offset < data.size() && std::chrono::steady_clock::now() < deadline)
    {
      const auto written = ::write(fd_, data.data() + offset, data.size() - offset);
      if (written > 0)
      {
        offset += written;
      }
      else if (written < 0 && errno != EAGAIN && errno != EINTR)
      {
        return false;
      }
      else
      {
        pollfd pfd{fd_, POLLOUT, 0};
        poll(&pfd, 1, 1);
      }
    }
    return offset == data.size();
  }
  bool motor(double left, double right)
  {
    Bytes payload;
    for (double value : {left, right})
    {
      const float f = static_cast<float>(value);
      uint32_t bits;
      std::memcpy(&bits, &f, sizeof(bits));
      for (int i = 0; i < 4; ++i)
      {
        payload.push_back((bits >> (8 * i)) & 0xff);
      }
    }
    return send(0x01, payload);
  }
  bool receive(std::vector<Bytes> & frames)
  {
    pollfd pfd{fd_, POLLIN, 0};
    if (poll(&pfd, 1, 0) < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))
    {
      return false;
    }
    uint8_t data[4096];
    const auto count = ::read(fd_, data, sizeof(data));
    if (count < 0)
    {
      return errno == EAGAIN || errno == EINTR;
    }
    frames = parser.feed(Bytes(data, data + count));
    return true;
  }
};
}  // namespace murin_control
