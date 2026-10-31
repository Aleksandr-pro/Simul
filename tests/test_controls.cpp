// РАЗЛОМ — тесты сенсорного управления. Без GPU и без телефона: проверяем
// владение пальцами, дедзону стика и страховку от «залипших» касаний.
#include <vector>

#include "razlom/client/controls.hpp"
#include "test_util.hpp"

using razlom::client::ControlRect;
using razlom::client::kNoTouch;
using razlom::client::stickReading;
using razlom::client::StickReading;
using razlom::client::TouchControls;
using razlom::client::TouchId;

namespace {

// Раскладка как на телефоне 2340×1080: стик слева, кнопки справа.
TouchControls::Layout phoneLayout() {
    TouchControls::Layout l;
    l.stick = ControlRect{60.0f, 760.0f, 280.0f, 280.0f};
    l.breakBlock = ControlRect{1800.0f, 500.0f, 120.0f, 120.0f};
    l.placeBlock = ControlRect{1940.0f, 500.0f, 120.0f, 120.0f};
    l.jump = ControlRect{1800.0f, 640.0f, 120.0f, 120.0f};
    l.explode = ControlRect{1940.0f, 640.0f, 120.0f, 120.0f};
    l.fly = ControlRect{1940.0f, 780.0f, 120.0f, 120.0f};
    l.attack = ControlRect{1800.0f, 780.0f, 120.0f, 120.0f};
    for (int i = 0; i < 9; ++i) l.hotbar[i] = ControlRect{700.0f + 80.0f * i, 980.0f, 70.0f, 70.0f};
    l.stickRadius = 100.0f;
    l.lookMinX = 820.0f;
    return l;
}

TouchControls makeControls() {
    TouchControls c;
    c.setLayout(phoneLayout());
    return c;
}

}  // namespace

// Деадзона: мелкое дрожание пальца не двигает игрока, за порогом скорость растёт плавно.
TEST(stick_deadzone_and_ramp) {
    const StickReading zero = stickReading(0.05f, 0.05f);
    CHECK(zero.mag == 0.0f);
    CHECK(stickReading(0.0f, 0.0f).mag == 0.0f);

    const StickReading tiny = stickReading(0.2f, 0.0f);
    CHECK(tiny.mag > 0.0f);
    CHECK(tiny.mag < 0.1f);

    const StickReading half = stickReading(0.6f, 0.0f);
    const StickReading full = stickReading(1.0f, 0.0f);
    CHECK(half.mag > tiny.mag);
    CHECK(full.mag > half.mag);
    CHECK_NEAR(full.mag, 1.0f, 1e-4);
    CHECK_NEAR(full.dx, 1.0f, 1e-4);

    // за пределами радиуса не разгоняемся дальше максимума
    const StickReading beyond = stickReading(3.0f, 0.0f);
    CHECK(beyond.mag <= 1.0f + 1e-4f);
}

// Палец с идентификатором 0 — обычное дело на Android, он не должен означать «нет касания».
TEST(touch_id_zero_is_a_real_finger) {
    TouchControls c = makeControls();
    c.begin(0, 200.0f, 900.0f);  // палец 0 на стике
    CHECK(c.stickActive());
    c.move(0, 200.0f, 800.0f);   // вверх на 100 px = полный вперёд
    CHECK_NEAR(c.stick().dy, 1.0f, 0.02);
    c.end(0);
    CHECK(!c.stickActive());
    CHECK_EQ(c.stick().mag, 0.0f);
}

// Второй палец не отбирает стик и обзор, а после поднятия первого обзор жив.
TEST(two_fingers_keep_their_roles) {
    TouchControls c = makeControls();
    c.begin(0, 200.0f, 900.0f);       // стик
    c.begin(1, 1500.0f, 400.0f);      // обзор
    c.move(1, 1600.0f, 430.0f);
    float dx = 0.0f, dy = 0.0f;
    CHECK(c.takeLook(dx, dy));
    CHECK_NEAR(dx, 100.0f, 1e-3);
    CHECK_NEAR(dy, 30.0f, 1e-3);

    // третий палец на правой половине не перехватывает обзор
    c.begin(2, 1900.0f, 200.0f);
    c.move(1, 1650.0f, 430.0f);
    CHECK(c.takeLook(dx, dy));
    CHECK_NEAR(dx, 50.0f, 1e-3);
    CHECK_NEAR(dy, 0.0f, 1e-3);

    // первый палец (стик) ушёл — обзор продолжает работать
    c.end(0);
    c.move(1, 1660.0f, 430.0f);
    CHECK(c.takeLook(dx, dy));
    CHECK_NEAR(dx, 10.0f, 1e-3);
}

// Главная страховка: событие об отпускании потерялось (системный жест, свёрнутое
// приложение) — стик обязан вернуться в ноль, иначе игрок улетает из мира.
TEST(stuck_stick_resets_when_finger_is_gone) {
    TouchControls c = makeControls();
    c.begin(7, 200.0f, 900.0f);
    c.move(7, 200.0f, 700.0f);
    CHECK(c.stick().mag > 0.5f);

    c.syncDown({});  // пальцев на экране нет, а end() не пришёл
    CHECK(!c.stickActive());
    CHECK_EQ(c.stick().mag, 0.0f);
    CHECK(!c.breakHeld());
    CHECK(!c.placeHeld());
    CHECK(!c.jumpHeld());
}

