#include "recomp.h"
#include "input_defaults.h"
#include "input_log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

// GITE01 rev 0: called inside the ordinary pitch update after stick acceleration,
// before its existing pitch clamps. The body Euler heading is in degrees too.
// No host thread writes guest memory, and no controller sample is amplified.
static bool g_yawlog=false;
// Mouse turn for a possessed body. The body's heading at +0x690 is put back by a block copy
// between the mouse hook and the game's turn code (80076F6C), so the hook's write is lost;
// the turn code then adds whatever the copy wiped out. If the hook's value survived (Raimi's
// own ghost), nothing is added twice.
static double g_loaded_yaw=0.0;static uint32_t g_loaded_ea=0;
extern "C" int gcn_mouse_owns_camera();
namespace gx { void render_rehud_set(float hp, float maxhp, int body); }
extern "C" void gcn_set_play_mode(int);
static float g_pending_yaw=0.f,g_written_yaw=0.f;static uint32_t g_written_ea=0;
// Test aid (sdk/tables/patches.txt): every guest store to a heading at +0x690 goes through
// here, so mouselog.txt can show which code rewrites the possessed body's heading.
extern "C" void gcn_yaw_store(CPU* c,uint32_t pc,uint32_t ea,double v){
    if(g_yawlog){static unsigned n=0;if(n++<400)fprintf(stderr,"[yawstore] pc=%08X ea=%08X v=%.4f old=%.4f\n",pc,ea,v,LDF32(ea));}
    STF32(ea,v);
}
extern "C" double gcn_yaw_load(CPU* c,uint32_t pc,uint32_t ea){
    double v=LDF32(ea);
    if(g_written_ea==ea&&g_pending_yaw!=0.f&&std::fabs(std::remainder((float)v-g_written_yaw,360.f))>0.01f)
        v=std::remainder((float)v-g_pending_yaw,360.f);
    g_pending_yaw=0.f;g_written_ea=0;
    g_loaded_yaw=v;g_loaded_ea=ea;
    if(g_yawlog){static unsigned n=0;if(n++<400)fprintf(stderr,"[yawload] pc=%08X ea=%08X v=%.4f\n",pc,ea,v);}
    return v;
}
// 80076F80: the turn code stores heading - dt * turn rate. While the mouse steers, the
// heading it loaded (with the mouse's turn) is stored instead, so the game's turn rate --
// and the aim assist in it -- cannot drag the view.
extern "C" void gcn_yaw_store_turn(CPU* c,uint32_t ea,double v){
    if(ea==g_loaded_ea&&gcn_mouse_owns_camera())v=g_loaded_yaw;
    g_loaded_ea=0;
    STF32(ea,v);
}
extern "C" void gcn_geist_mouselook(CPU* cpu,uint32_t actor,int pitch_limit) {
    if(!actor)return;
    static bool flog=false;{static unsigned m=0;if(++m%30==0){FILE* f=fopen("mouselog.txt","r");flog=f!=nullptr;g_yawlog=flog;if(f)fclose(f);}}
    // Test aid: actordump.txt (in the game folder) dumps the actor's angle-like floats once.
    {static unsigned n=0;if(++n%20==0){if(FILE* f=fopen("actordump.txt","r")){fclose(f);remove("actordump.txt");
        fprintf(stderr,"[actor] base=%08X players=%08X flags=%08X a=%u b=%u limit=%d\n",actor,LD32(cpu->r[13]+(uint32_t)-24896),LD32(actor+0x50),LD32(actor+0x1CA8),LD32(actor+0x1CB0),pitch_limit);
        for(uint32_t off=0;off<0x2800;off+=4){float v=(float)LDF32(actor+off);if(std::isfinite(v)&&std::fabs(v)>0.001f&&std::fabs(v)<100000.f)fprintf(stderr,"[actor] %04X %.4f\n",off,v);}}}}
    if(pitch_limit<=0||pitch_limit>180)return;
    const uint32_t players=LD32(cpu->r[13]+(uint32_t)-24896);
    // This is a contiguous array of 0x2800-byte player objects, not pointers.
    if(players!=actor)return;
    // Host HUD: the player object's health (+0x2730, the store the infinite-health cheat
    // patches at 8005EFAC) and maximum (+0x2734), and whether a body is possessed.
    {const uint32_t fl=LD32(actor+0x50);const float hp=(float)LDF32(actor+0x2730),mx=(float)LDF32(actor+0x2734);
     gx::render_rehud_set(std::isfinite(hp)?hp:0.f,std::isfinite(mx)?mx:0.f,(fl&0x200000)?1:0);}
    const uint32_t flags=LD32(actor+0x50);
    if((flags&2)||LD32(actor+0x1CA8)==1||LD32(actor+0x1CB0))return;
    // Test aid: headscan.txt -- first time, remember every float in MEM1 equal to the
    // current heading; next time, list those that now equal the (changed) heading.
    {static unsigned n=0;if(++n%20==0){if(FILE* f=fopen("headscan.txt","r")){fclose(f);remove("headscan.txt");
        static std::vector<uint32_t> cand;static bool have=false;const float h=(float)LDF32(actor+0x690);
        if(!have){cand.clear();for(uint32_t a=0x80000000u;a<0x81800000u;a+=4){float v=(float)LDF32(a);const float hr=h*0.0174532925f;if(std::fabs(v-h)<0.001f||std::fabs(v+h)<0.001f||std::fabs(v-hr)<0.0005f||std::fabs(v+hr)<0.0005f)cand.push_back(a);}have=true;fprintf(stderr,"[headscan] %zu candidates for %.4f\n",cand.size(),h);}
        else{for(uint32_t a:cand){float v=(float)LDF32(a);const float hr=h*0.0174532925f;if(std::fabs(v-h)<0.01f||std::fabs(std::remainder(v-h,360.f))<0.01f||std::fabs(v-hr)<0.002f||std::fabs(v+hr)<0.002f||std::fabs(std::remainder(v-hr,6.2831853f))<0.002f){fprintf(stderr,"[headscan] %08X now %.4f (actor+%X)\n",a,v,a-actor);
                    for(uint32_t o=0;o<0x2800;o+=4){uint32_t q=LD32(actor+o);if(q<=a&&a-q<0x4000)fprintf(stderr,"[headscan]   actor+%04X -> %08X (+%X)\n",o,q,a-q);}
                    const uint32_t r13=cpu->r[13];for(int32_t o=-0x8000;o<0x8000;o+=4){uint32_t q=LD32(r13+(uint32_t)o);if(q<=a&&a-q<0x4000)fprintf(stderr,"[headscan]   r13%+d -> %08X (+%X)\n",o,q,a-q);}}}have=false;}}}}
    gcn_set_play_mode((flags&0x200000)?2:1);
    float motion[2]={};bool valid=input_native_mouse_take(motion[0],motion[1]);
    // Use the existing guest-thread input record/replay channel so mouse sessions
    // remain reproducible, including changes to capture and native-look settings.
    input_pose("desktop_mouse",&valid,motion,2);
    if(flog&&!valid){static unsigned q=0;if(++q%60==0)fprintf(stderr,"[native-mouse] not valid\n");}
    if(!valid)return;
    const float dx=motion[0],dy=motion[1];
    const float old_pitch=(float)LDF32(actor+0x1F0),old_yaw=(float)LDF32(actor+0x690);
    if(!std::isfinite(old_pitch)||!std::isfinite(old_yaw))return;
    if(dy)STF32(actor+0x1F0,std::clamp(old_pitch+dy,-(float)pitch_limit,(float)pitch_limit));
    if(dx){
        float yaw=std::remainder(old_yaw-dx,360.f);STF32(actor+0x690,yaw);
        g_pending_yaw+=dx;g_written_yaw=yaw;g_written_ea=actor+0x690;
        // A possessed body keeps copies of its heading at 0x718 and 0x1580 and restores 0x690
        // from them (the stick turns all three together), so the mouse has to turn the target too or the body swings
        // straight back. Raimi's own ghost keeps no such target; leave it alone when the two
        // do not agree.
        auto turn=[&](uint32_t a){
            const float h=(float)LDF32(a);
            if(std::isfinite(h)&&std::fabs(std::remainder(h-old_yaw,360.f))<5.f)STF32(a,std::remainder(h-dx,360.f));
        };
        turn(actor+0x718);turn(actor+0x1580);
        // ...and the possessed body's controlling object (pointer at r13-19476) keeps the
        // heading the body is rebuilt from each frame, at +0x1350.
        const uint32_t ctl=LD32(cpu->r[13]+(uint32_t)-19476);
        if(ctl>=0x80000000u&&ctl<0x81800000u)turn(ctl+0x1350);
    }
    if(flog)fprintf(stderr,"[yawhook] ea=%08X wrote %.4f\n",actor+0x690,LDF32(actor+0x690));
    if((dx||dy)&&(flog||getenv("GCN_MOUSE_LOG")))fprintf(stderr,"[native-mouse] dx=%.6f dy=%.6f yaw=%.6f->%.6f pitch=%.6f->%.6f limit=%d\n",dx,dy,old_yaw,LDF32(actor+0x690),old_pitch,LDF32(actor+0x1F0),pitch_limit);
}
