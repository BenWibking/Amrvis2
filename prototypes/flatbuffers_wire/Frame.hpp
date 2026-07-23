#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace amrvis::wire::prototype {

inline constexpr std::uint16_t protocolMajor = 1;
inline constexpr std::uint16_t protocolMinor = 0;
inline constexpr std::uint32_t maximumFrameBytes = 128U * 1024U * 1024U;

class Socket {
  public:
    Socket() = default;
    explicit Socket(int descriptor) noexcept;
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] int descriptor() const noexcept;

  private:
    int m_descriptor = -1;
};

struct Listener {
    Socket socket;
    std::uint16_t port = 0;
};

[[nodiscard]] Listener listenOnLoopback(std::uint16_t port);
[[nodiscard]] Socket acceptConnection(const Socket& listener);
[[nodiscard]] Socket connectTo(const std::string& host, std::uint16_t port);

void writeFrame(const Socket& socket, std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> readFrame(const Socket& socket);

} // namespace amrvis::wire::prototype
