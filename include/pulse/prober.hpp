#pragma once

#include <cstdint>
#include <vector>

namespace pulse {

struct Endpoint {
    std::uint32_t address = 0;
    std::uint16_t port = 0;
};

struct EndpointStats {
    std::uint64_t sent = 0;
    std::uint64_t answered = 0;
    std::uint64_t missed_in_row = 0;
    std::uint16_t last_answered_tick = 0;
    bool answered_last_tick = false;
};

class Prober {
public:
    Prober() noexcept = default;
    ~Prober() noexcept;

    Prober(const Prober&) = delete;
    Prober& operator=(const Prober&) = delete;

    [[nodiscard]] bool open() noexcept;
    void close() noexcept;

    void set_endpoints(const std::vector<Endpoint>& endpoints);

    std::uint16_t tick() noexcept;
    void collect(std::uint32_t window_ms) noexcept;

    [[nodiscard]] const std::vector<EndpointStats>& stats() const noexcept {
        return stats_;
    }

private:
    std::vector<Endpoint> endpoints_;
    std::vector<EndpointStats> stats_;
    std::uint16_t current_tick_ = 0;
    int socket_ = -1;
};

}  // namespace pulse
