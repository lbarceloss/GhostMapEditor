#include "platform_win.h"
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string.h>
#include <stdlib.h>
#include <vector>

static ULONG_PTR g_gdiplusToken = 0;

static bool EnsureGdiplus() {
    if (g_gdiplusToken) return true;
    Gdiplus::GdiplusStartupInput in;
    return Gdiplus::GdiplusStartup(&g_gdiplusToken, &in, NULL) == Gdiplus::Ok;
}

bool DecodeImageToRGBA(const char* path, int* width, int* height, unsigned char** pixels) {
    if (!path || !width || !height || !pixels) return false;
    if (!EnsureGdiplus()) return false;

    int n = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
    if (n <= 0) return false;
    wchar_t* wp = (wchar_t*)malloc(sizeof(wchar_t) * n);
    if (!wp) return false;
    MultiByteToWideChar(CP_ACP, 0, path, -1, wp, n);

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(wp, FALSE);
    free(wp);
    if (!bmp) return false;
    if (bmp->GetLastStatus() != Gdiplus::Ok) { delete bmp; return false; }

    UINT w = bmp->GetWidth(), h = bmp->GetHeight();
    if (w == 0 || h == 0 || w > 16384 || h > 16384) { delete bmp; return false; }

    Gdiplus::Rect rect(0, 0, (INT)w, (INT)h);
    Gdiplus::BitmapData bd;
    if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) {
        delete bmp;
        return false;
    }
    unsigned char* out = (unsigned char*)malloc((size_t)w * h * 4);
    if (out) {
        for (UINT y = 0; y < h; y++) {
            const unsigned char* src = (const unsigned char*)bd.Scan0 + (size_t)y * bd.Stride;            unsigned char* dst = out + (size_t)y * w * 4;
            for (UINT x = 0; x < w; x++) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        }
    }
    bmp->UnlockBits(&bd);
    delete bmp;
    if (!out) return false;

    *width = (int)w;
    *height = (int)h;
    *pixels = out;
    return true;
}

void FreeDecodedImage(unsigned char* pixels) { free(pixels); }

std::string Cp949ToUtf8(const std::string& s) {
    if (s.empty()) return std::string();
    int wn = MultiByteToWideChar(949, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (wn <= 0) return s;    std::wstring w((size_t)wn, L'\0');
    MultiByteToWideChar(949, 0, s.c_str(), (int)s.size(), &w[0], wn);

    int un = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wn, NULL, 0, NULL, NULL);
    if (un <= 0) return s;
    std::string out((size_t)un, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wn, &out[0], un, NULL, NULL);
    return out;
}

std::string FindKoreanFont() {
    char dir[MAX_PATH] = { 0 };
    if (!GetWindowsDirectoryA(dir, MAX_PATH)) return std::string();
    static const char* candidates[] = { "malgun.ttf", "gulim.ttc", "batang.ttc", "GulimChe.ttf", NULL };
    for (int i = 0; candidates[i]; i++) {
        std::string p = std::string(dir) + "\\Fonts\\" + candidates[i];
        DWORD attr = GetFileAttributesA(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return p;
    }
    return std::string();
}

std::string OpenFileDialog(const char* title, const char* filter, const char* initialDir) {
    char file[MAX_PATH * 4] = { 0 };
    char cwd[MAX_PATH] = { 0 };
    GetCurrentDirectoryA(MAX_PATH, cwd);

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = (initialDir && *initialDir) ? initialDir : NULL;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    std::string result;
    if (GetOpenFileNameA(&ofn)) result = file;
    SetCurrentDirectoryA(cwd);
    return result;
}

std::string SaveFileDialog(const char* title, const char* filter, const char* initialDir,
                           const char* defaultName) {
    char file[MAX_PATH * 4] = { 0 };
    if (defaultName && *defaultName) strncpy(file, defaultName, sizeof(file) - 1);
    char cwd[MAX_PATH] = { 0 };
    GetCurrentDirectoryA(MAX_PATH, cwd);

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = (initialDir && *initialDir) ? initialDir : NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_OVERWRITEPROMPT;

    std::string result;
    if (GetSaveFileNameA(&ofn)) result = file;
    SetCurrentDirectoryA(cwd);
    return result;
}

bool RunAndWait(const std::string& cmdline, const std::string& workDir,
                std::string& output, int& exitCode) {
    output.clear();
    exitCode = -1;

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = NULL;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::vector<char> cmd(cmdline.begin(), cmdline.end());
    cmd.push_back(0);

    BOOL ok = CreateProcessA(NULL, cmd.data(), NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL,
                             workDir.empty() ? NULL : workDir.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        output = "nao consegui iniciar o processo";
        return false;
    }

    char buf[4096];
    DWORD n = 0;
    while (ReadFile(rd, buf, sizeof(buf) - 1, &n, NULL) && n > 0) {
        buf[n] = 0;
        output += buf;
    }
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    exitCode = (int)code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

std::string PickFolderDialog(const char* title) {
    char cwd[MAX_PATH] = { 0 };
    GetCurrentDirectoryA(MAX_PATH, cwd);
    OleInitialize(NULL);

    char path[MAX_PATH] = { 0 };
    BROWSEINFOA bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = GetActiveWindow();
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    std::string result;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        if (SHGetPathFromIDListA(pidl, path)) result = path;
        CoTaskMemFree(pidl);
    }
    SetCurrentDirectoryA(cwd);
    return result;
}

ProcMem::~ProcMem() { Detach(); }

bool ProcMem::Attach(const char* exeName) {
    Detach();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    DWORD found = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, exeName) == 0) { found = pe.th32ProcessID; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    if (!found) return false;

    pid_ = found;
    handle_ = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, found);
    if (!handle_) { pid_ = 0; return false; }
    return true;
}

void ProcMem::Detach() {
    if (handle_) CloseHandle((HANDLE)handle_);
    handle_ = nullptr;
    pid_ = 0;
}

bool ProcMem::ReadFloat(unsigned int addr, float& out) {
    if (!handle_) return false;
    SIZE_T n = 0;
    return ReadProcessMemory((HANDLE)handle_, (LPCVOID)(uintptr_t)addr, &out, 4, &n) && n == 4;
}