// Живой палец страховкой не сбрасывается.
TEST(watchdog_keeps_live_finger) {
    TouchControls c = makeControls();
    c.begin(3, 200.0f, 900.0f);
    c.move(3, 200.0f, 800.0f);
    const StickReading before = c.stick();
    c.syncDown({3});
    CHECK(c.stickActive());
    CHECK_NEAR(c.stick().mag, before.mag, 1e-4);
}

// Кнопка ЛОМ держится, пока палец на ней, и отпускается вместе с пальцем.
TEST(break_button_holds_until_release) {
    TouchControls c = makeControls();
    const TouchControls::Events ev = c.begin(4, 1850.0f, 550.0f);
    CHECK(ev.slot == -1);  // слот хотбара не выбран
    CHECK(!ev.explode);
    CHECK(c.breakHeld());
    c.syncDown({4});
    CHECK(c.breakHeld());
    c.end(4);
    CHECK(!c.breakHeld());
}

// Кнопку действия держит один палец: второй не перехватывает роль, а отпускание
// первого освобождает её для следующего касания.
TEST(action_button_ownership) {
    TouchControls c = makeControls();
    c.begin(5, 1850.0f, 550.0f);  // ЛОМ
    CHECK(c.breakHeld());
    // второй палец на СТАВ игнорируется: действие уже занято
    c.begin(6, 1990.0f, 550.0f);
    CHECK(!c.placeHeld());
    c.end(5);
    CHECK(!c.breakHeld());
    c.begin(6, 1990.0f, 550.0f);  // теперь можно взять СТАВ
    CHECK(c.placeHeld());
    c.end(6);
    CHECK(!c.placeHeld());
}

// Кнопки-действия срабатывают один раз в момент касания.
TEST(instant_buttons_fire_once) {
    TouchControls c = makeControls();
    TouchControls::Events ev = c.begin(1, 2000.0f, 700.0f);  // ТНТ
    CHECK(ev.explode);
    ev = c.begin(2, 2000.0f, 840.0f);  // ПОЛЁТ
    CHECK(ev.fly);
    ev = c.begin(3, 1850.0f, 840.0f);  // УДАР
    CHECK(ev.attack);
    ev = c.begin(4, 735.0f, 1010.0f);  // 3-й слот хотбара (индекс 0 у 700)
    CHECK(ev.slot >= 0);
    CHECK(ev.slot <= 8);
}

// Прыжок: удержание даёт автопрыжок при приземлении (jumpHeld), тап — отдельный флаг.
TEST(jump_hold_and_tap) {
    TouchControls c = makeControls();
    const TouchControls::Events ev = c.begin(9, 1850.0f, 700.0f);
    CHECK(ev.jumpTap);
    CHECK(c.jumpHeld());
    c.syncDown({9});
    CHECK(c.jumpHeld());
    c.end(9);
    CHECK(!c.jumpHeld());
}

// Стик «едет» за пальцем: даже если палец ушёл далеко, ввод остаётся на максимуме,
// а после возврата к центру — снова ноль.
TEST(stick_follows_far_finger) {
    TouchControls c = makeControls();
    c.begin(1, 200.0f, 900.0f);
    c.move(1, 200.0f, 400.0f);  // 500 px вверх при радиусе 100
    CHECK_NEAR(c.stick().mag, 1.0f, 1e-3);
    CHECK_NEAR(c.stick().dy, 1.0f, 1e-3);
    c.move(1, 200.0f, 1200.0f);  // ушли далеко вниз — направление меняется
    CHECK_NEAR(c.stick().dy, -1.0f, 1e-3);
}

// Сброс по смене экрана/паузе: ничего не остаётся «нажатым».
TEST(reset_clears_everything) {
    TouchControls c = makeControls();
    c.begin(1, 200.0f, 900.0f);
    c.begin(2, 1850.0f, 550.0f);
    c.begin(3, 1850.0f, 700.0f);
    c.reset();
    CHECK(!c.stickActive());
    CHECK(!c.breakHeld());
    CHECK(!c.jumpHeld());
    CHECK_EQ(c.stick().mag, 0.0f);
    float dx = 0.0f, dy = 0.0f;
    CHECK(!c.takeLook(dx, dy));
}

// Один палец не может занять две роли.
TEST(one_finger_one_role) {
    TouchControls c = makeControls();
    c.begin(1, 200.0f, 900.0f);   // стик
    c.begin(1, 1850.0f, 550.0f);  // тот же палец «на» ЛОМ — игнорируется
    CHECK(!c.breakHeld());
    CHECK(c.stickActive());
}

// Левая половина не становится зоной обзора (иначе камера крутится при ходьбе).
TEST(look_zone_starts_at_threshold) {
    TouchControls c = makeControls();
    c.begin(1, 400.0f, 300.0f);  // левее lookMinX, вне стика
    c.move(1, 600.0f, 320.0f);
    float dx = 0.0f, dy = 0.0f;
    CHECK(!c.takeLook(dx, dy));
    CHECK(!c.stickActive());
}

int main() { return test::runAll(); }
