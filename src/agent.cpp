#include "pulse/agent.hpp"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.hpp"
#include "socket_options.hpp"

namespace pulse {

Agent::Agent(const AgentConfig& config) noexcept : config_(config) {}

Agent::~Agent() noexcept {
    stop();
}

AgentError Agent::start() noexcept {
    if (running_.exchange(true, std::memory_order_relaxed)) {
        return AgentError::AlreadyStarted;
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        running_.store(false, std::memory_order_relaxed);
        return AgentError::SocketFailed;
    }

    if (!set_receive_timeout(socket_, config_.poll_timeout_ms)) {
        close_socket();
        running_.store(false, std::memory_order_relaxed);
        return AgentError::TimeoutFailed;
    }

    ::sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config_.port);

    if (::bind(socket_, reinterpret_cast<::sockaddr*>(&address), sizeof(address)) < 0) {
        close_socket();
        running_.store(false, std::memory_order_relaxed);
        return AgentError::BindFailed;
    }

    worker_ = std::thread(&Agent::serve, this);

    return AgentError::Ok;
}

void Agent::stop() noexcept {
    if (!running_.exchange(false, std::memory_order_relaxed)) {
        return;
    }

    if (socket_ >= 0) {
        ::shutdown(socket_, SHUT_RDWR);
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    close_socket();
}

void Agent::close_socket() noexcept {
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
}

void Agent::serve() noexcept {
    Message pending;
    ::sockaddr_in pending_peer{};
    ::socklen_t pending_peer_size = 0;
    std::uint64_t seen_seq = 0;
    bool has_pending = false;

    std::uint8_t buffer[MESSAGE_SIZE];

    while (running_.load(std::memory_order_relaxed)) {
        ::sockaddr_in peer{};
        ::socklen_t peer_size = sizeof(peer);

        const ::ssize_t received = ::recvfrom(
            socket_,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<::sockaddr*>(&peer),
            &peer_size);

        if (received > 0) {
            Message request;
            if (decode(buffer, static_cast<std::size_t>(received), request)) {
                pending = request;
                pending_peer = peer;
                pending_peer_size = peer_size;
                seen_seq = alive_seq_.load(std::memory_order_relaxed);
                has_pending = true;
            }
        }

        if (!has_pending) {
            continue;
        }

        if (alive_seq_.load(std::memory_order_relaxed) == seen_seq) {
            continue;
        }

        encode(pending, buffer);

        ::sendto(
            socket_,
            buffer,
            MESSAGE_SIZE,
            0,
            reinterpret_cast<const ::sockaddr*>(&pending_peer),
            pending_peer_size);

        has_pending = false;
    }
}

}  // namespace pulse
