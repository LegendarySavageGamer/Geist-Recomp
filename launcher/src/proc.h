#pragma once
#include <cstdio>
#include <string>
#include <vector>

// Runs argv[0] with the arguments, in `cwd`, appending its stdout and stderr to `log`.
// Waits; returns the exit code (or -1 if it could not start). No console window on Windows.
int proc_run(const std::vector<std::string>& argv, const std::string& log, const std::string& cwd);
// Starts a program and returns at once (the game).
bool proc_spawn_detached(const std::vector<std::string>& argv, const std::string& cwd, const std::string& log = "");
// fopen with a UTF-8 path on every platform.
FILE* proc_fopen(const std::string& path, const char* mode);
// Opens a file or folder with the system's default handler (Notepad, Explorer...).
void proc_open_with_shell(const std::string& path);
// The folder the running executable is in.
std::string proc_exe_dir();
