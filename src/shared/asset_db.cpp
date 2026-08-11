#include "asset_db.h"
#include <filesystem>
#include <algorithm>
#include <ctype.h>

namespace fs = std::filesystem;

std::string ToLower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::string PathFileName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? path : path.substr(p + 1);
}

std::string PathStem(const std::string& path) {
    std::string f = PathFileName(path);
    size_t d = f.find_last_of('.');
    return (d == std::string::npos) ? f : f.substr(0, d);
}

std::string PathDir(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? std::string(".") : path.substr(0, p);
}

static bool WantedExt(const std::string& lowerName) {
    static const char* exts[] = {
        ".pet", ".mpet", ".apet", ".bpet", ".gbin", ".sgbin", ".aibin", ".sbin",
        ".dds", ".jpg", ".jpeg", ".png", ".tga", ".bmp", ".txt", ".xml", nullptr
    };
    size_t d = lowerName.find_last_of('.');
    if (d == std::string::npos) return false;
    std::string e = lowerName.substr(d);
    for (int i = 0; exts[i]; i++) if (e == exts[i]) return true;
    return false;
}

std::vector<std::string> ListFiles(const std::string& dir, const std::string& suffix) {
    std::vector<std::string> out;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return out;
    const std::string suf = ToLower(suffix);
    for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        try {
            std::string name = ToLower(it->path().filename().string());
            if (name.size() >= suf.size() && name.compare(name.size() - suf.size(), suf.size(), suf) == 0)
                out.push_back(it->path().string());
        } catch (...) { continue; }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void AssetDB::Clear() {
    byName_.clear();
    byStem_.clear();
    roots_.clear();
}

void AssetDB::AddRoot(const std::string& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
    for (const auto& r : roots_) if (ToLower(r) == ToLower(dir)) return;
    roots_.push_back(dir);

    const size_t MAX_FILES = 400000;
    size_t seen = 0;

    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end && seen < MAX_FILES; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;

        std::string full;
        std::string name;
        try {
            full = it->path().string();
            name = it->path().filename().string();
        } catch (...) { continue; }  

        seen++;
        std::string lower = ToLower(name);
        if (!WantedExt(lower)) continue;

        auto found = byName_.find(lower);
        if (found == byName_.end()) {
            byName_[lower] = full;
        } else if (full.size() < found->second.size()) {
            found->second = full;
        }

        std::string stem = ToLower(PathStem(name));
        if (byStem_.find(stem) == byStem_.end()) byStem_[stem] = full;
    }
}

std::vector<std::string> AssetDB::FindAllBySuffix(const std::string& suffix) const {
    std::vector<std::string> out;
    const std::string suf = ToLower(suffix);
    if (suf.empty()) return out;
    for (const auto& kv : byName_) {
        const std::string& n = kv.first;    
        if (n.size() < suf.size()) continue;
        if (n.compare(n.size() - suf.size(), suf.size(), suf) == 0) out.push_back(kv.second);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string AssetDB::Find(const std::string& filename) const {
    if (filename.empty()) return std::string();
    auto it = byName_.find(ToLower(PathFileName(filename)));
    return it == byName_.end() ? std::string() : it->second;
}

std::string AssetDB::FindTexture(const std::string& filename) const {
    std::string direct = Find(filename);
    if (!direct.empty()) return direct;

    static const char* exts[] = { ".dds", ".jpg", ".png", ".tga", ".bmp", ".jpeg", nullptr };
    std::string stem = PathStem(filename);
    for (int i = 0; exts[i]; i++) {
        std::string cand = Find(stem + exts[i]);
        if (!cand.empty()) return cand;
    }
    auto it = byStem_.find(ToLower(stem));
    return it == byStem_.end() ? std::string() : it->second;
}

std::string AssetDB::GuessMapRoot(const std::string& gbinPath) {
    std::error_code ec;
    fs::path dir = fs::path(gbinPath).parent_path();
    fs::path best = dir;
    static const char* markers[] = { "ase", "texture_dds", "map", "skybox", "text", nullptr };

    for (int up = 0; up < 4; up++) {
        if (dir.empty()) break;
        int hits = 0;
        for (int i = 0; markers[i]; i++) {
            if (fs::is_directory(dir / markers[i], ec)) hits++;
        }
        if (hits >= 2) { best = dir; break; }  
        fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    try { return best.string(); } catch (...) { return PathDir(gbinPath); }
}
