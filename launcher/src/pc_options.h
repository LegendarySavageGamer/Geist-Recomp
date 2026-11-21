#pragma once
#include "../../gcn-recomp/runtime/controls_editor.h"
// The launcher edits the same graphics/audio keys used by F1. The game applies
// these at startup; controls have their own live-reload path.
struct PcOptions {
    struct Option {std::string label,key;std::vector<std::string> values;int selected=0;};
    controls::Editor file;
    std::vector<Option> options;std::vector<int> defaults;
    bool audio=false;int focus=0;std::string status;
    void open(const std::string& path,bool is_audio){
        audio=is_audio;file.open(path,"");options.clear();focus=0;status="Changes apply the next time you start the game.";
        if(audio) {
            std::vector<std::string> volume;for(int i=0;i<=100;i+=5)volume.push_back(std::to_string(i));
            options={{"Master volume","master_volume",volume,20},{"Music and voices (shared)","music_volume",volume,20},{"Sound effects","effects_volume",volume,20}};
            if(!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO))SDL_InitSubSystem(SDL_INIT_AUDIO);
            std::vector<std::string> devices{"default"};for(int i=0;i<SDL_GetNumAudioDevices(0);++i)if(const char* n=SDL_GetAudioDeviceName(i,0))devices.push_back(n);
            options.push_back({"Audio output","audio_device",devices});options.push_back({"Audio buffer","audio_buffer_ms",{"20","40"}});
        }else options={
            {"Display mode","display_mode",{"windowed","borderless","fullscreen"}},
            {"Aspect ratio","aspect",{"4:3","16:9","16:10","21:9","stretch"},1},
            {"Window resolution","window_size",{"640x480","1280x720","1280x960","1600x900","1920x1080","2560x1440","3840x2160"},1},
            {"Fullscreen resolution","fullscreen_resolution",{"0x0@0","1280x720@0","1600x900@0","1920x1080@0","2560x1440@0","3840x2160@0"}},
            {"Internal resolution","internal_resolution",{"auto","1","2","3","4","5","6","7","8"}},
            {"Scaling filter","filter",{"smooth","sharp"}},
            {"VSync","vsync",{"off","on"}},
            {"Show FPS","show_fps",{"off","on"}},
            {"Frame limit","frame_limit",{"0","60"}},
            {"Performance graph","performance_overlay",{"off","on"}},
            {"Save shader cache","shader_cache",{"off","on"},1}};
        if(!audio){std::vector<std::string> monitors;for(int i=0;i<std::max(1,SDL_GetNumVideoDisplays());++i)monitors.push_back(std::to_string(i+1));options.insert(options.begin()+1,{"Monitor","monitor",monitors});}
        defaults.clear();for(const auto& option:options)defaults.push_back(option.selected);
        for(auto& option:options)for(const auto& line:file.lines){auto eq=line.find('=');if(eq==std::string::npos||controls::trim(line.substr(0,eq))!=option.key)continue;
            std::string v=controls::trim(line.substr(eq+1));auto i=std::find(option.values.begin(),option.values.end(),v);
            // Preserve custom resolutions, refresh rates and other valid current values.
            if(i==option.values.end()){option.values.push_back(v);option.selected=(int)option.values.size()-1;}else option.selected=(int)(i-option.values.begin());
        }
    }
    void restore(){for(size_t i=0;i<options.size();++i){options[i].selected=defaults[i];change((int)i,0);}status="Defaults restored. Apply or Save to keep them.";}
    void change(int i,int delta){auto& o=options[i];o.selected=(o.selected+delta+(int)o.values.size())%(int)o.values.size();
        for(auto& line:file.lines){auto eq=line.find('=');if(eq!=std::string::npos&&controls::trim(line.substr(0,eq))==o.key)line="";}
        file.lines.push_back(o.key+" = "+o.values[o.selected]);status="Unsaved changes. Save to keep them.";
    }
    std::string value(int i)const {const auto& o=options[i];const auto& v=o.values[o.selected];
        if(o.key=="fullscreen_resolution"&&v=="0x0@0")return "Desktop";
        if(o.key=="aspect"&&v=="stretch")return "Legacy stretch";
        if(o.key=="internal_resolution"&&v!="auto")return v+"x";
        if(o.key=="frame_limit")return v=="0"?"Native timing":"60 FPS";
        if(o.key=="audio_device"&&v=="default")return "System default";
        if(o.key=="audio_buffer_ms")return v=="20"?"Balanced (~21 ms queue)":"Stable (~40 ms queue)";
        if(o.key=="monitor"){int i=atoi(v.c_str())-1;const char* n=i>=0&&i<SDL_GetNumVideoDisplays()?SDL_GetDisplayName(i):nullptr;return n?v+": "+n:v;}
        if(audio&&o.key.find("volume")!=std::string::npos)return v+"%";return v;
    }
    bool save(){bool ok=file.save();status=ok?"Saved. Changes apply on the next game launch.":file.status;return ok;}
};
