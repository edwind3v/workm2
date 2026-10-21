#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <vector>

class CEterPackManager;
class CMappedFile;

// Owns decoded file bytes only. It never constructs engine/graphics resources.
class CReadAheadFileCache
{
public:
    explicit CReadAheadFileCache(CEterPackManager& owner);
    ~CReadAheadFileCache();
    void Request(const std::vector<std::string>& files);
    bool TryGet(CMappedFile& file, const char* name, LPCVOID* data);
    bool Ready(const char* name);
    void Clear();
    static bool IsWorker();
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
