#pragma once

#include "simul/core/engine.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace simul::core {

/**
 * Компактная нативная бинарная сериализация состояния движка и SoA-буферов (.simsave).
 * Формат: [Magic 'SIML'][Version u32][PayloadLen u32][CRC32 u32][Payload bytes...]
 */
class BinarySave {
public:
    static std::vector<std::uint8_t> serialize_to_bytes(const Engine& engine);
    static bool deserialize_from_bytes(Engine& engine, const std::uint8_t* data, std::size_t size, std::string& out_error);

    static bool save_to_file(const Engine& engine, const std::string& path, std::string& out_error);
    static bool load_from_file(Engine& engine, const std::string& path, std::string& out_error);
};

} // namespace simul::core
