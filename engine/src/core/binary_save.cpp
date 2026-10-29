#include "simul/core/binary_save.hpp"
#include <cstdio>
#include <cstring>

namespace simul::core {

namespace {

constexpr std::uint32_t kMagic = 0x4C4D4953u; // 'SIML' little-endian
constexpr std::uint32_t kFormatVersion = 1u;

std::uint32_t compute_crc32(const std::uint8_t* data, std::size_t len) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const std::uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

class ByteWriter {
public:
    std::vector<std::uint8_t> buf;

    template <typename T>
    void pod(const T& v) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
        buf.insert(buf.end(), p, p + sizeof(T));
    }

    void bytes(const void* src, std::size_t n) {
        const auto* p = static_cast<const std::uint8_t*>(src);
        buf.insert(buf.end(), p, p + n);
    }

    void str(const std::string& s) {
        const auto n = static_cast<std::uint32_t>(s.size());
        pod(n);
        if (n > 0) bytes(s.data(), n);
    }
};

class ByteReader {
public:
    const std::uint8_t* ptr = nullptr;
    std::size_t len = 0;
    std::size_t pos = 0;

    bool ok(std::size_t need) const noexcept { return pos + need <= len; }

    template <typename T>
    bool pod(T& out) noexcept {
        if (!ok(sizeof(T))) return false;
        std::memcpy(&out, ptr + pos, sizeof(T));
        pos += sizeof(T);
        return true;
    }

    bool bytes(void* dst, std::size_t n) noexcept {
        if (!ok(n)) return false;
        if (n > 0) std::memcpy(dst, ptr + pos, n);
        pos += n;
        return true;
    }

    bool str(std::string& out) {
        std::uint32_t n = 0;
        if (!pod(n) || n > 10'000'000u || !ok(n)) return false;
        out.resize(n);
        if (n > 0) std::memcpy(out.data(), ptr + pos, n);
        pos += n;
        return true;
    }
};

} // namespace

std::vector<std::uint8_t> BinarySave::serialize_to_bytes(const Engine& engine) {
    ByteWriter w;

    // 1. PRNG + Clock + Recommender + WorldPool
    const auto st = engine.prng.state();
    w.pod(st);
    w.pod(engine.clock.total_hours());
    w.pod(static_cast<std::uint8_t>(engine.recommender.regime_id()));
    w.pod(engine.recommender.trust_score());
    w.pod(engine.recommender.last_publish_day());
    w.pod(static_cast<std::int32_t>(engine.recommender.streak()));
    w.pod(engine.world_pool_multiplier());

    // 2. Pending consequences
    const auto& heap = engine.clock.raw_heap();
    w.pod(static_cast<std::uint32_t>(heap.size()));
    for (const auto& c : heap) {
        w.pod(c.id);
        w.str(c.source_dilemma_id);
        w.pod(c.trigger_at_hour);
        w.str(c.headline_ru);
        w.str(c.description_ru);
        w.pod(c.trust_delta);
        w.pod(c.reputation_delta);
        w.pod(c.cash_delta_usd);
        w.pod(c.loyalty_delta);
        w.pod(c.nostalgia_delta);
        w.pod(c.burnout_delta);
    }

    // 3. Creator & Team
    const auto& cr = engine.creator_system.creator;
    w.str(cr.handle);
    w.str(cr.display_name);
    w.pod(static_cast<std::uint8_t>(cr.origin_niche));
    w.pod(static_cast<std::uint8_t>(cr.current_niche));
    w.pod(static_cast<std::int32_t>(cr.equipment_tier));
    w.pod(cr.energy);
    w.pod(cr.burnout);
    w.pod(cr.personal_life);
    w.pod(cr.reputation);
    w.pod(cr.cash_usd);
    w.pod(cr.skills);
    w.pod(static_cast<std::uint8_t>(engine.creator_system.current_stage()));
    w.pod(engine.creator_system.next_member_id());
    const auto& team = engine.creator_system.members();
    w.pod(static_cast<std::uint32_t>(team.size()));
    for (const auto& m : team) {
        w.pod(m.id);
        w.str(m.name_ru);
        w.pod(static_cast<std::uint8_t>(m.role));
        w.pod(m.skill);
        w.pod(m.morale);
        w.pod(m.monthly_salary_usd);
        w.pod(m.quit_risk);
    }

    // 4. SoA AudiencePool (прямой дамп активных слайсов памяти!)
    const auto n_ag = static_cast<std::uint32_t>(engine.audience.active_count());
    w.pod(n_ag);
    w.pod(engine.audience.subscribers());
    if (n_ag > 0) {
        w.bytes(engine.audience.segment.data(), n_ag * sizeof(std::uint8_t));
        w.bytes(engine.audience.subscribed.data(), n_ag * sizeof(std::uint8_t));
        w.bytes(engine.audience.primary_niche.data(), n_ag * sizeof(std::uint8_t));
        w.bytes(engine.audience.loyalty.data(), n_ag * sizeof(float));
        w.bytes(engine.audience.patience.data(), n_ag * sizeof(float));
        w.bytes(engine.audience.ad_tolerance.data(), n_ag * sizeof(float));
        w.bytes(engine.audience.nostalgia.data(), n_ag * sizeof(float));
        w.bytes(engine.audience.fatigue.data(), n_ag * sizeof(float));
        w.bytes(engine.audience.joined_day.data(), n_ag * sizeof(std::uint16_t));
    }

    // 5. Trends & Dilemmas
    w.pod(engine.trends.next_id());
    const auto& trs = engine.trends.active_trends();
    w.pod(static_cast<std::uint32_t>(trs.size()));
    for (const auto& t : trs) {
        w.pod(t.id);
        w.str(t.title_ru);
        w.pod(static_cast<std::uint8_t>(t.niche));
        w.pod(static_cast<std::uint8_t>(t.phase));
        w.pod(t.virality_boost);
        w.pod(t.controversy_risk);
        w.pod(t.created_day);
        w.pod(t.expires_day);
    }

    w.pod(engine.dilemmas.next_seq());
    const auto& res_ids = engine.dilemmas.resolved_ids();
    w.pod(static_cast<std::uint32_t>(res_ids.size()));
    for (const auto& id : res_ids) w.str(id);

    // Собираем заголовок с CRC32
    ByteWriter header;
    header.pod(kMagic);
    header.pod(kFormatVersion);
    header.pod(static_cast<std::uint32_t>(w.buf.size()));
    header.pod(compute_crc32(w.buf.data(), w.buf.size()));
    header.bytes(w.buf.data(), w.buf.size());
    return header.buf;
}

