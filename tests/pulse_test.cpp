#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "pulse/agent.hpp"
#include "pulse/prober.hpp"

namespace {

constexpr std::uint32_t LOOPBACK = 0x7F000001;
constexpr std::uint16_t FIRST_PORT = 19990;
constexpr std::uint16_t SECOND_PORT = 19991;
constexpr std::uint16_t SILENT_PORT = 19992;
constexpr std::uint32_t POLL_TIMEOUT_MS = 20;
constexpr std::uint32_t WINDOW_MS = 200;
constexpr std::uint32_t SLOW_POLL_TIMEOUT_MS = 2000;
constexpr std::uint32_t TICK_INTERVAL_MS = 5;

int failures = 0;

void check(bool condition, const std::string& name) {
    std::printf("%-50s %s\n", name.c_str(), condition ? "ok" : "FAILED");
    if (!condition) {
        ++failures;
    }
}

pulse::AgentConfig agent_config(std::uint16_t port, std::uint32_t poll_timeout_ms) {
    pulse::AgentConfig config;
    config.port = port;
    config.poll_timeout_ms = poll_timeout_ms;
    return config;
}

pulse::Endpoint endpoint(std::uint16_t port) {
    pulse::Endpoint result;
    result.address = LOOPBACK;
    result.port = port;
    return result;
}

class Ticker {
public:
    explicit Ticker(pulse::Agent& agent) : running_(true) {
        thread_ = std::thread([this, &agent] {
            while (running_.load()) {
                agent.alive();
                std::this_thread::sleep_for(std::chrono::milliseconds(TICK_INTERVAL_MS));
            }
        });
    }

    ~Ticker() {
        running_.store(false);
        thread_.join();
    }

private:
    std::atomic<bool> running_;
    std::thread thread_;
};

void test_alive_backend_answers() {
    pulse::Agent agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    check(agent.start() == pulse::AgentError::Ok, "agent starts");

    Ticker ticker{agent};

    pulse::Prober prober;
    check(prober.open(), "prober opens");
    prober.set_endpoints({endpoint(FIRST_PORT)});

    prober.tick();
    prober.collect(WINDOW_MS);

    check(prober.stats()[0].answered_last_tick, "ticking backend answers");
    check(prober.stats()[0].missed_in_row == 0, "answered backend has no misses");
}

void test_stalled_backend_stays_silent() {
    pulse::Agent agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(agent.start());

    agent.alive();
    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_TIMEOUT_MS * 2));

    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(FIRST_PORT)});

    prober.tick();
    prober.collect(WINDOW_MS);

    check(!prober.stats()[0].answered_last_tick, "stalled backend stays silent");
    check(prober.stats()[0].missed_in_row == 1, "missed counter grows");
}

void test_missed_counter_accumulates() {
    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(SILENT_PORT)});

    for (int i = 0; i < 3; ++i) {
        prober.tick();
        prober.collect(WINDOW_MS / 4);
    }

    check(prober.stats()[0].missed_in_row == 3, "misses accumulate across ticks");
    check(prober.stats()[0].answered == 0, "no answers from silent port");
}

void test_mixed_backends() {
    pulse::Agent alive_agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(alive_agent.start());
    Ticker ticker{alive_agent};

    pulse::Agent stalled_agent{agent_config(SECOND_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(stalled_agent.start());

    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(FIRST_PORT), endpoint(SECOND_PORT), endpoint(SILENT_PORT)});

    prober.tick();
    prober.collect(WINDOW_MS);

    check(prober.stats()[0].answered_last_tick, "live backend answered");
    check(!prober.stats()[1].answered_last_tick, "stalled backend did not answer");
    check(!prober.stats()[2].answered_last_tick, "absent backend did not answer");
}

void test_recovery_resets_misses() {
    pulse::Agent agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(agent.start());

    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(FIRST_PORT)});

    prober.tick();
    prober.collect(WINDOW_MS / 4);
    check(prober.stats()[0].missed_in_row == 1, "missed before recovery");

    Ticker ticker{agent};

    prober.tick();
    prober.collect(WINDOW_MS);

    check(prober.stats()[0].answered_last_tick, "backend recovered");
    check(prober.stats()[0].missed_in_row == 0, "recovery resets missed counter");
}

void test_stale_reply_ignored() {
    pulse::Agent agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(agent.start());

    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(FIRST_PORT)});

    prober.tick();

    agent.alive();
    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_TIMEOUT_MS * 2));

    prober.tick();
    prober.collect(WINDOW_MS / 4);

    check(!prober.stats()[0].answered_last_tick, "reply from previous tick ignored");
}

