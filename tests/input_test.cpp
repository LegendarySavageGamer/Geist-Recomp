// Tests GeistNative's controls.txt against the real input_map code, with SDL's dummy video
// driver, injected mouse events and an SDL virtual game controller.
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include "input_map.h"

bool pad_motor(int) { return false; }
void pad_set_state(int, const PadState&) {}

static int g_fail;
#define CHECK(c, msg) do { if (c) printf("PASS  %s\n", msg); else { printf("FAIL  %s\n", msg); g_fail++; } } while (0)

static void pump() {
    SDL_PumpEvents();
    SDL_Event e;
    while (SDL_PollEvent(&e)) input_map_event(e);
}
static PadState poll() { PadState p; input_map_apply(p); return p; }

int main(int argc, char** argv) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) { printf("SDL_Init: %s\n", SDL_GetError()); return 2; }
    SDL_Window* w = SDL_CreateWindow("t", 0, 0, 64, 64, 0);
    (void)w;
    bool ok = input_map_load(argv[1]);
    CHECK(ok, "controls.txt parses with no errors");
    CHECK(!input_map_defaults_on(), "defaults = off honoured");
    CHECK(input_map_quit_scancode() == 0, "quit_key = none honoured");

    PadState p = poll();
    CHECK(p.buttons == 0 && p.stick_x == 0x80 && p.cstick_x == 0x80, "idle: nothing pressed, sticks centred");

    // ---- mouse ----
    SDL_Event e{};
    e.type = SDL_MOUSEBUTTONDOWN; e.button.button = SDL_BUTTON_LEFT; e.button.state = SDL_PRESSED;
    input_map_event(e);  // click captures the pointer
    e = {}; e.type = SDL_MOUSEMOTION; e.motion.xrel = 25; e.motion.yrel = -50;
    input_map_event(e);
    p = poll();
    printf("      mouse +25,-50 px -> cstick_x=%d cstick_y=%d\n", p.cstick_x, p.cstick_y);
    CHECK(p.cstick_x > 0x80 + 40 && p.cstick_x < 0x80 + 60, "mouse right 25px -> C-stick about half right");
    CHECK(p.cstick_y >= 0x80 + 99, "mouse up 50px -> C-stick fully up (GameCube up = +)");
    p = poll();
    CHECK(p.cstick_x == 0x80 && p.cstick_y == 0x80, "mouse stops -> C-stick recentres next poll");
    e = {}; e.type = SDL_MOUSEWHEEL; e.wheel.y = 1;
    input_map_event(e);
    p = poll();
    CHECK(p.buttons & PAD_UP, "mouse wheel up -> D-pad up for one poll");
    p = poll();
    CHECK(!(p.buttons & PAD_UP), "wheel press released next poll");
    e = {}; e.type = SDL_KEYDOWN; e.key.keysym.scancode = SDL_SCANCODE_F1;
    input_map_event(e);  // F1 releases capture
    e = {}; e.type = SDL_MOUSEMOTION; e.motion.xrel = 40;
    input_map_event(e);
    p = poll();
    CHECK(p.cstick_x == 0x80, "after F1 (released) mouse movement no longer aims");

    // ---- virtual controller (standard Xbox-style layout, as SDL presents every pad) ----
    SDL_VirtualJoystickDesc d{};
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.name = "Virtual DualSense";
    int idx = SDL_JoystickAttachVirtualEx(&d);
    CHECK(idx >= 0, "virtual controller attached");
    pump();
    CHECK(input_map_pad_count() == 1, "controller detected through hot-plug event");
    SDL_Joystick* j = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(idx));
    if (!j) j = SDL_JoystickOpen(idx);

    auto setb = [&](int b, int v) { SDL_JoystickSetVirtualButton(j, b, v); SDL_JoystickUpdate(); };
    auto seta = [&](int a, int v) { SDL_JoystickSetVirtualAxis(j, a, v); SDL_JoystickUpdate(); };

    setb(SDL_CONTROLLER_BUTTON_A, 1); p = poll(); setb(SDL_CONTROLLER_BUTTON_A, 0);
    CHECK(p.buttons == PAD_A, "pad a (bottom: Xbox A / Cross / Switch B) -> GameCube A");
    setb(SDL_CONTROLLER_BUTTON_X, 1); p = poll(); setb(SDL_CONTROLLER_BUTTON_X, 0);
    CHECK(p.buttons == PAD_B, "pad x (left) -> GameCube B");
    setb(SDL_CONTROLLER_BUTTON_B, 1); p = poll(); setb(SDL_CONTROLLER_BUTTON_B, 0);
    CHECK(p.buttons == PAD_X, "pad b (right) -> GameCube X");
    setb(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1); p = poll(); setb(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
    CHECK(p.buttons == PAD_Z, "right bumper -> Z");
    setb(SDL_CONTROLLER_BUTTON_START, 1); p = poll(); setb(SDL_CONTROLLER_BUTTON_START, 0);
    CHECK(p.buttons == PAD_START, "options/menu -> Start");
    setb(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 1); p = poll(); setb(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0);
    CHECK(p.buttons == PAD_LEFT, "d-pad left -> d-pad left");

    // A virtual trigger's raw axis runs -32768 (released) .. 32767 (full); SDL rescales it.
    seta(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768); seta(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    p = poll();
    CHECK(p.trig_l == 0 && p.trig_r == 0 && !(p.buttons & (PAD_L | PAD_R)), "triggers released read 0");
    seta(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767); p = poll(); seta(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    CHECK((p.buttons & PAD_R) && p.trig_r == 255, "right trigger fully -> R analog 255 + click");
    seta(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0); p = poll(); seta(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    printf("      left trigger half -> trig_l=%d\n", p.trig_l);
    CHECK(!(p.buttons & PAD_L) && p.trig_l > 100 && p.trig_l < 140, "left trigger half -> L analog ~half, no click");

    seta(SDL_CONTROLLER_AXIS_LEFTY, -32768); p = poll(); seta(SDL_CONTROLLER_AXIS_LEFTY, 0);
    CHECK(p.stick_y >= 0x80 + 99, "left stick pushed up -> main stick up");
    seta(SDL_CONTROLLER_AXIS_LEFTX, 2000); p = poll(); seta(SDL_CONTROLLER_AXIS_LEFTX, 0);
    CHECK(p.stick_x == 0x80, "small stick drift inside dead zone ignored");
    seta(SDL_CONTROLLER_AXIS_RIGHTX, -32768); p = poll(); seta(SDL_CONTROLLER_AXIS_RIGHTX, 0);
    CHECK(p.cstick_x <= 0x80 - 99, "right stick left -> C-stick left");

    // second controller at the same time
    d.name = "Virtual Xbox";
    int idx2 = SDL_JoystickAttachVirtualEx(&d);
    pump();
    CHECK(input_map_pad_count() == 2, "second controller detected");
    SDL_Joystick* j2 = SDL_JoystickOpen(idx2);
    SDL_JoystickSetVirtualButton(j2, SDL_CONTROLLER_BUTTON_Y, 1); SDL_JoystickUpdate();
    p = poll();
    CHECK(p.buttons == PAD_Y, "button on the second controller works too");

    SDL_JoystickDetachVirtual(idx);
    pump();
    CHECK(input_map_pad_count() == 1, "unplugging a controller is handled");

    printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}

// ffb.cpp listens for the game's motion telemetry; not used here.
#include "telemetry.h"
namespace telemetry { void set_listener(void (*)(const Motion&)) {} }