bool BinarySave::deserialize_from_bytes(
    Engine& engine, const std::uint8_t* data, std::size_t size, std::string& out_error) {
    if (!data || size < 16) {
        out_error = "Повреждённый заголовок сохранения (.simsave)";
        return false;
    }

    ByteReader hdr{data, size, 0};
    std::uint32_t magic = 0, ver = 0, payload_len = 0, expected_crc = 0;
    hdr.pod(magic);
    hdr.pod(ver);
    hdr.pod(payload_len);
    hdr.pod(expected_crc);

    if (magic != kMagic || ver != kFormatVersion || payload_len + 16u != size) {
        out_error = "Неверная версия или размер файла .simsave";
        return false;
    }
    if (compute_crc32(data + 16, payload_len) != expected_crc) {
        out_error = "Ошибка контрольной суммы CRC32 сохранения!";
        return false;
    }

    ByteReader r{data + 16, payload_len, 0};
    SeededPrng::State st{};
    std::int64_t total_hours = 0;
    std::uint8_t reg_id = 0;
    float trust = 1.0f, pool_mult = 1.0f;
    std::int32_t last_pub = -1, streak = 0;

    if (!r.pod(st) || !r.pod(total_hours) || !r.pod(reg_id) ||
        !r.pod(trust) || !r.pod(last_pub) || !r.pod(streak) || !r.pod(pool_mult)) {
        out_error = "Ошибка чтения ядра сохранения";
        return false;
    }

    std::uint32_t heap_n = 0;
    if (!r.pod(heap_n) || heap_n > 10000u) return false;
    std::vector<ScheduledConsequence> heap(heap_n);
    for (std::uint32_t i = 0; i < heap_n; ++i) {
        auto& c = heap[i];
        if (!r.pod(c.id) || !r.str(c.source_dilemma_id) || !r.pod(c.trigger_at_hour) ||
            !r.str(c.headline_ru) || !r.str(c.description_ru) || !r.pod(c.trust_delta) ||
            !r.pod(c.reputation_delta) || !r.pod(c.cash_delta_usd) || !r.pod(c.loyalty_delta) ||
            !r.pod(c.nostalgia_delta) || !r.pod(c.burnout_delta)) {
            return false;
        }
    }

    CreatorState cr{};
    std::uint8_t orig_n = 0, cur_n = 0, stg = 0;
    std::int32_t eq_tier = 1;
    std::uint32_t next_m_id = 1, team_n = 0;
    if (!r.str(cr.handle) || !r.str(cr.display_name) || !r.pod(orig_n) || !r.pod(cur_n) ||
        !r.pod(eq_tier) || !r.pod(cr.energy) || !r.pod(cr.burnout) || !r.pod(cr.personal_life) ||
        !r.pod(cr.reputation) || !r.pod(cr.cash_usd) || !r.pod(cr.skills) ||
        !r.pod(stg) || !r.pod(next_m_id) || !r.pod(team_n) || team_n > 100u) {
        return false;
    }
    cr.origin_niche = static_cast<NicheId>(orig_n);
    cr.current_niche = static_cast<NicheId>(cur_n);
    cr.equipment_tier = eq_tier;

    std::vector<TeamMember> team(team_n);
    for (std::uint32_t i = 0; i < team_n; ++i) {
        std::uint8_t role_u = 0;
        if (!r.pod(team[i].id) || !r.str(team[i].name_ru) || !r.pod(role_u) ||
            !r.pod(team[i].skill) || !r.pod(team[i].morale) ||
            !r.pod(team[i].monthly_salary_usd) || !r.pod(team[i].quit_risk)) {
            return false;
        }
        team[i].role = static_cast<TeamRole>(role_u);
    }

    std::uint32_t n_ag = 0;
    std::int64_t macro_subs = 0;
    if (!r.pod(n_ag) || !r.pod(macro_subs)) return false;
    const std::size_t copy_ag = std::min<std::size_t>(n_ag, engine.audience.capacity());
    const std::size_t skip_ag = n_ag > copy_ag ? (n_ag - copy_ag) : 0;

    auto read_slice = [&](void* dst, std::size_t elem_sz) -> bool {
        if (!r.bytes(dst, copy_ag * elem_sz)) return false;
        if (skip_ag > 0) {
            if (!r.ok(skip_ag * elem_sz)) return false;
            r.pos += skip_ag * elem_sz;
        }
        return true;
    };

    if (n_ag > 0) {
        if (!read_slice(engine.audience.segment.data(), sizeof(std::uint8_t)) ||
            !read_slice(engine.audience.subscribed.data(), sizeof(std::uint8_t)) ||
            !read_slice(engine.audience.primary_niche.data(), sizeof(std::uint8_t)) ||
            !read_slice(engine.audience.loyalty.data(), sizeof(float)) ||
            !read_slice(engine.audience.patience.data(), sizeof(float)) ||
            !read_slice(engine.audience.ad_tolerance.data(), sizeof(float)) ||
            !read_slice(engine.audience.nostalgia.data(), sizeof(float)) ||
            !read_slice(engine.audience.fatigue.data(), sizeof(float)) ||
            !read_slice(engine.audience.joined_day.data(), sizeof(std::uint16_t))) {
            return false;
        }
    }

    std::uint32_t tr_next = 1, tr_n = 0;
    if (!r.pod(tr_next) || !r.pod(tr_n) || tr_n > 100u) return false;
    std::vector<TrendCard> trs(tr_n);
    for (std::uint32_t i = 0; i < tr_n; ++i) {
        std::uint8_t ni = 0, ph = 0;
        if (!r.pod(trs[i].id) || !r.str(trs[i].title_ru) || !r.pod(ni) || !r.pod(ph) ||
            !r.pod(trs[i].virality_boost) || !r.pod(trs[i].controversy_risk) ||
            !r.pod(trs[i].created_day) || !r.pod(trs[i].expires_day)) {
            return false;
        }
        trs[i].niche = static_cast<NicheId>(ni);
        trs[i].phase = static_cast<TrendPhase>(ph);
    }

    std::uint32_t dil_seq = 1, res_n = 0;
    if (!r.pod(dil_seq) || !r.pod(res_n) || res_n > 1000u) return false;
    std::vector<std::string> res_ids(res_n);
    for (std::uint32_t i = 0; i < res_n; ++i) {
        if (!r.str(res_ids[i])) return false;
    }

    engine.prng.set_state(st);
    engine.clock.restore(total_hours, std::move(heap));
    engine.recommender.restore_raw(static_cast<AlgorithmRegimeId>(reg_id), trust, last_pub, streak);
    engine.set_world_pool_multiplier(pool_mult);
    engine.creator_system.restore_raw(std::move(cr), static_cast<ChannelStage>(stg), std::move(team), next_m_id);
    engine.audience.set_counts_raw(copy_ag, macro_subs);
    engine.trends.restore_raw(std::move(trs), tr_next);
    engine.dilemmas.restore_raw(std::move(res_ids), dil_seq);
    engine.replace_videos_raw({});
    return true;
}

bool BinarySave::save_to_file(const Engine& engine, const std::string& path, std::string& out_error) {
    const auto bytes = serialize_to_bytes(engine);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        out_error = "Не удалось открыть файл сохранения для записи: " + path;
        return false;
    }
    const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (written != bytes.size()) {
        out_error = "Неполная запись сохранения";
        return false;
    }
    return true;
}

bool BinarySave::load_from_file(Engine& engine, const std::string& path, std::string& out_error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        out_error = "Файл сохранения не найден: " + path;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 16) {
        std::fclose(f);
        out_error = "Пустой или битый файл .simsave";
        return false;
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    const std::size_t rd = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (rd != buf.size()) {
        out_error = "Ошибка чтения файла .simsave";
        return false;
    }
    return deserialize_from_bytes(engine, buf.data(), buf.size(), out_error);
}

} // namespace simul::core
