// See proc.h.
#include "proc.h"
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

static std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// Quoting per the MSVC runtime's argv parsing rules (backslashes before a quote double).
static std::wstring quote(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    size_t bs = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { bs++; continue; }
        if (c == L'"') { q.append(bs * 2 + 1, L'\\'); q += L'"'; bs = 0; continue; }
        q.append(bs, L'\\');
        bs = 0;
        q += c;
    }
    q.append(bs * 2, L'\\');
    q += L'"';
    return q;
}

static std::wstring cmdline(const std::vector<std::string>& argv) {
    std::wstring c;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i) c += L' ';
        c += quote(wide(argv[i]));
    }
    return c;
}

int proc_run(const std::vector<std::string>& argv, const std::string& log, const std::string& cwd) {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE out = CreateFileW(wide(log).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out;
    si.hStdError = out;
    PROCESS_INFORMATION pi = {};
    std::wstring cl = cmdline(argv);
    std::wstring wcwd = wide(cwd);
    BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    const DWORD start_error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && out != INVALID_HANDLE_VALUE) {
        wchar_t message[1024] = {};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, start_error, 0, message, 1024, nullptr);
        const std::wstring detail = L"Could not start " + quote(wide(argv.front())) +
                                   L" (Windows error " + std::to_wstring(start_error) + L"): " +
                                   message + L"\r\n";
        const int size = WideCharToMultiByte(CP_UTF8, 0, detail.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, detail.c_str(), -1, utf8.data(), size, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size() - 1), &written, nullptr);
    }
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

bool proc_spawn_detached(const std::vector<std::string>& argv, const std::string& cwd, const std::string& log) {
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    HANDLE out = INVALID_HANDLE_VALUE;
    if (!log.empty()) {
        SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
        out = CreateFileW(wide(log).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out != INVALID_HANDLE_VALUE) {
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = si.hStdError = out;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        }
    }
    PROCESS_INFORMATION pi = {};
    std::wstring cl = cmdline(argv);
    std::wstring wcwd = wide(cwd);
    // Let the game take the foreground: Windows otherwise opens it behind this window.
    AllowSetForegroundWindow(ASFW_ANY);
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    BOOL ok = CreateProcessW(nullptr, cl.data(), nullptr, nullptr, out != INVALID_HANDLE_VALUE, 0, nullptr, wcwd.c_str(), &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

FILE* proc_fopen(const std::string& path, const char* mode) { return _wfopen(wide(path).c_str(), wide(mode).c_str()); }

void proc_open_with_shell(const std::string& path) { ShellExecuteW(nullptr, L"open", wide(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

std::string proc_exe_dir() {
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    return std::filesystem::path(std::wstring(buf, n)).parent_path().u8string();
}

#else  // POSIX: used to test the same pipeline on Linux
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
extern char** environ;

static int run(const std::vector<std::string>& argv, const std::string& log, const std::string& cwd, bool wait) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (!cwd.empty() && chdir(cwd.c_str()) != 0) _exit(127);
        if (!log.empty()) {
            int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }
        }
        std::vector<char*> a;
        for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        execvp(a[0], a.data());
        _exit(127);
    }
    if (!wait) return 0;
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int proc_run(const std::vector<std::string>& argv, const std::string& log, const std::string& cwd) { return run(argv, log, cwd, true); }
bool proc_spawn_detached(const std::vector<std::string>& argv, const std::string& cwd, const std::string& log) { return run(argv, log, cwd, false) == 0; }
FILE* proc_fopen(const std::string& path, const char* mode) { return fopen(path.c_str(), mode); }
void proc_open_with_shell(const std::string& path) { run({"xdg-open", path}, "", "", false); }
std::string proc_exe_dir() { return std::filesystem::canonical("/proc/self/exe").parent_path().string(); }
#endif
