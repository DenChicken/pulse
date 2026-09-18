#pragma once

#include <cstdint>
#include <sys/socket.h>
#include <sys/time.h>

namespace pulse {

constexpr std::uint32_t MS_PER_SECOND = 1000;
constexpr std::uint32_t US_PER_MS = 1000;

inline bool set_receive_timeout(int socket, std::uint32_t timeout_ms) noexcept {
    ::timeval timeout{};
    timeout.tv_sec = timeout_ms / MS_PER_SECOND;
    timeout.tv_usec = (timeout_ms % MS_PER_SECOND) * US_PER_MS;

    return ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
}

}  // namespace pulse
