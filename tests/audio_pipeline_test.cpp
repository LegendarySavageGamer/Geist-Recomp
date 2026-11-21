#include "audio_output.h"
#include "dtk_reader.h"
#include "performance.h"
#include "hw/dtk.h"
#include <SDL.h>
#include <cassert>
#include <cmath>
#include <vector>
#include <thread>
#include <cstring>
#include <iostream>
extern void audio_push_dma(const int16_t*,uint32_t);
extern void audio_render(int16_t*,int);
extern bool audio_open();extern void audio_close();extern void audio_update();extern void audio_set_latency(int);extern void audio_set_device(const char*);
static uint32_t input_rate=32000;static bool music=false;static unsigned reads=0;static std::atomic<int> read_delay{3};
uint32_t ai_dma_sample_rate(){return input_rate;}
uint32_t ai_volume_left(){return 255;}uint32_t ai_volume_right(){return 255;}
bool ai_stream_playing(){return music;}uint32_t ai_stream_rate(){return 48000;}void ai_stream_advance(uint32_t){}
bool iso_read(uint64_t,void* p,uint32_t n){assert(!perf::in_audio_callback);++reads;memset(p,0,n);std::this_thread::sleep_for(std::chrono::milliseconds(read_delay.load()));return true;}
static void ring_test(){
 AudioOutputRing<1024> ring;std::array<int16_t,128> source;source.fill(10000);assert(ring.push(source.data(),64)==64);std::array<int16_t,256> out{};assert(ring.pull(out.data(),128)==64);assert(out[0]==10000&&out[254]==0);for(int i=65;i<128;++i)assert(abs(out[i*2]-out[(i-1)*2])<=160);
 ring.reset();constexpr int total=1000000;std::thread producer([&]{std::array<int16_t,128> p;for(int at=0;at<total;){for(int i=0;i<64;++i)p[i*2]=p[i*2+1]=(int16_t)(at+i);size_t n=ring.push(p.data(),std::min(64,total-at));at+=(int)n;if(!n)std::this_thread::yield();}});
 for(int at=0;at<total;){if(ring.size()<64){std::this_thread::yield();continue;}std::array<int16_t,128> p;assert(ring.pull(p.data(),64)==64);for(int i=0;i<64;++i)assert(p[i*2]==(int16_t)(at+i)&&p[i*2+1]==p[i*2]);at+=64;}producer.join();assert(ring.size()==0);
}
static void resample(uint32_t rate){input_rate=rate;std::vector<int16_t> got;std::array<int16_t,480> data{},out{};int phase=0;for(int block=0;block<400;++block){int frames=rate/200;for(int i=0;i<frames;++i){int16_t x=(int16_t)(12000*sin(2*3.141592653589793*1000*(phase++)/rate));uint16_t be=((uint16_t)x>>8)|((uint16_t)x<<8);data[2*i+1]=(int16_t)be;int16_t half=x/2;data[2*i]=(int16_t)(((uint16_t)half>>8)|((uint16_t)half<<8));}audio_push_dma(data.data(),frames);audio_render(out.data(),240);got.insert(got.end(),out.begin(),out.end());}int crossings=0;for(size_t i=4800;i+2<got.size();i+=2){assert(abs(got[i]-2*got[i+1])<=3);if(got[i]<=0&&got[i+2]>0)++crossings;}double hz=crossings/((got.size()/2-2400)/48000.0);assert(hz>980&&hz<1020);std::cout<<"PASS: "<<rate<<" -> 48000 Hz; measured sine "<<hz<<" Hz\n";}
static bool patterned_read(uint64_t at,void* p,uint32_t n){std::this_thread::sleep_for(std::chrono::milliseconds(80));auto* b=(uint8_t*)p;for(uint32_t i=0;i<n;++i)b[i]=(uint8_t)((at+i)%251);return true;}
static void reader_test(){AsyncDtkReader reader(patterned_read);reader.prefetch(0,131072,0);reader.prefetch(262144,65536,262144,false);uint8_t b[32];bool failed=false;auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(!reader.read(0,b,32,failed)&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(!failed);for(int i=0;i<32;++i)assert(b[i]==i);while(!reader.read(32768,b,32,failed)&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));for(int i=0;i<32;++i)assert(b[i]==(32768+i)%251);std::cout<<"PASS: 80ms delayed read-ahead and next-track prefetch preserve current blocks\n";}
int main(){reader_test();ring_test();resample(32000);resample(48000);
 {std::lock_guard<std::mutex> l(g_dtk_mutex);g_dtk={};g_dtk.playing=true;g_dtk.len=1024*1024;}
 music=true;reads=0;std::vector<int16_t> mix(48000*2);audio_render(mix.data(),48000);assert(reads<=2);std::cout<<"PASS: one second of DTK used "<<reads<<" positional reads (instead of ~1715 block reads)\n";
 perf::overlay=true; _putenv("GCN_PERF_CSV=audio-unit-unused.csv");SDL_SetHint(SDL_HINT_AUDIODRIVER,"dummy");assert(SDL_Init(SDL_INIT_AUDIO)==0);read_delay=100;assert(audio_open());SDL_Delay(1200);auto first=perf::callback_count.load();assert(first>50);audio_set_latency(40);SDL_Delay(500);audio_set_device("intentional missing test output");audio_update();SDL_Delay(500);assert(perf::callback_count.load()>first);audio_close();assert(perf::callback_disc_reads==0);assert(perf::callback_file_writes==0);std::cout<<"Audio callback max ms="<<perf::callback_max_ns.load()/1e6<<" output underruns="<<perf::output_underruns.load()<<" music wait buffers="<<perf::music_wait_buffers.load()<<"\n";auto stopped=perf::callback_count.load();SDL_Delay(50);assert(perf::callback_count==stopped);SDL_Quit();
 std::cout<<"PASS: concurrent SPSC million-frame order, bounded underflow fade, no callback disc access, real SDL callback consumption, latency change, missing-device fallback and clean stop\n";
}

