#define GEIST_REPORT_INTEGRATION_TEST
#include "../launcher/src/bug_report.cpp"
#include <iostream>
struct BugReportIntegrationTest {
    static int run(const char* root,const char* png,const char* mp4){
        BugReportForm form;form.open(root,"1.1.1-integration-test");
        form.category_=9;
        form.fields_[0]="AUTOMATED GEIST v1.1.1 INTEGRATION TEST - no player bug. Verifies the real launcher upload path and both evidence files.";
        form.fields_[1]="Release verification on the development PC.";
        form.fields_[2]="Submit this marked test through the native launcher code, public HTTPS proxy and protected report service.";
        form.fields_[3]="Discord receives the complete report, PNG screenshot and playable MP4; client confirms a matching unique report ID.";
        form.fields_[4]="This message is the live integration-test delivery.";
        for(const char* path:{png,mp4})form.files_.push_back({path,fs::u8path(path).filename().u8string(),(size_t)fs::file_size(fs::u8path(path))});
        form.submit();
        if(!form.pending_.valid()){std::cerr<<form.status_<<"\n";return 1;}
        auto result=form.pending_.get();
        std::cout<<(result.success?"PASS":"FAIL")<<": "<<result.message<<" report_id="<<result.id<<"\n";
        return result.success?0:1;
    }
};
int main(int argc,char** argv){if(argc!=4)return 2;return BugReportIntegrationTest::run(argv[1],argv[2],argv[3]);}
