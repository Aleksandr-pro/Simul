// РАЗЛОМ — игровая сессия: игроки, правила режима, правки мира, физика разрушения.
// Одна и та же сессия крутится и в одиночной игре, и в сетевом хосте.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "razlom/core/fluids.hpp"
#include "razlom/core/physics.hpp"
#include "razlom/core/structure.hpp"
#include "razlom/core/world.hpp"
#include "razlom/net/protocol.hpp"

namespace razlom {

enum class GameMode : uint8_t {
    Sandbox = 0,       // песочница: креатив, полёт, без урона
    LastStanding = 1,  // последний выживший: PvP, здоровье, респавн
    BaseDefense = 2,   // защита базы (каркас режима, включается редактором)
};

const char* gameModeName(GameMode mode);
const char* gameModeTagline(GameMode mode);

struct Player {
    uint32_t id = 0;
    std::string name;
    Vec3 pos;
    Vec3 velocity;
    float yaw = 0.0f;
    float pitch = 0.0f;
    int16_t health = 100;
    uint8_t slot = 0;
    uint8_t flags = 0;
    bool alive = true;
    float respawnTimer = 0.0f;
};

struct DebrisEntity {
    uint32_t id = 0;
    Vec3 pos;
    Vec3 vel;
    uint8_t block = kAir;
    float life = 0.0f;
};

enum class SessionEventKind : uint8_t {
    Chat = 0,
    Join = 1,
    Leave = 2,
    Explosion = 3,
    Collapse = 4,
    Damage = 5,
    Win = 6,
    Info = 7,
};

struct SessionEvent {
    SessionEventKind kind = SessionEventKind::Info;
    Vec3 pos;
    std::string text;
    uint32_t playerId = 0;
};

struct SessionConfig {
    uint32_t seed = 20251101u;
    GameMode mode = GameMode::Sandbox;
    int maxPlayers = 4;
    int tickRate = 20;
};

class Session {
public:
    explicit Session(const SessionConfig& config = {});

    void reset(uint32_t seed, GameMode mode);

    // --- игроки ---
    uint32_t addPlayer(const std::string& name, std::string* error = nullptr);
    void removePlayer(uint32_t id);
    Player* findPlayer(uint32_t id);
    const Player* findPlayer(uint32_t id) const;
    const std::vector<Player>& players() const { return players_; }
    size_t playerCount() const { return players_.size(); }

    // --- команды ---
    bool applyInput(uint32_t id, const Vec3& pos, float yaw, float pitch, uint8_t flags, uint8_t slot);
    bool requestBreak(uint32_t id, const IVec3& pos);
    bool requestPlace(uint32_t id, const IVec3& pos, uint8_t block);
    bool requestExplode(uint32_t id, const Vec3& pos, float radius);
    bool requestAttack(uint32_t id);
    void requestRespawn(uint32_t id);
    void requestMode(GameMode mode);
    void chat(uint32_t id, const std::string& text);

    // --- симуляция ---
    void tick(float dt);

    // --- выборки для сети/рендера ---
    std::vector<BlockChange> takeChanges();
    std::vector<SessionEvent> takeEvents();
    std::vector<net::PlayerSnapshot> playerSnapshots() const;
    std::vector<net::DebrisSnapshot> debrisSnapshot() const;
    const std::vector<BlockChange>& history() const { return history_; }
    const std::vector<DebrisEntity>& debris() const { return debris_; }

    const World& world() const { return world_; }
    World& world() { return world_; }
    uint32_t tickIndex() const { return tickIndex_; }
    uint32_t seed() const { return config_.seed; }
    GameMode mode() const { return config_.mode; }
    int tickRate() const { return config_.tickRate; }
    bool creative() const { return config_.mode == GameMode::Sandbox; }
    bool pvpEnabled() const { return config_.mode == GameMode::LastStanding; }
    int aliveCount() const;
    uint32_t worldHash() const;
    Vec3 spawnPoint() const { return spawnPoint_; }
    size_t historySize() const { return history_.size(); }

private:
    struct PendingSolve {
        IVec3 origin;
        std::vector<IVec3> seeds;
    };
    struct PendingExplosion {
        Vec3 pos;
        float radius = 4.0f;
        float fuse = 0.0f;
    };

    void recordHistory(const BlockChange& change);
    void spawnDebrisFromCollapse(const CollapseResult& collapse);
    void scheduleSolve(const std::vector<IVec3>& seeds, const IVec3& origin);
    void processSolves(int maxSolves);
    void doExplosion(const Vec3& pos, float radius);
    void damagePlayer(Player& player, int amount, const std::string& reason);
    void respawn(Player& player);
    void updateModeLogic(float dt);
    Vec3 eyePosition(const Player& player) const;
    bool withinReach(const Player& player, const IVec3& pos) const;
    void afterWorldEdit(const IVec3& pos);

    SessionConfig config_;
    World world_;
    StructureSolver solver_;
    FluidSim fluids_;
    FireSim fire_;
    std::vector<Player> players_;
    std::vector<DebrisEntity> debris_;
    std::vector<BlockChange> history_;
    std::unordered_map<int, uint32_t> historyIndex_;  // индекс блока -> слот в истории
    std::vector<SessionEvent> events_;
    std::vector<PendingSolve> solves_;
    std::vector<PendingExplosion> explosions_;
    std::vector<BlockChange> pendingChanges_;
    Vec3 spawnPoint_;
    uint32_t nextPlayerId_ = 1;
    uint32_t nextDebrisId_ = 1;
    uint32_t tickIndex_ = 0;
    float simAccumulator_ = 0.0f;
    float modeTimer_ = 0.0f;
    float winResetTimer_ = -1.0f;
    uint32_t winnerId_ = 0;
};

}  // namespace razlom
