#include "bug_report.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <winhttp.h>

namespace fs = std::filesystem;
namespace {
constexpr size_t max_evidence = 8 * 1024 * 1024;
const char* categories[] = {"Game crash", "Graphics problem", "Widescreen issue",
    "Mouse or keyboard problem", "Controller problem", "Audio problem", "Performance problem",
    "Save file problem", "Launcher issue", "Other"};
const char* labels[] = {"What happened?", "What were you doing?", "Steps to reproduce",
    "Expected behavior", "Actual behavior", "Additional comments (optional)",
    "Video sharing link (optional, HTTPS)", "Hardware information (optional)", ""};
const Color text{224, 239, 255, 255}, dim{147, 179, 208, 255}, cyan{90, 201, 255, 255};
std::wstring wide(const std::string& input) {
    int n = MultiByteToWideChar(CP_UTF8, 0, input.data(), (int)input.size(), nullptr, 0);
    std::wstring output(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, input.data(), (int)input.size(), output.data(), n);
    return output;
}
std::string json(const std::string& input) {
    std::string result = "\"";
    for (unsigned char c : input) {
        if (c == '\\' || c == '"') { result += '\\'; result += c; }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (c >= 32) result += c;
    }
    return result + "\"";
}
std::string version_at(const std::string& root, const std::string& fallback) {
    std::ifstream input(fs::u8path(root) / "game" / "build_stamp.txt");
    for (std::string line; std::getline(input, line);) {
        if (line.rfind("sdk=", 0) == 0) return line.substr(4);
    }
    return fallback;
}
std::string endpoint_at(const std::string& root) {
    std::ifstream input(fs::u8path(root) / "reporting.ini");
    for (std::string line; std::getline(input, line);) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            const auto first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) { s.clear(); return; }
            s = s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
        };
        trim(key); trim(value);
        if (key == "endpoint") return value;
    }
    return {};
}
struct InternetHandle {
    HINTERNET value;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};
std::string send_https(const std::string& endpoint, const std::string& data,
                       const std::string& content_type, DWORD& status) {
    const auto url = wide(endpoint);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwUserNameLength =
        parts.dwPasswordLength = parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwUserNameLength ||
        parts.dwPasswordLength || parts.dwExtraInfoLength ||
        (std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) != L"/v1/reports" &&
         std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) != L"/geist-reports/v1/reports"))
        throw std::runtime_error("The report service needs a valid HTTPS /v1/reports endpoint.");
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    if (host == L"discord.com" || host == L"discordapp.com")
        throw std::runtime_error("Configure the protected report service URL in reporting.ini.");
    InternetHandle session(WinHttpOpen(L"GeistRecompiled/bug-reports",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) throw std::runtime_error("Windows could not open a network session. Keep your report and retry.");
    WinHttpSetTimeouts(session.value, 10000, 10000, 30000, 60000);
    InternetHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    if (!connection.value) throw std::runtime_error("Could not connect to the report service. Keep your report and retry.");
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    InternetHandle request(WinHttpOpenRequest(connection.value, L"POST", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) throw std::runtime_error("The report upload could not start. Keep your report and retry.");
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    const auto header = L"Content-Type: " + wide(content_type);
    if (!WinHttpSendRequest(request.value, header.c_str(), (DWORD)-1, (void*)data.data(),
            (DWORD)data.size(), (DWORD)data.size(), 0) || !WinHttpReceiveResponse(request.value, nullptr))
        throw std::runtime_error("Delivery could not be confirmed. Keep your report and retry. A timed-out upload may already have reached the team.");
    DWORD length = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("The report service response could not be read.");
    std::string response;
    char buffer[4096];
    DWORD read = 0;
    do {
        if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &read))
            throw std::runtime_error("The connection ended before delivery was confirmed. Keep your report and retry.");
        response.append(buffer, read);
        if (response.size() > 65536) throw std::runtime_error("The report service returned an invalid response.");
    } while (read);
    return response;
}
bool button(SDL_Renderer* r, const Font& font, const std::string& label, float x, float y,
            float width, float mx, float my, bool selected, bool click, bool enter) {
    bool hover = mx >= x && mx <= x + width && my >= y && my <= y + 38;
    std::vector<SDL_FPoint> points{{x,y+19},{x+17,y},{x+width-17,y},{x+width,y+19},
        {x+width-17,y+38},{x+17,y+38}};
    ui::poly(r, points, {8,35,68,240});
    ui::poly_outline(r, points, selected || hover ? 2.f : 1.f,
        selected || hover ? cyan : Color{64,115,163,230});
    font.draw(r,label,x+(width-font.width(label))/2,y+25,text);
    return (hover && click) || (selected && enter);
}
std::vector<std::string> wrap(const Font& font, const std::string& text, float width) {
    std::vector<std::string> lines(1);
    for (char c : text) {
        if (c == '\n') {lines.emplace_back(); continue;}
        if (!lines.back().empty() && font.width(lines.back()+c) > width) lines.emplace_back();
        lines.back() += c;
    }
    return lines;
}
}

