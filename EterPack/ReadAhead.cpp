#include "StdAfx.h"
#include "ReadAhead.h"
#include "EterPackManager.h"
#include "../EterBase/MapPerformance.h"
#include <condition_variable>
#include <thread>
#include <mutex>
#include <deque>
#include <map>
#include <set>
#include <cctype>
#include <sstream>

namespace
{
thread_local bool readAheadWorker = false;
std::string Key(const char* name)
{
    std::string key = name;
    for (auto& c : key)
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}
}

struct CReadAheadFileCache::Impl
{
    struct Request { std::string name, key; unsigned generation; };
    struct Entry { std::shared_ptr<const std::vector<BYTE>> data; DWORD used; };
    CEterPackManager& owner;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::deque<Request> requests;
    std::set<std::string> pending;
    std::map<std::string, Entry> ready;
    bool stop = false;
    bool failed = false;
    unsigned generation = 0;
    size_t bytes = 0, budget = 16u * 1024u * 1024u;
    std::map<std::string, std::vector<std::string>> manifest;

    explicit Impl(CEterPackManager& manager) : owner(manager)
    {
        MEMORYSTATUSEX memory = {};
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys >= 8ull * 1024ull * 1024ull * 1024ull)
            budget = 48u * 1024u * 1024u;
    }

    void LoadManifest()
    {
        // Optional data shipped in root. Parse only on the low-priority byte worker.
        CMappedFile file; LPCVOID data = nullptr;
        if (!owner.isExist("asset_dependencies.txt") || !owner.Get(file, "asset_dependencies.txt", &data) ||
            file.Size() > 16u * 1024u * 1024u) return;
        std::istringstream input(std::string(static_cast<const char*>(data), file.Size()));
        std::string line;
        if (!std::getline(input, line) || line != "ASSET_DEPS_V1") return;
        std::map<std::string, std::vector<std::string>> parsed;
        unsigned records = 0;
        while (std::getline(input, line) && records++ < 65536)
        {
            const auto split = line.find('\t');
            if (split == std::string::npos || line.size() > 2048) continue;
            const auto model = Key(line.substr(0, split).c_str());
            const auto texture = Key(line.substr(split + 1).c_str());
            if (model.empty() || texture.empty() || model.find("..") != std::string::npos || texture.find("..") != std::string::npos) continue;
            auto& dependencies = parsed[model];
            if (dependencies.size() < 128) dependencies.push_back(texture);
        }
        std::lock_guard<std::mutex> lock(mutex);
        manifest.swap(parsed);
    }

    std::vector<std::string> Discover(const Request& request, const std::vector<BYTE>& contents)
    {
        std::vector<std::string> files;
        const auto found = manifest.find(request.key);
        if (found != manifest.end()) return found->second;
        const auto dot = request.key.rfind('.');
        if (dot == std::string::npos || contents.size() > 512u * 1024u) return files;
        const auto extension = request.key.substr(dot);
        if (extension != ".msm" && extension != ".msa" && extension != ".mse" && extension != ".txt") return files;
        const auto slash = request.key.rfind('/');
        std::string directory = slash == std::string::npos ? "" : request.key.substr(0, slash + 1);
        auto resolve = [&](const std::string& file) {
            if (file.empty()) return;
            const auto normalized = Key(file.c_str());
            if (normalized.find(':') != std::string::npos || normalized.rfind("sound/", 0) == 0 || normalized.rfind("season", 0) == 0)
                files.push_back(normalized);
            else files.push_back(directory + normalized);
        };
        std::istringstream input(std::string(contents.begin(), contents.end()));
        std::string line;
        while (std::getline(input, line) && files.size() < 96)
        {
            if (extension == ".txt")
            {
                // Motion lists are plain data; area/property records are ignored.
                std::istringstream record(line);
                std::string mode, type, file;
                if (record >> mode >> type >> file)
                {
                    const auto key = Key(file.c_str());
                    if (key.size() >= 4 && key.substr(key.size() - 4) == ".msa") resolve(file);
                }
                continue;
            }
            const auto first = line.find('"'), last = line.find('"', first == std::string::npos ? 0 : first + 1);
            if (first == std::string::npos || last == std::string::npos) continue;
            const auto value = line.substr(first + 1, last - first - 1);
            const auto head = Key(line.substr(0, first).c_str());
            if (head.find("pathname") != std::string::npos && !value.empty())
            {
                directory = Key(value.c_str());
                if (directory.back() != '/') directory += '/';
            }
            const auto suffix = value.rfind('.');
            if (suffix == std::string::npos) continue;
            const auto ext = Key(value.substr(suffix).c_str());
            if (ext == ".gr2" || ext == ".dds" || ext == ".tga" || ext == ".png" || ext == ".jpg" || ext == ".spt" || ext == ".txt") resolve(value);
        }
        if (extension == ".msa" && request.key.size() > 13)
            files.push_back("sound/" + request.key.substr(13, request.key.size() - 13 - 4) + ".mss");
        return files;
    }

    void Run()
    {
        readAheadWorker = true;
        if (!SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN))
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        LoadManifest();
        for (;;)
        {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stop || !requests.empty(); });
                if (stop) return;
                request = std::move(requests.front());
                requests.pop_front();
            }
            std::shared_ptr<std::vector<BYTE>> bytesOwned;
            try
            {
                CMappedFile file;
                LPCVOID data = nullptr;
                if (owner.isExist(request.name.c_str()) && owner.Get(file, request.name.c_str(), &data) && file.Size() <= budget / 2)
                {
                    bytesOwned = std::make_shared<std::vector<BYTE>>(file.Size());
                    if (!bytesOwned->empty()) memcpy(bytesOwned->data(), data, bytesOwned->size());
                }
            }
            catch (const std::bad_alloc&) { bytesOwned.reset(); }
            const auto dependencies = bytesOwned ? Discover(request, *bytesOwned) : std::vector<std::string>{};
            std::lock_guard<std::mutex> lock(mutex);
            if (request.generation != generation) continue;
            pending.erase(request.key);
            const size_t size = bytesOwned ? bytesOwned->size() : 0;
            while (!ready.empty() && (bytes + size > budget || ready.size() >= 384))
            {
                auto victim = ready.begin();
                const DWORD now = GetTickCount();
                for (auto it = ready.begin(); it != ready.end(); ++it)
                    if (now - it->second.used > now - victim->second.used) victim = it;
                if (victim->second.data) bytes -= victim->second.data->size();
                ready.erase(victim);
            }
            ready.emplace(request.key, Entry{bytesOwned, GetTickCount()});
            bytes += size;
            for (const auto& name : dependencies)
            {
                const auto key = Key(name.c_str());
                if (pending.size() >= 256) break;
                if (ready.find(key) == ready.end() && pending.insert(key).second)
                    requests.push_back({name, key, generation});
            }
        }
    }
};

