#include "input_map.h"
#include "input_defaults.h"
#include "ffb.h"
#include <SDL.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstdlib>
void ffb_configure(const FfbSettings&){}
void ffb_attach(_SDL_Joystick*,int,float){}
void ffb_update(bool){}
void ffb_shutdown(){}
bool pad_motor(int){return false;}
static int serial=0;
static void send(const std::string& s){
    {std::ofstream f("backend-events.txt");f<<"# "<<++serial<<'\n'<<s<<'\n';}
    SDL_Delay(120);input_map_test_tick();SDL_Event e;while(SDL_PollEvent(&e))input_map_event(e);
    SDL_GameControllerUpdate();
}
static PadState sample(){PadState p;input_map_apply(p);return p;}
int main(){
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    _putenv("GCN_TEST_INPUT_FILE=backend-events.txt");
    assert(SDL_Init(SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS|SDL_INIT_TIMER)==0);
    {std::ofstream f("backend-controls.txt");f<<"defaults = off\nA = key K\nB = mouse x1\nX = pad y\ncstick_x = pad rightx deadzone 0.1\ncstick_x = mouse x gain 0.01\ncstick_y = mouse y gain 20\n";}
    assert(input_map_load("backend-controls.txt"));
    send("key K down");assert(sample().buttons&PAD_A);
    send("key K up\nkey J down");assert(!(sample().buttons&PAD_A));
    send("button x1 down");assert(sample().buttons&PAD_B);
    send("button x1 up");assert(!(sample().buttons&PAD_B));
    send("padbutton y down");assert(input_map_pad_count()==1);assert(sample().buttons&PAD_X);
    send("padbutton y up");assert(!(sample().buttons&PAD_X));
    float x,y;assert(input_native_mouse_take(x,y));
    send("padaxis rightx 24000");assert(sample().cstick_x>190);
    send("padaxis rightx 0");sample();
    input_native_mouse_take(x,y);send("mouse 1000 100");sample();
    assert(input_native_mouse_take(x,y));assert(std::fabs(x-.8f)<.001f&&std::fabs(y+160.f)<.001f);
    sample();input_native_mouse_take(x,y);assert(x==0&&y==0);
    input_map_release_mouse();send("mouse 20 0");input_map_release_mouse();sample();input_native_mouse_take(x,y);assert(x==0);
    input_map_shutdown();SDL_Quit();
    std::cout<<"PASS: real SDL virtual controller buttons/analog axis, keyboard and mouse-button remaps, default native look, 0.01x/20x sensitivity, no controller suppression, consume once and capture release\n";
}
