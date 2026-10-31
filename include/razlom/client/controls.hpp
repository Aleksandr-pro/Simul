// РАЗЛОМ — сенсорное управление: владение пальцами, стик, обзор, кнопки.
//
// Зачем отдельный класс, а не разбор событий в App: сенсорный ввод — это
// единственное место, где состояние легко «залипает» (палец подняли, а событие
// об окончании не пришло, или системный жест съел касание). Залипший стик в
// режиме полёта уводит игрока из мира за пару секунд — и мир «пропадает».
// Здесь вся логика собрана в одном месте, её можно прогнать в тестах без
// телефона: tests/test_controls.cpp.
//
// Правила:
//  * у каждого касания ровно одна роль (стик / обзор / кнопка), роль выдаётся
//    в момент касания и держится до его конца;
//  * идентификатор касания — uintptr_t из sokol_app; 0 — валидный id, поэтому
//    «нет пальца» — это kNoTouch, а не 0;
//  * каждое событие sokol содержит ВСЕ текущие касания, поэтому раз в кадр
//    вызывается syncDown(): если владелец роли больше не на экране — роль
//    сбрасывается. Это лечит и потерянные события, и системные отмены.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace razlom::client {

using TouchId = std::uintptr_t;

inline constexpr TouchId kNoTouch = ~static_cast<TouchId>(0);

// Мёртвая зона стика и порог «бега» (стик до упора).
inline constexpr float kStickDeadzone = 0.18f;
inline constexpr float kStickSprint = 0.92f;

struct ControlRect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool contains(float px, float py) const { return px >= x && py >= y && px <= x + w && py <= y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
};

// Деадзона с рескейлом: сразу за порогом скорость маленькая и плавно растёт до
// полной — на телефоне так проще попадать в «чуть-чуть вперёд», чем с обрывом.
struct StickReading {
    float dx = 0.0f;
    float dy = 0.0f;
    float mag = 0.0f;

    bool active() const { return mag > 0.0f; }
};

inline StickReading stickReading(float dx, float dy, float deadzone = kStickDeadzone) {
    StickReading out;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len <= deadzone || len < 1e-6f) return out;
    if (len > 1.0f) len = 1.0f;  // сам вектор при этом не трогаем — только длину
    const float dz = deadzone < 0.95f ? deadzone : 0.95f;
    out.mag = (len - dz) / (1.0f - dz);
    if (out.mag > 1.0f) out.mag = 1.0f;
    const float scale = out.mag / std::sqrt(dx * dx + dy * dy);
    out.dx = dx * scale;
    out.dy = dy * scale;
    return out;
}

// «Стик едет за пальцем»: если палец ушёл дальше радиуса, центр подтягивается,
// чтобы ввод не упирался в потолок и не терял точность.
inline void followStickOrigin(float& ox, float& oy, float px, float py, float radius) {
    if (radius <= 1e-3f) return;
    const float dx = px - ox;
    const float dy = py - oy;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= radius || len < 1e-6f) return;
    ox = px - dx / len * radius;
    oy = py - dy / len * radius;
}

class TouchControls {
public:
    struct Layout {
        ControlRect stick;
        ControlRect jump;
        ControlRect breakBlock;
        ControlRect placeBlock;
        ControlRect explode;
        ControlRect fly;
        ControlRect attack;
        ControlRect hotbar[9];
        float stickRadius = 70.0f;  // радиус отклонения стика, пиксели
        float lookMinX = 0.0f;      // левее этой границы обзор не начинается
    };

    // Немедленные события одного касания (кнопки-действия).
    struct Events {
        int slot = -1;         // выбран слот хотбара
        bool explode = false;
        bool fly = false;
        bool attack = false;
        bool jumpTap = false;
    };

    void setLayout(const Layout& layout) { layout_ = layout; }

    Events begin(TouchId id, float x, float y) {
        Events ev;
        if (id == kNoTouch) return ev;
        // уже чем-то владеет — второе касание тем же пальцем игнорируем
        if (ownsAnything(id)) return ev;

        // кнопки действий: они забирают касание целиком
        for (int i = 0; i < 9; ++i) {
            if (layout_.hotbar[i].contains(x, y)) {
                ev.slot = i;
                return ev;
            }
        }
        if (layout_.explode.contains(x, y)) {
            ev.explode = true;
            return ev;
        }
        if (layout_.fly.contains(x, y)) {
            ev.fly = true;
            return ev;
        }
        if (layout_.attack.contains(x, y)) {
            ev.attack = true;
            return ev;
        }
        if (layout_.jump.contains(x, y) && jumpOwner_ == kNoTouch) {
            jumpOwner_ = id;
            jumpHeld_ = true;
            ev.jumpTap = true;
            return ev;
        }
        // ЛОМ/СТАВ держит один палец: пока он на кнопке, второй её не перехватывает.
        if (layout_.breakBlock.contains(x, y) && actionOwner_ == kNoTouch) {
            actionOwner_ = id;
            breakHeld_ = true;
            return ev;
        }
        if (layout_.placeBlock.contains(x, y) && actionOwner_ == kNoTouch) {
            actionOwner_ = id;
            placeHeld_ = true;
            return ev;
        }

        // касание по кнопке действия, занятой другим пальцем, не превращается
        // в обзор: иначе камера дёргается от «лишнего» пальца
        if (layout_.jump.contains(x, y) || layout_.breakBlock.contains(x, y) ||
            layout_.placeBlock.contains(x, y)) {
            return ev;
        }

        // стик: точка касания становится центром (палец не обязательно в центре кружка)
        if (layout_.stick.contains(x, y) && stickOwner_ == kNoTouch) {
            stickOwner_ = id;
            stickOriginX_ = x;
            stickOriginY_ = y;
            stick_ = StickReading{};
            return ev;
        }

        // всё остальное правее границы — обзор
        if (x >= layout_.lookMinX && lookOwner_ == kNoTouch) {
            lookOwner_ = id;
            lookLastX_ = x;
            lookLastY_ = y;
            return ev;
        }
        return ev;
    }