void BugReportForm::open(const std::string& root, const std::string& version) {
    root_ = root; version_ = version_at(root, version);
    submitted_ = false; report_id_.clear(); page_ = 0; focus_ = 0;
}
void BugReportForm::end_edit() { editing_ = -1; SDL_StopTextInput(); }
bool BugReportForm::event(const SDL_Event& e) {
    if (editing_ < 0) return false;
    std::string& field = fields_[editing_];
    if (e.type == SDL_TEXTINPUT) {
        if (field.size() + strlen(e.text.text) <= 4000) field += e.text.text;
        return true;
    }
    if (e.type != SDL_KEYDOWN) return false;
    const auto key = e.key.keysym.sym;
    if (key == SDLK_ESCAPE || key == SDLK_TAB) { end_edit(); return true; }
    if (key == SDLK_BACKSPACE && !field.empty()) {
        size_t at = field.size()-1;
        while (at && (static_cast<unsigned char>(field[at]) & 0xc0) == 0x80) --at;
        field.erase(at); return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (editing_ != 6 && field.size() < 4000) field += '\n';
        return true;
    }
    if (key == SDLK_v && (e.key.keysym.mod & KMOD_CTRL)) {
        char* clipboard = SDL_GetClipboardText();
        if (clipboard) {
            if (field.size() + strlen(clipboard) <= 4000) field += clipboard;
            SDL_free(clipboard);
        }
        return true;
    }
    return true;
}
void BugReportForm::attach() {
    wchar_t path[4096]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Screenshots and videos (*.png;*.jpg;*.jpeg;*.mp4)\0*.png;*.jpg;*.jpeg;*.mp4\0";
    dialog.lpstrTitle = L"Attach evidence to your Geist report";
    dialog.lpstrFile = path; dialog.nMaxFile = 4096;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return;
    const fs::path file(path);
    std::error_code ec;
    size_t total = fs::file_size(file, ec);
    if (ec) {status_="Could not read that file."; return;}
    for (const auto& item : files_) total += item.size;
    if (files_.size() >= 4 || total > max_evidence) {
        status_="Use at most 4 files totaling 8 MiB. Add an HTTPS sharing link for a larger video.";
        return;
    }
    std::string extension=file.extension().u8string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)tolower(c);});
    if (extension!=".png" && extension!=".jpg" && extension!=".jpeg" && extension!=".mp4") {
        status_="Choose a PNG, JPEG or MP4 file."; return;
    }
    for (const auto& item : files_) if (fs::u8path(item.path) == file) {
        status_="This file is already attached."; return;
    }
    files_.push_back({file.u8string(),file.filename().u8string(),(size_t)fs::file_size(file)});
    status_="Evidence selected. Files upload only when you submit the report.";
}
void BugReportForm::submit() {
    for (int i=0;i<5;++i) if (fields_[i].find_first_not_of(" \n\r\t") == std::string::npos) {
        status_="Complete what happened, what you were doing, steps, expected and actual behavior.";
        return;
    }
    const std::string endpoint = endpoint_at(root_);
    if (endpoint.empty()) {
        status_="The report service is awaiting configuration. Your draft is still here.";
        return;
    }
    end_edit();
    const auto fields=fields_;
    const auto files=files_;
    const auto version=version_;
    const auto category=std::string(categories[category_]);
    status_="Sending report and evidence. Please keep the launcher open.";
    pending_=std::async(std::launch::async,[=]() -> Result {
        try {
            const std::string names[]={"description","activity","steps","expected","actual","comments","evidence_link","hardware"};
            std::string payload="{\"version\":"+json(version)+",\"category\":"+json(category);
            for (int i=0;i<8;++i) payload+=","+json(names[i])+":"+json(fields[i]);
            payload+="}";
            // Fixed delimiter is checked against every part before use.
            const std::string boundary="----GeistReportEvidence7B29A4F30E";
            std::string data="--"+boundary+"\r\nContent-Disposition: form-data; name=\"report\"\r\n\r\n"+payload+"\r\n";
            if (payload.find(boundary)!=std::string::npos) throw std::runtime_error("Please remove the upload separator from the report text.");
            size_t total=0;
            for (size_t i=0;i<files.size();++i) {
                std::ifstream input(fs::u8path(files[i].path),std::ios::binary|std::ios::ate);
                const auto size=input.tellg();
                if (size<0 || (size_t)size!=files[i].size || (total+=(size_t)size)>max_evidence)
                    throw std::runtime_error("An evidence file changed. Remove it and attach it again.");
                input.seekg(0);
                std::string bytes((size_t)size,0);
                if (!input.read(bytes.data(),bytes.size())) throw std::runtime_error("Could not read an evidence file.");
                if (bytes.find(boundary)!=std::string::npos) throw std::runtime_error("An evidence file contains an unsupported upload separator.");
                std::string extension=fs::u8path(files[i].path).extension().u8string();
                std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)tolower(c);});
                const std::string name="evidence-"+std::to_string(i+1)+extension;
                data+="--"+boundary+"\r\nContent-Disposition: form-data; name=\"file\"; filename=\""+name+"\"\r\nContent-Type: application/octet-stream\r\n\r\n"+bytes+"\r\n";
            }
            data+="--"+boundary+"--\r\n";
            DWORD code=0;
            const auto response=send_https(endpoint,data,"multipart/form-data; boundary="+boundary,code);
            std::smatch match;
            const std::regex id("\"report_id\"\\s*:\\s*\"([A-F0-9]{12})\"");
            const std::regex count("\"attachments\"\\s*:\\s*"+std::to_string(files.size())+"([,}])");
            if (code==201 && std::regex_search(response,match,id) &&
                std::regex_search(response,std::regex("\"delivered\"\\s*:\\s*true")) &&
                std::regex_search(response,count))
                return {true,"Your report and "+std::to_string(files.size())+" evidence file(s) were delivered.",match[1]};
            if (code==429) return {false,"Too many reports. Please wait before retrying.",{}};
            const std::regex error("\"error\"\\s*:\\s*\"([^\"\\\\]{1,1000})\"");
            if (std::regex_search(response,match,error)) return {false,match[1],{}};
            return {false,"The report or its evidence could not be confirmed. Your draft is still here.",{}};
        } catch (const std::exception& e) {return {false,e.what(),{}};}
    });
}
bool BugReportForm::draw(SDL_Renderer* r,const Font& heading,const Font& body,const Font& small,
    float mx,float my,bool click,int nav,bool enter,bool back) {
    const bool busy=pending_.valid() && pending_.wait_for(std::chrono::seconds(0))!=std::future_status::ready;
    if (pending_.valid() && !busy) {
        const auto result=pending_.get();
        status_=result.message;
        submitted_=result.success;
        report_id_=result.id;
        if (submitted_) {fields_.fill({}); files_.clear();}
    }
    ui::rect(r,472,96,770,570,{3,15,34,245});
    ui::line(r,472,96,1242,96,2,cyan);
    heading.draw(r,submitted_ ? "BUG REPORT SUBMITTED" : "REPORT A BUG",505,143,text);
    if (submitted_) {
        body.draw(r,"Thank you for helping improve Geist Recompiled!",505,207,text);
        body.draw(r,"Your report has been sent to the development team.",505,243,text);
        body.draw(r,"Report ID: "+report_id_,505,300,cyan);
        small.draw(r,"Please save this ID for future reference.",505,337,dim);
        return button(r,body,"Back",1025,565,175,mx,my,true,click,enter)||back;
    }
    small.draw(r,"Installed version "+version_+"  |  "+std::to_string(page_+1)+" / 4",505,174,dim);
    if (busy) {
        for (auto& line:wrap(body,status_,660)) {body.draw(r,line,505,250,text);}
        return false;
    }
    const int first=page_==0?0:page_==1?2:page_==2?5:8;
    const int last=page_==0?2:page_==1?5:8;
    const int field_count=last-first;
    const int category_offset=page_==0?1:0;
    const int file_offset=field_count+category_offset;
    const int count=file_offset+(page_==3?(int)files_.size()+3:2);
    if (nav && editing_<0) focus_=(focus_+nav+count)%count;
    if (back) {end_edit();if(page_){--page_;focus_=0;return false;}return true;}
    float top=190;
    if (page_==0) {
        if(button(r,small,std::string("Category: ")+categories[category_],505,top,695,mx,my,focus_==0,click,enter))
            category_=(category_+1)%10;
        top+=54;
    }
    const float height=page_==2?51.f:page_==0?110.f:94.f;
    for(int i=first;i<last;++i) {
        const float y=top+(i-first)*(height+27);
        const bool hover=mx>=505 && mx<=1200 && my>=y+7 && my<=y+height;
        const bool selected=focus_==i-first+category_offset;
        small.draw(r,labels[i],505,y,dim);
        ui::rect(r,505,y+7,695,height-7,{7,29,53,255});
        if(selected||editing_==i) ui::line(r,505,y+7,1200,y+7,1,cyan);
        if((hover&&click)||(selected&&enter)) {
            end_edit();editing_=i;focus_=i-first+category_offset;SDL_StartTextInput();
        }
        auto lines=wrap(small,fields_[i].empty()?"Select to type...":fields_[i],675);
        const size_t visible=(size_t)((height-14)/18);
        const size_t start=editing_==i && lines.size()>visible?lines.size()-visible:0;
        for(size_t j=start;j<lines.size()&&j<start+visible;++j)
            small.draw(r,lines[j],515,y+25+(j-start)*18,fields_[i].empty()?dim:text);
    }
    if(page_==3) {
        float y=235;
        for(size_t i=0;i<files_.size();++i,y+=48) {
            std::string name=files_[i].name;
            while(small.width(name)>420 && name.size()>3) name.erase(name.size()-1);
            small.draw(r,name+"  ("+std::to_string(files_[i].size/1024)+" KiB)",505,y+18,text);
            if(button(r,small,"Remove",1100,y,100,mx,my,focus_==file_offset+(int)i,click,enter))
                {files_.erase(files_.begin()+i);focus_=0;break;}
        }
        if(button(r,small,"Attach PNG / JPG / MP4",505,465,300,mx,my,
            focus_==file_offset+(int)files_.size(),click,enter)) attach();
        small.draw(r,"Up to 4 files, 8 MiB total",824,490,dim);
    }
    float sy=page_==3?540:584;
    auto lines=wrap(small,status_.empty()?"Text fields support paste. Tab or Escape finishes editing.":status_,695);
    for(size_t j=0;j<lines.size()&&j<2;++j) small.draw(r,lines[j],505,sy+j*17,dim);
    const int action_index=page_==3?file_offset+(int)files_.size()+1:file_offset;
    const float by=620;
    if(button(r,body,page_?"Previous":"Back",505,by,175,mx,my,focus_==action_index,click,enter)) {
        end_edit();if(page_){--page_;focus_=0;}else return true;
    }
    if(button(r,body,page_==3?"Submit Report":"Next",1000,by,200,mx,my,focus_==action_index+1,click,enter)) {
        end_edit();if(page_<3){++page_;focus_=0;}else submit();
    }
    return false;
}
