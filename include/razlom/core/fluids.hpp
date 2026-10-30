// РАЗЛОМ — вода и огонь: клеточные автоматы с бюджетом операций на тик.
// Симуляция живёт на сервере, клиентам уходят только изменения блоков.
#pragma once

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "razlom/core/math.hpp"
#include "razlom/core/world.hpp"

namespace razlom {

class FluidSim {
public:
    FluidSim();

    void clear();
    void wake(int x, int y, int z);
    void wakeAround(const World& world, int x, int y, int z, int radius = 1);
    void wakeWorldColumn(const World& world, int x, int z);

    // Обрабатывает не больше budget ячеек. Возвращает количество изменённых блоков.
    int tick(World& world, int budget = 4096);

    size_t pending() const { return queue_.size(); }
    bool busy() const { return !queue_.empty(); }

private:
    void push(int idx);
    void settle(World& world, int x, int y, int z);

    std::deque<int> queue_;
    std::vector<uint8_t> queued_;
};

class FireSim {
public:
    void clear();
    void ignite(World& world, int x, int y, int z, int life = 14);
    // Возвращает количество изменённых блоков.
    int tick(World& world, uint32_t tickIndex, int budget = 1024);
    bool burning(int idx) const { return life_.find(idx) != life_.end(); }
    size_t burningCount() const { return life_.size(); }

private:
    void remove(World& world, int idx, int x, int y, int z);

    std::unordered_map<int, int> life_;   // индекс блока -> оставшиеся тики
    std::deque<int> order_;
};

// Взрыв: кратер + поджог + возврат затронутых блоков (для запуска обрушения).
struct ExplosionResult {
    std::vector<IVec3> affected;
    int destroyed = 0;
    int ignited = 0;
};

ExplosionResult explode(World& world, const Vec3& center, float radius, FireSim* fire, FluidSim* fluids);

}  // namespace razlom
