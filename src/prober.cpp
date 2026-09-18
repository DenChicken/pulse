#include "pulse/prober.hpp"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>

#include "protocol.hpp"
#include "socket_options.hpp"

namespace pulse {
namespace {

constexpr std::uint32_t COLLECT_POLL_MS = 5;
constexpr std::uint16_t NO_TICK = 0;

}  // namespace

Prober::~Prober() noexcept {
    close();
}

bool Prober::open() noexcept {
    if (socket_ >= 0) {
        return true;
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        return false;
    }

    if (!set_receive_timeout(socket_, COLLECT_POLL_MS)) {
        close();
        return false;
    }

    return true;
}

void Prober::close() noexcept {
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
}

void Prober::set_endpoints(const std::vector<Endpoint>& endpoints) {
    endpoints_ = endpoints;
    stats_.assign(endpoints.size(), EndpointStats{});
}

std::uint16_t Prober::tick() noexcept {
    if (socket_ < 0) {
        return current_tick_;
    }

    if (++current_tick_ == NO_TICK) {
        ++current_tick_;
    }

    std::uint8_t buffer[MESSAGE_SIZE];

    for (std::size_t i = 0; i < endpoints_.size(); ++i) {
        stats_[i].answered_last_tick = false;

        Message request;
        request.tick_id = current_tick_;
        request.backend_slot = static_cast<std::uint16_t>(i);
        encode(request, buffer);

        ::sockaddr_in peer{};
        peer.sin_family = AF_INET;
        peer.sin_addr.s_addr = htonl(endpoints_[i].address);
        peer.sin_port = htons(endpoints_[i].port);

        const ::ssize_t sent = ::sendto(
            socket_,
            buffer,
            MESSAGE_SIZE,
            0,
            reinterpret_cast<const ::sockaddr*>(&peer),
            sizeof(peer));

        if (sent == static_cast<::ssize_t>(MESSAGE_SIZE)) {
            ++stats_[i].sent;
        }
    }

    return current_tick_;
}

void Prober::collect(std::uint32_t window_ms) noexcept {
    if (socket_ < 0 || current_tick_ == NO_TICK) {
        return;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(window_ms);

    std::uint8_t buffer[MESSAGE_SIZE];
    std::size_t answered_count = 0;

    while (answered_count < stats_.size() && std::chrono::steady_clock::now() < deadline) {
        const ::ssize_t received = ::recvfrom(socket_, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (received <= 0) {
            continue;
        }

        Message reply;
        if (!decode(buffer, static_cast<std::size_t>(received), reply)) {
            continue;
        }

        if (reply.tick_id != current_tick_) {
            continue;
        }

        if (reply.backend_slot >= stats_.size()) {
            continue;
        }

        EndpointStats& stats = stats_[reply.backend_slot];
        if (stats.answered_last_tick) {
            continue;
        }

        stats.answered_last_tick = true;
        stats.last_answered_tick = reply.tick_id;
        ++stats.answered;
        stats.missed_in_row = 0;
        ++answered_count;
    }

    for (EndpointStats& stats : stats_) {
        if (!stats.answered_last_tick) {
            ++stats.missed_in_row;
        }
    }
}

}  // namespace pulse
