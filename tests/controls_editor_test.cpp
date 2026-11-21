#include "controls_editor.h"
#include "controls_defaults.h"
#include "../launcher/src/pc_options.h"
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    assert(argc==2);std::string p=argv[1];
    const std::string original="defaults = off\r\nstick_up = key W\r\nstick_down = key S\r\ncstick_x = mouse x gain 1.0\r\nA = pad a\r\n# preserve me\r\nZ = \"custom wheel\" button 5\r\n";
    {std::ofstream f(p,std::ios::binary);f<<original;}
    controls::Editor e;e.open(p,controls::factory);assert(e.bindings.size()==4&&!e.dirty());
    e.selected=0;e.propose("key \"S\"");assert(e.conflict&&!e.dirty());
    e.accept();assert(e.dirty());assert(e.save());assert(controls::read(p).find("# preserve me")!=std::string::npos);
    assert(controls::read(p).find("custom wheel")!=std::string::npos);
    controls::Editor f;f.open(p,controls::factory);assert(f.bindings[0].source=="key \"S\"");
    f.selected=2;f.edit_option("gain",.05f);assert(std::fabs(f.bindings[2].option("gain",0)-1.05f)<.0001f);
    f.edit_option("invert",0);assert(f.bindings[2].source.find("invert")!=std::string::npos);
    auto before=controls::read(p);f.command(controls::Editor::Cancel);assert(controls::read(p)==before);
    e.open(p,controls::factory);e.propose("key K");
    {std::ofstream out(p,std::ios::app);out<<"# externally changed\n";}
    assert(!e.save());assert(controls::read(p).find("externally changed")!=std::string::npos);
    e.open(p,controls::factory);e.command(controls::Editor::Defaults);assert(e.dirty());
    assert(e.save());assert(controls::read(p).find("custom wheel")!=std::string::npos);
    controls::Editor g;g.open(p,controls::factory);assert(g.bindings.size()>40);
    g.command(controls::Editor::Controller);assert(g.tab==1&&!g.rows().empty());
    g.selected=g.rows()[0];g.command(controls::Editor::Rebind);
    SDL_Event ev{};ev.type=SDL_CONTROLLERBUTTONDOWN;ev.cbutton.button=SDL_CONTROLLER_BUTTON_TOUCHPAD;
    assert(g.event(ev));assert(!g.capturing);assert(g.bindings[g.selected].source=="pad touchpad");
    g.command(controls::Editor::Apply);assert(g.applied&&!g.closed&&!g.dirty());
    controls::Editor launcher;launcher.open(p,controls::factory);assert(launcher.lines==g.lines);
    launcher.put_setting("mouse_smoothing",1);launcher.command(controls::Editor::Save);assert(launcher.closed);
    controls::Editor f1;f1.open(p,controls::factory);assert(f1.setting("mouse_smoothing",0)==1);
    f1.command(controls::Editor::Defaults);assert(f1.setting("mouse_native",0)==1);assert(f1.setting("mouse_smoothing",1)==0);
    f1.command(controls::Editor::Apply);assert(f1.applied&&!f1.closed);
    auto ini=p+".ini";{std::ofstream out(ini);out<<"aspect = 16:9\nwindow_size = 1920x1080\nmaster_volume = 65\ncustom = retained\n";}
    PcOptions graphics;graphics.open(ini,false);assert(graphics.value(2)=="16:9"&&graphics.value(3)=="1920x1080");
    graphics.change(2,-1);assert(graphics.save());PcOptions reopened;reopened.open(ini,false);assert(reopened.value(2)=="4:3");
    PcOptions audio;audio.open(ini,true);assert(audio.value(0)=="65%");audio.change(0,1);assert(audio.save());
    assert(controls::read(ini).find("custom = retained")!=std::string::npos);graphics.open(ini,false);graphics.restore();assert(graphics.value(2)=="16:9");assert(graphics.save());PcOptions persistent;persistent.open(ini,false);assert(persistent.value(2)=="16:9");
    std::cout<<"PASS: Apply, Save, Cancel, Restore Defaults, shared launcher/F1 persistence, graphics/audio persistence, conflicts, CRLF, atomic save, external-change protection, custom entries and controller capture\n";
}
