// Standalone, read-only GR2 dependency extraction using the client's Granny SDK.
// Build Win32/v143. The portable executable needs only the matching granny2.dll.
#define NOMINMAX
#include <windows.h>
#include <granny.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::string Key(std::string value)
{
    for (auto& c : value)
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

static std::string Trim(std::string value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

static bool Safe(const std::string& value)
{
    return !value.empty() && value.find("..") == std::string::npos &&
        value.find_first_of("\t\r\n") == std::string::npos && value.find('\0') == std::string::npos;
}

static std::string VirtualName(const fs::path& relative)
{
    auto name = Key(relative.generic_string());
    if (name.rfind("ymir work/", 0) == 0) name = "d:/" + name;
    else if (name.rfind("d/ymir work/", 0) == 0) name = "d:/" + name.substr(2);
    if (!Safe(name)) throw std::runtime_error("Unsupported virtual filename: " + name);
    return name;
}

static fs::path ExecutableDirectory()
{
    std::vector<wchar_t> name(32768);
    const auto count = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
    if (!count || count >= name.size()) throw std::runtime_error("Cannot locate executable directory");
    return fs::path(std::wstring(name.data(), count)).parent_path();
}

static std::vector<fs::path> ArchiveDirectories(const fs::path& pack, std::ostringstream& report)
{
    std::vector<fs::path> result;
    std::set<std::string> added;
    auto add = [&](const fs::path& relative) {
        if (relative.empty() || relative.is_absolute() || !Safe(relative.generic_string()))
            throw std::runtime_error("Invalid archive folder in Index");
        const auto directory = pack / relative;
        if (!fs::is_directory(directory))
            throw std::runtime_error("Archive is not unpacked: " + directory.string());
        if (added.insert(Key(relative.generic_string())).second) result.push_back(directory);
    };
    if (fs::exists(pack / "Index"))
    {
        std::ifstream input(pack / "Index", std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read pack/Index");
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(input, line))
        {
            line = Trim(line);
            if (!line.empty()) lines.push_back(line);
        }
        if (!lines.empty() && lines[0].rfind("\xef\xbb\xbf", 0) == 0) lines[0].erase(0, 3);
        if (lines.empty() || (lines[0] != "PACK" && lines[0] != "FILE") || (lines.size() - 1) % 2)
            throw std::runtime_error("Invalid Index: expected PACK/FILE followed by directory/archive pairs");
        for (size_t i = 1; i + 1 < lines.size(); i += 2) add(fs::path(lines[i + 1]));
        report << "Archive order: pack/Index; first archive wins duplicate virtual paths.\n";
    }
    else
    {
        std::vector<fs::path> indexes;
        for (const auto& entry : fs::directory_iterator(pack))
            if (entry.is_regular_file() && Key(entry.path().extension().string()) == ".eix")
                indexes.push_back(entry.path().stem());
        std::sort(indexes.begin(), indexes.end());
        if (indexes.empty())
            throw std::runtime_error("No pack/Index or .eix files found. Keep Index beside the unpacked folders");
        for (const auto& archive : indexes) add(archive);
        report << "WARNING: no Index; alphabetical .eix order used. Supply Index to match patch priority.\n";
    }
    add("root");
    if (result.empty()) throw std::runtime_error("No unpacked archive directories found");
    return result;
}

// Replacing a manifest never touches the previous version until a complete file
// is closed. Backups use CREATE_NEW semantics via CopyFile's fail-if-exists flag.
static void WriteAtomic(const fs::path& destination, const std::string& contents, const fs::path& backupDirectory)
{
    fs::create_directories(destination.parent_path());
    auto temporary = destination;
    temporary += L".tmp-" + std::to_wstring(GetCurrentProcessId());
    if (fs::exists(temporary)) throw std::runtime_error("Temporary output already exists: " + temporary.string());
    try
    {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output) throw std::runtime_error("Cannot write output: " + destination.string());
            output.close();
            if (!output) throw std::runtime_error("Cannot close output: " + destination.string());
        }
        if (fs::exists(destination))
        {
            fs::create_directories(backupDirectory);
            SYSTEMTIME now{}; GetLocalTime(&now);
            wchar_t suffix[96];
            swprintf_s(suffix, L".bak-%04u%02u%02u-%02u%02u%02u-%03u-%lu",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                now.wMilliseconds, GetCurrentProcessId());
            auto original = backupDirectory / destination.filename(); original += suffix;
            if (!CopyFileW(destination.c_str(), original.c_str(), TRUE))
                throw std::runtime_error("Cannot back up existing output; replacement cancelled");
        }
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace output: " + destination.string());
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

static std::set<std::string> Textures(const fs::path& model)
{
    const auto size = fs::file_size(model);
    if (!size || size > 512u * 1024u * 1024u || size > std::numeric_limits<int>::max())
        throw std::runtime_error("Invalid or excessively large GR2 file");
    std::vector<char> bytes(static_cast<size_t>(size));
    std::ifstream input(model, std::ios::binary);
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("Cannot read GR2 file");
    std::unique_ptr<granny_file, decltype(&GrannyFreeFile)> file(
        GrannyReadEntireFileFromMemory(static_cast<int>(bytes.size()), bytes.data()), &GrannyFreeFile);
    const auto* info = file ? GrannyGetFileInfo(file.get()) : nullptr;
    if (!info) throw std::runtime_error("Granny could not decode GR2 file");
    if (info->TextureCount < 0 || info->TextureCount > 65536 || (info->TextureCount && !info->Textures))
        throw std::runtime_error("Invalid GR2 texture table");
    std::set<std::string> result;
    for (int i = 0; i < info->TextureCount; ++i)
    {
        const auto* texture = info->Textures[i];
        if (texture && texture->FromFileName && *texture->FromFileName)
            result.insert(Key(texture->FromFileName));
    }
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        auto pack = ExecutableDirectory();
        fs::path output, reportFile;
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring option = argv[i];
            if (option == L"--help" || option == L"-h")
            {
                std::cout << "GenerateAssetDependencies [--pack FOLDER] [--output FILE] [--report FILE]\n"
                    "Default pack: executable folder. Default output: pack/root/asset_dependencies.txt\n"
                    "Reads unpacked GR2 files. Requires the matching Win32 granny2.dll beside the EXE.\n"
                    "Exit codes: 0=generated, 1=failed (existing manifest preserved), 2=usage error.\n";
                return 0;
            }
            if (i + 1 >= argc || (option != L"--pack" && option != L"--output" && option != L"--report"))
            {
                std::cerr << "Unknown option or missing value. Use --help.\n";
                return 2;
            }
            if (option == L"--pack") pack = fs::absolute(argv[++i]);
            else if (option == L"--output") output = fs::absolute(argv[++i]);
            else reportFile = fs::absolute(argv[++i]);
        }
        pack = fs::absolute(pack).lexically_normal();
        if (!fs::is_directory(pack)) throw std::runtime_error("Pack folder does not exist");
        if (output.empty()) output = pack / "root" / "asset_dependencies.txt";
        if (reportFile.empty()) reportFile = pack / "asset_dependencies_report.txt";
        if (Key(output.lexically_normal().generic_string()) == Key(reportFile.lexically_normal().generic_string()) ||
            Key(output.extension().string()) != ".txt" || Key(reportFile.extension().string()) != ".txt")
            throw std::runtime_error("Output and report must be different .txt files");
        SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
        std::ostringstream report;
        report << "Asset dependency generator v1; Granny 2.11.8.0; ASSET_DEPS_V1\n"
            << "Pack: " << pack.string() << "\n";
        const auto directories = ArchiveDirectories(pack, report);
        std::map<std::string, fs::path> files;
        size_t duplicateCount = 0, fileCount = 0;
        for (const auto& directory : directories)
        {
            std::cout << "Scanning " << directory.filename().string() << std::endl;
            for (const auto& entry : fs::recursive_directory_iterator(directory))
            {
                if (entry.is_symlink()) throw std::runtime_error("Symlinks are unsupported: " + entry.path().string());
                if (!entry.is_regular_file()) continue;
                const auto name = VirtualName(entry.path().lexically_relative(directory));
                ++fileCount;
                const auto [old, inserted] = files.emplace(name, entry.path());
                if (!inserted)
                {
                    ++duplicateCount;
                    report << "DUPLICATE\t" << name << "\tusing=" << old->second.string()
                        << "\tignored=" << entry.path().string() << '\n';
                }
            }
        }
        std::set<std::pair<std::string, std::string>> pairs;
        size_t modelCount = 0, missing = 0, errors = 0;
        std::set<std::string> modelsWithTextures;
        for (const auto& [name, disk] : files)
        {
            if (Key(disk.extension().string()) != ".gr2") continue;
            ++modelCount;
            try
            {
                const auto references = Textures(disk);
                size_t dependencies = 0;
                for (const auto& texture : references)
                {
                    // Match CGrannyMaterial::__GetImagePointer: relative texture
                    // filenames are resolved against the model's virtual directory.
                    const auto resolved = texture.size() > 2 && texture[1] != ':' ?
                        name.substr(0, name.rfind('/') + 1) + texture : texture;
                    if (!Safe(resolved) || !files.count(resolved))
                    {
                        ++missing;
                        report << "UNRESOLVED\t" << name << "\treference=" << texture
                            << "\tresolved=" << resolved << '\n';
                        continue;
                    }
                    if (++dependencies > 128 || name.size() + resolved.size() + 1 > 2048)
                        throw std::runtime_error("Dependency exceeds client's per-model/line limit");
                    pairs.emplace(name, resolved);
                    modelsWithTextures.insert(name);
                }
            }
            catch (const std::exception& error)
            {
                ++errors;
                report << "ERROR\t" << name << '\t' << error.what() << '\n';
                std::cerr << "Cannot inspect " << name << ": " << error.what() << std::endl;
            }
            if (modelCount % 250 == 0) std::cout << "Inspected " << modelCount << " GR2 files" << std::endl;
        }
        if (!modelCount) { ++errors; report << "ERROR\tNo GR2 files found; check extraction layout.\n"; }
        if (pairs.size() > 65536) { ++errors; report << "ERROR\tClient supports at most 65536 manifest records.\n"; }
        std::ostringstream manifest;
        manifest << "ASSET_DEPS_V1\n";
        for (const auto& [model, texture] : pairs) manifest << model << '\t' << texture << '\n';
        const auto manifestBytes = manifest.str();
        if (manifestBytes.size() > 16u * 1024u * 1024u) { ++errors; report << "ERROR\tManifest exceeds 16 MiB.\n"; }
        report << "\nFiles scanned: " << fileCount << "\nUnique virtual files: " << files.size()
            << "\nDuplicate paths: " << duplicateCount << "\nGR2 files inspected: " << modelCount
            << "\nModels with resolved textures: " << modelsWithTextures.size()
            << "\nDependency pairs: " << pairs.size() << "\nUnresolved texture references: " << missing
            << "\nErrors: " << errors << "\nManifest bytes: " << manifestBytes.size()
            << "\nStatus: " << (errors ? "FAILED; existing manifest preserved" : "Ready for manifest replacement") << '\n';
        WriteAtomic(reportFile, report.str(), reportFile.parent_path() / "asset_dependencies_backups");
        if (errors) throw std::runtime_error("Incomplete scan; manifest not replaced. See " + reportFile.string());
        const auto backupBase = Key(output.parent_path().generic_string()) == Key((pack / "root").generic_string()) ?
            pack : output.parent_path();
        WriteAtomic(output, manifestBytes, backupBase / "asset_dependencies_backups");
        std::cout << "\nGenerated: " << output.string() << "\nModels scanned: " << modelCount
            << "\nModels with textures: " << modelsWithTextures.size() << "\nDependency pairs: " << pairs.size()
            << "\nUnresolved references: " << missing << " (see report; missing assets are not guessed)"
            << "\nReport: " << reportFile.string()
            << "\nRepack root with PML and deploy its .eix/.epk pair before launching the client.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << std::endl;
        return 1;
    }
}
