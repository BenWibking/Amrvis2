#include "Frame.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <limits>
#include <utility>

namespace amrvis::wire::prototype {
namespace {

[[noreturn]] void throwSystemError(const std::string& operation) {
    throw std::runtime_error(operation + ": " + std::string(std::strerror(errno)));
}

void readExact(int descriptor, std::span<std::uint8_t> destination) {
    std::size_t completed = 0;
    while (completed < destination.size()) {
        const auto count =
            ::recv(descriptor, destination.data() + completed, destination.size() - completed, 0);
        if (count == 0) {
            throw std::runtime_error("connection closed inside a wire frame");
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("recv");
        }
        completed += static_cast<std::size_t>(count);
    }
}

void writeExact(int descriptor, std::span<const std::uint8_t> source) {
    std::size_t completed = 0;
    while (completed < source.size()) {
        const auto count =
            ::send(descriptor, source.data() + completed, source.size() - completed, 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("send");
        }
        completed += static_cast<std::size_t>(count);
    }
}

} // namespace

Socket::Socket(int descriptor) noexcept : m_descriptor(descriptor) {}

Socket::~Socket() {
    if (m_descriptor >= 0) {
        ::close(m_descriptor);
    }
}

Socket::Socket(Socket&& other) noexcept : m_descriptor(std::exchange(other.m_descriptor, -1)) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (m_descriptor >= 0) {
            ::close(m_descriptor);
        }
        m_descriptor = std::exchange(other.m_descriptor, -1);
    }
    return *this;
}

int Socket::descriptor() const noexcept {
    return m_descriptor;
}

Listener listenOnLoopback(std::uint16_t port) {
    Socket socket(::socket(AF_INET, SOCK_STREAM, 0));
    if (socket.descriptor() < 0) {
        throwSystemError("socket");
    }
    const int reuse = 1;
    if (::setsockopt(socket.descriptor(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        throwSystemError("setsockopt");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::bind(socket.descriptor(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) !=
        0) {
        throwSystemError("bind");
    }
    if (::listen(socket.descriptor(), 1) != 0) {
        throwSystemError("listen");
    }

    socklen_t addressSize = sizeof(address);
    if (::getsockname(socket.descriptor(), reinterpret_cast<sockaddr*>(&address), &addressSize) !=
        0) {
        throwSystemError("getsockname");
    }
    return {std::move(socket), ntohs(address.sin_port)};
}

Socket acceptConnection(const Socket& listener) {
    for (;;) {
        const auto descriptor = ::accept(listener.descriptor(), nullptr, nullptr);
        if (descriptor >= 0) {
            return Socket(descriptor);
        }
        if (errno != EINTR) {
            throwSystemError("accept");
        }
    }
}

Socket connectTo(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    const auto status = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (status != 0) {
        throw std::runtime_error("getaddrinfo: " + std::string(gai_strerror(status)));
    }

    int lastError = ECONNREFUSED;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        Socket socket(::socket(address->ai_family, address->ai_socktype, address->ai_protocol));
        if (socket.descriptor() < 0) {
            lastError = errno;
            continue;
        }
        if (::connect(socket.descriptor(), address->ai_addr, address->ai_addrlen) == 0) {
            ::freeaddrinfo(addresses);
            return socket;
        }
        lastError = errno;
    }
    ::freeaddrinfo(addresses);
    errno = lastError;
    throwSystemError("connect");
}

void writeFrame(const Socket& socket, std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > maximumFrameBytes ||
        payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("wire frame size is outside the allowed range");
    }
    const auto networkSize = htonl(static_cast<std::uint32_t>(payload.size()));
    const auto* sizeBytes = reinterpret_cast<const std::uint8_t*>(&networkSize);
    writeExact(socket.descriptor(), std::span<const std::uint8_t>(sizeBytes, sizeof(networkSize)));
    writeExact(socket.descriptor(), payload);
}

std::vector<std::uint8_t> readFrame(const Socket& socket) {
    std::array<std::uint8_t, sizeof(std::uint32_t)> sizeBytes{};
    readExact(socket.descriptor(), sizeBytes);
    std::uint32_t networkSize = 0;
    std::memcpy(&networkSize, sizeBytes.data(), sizeof(networkSize));
    const auto size = ntohl(networkSize);
    if (size == 0 || size > maximumFrameBytes) {
        throw std::runtime_error("wire frame size is outside the allowed range");
    }
    std::vector<std::uint8_t> payload(size);
    readExact(socket.descriptor(), payload);
    return payload;
}

} // namespace amrvis::wire::prototype
