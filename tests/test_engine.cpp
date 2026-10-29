#include "simul/core/binary_save.hpp"
#include "simul/core/engine.hpp"
#include "simul/ui/renderer_2d.hpp"
#include "../game/src/viral_app.hpp"
#include "sokol_gfx.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const char* expr, const char* file, int line) {
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        std::fprintf(stderr, "[FAIL] %s:%d: %s\n", file, line, expr);
    }
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

simul::core::VideoReleaseInput make_strong_video(const char* title = "Сильный техно-выпуск") {
    simul::core::VideoReleaseInput in{};
    in.title = title;
    in.niche = simul::core::NicheId::Tech;
    in.production_budget_usd = 180.0;
    in.publish_hour_of_day = 19;
    in.editing.hook_strength = 0.92f;
    in.editing.pacing_tightness = 0.88f;
    in.editing.payoff_strength = 0.90f;
    in.editing.ad_placement_ratio = -1.0f;
    in.packaging.variant_a.id = 'A';
    in.packaging.variant_a.title = title;
    in.packaging.variant_a.visual_contrast = 0.88f;
    in.packaging.variant_a.curiosity_gap = 0.86f;
    in.packaging.variant_a.clickbait_level = 0.14f;
    return in;
}

simul::core::VideoReleaseInput make_weak_scam_video(const char* title = "ШОК СЕНСАЦИЯ") {
    simul::core::VideoReleaseInput in{};
    in.title = title;
    in.niche = simul::core::NicheId::Tech;
    in.production_budget_usd = 0.0;
    in.publish_hour_of_day = 4;
    in.sponsor = {true, "ScamCoin", 500.0, 0.95f};
    in.editing.hook_strength = 0.18f;
    in.editing.pacing_tightness = 0.20f;
    in.editing.payoff_strength = 0.15f;
    in.editing.ad_placement_ratio = 0.05f;
    in.packaging.variant_a.id = 'A';
    in.packaging.variant_a.title = title;
    in.packaging.variant_a.visual_contrast = 0.45f;
    in.packaging.variant_a.curiosity_gap = 0.40f;
    in.packaging.variant_a.clickbait_level = 0.92f;
    return in;
}

} // namespace

