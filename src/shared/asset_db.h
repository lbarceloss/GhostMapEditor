#pragma once
#include <string>
#include <vector>
#include <unordered_map>

class AssetDB {
public:
    void AddRoot(const std::string& dir);
    void Clear();
    std::string Find(const std::string& filename) const;
    std::string FindTexture(const std::string& filename) const;
    std::vector<std::string> FindAllBySuffix(const std::string& suffix) const;

    size_t FileCount() const { return byName_.size(); }
    const std::vector<std::string>& Roots() const { return roots_; }
    static std::string GuessMapRoot(const std::string& gbinPath);

private:
    std::unordered_map<std::string, std::string> byName_;    std::unordered_map<std::string, std::string> byStem_;    std::vector<std::string> roots_;
};
std::vector<std::string> ListFiles(const std::string& dir, const std::string& suffix);

std::string ToLower(std::string s);
std::string PathFileName(const std::string& path);
std::string PathStem(const std::string& path);
std::string PathDir(const std::string& path);