CReadAheadFileCache::CReadAheadFileCache(CEterPackManager& owner) : m_impl(new Impl(owner)) {}

CReadAheadFileCache::~CReadAheadFileCache()
{
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->stop = true;
    }
    m_impl->wake.notify_all();
    if (m_impl->worker.joinable()) m_impl->worker.join();
}

bool CReadAheadFileCache::IsWorker() { return readAheadWorker; }

void CReadAheadFileCache::Request(const std::vector<std::string>& files)
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->failed) return;
    for (const auto& name : files)
    {
        if (m_impl->pending.size() >= 256) break;
        const auto key = Key(name.c_str());
        const auto found = m_impl->ready.find(key);
        if (found != m_impl->ready.end()) { found->second.used = GetTickCount(); continue; }
        if (!m_impl->pending.insert(key).second) continue;
        m_impl->requests.push_back({name, key, m_impl->generation});
    }
    if (!m_impl->worker.joinable() && !m_impl->requests.empty())
    {
        try { m_impl->worker = std::thread([this] {
            try { m_impl->Run(); }
            catch (const std::exception&) {
                std::lock_guard<std::mutex> lock(m_impl->mutex);
                m_impl->failed = true; m_impl->pending.clear(); m_impl->requests.clear();
            }
        }); }
        catch (const std::system_error&) { m_impl->requests.clear(); m_impl->pending.clear(); }
    }
    m_impl->wake.notify_one();
}

bool CReadAheadFileCache::TryGet(CMappedFile& file, const char* name, LPCVOID* data)
{
    if (IsWorker()) return false;
    std::unique_lock<std::mutex> lock(m_impl->mutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    const auto found = m_impl->ready.find(Key(name));
    if (found == m_impl->ready.end() || !found->second.data) return false;
    found->second.used = GetTickCount();
    // The mapped-file handle retains ownership after cache eviction/map changes.
    file.LinkOwned(found->second.data);
    *data = found->second.data->data();
    MapPerformance::Count(MapPerformance::Counter::ReadAheadHits);
    return true;
}

bool CReadAheadFileCache::Ready(const char* name)
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto key = Key(name);
    if (m_impl->ready.find(key) == m_impl->ready.end()) return false;
    const auto manifest = m_impl->manifest.find(key);
    bool complete = true;
    if (manifest != m_impl->manifest.end())
        for (const auto& dependency : manifest->second)
            if (m_impl->ready.find(dependency) == m_impl->ready.end())
            {
                complete = false;
                if (m_impl->pending.size() < 256 && m_impl->pending.insert(dependency).second)
                    m_impl->requests.push_back({dependency, dependency, m_impl->generation});
            }
    if (!complete) m_impl->wake.notify_one();
    return complete;
}

void CReadAheadFileCache::Clear()
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    ++m_impl->generation;
    m_impl->requests.clear();
    m_impl->pending.clear();
    m_impl->ready.clear();
    m_impl->bytes = 0;
}