void test_double_start_rejected() {
    pulse::Agent agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    check(agent.start() == pulse::AgentError::Ok, "first start succeeds");
    check(agent.start() == pulse::AgentError::AlreadyStarted, "second start rejected");
}

void test_busy_port_rejected() {
    pulse::Agent first{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(first.start());

    pulse::Agent second{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    check(second.start() == pulse::AgentError::BindFailed, "busy port rejected");
}

void test_stop_is_prompt() {
    pulse::Agent agent{agent_config(FIRST_PORT, SLOW_POLL_TIMEOUT_MS)};
    static_cast<void>(agent.start());

    const auto started = std::chrono::steady_clock::now();
    agent.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    check(elapsed < std::chrono::milliseconds(500), "stop does not wait for poll timeout");
}

void test_stop_is_idempotent() {
    pulse::Agent agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(agent.start());
    agent.stop();
    agent.stop();
    check(true, "double stop does not crash");
}

void test_empty_endpoints() {
    pulse::Prober prober;
    static_cast<void>(prober.open());

    prober.tick();
    prober.collect(WINDOW_MS / 10);

    check(prober.stats().empty(), "empty endpoint list stays empty");
}

void test_set_endpoints_resets_stats() {
    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(SILENT_PORT)});

    prober.tick();
    prober.collect(WINDOW_MS / 10);
    check(prober.stats()[0].missed_in_row == 1, "missed recorded before reset");

    prober.set_endpoints({endpoint(SILENT_PORT)});
    check(prober.stats()[0].missed_in_row == 0, "set_endpoints resets stats");
}

void test_collect_returns_early() {
    pulse::Agent agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(agent.start());
    Ticker ticker{agent};

    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(FIRST_PORT)});

    prober.tick();

    const auto started = std::chrono::steady_clock::now();
    prober.collect(2000);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    check(prober.stats()[0].answered_last_tick, "backend answered before window end");
    check(elapsed < std::chrono::milliseconds(1000), "collect returns once all answered");
}

void test_tick_without_open() {
    pulse::Prober prober;
    prober.set_endpoints({endpoint(FIRST_PORT)});

    prober.tick();
    prober.collect(WINDOW_MS / 10);

    check(prober.stats()[0].sent == 0, "tick without open sends nothing");
    check(prober.stats()[0].missed_in_row == 0, "tick without open records no miss");
}

void test_collect_without_tick() {
    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(SILENT_PORT)});

    prober.collect(WINDOW_MS / 10);

    check(prober.stats()[0].missed_in_row == 0, "collect without tick records no miss");
}

void test_newer_request_replaces_pending() {
    pulse::Agent agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(agent.start());

    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(FIRST_PORT)});

    prober.tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(POLL_TIMEOUT_MS));

    prober.tick();
    Ticker ticker{agent};
    prober.collect(WINDOW_MS);

    check(prober.stats()[0].answered_last_tick, "agent answers newest request only");
    check(prober.stats()[0].answered == 1, "exactly one answer recorded");
}

void test_repeated_ticks_single_answer() {
    pulse::Agent agent{agent_config(FIRST_PORT, POLL_TIMEOUT_MS)};
    static_cast<void>(agent.start());
    Ticker ticker{agent};

    pulse::Prober prober;
    static_cast<void>(prober.open());
    prober.set_endpoints({endpoint(FIRST_PORT)});

    for (int i = 0; i < 3; ++i) {
        prober.tick();
        prober.collect(WINDOW_MS);
    }

    check(prober.stats()[0].answered == 3, "three ticks give three answers");
    check(prober.stats()[0].sent == 3, "three ticks counted as sent");
}

}  // namespace

int main() {
    test_alive_backend_answers();
    test_stalled_backend_stays_silent();
    test_missed_counter_accumulates();
    test_mixed_backends();
    test_recovery_resets_misses();
    test_stale_reply_ignored();
    test_double_start_rejected();
    test_busy_port_rejected();
    test_stop_is_prompt();
    test_stop_is_idempotent();
    test_empty_endpoints();
    test_set_endpoints_resets_stats();
    test_collect_returns_early();
    test_tick_without_open();
    test_collect_without_tick();
    test_newer_request_replaces_pending();
    test_repeated_ticks_single_answer();

    std::printf("\n%s\n", failures == 0 ? "ALL PASSED" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
