#pragma once
#include "input_switcher.h"
#include <array>
#include <optional>

namespace lg
{
// DDC/CI Set VCP wire data: destination, source, length, opcode, feature,
// big-endian value, then XOR checksum. LG selects inputs with feature F4.
inline std::optional<std::array<unsigned char, 8>> EncodeDdcWrite(const InputCommand &command)
{
    if (!ValidCommand(command) || command.featureCode != 0xf4)
        return std::nullopt;
    std::array<unsigned char, 8> bytes{0x6e,
                                       static_cast<unsigned char>(command.sourceAddress),
                                       0x84,
                                       0x03,
                                       static_cast<unsigned char>(command.featureCode),
                                       static_cast<unsigned char>(command.value >> 8),
                                       static_cast<unsigned char>(command.value),
                                       0};
    for (std::size_t i = 0; i < bytes.size() - 1; ++i)
        bytes.back() ^= bytes[i];
    return bytes;
}
} // namespace lg
