#pragma once
#include <string>
bool DecodeImageToRGBA(const char* path, int* width, int* height, unsigned char** pixels);
void FreeDecodedImage(unsigned char* pixels);
std::string Cp949ToUtf8(const std::string& cp949);
std::string FindKoreanFont();
std::string OpenFileDialog(const char* title, const char* filter, const char* initialDir);
std::string SaveFileDialog(const char* title, const char* filter, const char* initialDir,
                           const char* defaultName);
std::string PickFolderDialog(const char* title);

// Roda um comando escondido e espera terminar. Junta stdout+stderr em `output`.
// Usado pelo Ghost Map Editor pra chamar o aplicar.py sem piscar console.
bool RunAndWait(const std::string& cmdline, const std::string& workDir,
                std::string& output, int& exitCode);
class ProcMem {
public:
    ~ProcMem();
    bool Attach(const char* exeName);
    void Detach();
    bool Attached() const { return handle_ != nullptr; }
    unsigned long Pid() const { return pid_; }
    bool ReadFloat(unsigned int addr, float& out);
private:
    void* handle_ = nullptr;
    unsigned long pid_ = 0;
};
