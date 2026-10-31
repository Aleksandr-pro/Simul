// РАЗЛОМ — сторож кадра. Решает, что делать, если мир пропал из кадра:
// пересобрать геометрию, вернуть камеру к спавну или пересобрать мир целиком.
// Логика вынесена в чистую функцию, чтобы её можно было проверить без GPU.
#pragma once

#include "razlom/client/sky.hpp"

namespace razlom::client {

// Сколько секунд «пустого» кадра терпим до первого шага восстановления.
inline constexpr float kWatchDelay = 0.9f;

enum class WatchAction {
    None,
    Rebuild,              // пересобрать GPU-геометрию чанков
    RebuildAndRespawn,    // + вернуть камеру к спавну
    ResetWorldAndRespawn, // + полный сброс мешей мира
    Log,                  // записать состояние в журнал
};

struct WorldWatch {
    float timer = 0.0f;
    int recoveries = 0;

    void reset() {
        timer = 0.0f;
        recoveries = 0;
    }

    // visibleChunks — сколько чанков попало в кадр в прошлом кадре.
    // pitch — наклон камеры: взгляд в небо это нормальный пустой кадр.
    WatchAction tick(float dt, bool playing, bool worldReady, int visibleChunks, float pitch) {
        if (!playing || !worldReady) {
            reset();
            return WatchAction::None;
        }
        const bool skyGazing = pitch > kSkyGazePitch;
        if (visibleChunks > 0 || skyGazing) {
            reset();
            return WatchAction::None;
        }
        timer += dt;
        if (timer < kWatchDelay) return WatchAction::None;
        timer = 0.0f;
        ++recoveries;
        switch (recoveries) {
            case 1: return WatchAction::Rebuild;
            case 2: return WatchAction::RebuildAndRespawn;
            case 3: return WatchAction::ResetWorldAndRespawn;
            default: break;
        }
        if (recoveries % 5 == 0) return WatchAction::Log;
        return WatchAction::None;
    }
};

}  // namespace razlom::client
