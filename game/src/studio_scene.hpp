#pragma once

#include "simul/core/types.hpp"
#include "simul/ui/renderer_2d.hpp"
#include <cstdint>
#include <string_view>

namespace simul::game {

struct ThumbnailDesign {
    int palette_idx = 0;     // 0=Кибер-неон, 1=Огонь/Хайп, 2=Изумруд, 3=Золото/Люкс
    int focal_idx = 0;       // 0=Эмоция лица, 1=Сравнение VS, 2=Красная стрелка, 3=Взрывной график
    int sticker_idx = 1;     // 0=Без плашки, 1="ПРОВЕРКА!", 2="БЕЗ ШАНСОВ?!", 3="ШОК ФИНАЛ!"
    float clickbait = 0.16f; // 0.0 .. 0.95
};

/** Вычисляет объективные метрики обложки (контраст, интригу и кликбейт) из визуального конструктора */
void apply_thumbnail_design_to_variant(
    const ThumbnailDesign& design,
    core::NicheId niche,
    core::PackagingVariant& out_variant) noexcept;

/** Рисует живую анимированную 2D-комнату блогера (прокачивается с уровнем оборудования и наймом команды) */
void draw_creator_studio_room(
    ui::Renderer2D& r,
    float x, float y, float w, float h,
    float anim_time_sec,
    std::uint8_t equipment_tier,
    std::size_t team_count,
    float creator_energy,
    bool is_live_airing);

/** Рисует визуальное превью ролика 16:9 + карточку в мобильной ленте */
void draw_thumbnail_canvas(
    ui::Renderer2D& r,
    float x, float y, float w,
    const ThumbnailDesign& design,
    std::string_view video_title,
    std::string_view channel_name,
    char variant_letter,
    float predicted_ctr);

} // namespace simul::game