    void move(TouchId id, float x, float y) {
        if (id == kNoTouch) return;
        if (id == stickOwner_) {
            float dx = (x - stickOriginX_) / safeRadius();
            float dy = -(y - stickOriginY_) / safeRadius();
            const float len = std::sqrt(dx * dx + dy * dy);
            // подтягиваем центр, чтобы палец не «упирался» в край
            if (len > 1.0f) {
                followStickOrigin(stickOriginX_, stickOriginY_, x, y, safeRadius());
                dx = (x - stickOriginX_) / safeRadius();
                dy = -(y - stickOriginY_) / safeRadius();
            }
            stick_ = stickReading(dx, dy);
            return;
        }
        if (id == lookOwner_) {
            lookAccumX_ += x - lookLastX_;
            lookAccumY_ += y - lookLastY_;
            lookLastX_ = x;
            lookLastY_ = y;
        }
    }

    // Палец поднят (или касание отменено системой).
    void end(TouchId id) {
        if (id == kNoTouch) return;
        if (id == stickOwner_) resetStick();
        if (id == lookOwner_) resetLook();
        if (id == jumpOwner_) {
            jumpOwner_ = kNoTouch;
            jumpHeld_ = false;
        }
        if (id == actionOwner_) {
            actionOwner_ = kNoTouch;
            breakHeld_ = false;
            placeHeld_ = false;
        }
    }

    // Страховка от потерянных событий: сверяем владельцев с реально нажатыми пальцами.
    void syncDown(const std::vector<TouchId>& down) {
        auto alive = [&down](TouchId id) {
            for (TouchId d : down)
                if (d == id) return true;
            return false;
        };
        if (stickOwner_ != kNoTouch && !alive(stickOwner_)) resetStick();
        if (lookOwner_ != kNoTouch && !alive(lookOwner_)) resetLook();
        if (jumpOwner_ != kNoTouch && !alive(jumpOwner_)) {
            jumpOwner_ = kNoTouch;
            jumpHeld_ = false;
        }
        if (actionOwner_ != kNoTouch && !alive(actionOwner_)) {
            actionOwner_ = kNoTouch;
            breakHeld_ = false;
            placeHeld_ = false;
        }
    }

    // Полный сброс: смена экрана, потеря фокуса, пауза.
    void reset() {
        resetStick();
        resetLook();
        jumpOwner_ = kNoTouch;
        jumpHeld_ = false;
        actionOwner_ = kNoTouch;
        breakHeld_ = false;
        placeHeld_ = false;
        lookAccumX_ = 0.0f;
        lookAccumY_ = 0.0f;
    }

    StickReading stick() const { return stick_; }
    bool sprinting() const { return stick_.mag >= kStickSprint; }
    bool breakHeld() const { return breakHeld_; }
    bool placeHeld() const { return placeHeld_; }
    bool jumpHeld() const { return jumpHeld_; }
    bool stickActive() const { return stickOwner_ != kNoTouch; }

    // Накопленный за кадр сдвиг обзора; после чтения обнуляется.
    bool takeLook(float& dx, float& dy) {
        dx = lookAccumX_;
        dy = lookAccumY_;
        lookAccumX_ = 0.0f;
        lookAccumY_ = 0.0f;
        return dx != 0.0f || dy != 0.0f;
    }

private:
    bool ownsAnything(TouchId id) const {
        return id == stickOwner_ || id == lookOwner_ || id == jumpOwner_ || id == actionOwner_;
    }
    float safeRadius() const { return layout_.stickRadius > 1.0f ? layout_.stickRadius : 70.0f; }
    void resetStick() {
        stickOwner_ = kNoTouch;
        stick_ = StickReading{};
    }
    void resetLook() { lookOwner_ = kNoTouch; }

    Layout layout_{};
    TouchId stickOwner_ = kNoTouch;
    TouchId lookOwner_ = kNoTouch;
    TouchId jumpOwner_ = kNoTouch;
    TouchId actionOwner_ = kNoTouch;
    float stickOriginX_ = 0.0f;
    float stickOriginY_ = 0.0f;
    StickReading stick_{};
    float lookLastX_ = 0.0f;
    float lookLastY_ = 0.0f;
    float lookAccumX_ = 0.0f;
    float lookAccumY_ = 0.0f;
    bool breakHeld_ = false;
    bool placeHeld_ = false;
    bool jumpHeld_ = false;
};

}  // namespace razlom::client
