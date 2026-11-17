// Ask Windows' hybrid-graphics drivers (laptops with Intel/AMD integrated graphics plus an
// NVIDIA or AMD discrete GPU) to run GeistNative on the discrete GPU. The NVIDIA Optimus and
// AMD PowerXpress drivers look for these exported symbols in the .exe. Desktop PCs with a
// single GPU ignore them.
#ifdef _WIN32
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
