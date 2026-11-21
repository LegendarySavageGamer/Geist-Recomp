#define GEIST_REPORT_INTEGRATION_TEST
#include "../launcher/src/bug_report.cpp"
#include <cassert>
#include <iostream>
struct BugReportIntegrationTest {
 static int run(const char* font_path){
  SDL_SetHint(SDL_HINT_VIDEODRIVER,"dummy");assert(SDL_Init(SDL_INIT_VIDEO)==0);
  auto* surface=SDL_CreateRGBSurfaceWithFormat(0,1280,720,32,SDL_PIXELFORMAT_RGBA32);
  auto* r=SDL_CreateSoftwareRenderer(surface);assert(r);ui::init(r);
  std::ifstream input(font_path,std::ios::binary);std::vector<unsigned char> ttf{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
  Font heading,body,small;assert(heading.load(r,ttf,30)&&body.load(r,ttf,22)&&small.load(r,ttf,18));
  BugReportForm f;f.open(".","1.1.1");
  auto click=[&](float x,float y){return f.draw(r,heading,body,small,x,y,true,0,false,false);};
  auto type=[&](const char* text){SDL_Event e{};e.type=SDL_TEXTINPUT;snprintf(e.text.text,sizeof e.text.text,"%s",text);assert(f.event(e));};
  auto tab=[&](){SDL_Event e{};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_TAB;assert(f.event(e));};
  click(600,270);assert(f.editing_==0);type("UI navigation test");tab();assert(f.fields_[0]=="UI navigation test");
  click(1100,635);assert(f.page_==1);click(600,220);assert(f.editing_==2);type("Step one");tab();
  click(1100,635);assert(f.page_==2);click(600,220);type("Draft remains after leaving");tab();
  click(1100,635);assert(f.page_==3);click(1100,635);assert(!f.pending_.valid()&&!f.status_.empty()&&!f.submitted_);
  click(590,635);assert(f.page_==2);click(590,635);assert(f.page_==1);click(590,635);assert(f.page_==0);
  assert(click(590,635));f.open(".","1.1.1");assert(f.fields_[0]=="UI navigation test"&&f.fields_[5]=="Draft remains after leaving");
  SDL_DestroyRenderer(r);SDL_FreeSurface(surface);SDL_Quit();
  std::cout<<"PASS: real report-form drawing and click navigation, SDL text entry, Tab, all four pages, Previous/Back, draft preservation and invalid-submit feedback\n";return 0;
 }
};
int main(int argc,char** argv){assert(argc==2);return BugReportIntegrationTest::run(argv[1]);}
