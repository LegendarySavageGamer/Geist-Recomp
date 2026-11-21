#pragma once
#include "ui.h"
#include <array>
#include <future>
#include <string>
#include <vector>

class BugReportForm {
#ifdef GEIST_REPORT_INTEGRATION_TEST
    friend struct BugReportIntegrationTest;
#endif
public:
    void open(const std::string& root, const std::string& version);
    bool event(const SDL_Event& e);
    // True when Back was selected. Editing and failed submissions preserve the draft.
    bool draw(SDL_Renderer* renderer, const Font& heading, const Font& body, const Font& small,
              float mx, float my, bool click, int nav, bool enter, bool back);
private:
    struct Attachment { std::string path, name; size_t size = 0; };
    struct Result { bool success = false; std::string message, id; };
    std::string root_, version_, status_, report_id_;
    std::array<std::string, 9> fields_;
    std::vector<Attachment> files_;
    std::future<Result> pending_;
    int category_ = 0, page_ = 0, focus_ = 0, editing_ = -1;
    bool submitted_ = false;
    void attach();
    void submit();
    void end_edit();
};
