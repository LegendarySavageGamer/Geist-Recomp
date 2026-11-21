#include "recomp.h"
#include "input_defaults.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
extern "C" {
uint8_t* g_mem;
uint8_t g_gp_buf[64];uint32_t g_gp_len;
void gp_flush_line(){assert(false);}
uint32_t vm_translate(uint32_t a,int){assert(false);return a;}
uint64_t vm_load_cross(uint32_t,int){assert(false);return 0;}
void vm_store_cross(uint32_t,uint64_t,int){assert(false);}
uint32_t mmio_read32(uint32_t){assert(false);return 0;}
void mmio_write32(uint32_t,uint32_t){assert(false);}
void gcn_geist_mouselook(CPU*,uint32_t,int);
}
bool input_pose(const char*,bool*,float*,int){return false;}
int main(){
    g_mem=(uint8_t*)calloc(1,0x1800000);assert(g_mem);
    CPU c{};c.r[13]=0x804DA520;
    const uint32_t list=0x80010000,actor=0x80020000;
    ST32(c.r[13]+(uint32_t)-24896,actor);ST32(actor+0x50,0x200000);
    input_native_mouse_configure(true);gcn_geist_mouselook(&c,actor,80);assert(input_native_mouse_active());
    input_native_mouse_push(5,10);input_native_mouse_push(15,20);
    gcn_geist_mouselook(&c,actor,80);assert(std::fabs(LDF32(actor+0x690)+20)<.001&&std::fabs(LDF32(actor+0x1F0)-30)<.001);
    gcn_geist_mouselook(&c,actor,80);assert(LDF32(actor+0x690)==-20); // consume once
    input_native_mouse_push(4000,1000);gcn_geist_mouselook(&c,actor,80);
    assert(LDF32(actor+0x1F0)==80&&std::fabs(LDF32(actor+0x690))<=180); // no stick speed cap
    ST32(actor+0x1CB0,1);auto before=LDF32(actor+0x690);input_native_mouse_push(30,0);gcn_geist_mouselook(&c,actor,80);assert(LDF32(actor+0x690)==before);
    input_native_mouse_clear();ST32(actor+0x1CB0,0);input_native_mouse_configure(false);input_native_mouse_push(100,0);gcn_geist_mouselook(&c,actor,80);assert(LDF32(actor+0x690)==before);
    free(g_mem);std::cout<<"PASS: relative motion accumulates, consumed once, unrestricted yaw, pitch limits, script lock and disabled mode\n";
}
