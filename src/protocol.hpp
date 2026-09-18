#pragma once

#include <cstddef>
#include <cstdint>

namespace pulse {

constexpr std::uint8_t PROTOCOL_MAGIC = 0x7E;
constexpr std::uint8_t PROTOCOL_VERSION = 1;

constexpr std::size_t MAGIC_OFFSET = 0;
constexpr std::size_t VERSION_OFFSET = 1;
constexpr std::size_t TICK_ID_OFFSET = 2;
constexpr std::size_t BACKEND_SLOT_OFFSET = 4;

constexpr std::size_t MESSAGE_SIZE = BACKEND_SLOT_OFFSET + sizeof(std::uint16_t);

struct Message {
    std::uint16_t tick_id = 0;
    std::uint16_t backend_slot = 0;
};

inline void write_u16(std::uint8_t* buffer, std::uint16_t value) noexcept {
    buffer[0] = static_cast<std::uint8_t>(value >> 8);
    buffer[1] = static_cast<std::uint8_t>(value);
}

inline std::uint16_t read_u16(const std::uint8_t* buffer) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(buffer[0]) << 8 | buffer[1]);
}

inline void encode(const Message& message, std::uint8_t* buffer) noexcept {
    buffer[MAGIC_OFFSET] = PROTOCOL_MAGIC;
    buffer[VERSION_OFFSET] = PROTOCOL_VERSION;
    write_u16(buffer + TICK_ID_OFFSET, message.tick_id);
    write_u16(buffer + BACKEND_SLOT_OFFSET, message.backend_slot);
}

inline bool decode(const std::uint8_t* buffer, std::size_t size, Message& message) noexcept {
    if (size != MESSAGE_SIZE) {
        return false;
    }

    if (buffer[MAGIC_OFFSET] != PROTOCOL_MAGIC || buffer[VERSION_OFFSET] != PROTOCOL_VERSION) {
        return false;
    }

    message.tick_id = read_u16(buffer + TICK_ID_OFFSET);
    message.backend_slot = read_u16(buffer + BACKEND_SLOT_OFFSET);

    return true;
}

}  // namespace pulse
