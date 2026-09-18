#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace pulse {

enum class AgentError {
    Ok,
    AlreadyStarted,
    SocketFailed,
    TimeoutFailed,
    BindFailed,
};

struct AgentConfig {
    std::uint16_t port = 0;
    std::uint32_t poll_timeout_ms = 50;
};

class Agent {
public:
    explicit Agent(const AgentConfig& config) noexcept;
    ~Agent() noexcept;

    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;

    [[nodiscard]] AgentError start() noexcept;
    void stop() noexcept;

    void alive() noexcept {
        alive_seq_.fetch_add(1, std::memory_order_relaxed);
    }

private:
    void serve() noexcept;
    void close_socket() noexcept;

    AgentConfig config_;
    std::atomic<std::uint64_t> alive_seq_{0};
    std::atomic<bool> running_{false};
    int socket_ = -1;
    std::thread worker_;
};

}  // namespace pulse
