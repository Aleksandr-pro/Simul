#include "simul/core/narrative.hpp"
#include "simul/core/math_utils.hpp"
#include <algorithm>
#include <cmath>

namespace simul::core {

namespace {

constexpr const char* kTrendTitles[kNicheCount][4] = {
    {
        "Спидран хардкорного инди-хита вслепую",
        "Выживание 100 дней на экстремальной карте",
        "Разбор секретного патча главного шутера",
        "Турнир стримеров без донатного снаряжения",
    },
    {
        "Краш-тест нового складного флагмана",
        "Сборка бесшумного ПК за минимальный бюджет",
        "Неделя с локальной нейросетью вместо смартфона",
        "Разоблачение поддельной техники с маркетплейсов",
    },
    {
        "Переезд в микро-студию: минимализм 30 дней",
        "Подъём в 5 утра: честный дневник энергии",
        "Месяц жизни на средний бюджет подписчика",
        "Неделя полного цифрового детокса",
    },
    {
        "Как алгоритмы лент управляют дофамином",
        "Экономика хайпа: почему пузыри лопаются",
        "Физика невозможных трюков из вирусных видео",
        "История одного техно-провала на миллиард",
    },
    {
        "Прятки в огромном павильоне на выживание",
        "Угадай профи среди пяти актёров",
        "24 часа в самой необычной локации",
        "Кулинарный баттл: блюдо за $5 против $500",
    },
};

} // namespace

void TrendSystem::seed_initial_trends(SeededPrng& prng, std::int32_t current_day) {
    trends_.clear();
    for (int i = 0; i < 4; ++i) {
        spawn_trend(prng, current_day);
    }
}

TrendCard TrendSystem::spawn_trend(SeededPrng& prng, std::int32_t current_day) {
    const int n_idx = prng.int_range(0, static_cast<int>(kNicheCount) - 1);
    const int t_idx = prng.int_range(0, 3);
    const int life = prng.int_range(8, 18);

    TrendCard c{};
    c.id = next_id_++;
    c.niche = static_cast<NicheId>(n_idx);
    c.title_ru = kTrendTitles[n_idx][t_idx];
    c.phase = TrendPhase::Emerging;
    c.virality_boost = prng.range(1.24f, 1.40f);
    c.controversy_risk = prng.range(0.05f, 0.25f);
    c.created_day = current_day;
    c.expires_day = current_day + life;

    trends_.push_back(c);
    return c;
}

void TrendSystem::advance_to_day(std::int32_t current_day, SeededPrng& prng) {
    std::vector<TrendCard> alive;
    alive.reserve(trends_.size() + 2);

    for (auto& t : trends_) {
        if (current_day > t.expires_day) continue;
        const float span = static_cast<float>(std::max(1, t.expires_day - t.created_day));
        const float prog = static_cast<float>(current_day - t.created_day) / span;
        if (prog < 0.28f) {
            t.phase = TrendPhase::Emerging;
            t.virality_boost = 1.28f;
        } else if (prog < 0.62f) {
            t.phase = TrendPhase::Peaking;
            t.virality_boost = 1.40f;
        } else if (prog < 0.85f) {
            t.phase = TrendPhase::Saturated;
            t.virality_boost = 0.92f;
        } else {
            t.phase = TrendPhase::Fading;
            t.virality_boost = 0.78f;
        }
        alive.push_back(std::move(t));
    }
    trends_ = std::move(alive);
    while (trends_.size() < 4) {
        spawn_trend(prng, current_day);
    }
}

const TrendCard* TrendSystem::find_by_id(std::uint32_t id) const noexcept {
    if (id == 0) return nullptr;
    for (const auto& t : trends_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

// --- DilemmaEngine ---

DilemmaEngine::DilemmaEngine() {
    {
        DilemmaDefinition d;
        d.id = "sketchy_energy_drink";
        d.min_stage = ChannelStage::Growing;
        d.title_ru = "Контракт от «НейроБуст Энерджи»";
        d.situation_ru =
            "Малоизвестный бренд энергетиков предлагает $3 200 за интеграцию без пометки рекламы. Состав вызывает вопросы у врачей.";

        DilemmaChoice c1;
        c1.id = "accept_unmarked";
        c1.label_ru = "Взять $3 200 и похвалить напиток";
        c1.preview_ru = "+$3 200 сейчас, высокий риск скандала через месяц";
        c1.immediate_cash_usd = 3200.0;
        c1.immediate_reputation_delta = -4.0f;
        c1.immediate_trust_delta = -0.05f;
        c1.has_delayed = true;
        c1.delay_days_min = 24;
        c1.delay_days_max = 46;
        c1.delayed_probability = 0.78f;
        c1.delayed_template.headline_ru = "Разоблачение «НейроБуст Энерджи» ударило по каналу";
        c1.delayed_template.description_ru =
            "Экспертиза вскрыла обман производителя. Зрители припомнили вашу скрытую рекламу — просел траст и лояльность.";
        c1.delayed_template.trust_delta = -0.24f;
        c1.delayed_template.reputation_delta = -18.0f;
        c1.delayed_template.cash_delta_usd = -600.0;
        c1.delayed_template.loyalty_delta = -0.18f;
        c1.delayed_template.nostalgia_delta = 0.05f;
        c1.delayed_template.burnout_delta = 10.0f;

        DilemmaChoice c2;
        c2.id = "decline_honestly";
        c2.label_ru = "Отказаться ради доверия аудитории";
        c2.preview_ru = "$0 сейчас, +репутация и шанс белого спонсора";
        c2.immediate_reputation_delta = 7.0f;
        c2.immediate_trust_delta = 0.04f;
        c2.has_delayed = true;
        c2.delay_days_min = 18;
        c2.delay_days_max = 35;
        c2.delayed_probability = 0.65f;
        c2.delayed_template.headline_ru = "Крупный техно-бренд оценил вашу честность";
        c2.delayed_template.description_ru =
            "После краха сомнительного энергетика проверенный бренд выделил вашему каналу чистый грант.";
        c2.delayed_template.trust_delta = 0.05f;
        c2.delayed_template.reputation_delta = 8.0f;
        c2.delayed_template.cash_delta_usd = 2100.0;
        c2.delayed_template.loyalty_delta = 0.09f;

        d.choices = {c1, c2};
        catalog_.push_back(std::move(d));
    }
    {
        DilemmaDefinition d;
        d.id = "drama_expose_rival";
        d.min_stage = ChannelStage::Growing;
        d.title_ru = "Слив переписки конкурента";
        d.situation_ru =
            "В руки попал спорный скриншот чата популярного блогера. Острое видео даст быстрый трафик, но вырвано из контекста.";

        DilemmaChoice c1;
        c1.id = "publish_drama";
        c1.label_ru = "Выпустить громкое разоблачение";
        c1.preview_ru = "+$1 400 с хайпа, риск ответного удара";
        c1.immediate_cash_usd = 1400.0;
        c1.immediate_energy_delta = -12.0f;
        c1.immediate_reputation_delta = -8.0f;
        c1.immediate_trust_delta = -0.04f;
        c1.has_delayed = true;
        c1.delay_days_min = 20;
        c1.delay_days_max = 40;
        c1.delayed_probability = 0.72f;
        c1.delayed_template.headline_ru = "Конкурент выложил полный контекст переписки";
        c1.delayed_template.description_ru =
            "Часть зрителей разочаровалась в подаче и перешла в сегмент критиков.";
        c1.delayed_template.trust_delta = -0.14f;
        c1.delayed_template.reputation_delta = -15.0f;
        c1.delayed_template.loyalty_delta = -0.15f;
        c1.delayed_template.burnout_delta = 14.0f;

        DilemmaChoice c2;
        c2.id = "ignore_drama";
        c2.label_ru = "Сфокусироваться на своём контенте";
        c2.preview_ru = "+энергия, +чистая репутация";
        c2.immediate_energy_delta = 5.0f;
        c2.immediate_reputation_delta = 4.0f;
        c2.immediate_trust_delta = 0.02f;

        d.choices = {c1, c2};
        catalog_.push_back(std::move(d));
    }
    {
        DilemmaDefinition d;
        d.id = "exclusive_agency_contract";
        d.min_stage = ChannelStage::Known;
        d.title_ru = "Эксклюзив с медиа-холдингом «Орион»";
        d.situation_ru =
            "Агентство предлагает $18 000 авансом в обмен на согласование тем ваших роликов на три месяца.";

        DilemmaChoice c1;
        c1.id = "sign_agency";
        c1.label_ru = "Подписать контракт (+$18 000)";
        c1.preview_ru = "Большой бюджет сразу, но рост ностальгии у олдов";
        c1.immediate_cash_usd = 18000.0;
        c1.immediate_reputation_delta = -3.0f;
        c1.has_delayed = true;
        c1.delay_days_min = 35;
        c1.delay_days_max = 65;
        c1.delayed_probability = 0.80f;
        c1.delayed_template.headline_ru = "Олды устали от продюсерской цензуры холдинга";
        c1.delayed_template.description_ru =
            "Волна комментариев «раньше было с душой» ударила по удержанию старых фанатов.";
        c1.delayed_template.loyalty_delta = -0.12f;
        c1.delayed_template.nostalgia_delta = 0.25f;
        c1.delayed_template.burnout_delta = 16.0f;

        DilemmaChoice c2;
        c2.id = "stay_indie";
        c2.label_ru = "Остаться независимым автором";
        c2.preview_ru = "Сохранить голос канала и уважение ветеранов";
        c2.immediate_reputation_delta = 6.0f;
        c2.immediate_trust_delta = 0.03f;

        d.choices = {c1, c2};
        catalog_.push_back(std::move(d));
    }
}

bool DilemmaEngine::is_resolved(const std::string& id) const noexcept {
    return std::find(resolved_ids_.begin(), resolved_ids_.end(), id) != resolved_ids_.end();
}

std::vector<const DilemmaDefinition*> DilemmaEngine::available_dilemmas(ChannelStage current_stage) const {
    std::vector<const DilemmaDefinition*> out;
    const auto cur_rank = static_cast<int>(current_stage);
    for (const auto& d : catalog_) {
        if (static_cast<int>(d.min_stage) <= cur_rank && !is_resolved(d.id)) {
            out.push_back(&d);
        }
    }
    return out;
}

DilemmaEngine::Resolution DilemmaEngine::resolve_choice(
    const std::string& dilemma_id,
    const std::string& choice_id,
    std::int64_t current_total_hours,
    SeededPrng& prng) {
    Resolution res{};
    for (const auto& d : catalog_) {
        if (d.id != dilemma_id) continue;
        for (const auto& ch : d.choices) {
            if (ch.id != choice_id) continue;
            resolved_ids_.push_back(d.id);
            res.choice = ch;
            if (ch.has_delayed && prng.chance(ch.delayed_probability)) {
                const int days = prng.int_range(ch.delay_days_min, ch.delay_days_max);
                res.has_scheduled = true;
                res.scheduled = ch.delayed_template;
                res.scheduled.id = next_seq_++;
                res.scheduled.source_dilemma_id = d.id;
                res.scheduled.trigger_at_hour = current_total_hours + static_cast<std::int64_t>(days) * 24;
            }
            return res;
        }
    }
    return res;
}

// --- CreatorAndTeamSystem ---

CreatorSkills CreatorAndTeamSystem::effective_skills() const noexcept {
    float edit_b = 0.0f;
    float script_b = 0.0f;
    float anal_b = 0.0f;
    for (const auto& m : team_) {
        const float mf = 0.5f + (m.morale / 100.0f) * 0.5f;
        if (m.role == TeamRole::Editor) edit_b += m.skill * 0.35f * mf;
        else if (m.role == TeamRole::Scriptwriter) script_b += m.skill * 0.35f * mf;
        else if (m.role == TeamRole::Manager) anal_b += m.skill * 0.30f * mf;
    }
    return {
        clamp_val(creator.skills.scriptwriting + script_b, 1.0f, 100.0f),
        clamp_val(creator.skills.editing + edit_b, 1.0f, 100.0f),
        creator.skills.charisma,
        clamp_val(creator.skills.analytics + anal_b, 1.0f, 100.0f),
    };
}

bool CreatorAndTeamSystem::sync_stage(std::int64_t subscribers) noexcept {
    const auto resolved = resolve_stage_spec(subscribers).id;
    if (static_cast<int>(resolved) > static_cast<int>(current_stage_)) {
        current_stage_ = resolved;
        return true;
    }
    return false;
}

bool CreatorAndTeamSystem::hire_member(TeamRole role, float skill, SeededPrng& prng, std::string& out_error) {
    const int max_slots = stage_spec().team_slots;
    if (static_cast<int>(team_.size()) >= max_slots) {
        out_error = "Нужен более высокий этап канала для расширения штата!";
        return false;
    }

    static constexpr const char* kEditorNames[] = {"Илья (Монтаж)", "Алина (Ритм)", "Тимур (VFX)"};
    static constexpr const char* kScriptNames[] = {"София (Сценарист)", "Марк (Шеф-ред)"};
    static constexpr const char* kCamNames[] = {"Денис (Оператор)", "Роман (Свет)"};
    static constexpr const char* kMgrNames[] = {"Екатерина (Менеджер)", "Павел (Бренды)"};

    TeamMember m{};
    m.id = next_member_id_++;
    m.role = role;
    m.skill = clamp_val(skill, 15.0f, 95.0f);
    m.morale = 84.0f;
    m.monthly_salary_usd = std::round(350.0 + static_cast<double>(m.skill) * 20.0);
    m.quit_risk = 0.04f;

    switch (role) {
        case TeamRole::Editor: m.name_ru = kEditorNames[prng.int_range(0, 2)]; break;
        case TeamRole::Scriptwriter: m.name_ru = kScriptNames[prng.int_range(0, 1)]; break;
        case TeamRole::Cameraman: m.name_ru = kCamNames[prng.int_range(0, 1)]; break;
        case TeamRole::Manager: m.name_ru = kMgrNames[prng.int_range(0, 1)]; break;
    }

    team_.push_back(std::move(m));
    return true;
}

bool CreatorAndTeamSystem::upgrade_equipment(double& out_cost, std::string& out_error) {
    if (creator.equipment_tier >= 5) {
        out_error = "Студия уже максимального 5-го уровня!";
        return false;
    }
    static constexpr double kCosts[6] = {0.0, 550.0, 2200.0, 9000.0, 36000.0, 0.0};
    out_cost = kCosts[creator.equipment_tier];
    if (creator.cash_usd < out_cost) {
        out_error = "Недостаточно бюджета на апгрейд оборудования!";
        return false;
    }
    creator.cash_usd -= out_cost;
    creator.equipment_tier += 1;
    return true;
}

void CreatorAndTeamSystem::apply_video_effort(bool delegated_to_team) noexcept {
    const bool delegated = delegated_to_team && !team_.empty();
    const float cost = delegated ? 16.0f : 32.0f;
    if (creator.energy < cost) {
        creator.burnout = clamp_val(creator.burnout + 12.0f, 0.0f, 100.0f);
        creator.personal_life = clamp_val(creator.personal_life - 7.0f, 0.0f, 100.0f);
    } else {
        creator.burnout = clamp_val(creator.burnout + (delegated ? 2.5f : 5.2f), 0.0f, 100.0f);
    }
    creator.energy = clamp_val(creator.energy - cost, 0.0f, 100.0f);

    creator.skills.scriptwriting = clamp_val(creator.skills.scriptwriting + 1.1f, 1.0f, 100.0f);
    creator.skills.editing = clamp_val(creator.skills.editing + (delegated ? 0.5f : 1.3f), 1.0f, 100.0f);
    creator.skills.charisma = clamp_val(creator.skills.charisma + 0.9f, 1.0f, 100.0f);
    creator.skills.analytics = clamp_val(creator.skills.analytics + 1.0f, 1.0f, 100.0f);
}

std::vector<TeamMember> CreatorAndTeamSystem::advance_days(
    int days, std::int32_t prev_day, std::int32_t next_day, SeededPrng& prng) {
    std::vector<TeamMember> quitters;
    if (days <= 0) return quitters;

    creator.energy = clamp_val(creator.energy + static_cast<float>(days) * 22.0f, 0.0f, 100.0f);
    creator.burnout = clamp_val(creator.burnout - static_cast<float>(days) * 2.2f, 0.0f, 100.0f);
    creator.personal_life = clamp_val(creator.personal_life + static_cast<float>(days) * 1.5f, 0.0f, 100.0f);

    const int months = (next_day / 30) - (prev_day / 30);
    if (months > 0 && !team_.empty()) {
        double payroll = 0.0;
        for (const auto& m : team_) payroll += m.monthly_salary_usd * months;

        if (creator.cash_usd >= payroll) {
            creator.cash_usd -= payroll;
            for (auto& m : team_) {
                m.morale = clamp_val(m.morale + 4.0f, 0.0f, 100.0f);
                m.quit_risk = clamp_val((100.0f - m.morale) / 160.0f, 0.01f, 0.75f);
            }
        } else {
            creator.cash_usd = std::max(0.0, creator.cash_usd - payroll);
            for (auto& m : team_) {
                m.morale = clamp_val(m.morale - 32.0f, 0.0f, 100.0f);
                m.quit_risk = clamp_val((100.0f - m.morale) / 110.0f, 0.05f, 0.90f);
            }
        }

        std::vector<TeamMember> staying;
        for (auto& m : team_) {
            if (m.morale < 55.0f && prng.chance(m.quit_risk)) {
                quitters.push_back(std::move(m));
            } else {
                staying.push_back(std::move(m));
            }
        }
        team_ = std::move(staying);
    }
    return quitters;
}

// --- HybridCommentGenerator ---

std::vector<GeneratedComment> HybridCommentGenerator::generate_for_video(
    const PublishedVideoRecord& video,
    bool is_niche_shifted,
    bool has_sketchy_sponsor,
    float clickbait_gap,
    int weakest_decile_index,
    SeededPrng& prng,
    std::size_t count) {
    static constexpr ViewerSegment kOrder[7] = {
        ViewerSegment::CoreFan,
        ViewerSegment::Subscriber,
        ViewerSegment::Critic,
        ViewerSegment::Veteran,
        ViewerSegment::Casual,
        ViewerSegment::Hater,
        ViewerSegment::BrandScout,
    };

    std::vector<GeneratedComment> out;
    out.reserve(count);
    const int drop_min = std::max(1, weakest_decile_index + 1);

    for (std::size_t i = 0; i < count; ++i) {
        ViewerSegment seg = kOrder[i % (video.max_wave_reached >= 2 ? 7 : 6)];
        GeneratedComment c{};
        c.id = seq_++;
        c.segment = seg;
        c.likes = std::max<std::int64_t>(1, std::llround((static_cast<double>(video.total_views) / 310.0) * prng.range(0.15f, 1.45f)));

        switch (seg) {
            case ViewerSegment::CoreFan:
                c.author_handle = prng.chance(0.5f) ? "@fan_first_sec" : "@oleg_viral";
                c.text = video.max_wave_reached >= 3
                             ? "Я здесь до миллиона! Алгоритм наконец-то вывел канал в тренды!"
                             : "Лайк с первой секунды, подача и монтаж держат до конца!";
                break;
            case ViewerSegment::Veteran:
                c.author_handle = "@old_since_zero";
                c.text = is_niche_shifted
                             ? "Эх, раньше на канале был свой вайб, а теперь резкая смена тематики..."
                             : "Олды на месте! Круто видеть, как вырос звук и свет без потери души.";
                break;
            case ViewerSegment::Critic:
                c.author_handle = "@frame_inspector";
                c.text = video.avg_retention < 0.45f
                             ? ("Идея хорошая, но на " + std::to_string(drop_min) + "-й минуте темп проседает.")
                             : "Респект за плотный монтаж без воды — хук цепляет сразу.";
                break;
            case ViewerSegment::Hater:
                c.author_handle = "@skip_sponsor";
                c.is_toxic = true;
                if (has_sketchy_sponsor) {
                    c.text = "Опять сомнительная реклама ради быстрых денег? Отписка.";
                } else if (clickbait_gap > 0.25f) {
                    c.text = "Превью обещает сенсацию, а внутри пересказ очевидного.";
                } else {
                    c.text = "Слишком громкий заголовок для обычного выпуска.";
                }
                break;
            case ViewerSegment::BrandScout:
                c.author_handle = "@brand_media_pro";
                c.text = "Отличное удержание аудитории! Написали на почту по спецпроекту.";
                break;
            default:
                c.author_handle = "@night_scroll";
                c.text = video.max_wave_reached >= 2
                             ? "Залетело из рекомендаций — досмотрел до финала и подписался!"
                             : "Коротко и по делу, жду следующий ролик.";
                break;
        }
        out.push_back(std::move(c));
    }

    if (llm_hook_) {
        llm_hook_(video, out);
    }

    std::sort(out.begin(), out.end(), [](const GeneratedComment& a, const GeneratedComment& b) {
        return a.likes > b.likes;
    });
    return out;
}

} // namespace simul::core