int main() {
    using namespace simul::core;

    // 1. Бит-в-бит детерминизм
    {
        Engine a({424242u, "@a", "A", NicheId::Tech, 700.0, 25'000, 12'000});
        Engine b({424242u, "@a", "A", NicheId::Tech, 700.0, 25'000, 12'000});
        std::string err;
        CHECK(a.publish_and_simulate_full_video(make_strong_video(), err));
        CHECK(b.publish_and_simulate_full_video(make_strong_video(), err));
        CHECK(a.published_videos().back().total_views == b.published_videos().back().total_views);
        CHECK(a.subscribers() == b.subscribers());
    }

    // 2. Продвижение по волнам: сильный ролик обходит слабый скам
    {
        Engine good({1001u, "@g", "G", NicheId::Tech, 700.0, 25'000, 12'000});
        Engine bad({1001u, "@b", "B", NicheId::Tech, 700.0, 25'000, 12'000});
        std::string err;
        CHECK(good.publish_and_simulate_full_video(make_strong_video(), err));
        CHECK(bad.publish_and_simulate_full_video(make_weak_scam_video(), err));
        CHECK(good.published_videos().back().max_wave_reached > bad.published_videos().back().max_wave_reached);
        CHECK(good.published_videos().back().total_views > bad.published_videos().back().total_views * 3);
    }

    // 3. A/B тест обложек выбирает лучший вариант B
    {
        Engine eng({2026u, "@ab", "AB", NicheId::Gaming, 700.0, 25'000, 12'000});
        auto in = make_strong_video("Коротко");
        in.niche = NicheId::Gaming;
        in.packaging.variant_a.title = "Тест";
        in.packaging.variant_a.visual_contrast = 0.20f;
        in.packaging.variant_a.curiosity_gap = 0.20f;
        in.packaging.has_variant_b = true;
        in.packaging.variant_b.id = 'B';
        in.packaging.variant_b.title = "Я прошёл невозможный рейд в одиночку без брони";
        in.packaging.variant_b.visual_contrast = 0.94f;
        in.packaging.variant_b.curiosity_gap = 0.92f;
        in.packaging.variant_b.clickbait_level = 0.14f;
        std::string err;
        CHECK(eng.publish_and_simulate_full_video(in, err));
        CHECK(eng.published_videos().back().post_mortem.winning_variant == 'B');
    }

    // 4. Механика шадоубана
    {
        Engine eng({909u, "@sb", "SB", NicheId::Tech, 700.0, 25'000, 12'000});
        std::string err;
        for (int i = 0; i < 4; ++i) {
            CHECK(eng.publish_and_simulate_full_video(make_weak_scam_video(), err));
            eng.advance_days(3);
        }
        CHECK(eng.recommender.is_shadowbanned());
        CHECK(eng.publish_and_simulate_full_video(make_strong_video(), err));
        CHECK(eng.published_videos().back().max_wave_reached == 0);
    }

    // 5. Бинарное сохранение .simsave с проверкой CRC32 и детерминизма после загрузки
    {
        Engine orig({314159u, "@save", "Save", NicheId::Tech, 900.0, 25'000, 10'000});
        std::string err;
        CHECK(orig.publish_and_simulate_full_video(make_strong_video("Ролик 1"), err));
        orig.advance_days(4);

        auto bytes = BinarySave::serialize_to_bytes(orig);
        CHECK(bytes.size() > 100'000u);

        Engine loaded({1u, "@x", "X", NicheId::Gaming, 10.0, 25'000, 1'000});
        CHECK(BinarySave::deserialize_from_bytes(loaded, bytes.data(), bytes.size(), err));

        CHECK(orig.publish_and_simulate_full_video(make_strong_video("Ролик 2"), err));
        CHECK(loaded.publish_and_simulate_full_video(make_strong_video("Ролик 2"), err));
        CHECK(orig.published_videos().back().total_views == loaded.published_videos().back().total_views);
        CHECK(orig.subscribers() == loaded.subscribers());

        // Портим 1 байт — CRC32 обязан отклонить повреждённый сейв!
        bytes.back() ^= 0xFFu;
        CHECK(!BinarySave::deserialize_from_bytes(loaded, bytes.data(), bytes.size(), err));
    }

    // 6. Проверка мобильного портретного UI (390x844 dp + safe-area), иллюстрации студии и конструктора превью
    {
        sg_desc desc{};
        sg_setup(&desc);

        simul::game::ThumbnailDesign td{1, 1, 2, 0.15f};
        PackagingVariant pv{};
        simul::game::apply_thumbnail_design_to_variant(td, NicheId::Tech, pv);
        CHECK(pv.visual_contrast > 0.80f);
        CHECK(pv.curiosity_gap > 0.85f);

        simul::game::ViralGameApp app;
        app.set_storage_directory("/tmp");
        app.set_safe_area(38.0f, 18.0f);
        CHECK(app.init());

        // Рендер на узком смартфоне (360x780 dp) и стандартном смартфоне (390x844 dp)
        app.update_and_render(0.016f, 360.0f, 780.0f);
        app.update_and_render(0.016f, 390.0f, 844.0f);

        // Тап по шагу «2. Монтаж» и «3. Обложка» в портретном режиме смартфона
        app.on_pointer_down(195.0f, 300.0f, true);
        app.on_pointer_up(195.0f, 300.0f, true);
        app.update_and_render(0.016f, 390.0f, 844.0f);

        app.on_pointer_down(330.0f, 300.0f, true);
        app.on_pointer_up(330.0f, 300.0f, true);
        app.update_and_render(0.016f, 390.0f, 844.0f);

        // Переключение по всем 5 вкладкам нижнего таб-бара
        for (int tab = 0; tab < 5; ++tab) {
            const float tx = 40.0f + static_cast<float>(tab) * 76.0f;
            app.on_pointer_down(tx, 800.0f, true);
            app.on_pointer_up(tx, 800.0f, true);
            app.update_and_render(0.016f, 390.0f, 844.0f);
        }

        app.shutdown();
        std::remove("/tmp/career_autosave.simsave");

        sg_shutdown();
    }

    std::printf("[Simul Native C++20 Tests] Passed: %d, Failed: %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
