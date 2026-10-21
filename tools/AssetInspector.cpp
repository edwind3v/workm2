// Offline inspection uses the same Win32 Granny SDK as the game. No GPU access.
#include <windows.h>
#include <bcrypt.h>
#include <granny.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
#include <lzo/lzo1x.h>
#pragma comment(lib, "bcrypt.lib")

static std::string Quote(const char* value)
{
    std::ostringstream out;
    out << '"';
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value ? value : ""); *p; ++p)
    {
        if (*p == '"' || *p == '\\') out << '\\' << *p;
        else if (*p < 32 || *p >= 127)
        {
            const char* hex = "0123456789abcdef";
            out << "\\u00" << hex[*p >> 4] << hex[*p & 15];
        }
        else out << *p;
    }
    return out.str() + '"';
}

class Fingerprint
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> storage;
    std::map<std::pair<const void*, const void*>, unsigned> seen;
    unsigned long long work = 0;
public:
    Fingerprint()
    {
        DWORD size = 0, result = 0;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
            BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &result, 0) < 0)
            throw std::runtime_error("SHA initialization failed");
        storage.resize(size);
        if (BCryptCreateHash(algorithm, &hash, storage.data(), size, nullptr, 0, 0) < 0)
            throw std::runtime_error("SHA allocation failed");
    }
    ~Fingerprint() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void Bytes(const void* p, size_t size)
    {
        if (size > 512u * 1024u * 1024u || (size && !p)) throw std::runtime_error("Invalid data size");
        if (size && BCryptHashData(hash, const_cast<PUCHAR>(static_cast<const unsigned char*>(p)), static_cast<ULONG>(size), 0) < 0)
            throw std::runtime_error("SHA update failed");
    }
    void Number(unsigned value) { Bytes(&value, sizeof(value)); }
    void String(const char* s) { Number(s ? static_cast<unsigned>(strlen(s)) : 0); if (s) Bytes(s, strlen(s)); }
    template<class T> static T Read(const unsigned char* p) { T value; memcpy(&value, p, sizeof(value)); return value; }
    void Array(const granny_data_type_definition* type, int count, const unsigned char* data, bool references, unsigned depth)
    {
        if (count < 0 || count > 16000000 || (count && (!data || !type))) throw std::runtime_error("Invalid array");
        Number(count);
        const int stride = references ? sizeof(void*) : count ? GrannyGetTotalObjectSize(type) : 0;
        if (count && stride <= 0) throw std::runtime_error("Invalid stride");
        for (int i = 0; i < count; ++i)
            Object(type, references ? Read<const void*>(data + i * stride) : data + i * stride, depth + 1);
    }
    void Object(const granny_data_type_definition* type, const void* object, unsigned depth = 0)
    {
        if (depth > 256 || ++work > 64000000) throw std::runtime_error("Object graph limit");
        if (!object || !type) { Number(0); return; }
        auto key = std::make_pair(static_cast<const void*>(type), object);
        auto old = seen.find(key);
        if (old != seen.end()) { Number(2); Number(old->second); return; }
        Number(1); Number(static_cast<unsigned>(seen.size()));
        seen.emplace(key, static_cast<unsigned>(seen.size()));
        auto* cursor = static_cast<const unsigned char*>(object);
        for (auto* member = type; member->Type != GrannyEndMember; ++member)
        {
            const int size = GrannyGetMemberTypeSize(member);
            const int unit = GrannyGetMemberUnitSize(member);
            const int width = GrannyGetMemberArrayWidth(member);
            if (size < 0 || unit < 0 || width < 0 || size > 1024 * 1024) throw std::runtime_error("Invalid member");
            // Provenance can change with a format rewrite. All runtime data,
            // texture paths, animation curves and ExtendedData stay covered.
            const bool provenance = type == GrannyFileInfoType && member->Name &&
                (!strcmp(member->Name, "ArtToolInfo") || !strcmp(member->Name, "ExporterInfo") || !strcmp(member->Name, "FromFileName"));
            if (!provenance)
            {
                String(member->Name); Number(member->Type); Number(width);
                for (int i = 0; i < width; ++i)
                {
                    const unsigned char* field = cursor + i * unit;
                    switch (member->Type)
                    {
                    case GrannyInlineMember: Object(member->ReferenceType, field, depth + 1); break;
                    case GrannyReferenceMember: Object(member->ReferenceType, Read<const void*>(field), depth + 1); break;
                    case GrannyReferenceToArrayMember:
                    case GrannyArrayOfReferencesMember:
                        Array(member->ReferenceType, Read<int>(field), Read<const unsigned char*>(field + 4),
                            member->Type == GrannyArrayOfReferencesMember, depth); break;
                    case GrannyVariantReferenceMember:
                        Object(Read<const granny_data_type_definition*>(field), Read<const void*>(field + 4), depth + 1); break;
                    case GrannyReferenceToVariantArrayMember:
                        Array(Read<const granny_data_type_definition*>(field), Read<int>(field + 4),
                            Read<const unsigned char*>(field + 8), false, depth); break;
                    case GrannyStringMember: String(Read<const char*>(field)); break;
                    case GrannyEmptyReferenceMember: Number(0); break;
                    default:
                        if (GrannyMemberHasPointers(member)) throw std::runtime_error("Unknown pointer member");
                        Bytes(field, unit); break;
                    }
                }
            }
            cursor += size;
        }
    }
    std::string Finish()
    {
        unsigned char result[32];
        if (BCryptFinishHash(hash, result, sizeof(result), 0) < 0) throw std::runtime_error("SHA finish failed");
        std::string value; const char* hex = "0123456789abcdef";
        for (auto byte : result) { value += hex[byte >> 4]; value += hex[byte & 15]; }
        return value;
    }
};

static int Index(const char* filename)
{
    std::ifstream input(filename, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), {});
    if (data.size() < 16) return 4;
    if (!memcmp(data.data(), "MCOZ", 4))
    {
        const auto encrypted = Fingerprint::Read<unsigned>(data.data() + 4);
        const auto compressed = Fingerprint::Read<unsigned>(data.data() + 8);
        const auto real = Fingerprint::Read<unsigned>(data.data() + 12);
        if (real > 64u * 1024u * 1024u || compressed > data.size() - 20 ||
            (encrypted && (encrypted % 8 || encrypted > data.size() - 16))) return 5;
        std::vector<unsigned char> plaintext;
        const unsigned char* packed = data.data() + 20;
        if (encrypted)
        {
            plaintext.assign(data.begin() + 16, data.begin() + 16 + encrypted);
            const unsigned key[4] = {45129401, 92367215, 681285731, 1710201};
            for (size_t offset = 0; offset < plaintext.size(); offset += 8)
            {
                unsigned y = Fingerprint::Read<unsigned>(plaintext.data() + offset);
                unsigned z = Fingerprint::Read<unsigned>(plaintext.data() + offset + 4);
                unsigned sum = 0x9e3779b9u * 32u;
                for (unsigned round = 0; round < 32; ++round)
                {
                    z -= ((y << 4 ^ y >> 5) + y) ^ (sum + key[(sum >> 11) & 3]);
                    sum -= 0x9e3779b9u;
                    y -= ((z << 4 ^ z >> 5) + z) ^ (sum + key[sum & 3]);
                }
                memcpy(plaintext.data() + offset, &y, 4); memcpy(plaintext.data() + offset + 4, &z, 4);
            }
            if (plaintext.size() < 4 || memcmp(plaintext.data(), "MCOZ", 4)) return 6;
            packed = plaintext.data() + 4;
            if (compressed > plaintext.size() - 4) return 7;
        }
        std::vector<unsigned char> decoded(real);
        lzo_uint length = real;
        if (lzo1x_decompress_safe(packed, compressed, decoded.data(), &length, nullptr) != LZO_E_OK || length != real) return 8;
        data.swap(decoded);
    }
    if (data.size() < 12 || memcmp(data.data(), "EPKD", 4) || Fingerprint::Read<unsigned>(data.data() + 4) != 2) return 9;
    const auto count = Fingerprint::Read<unsigned>(data.data() + 8);
    if (count > (data.size() - 12) / 192) return 10;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto* entry = data.data() + 12 + i * 192;
        if (!Fingerprint::Read<unsigned>(entry + 168)) continue;
        char name[162]{}; memcpy(name, entry + 4, 161);
        std::cout << "{\"name\":" << Quote(name) << ",\"type\":" << unsigned(entry[188])
            << ",\"bytes\":" << Fingerprint::Read<unsigned>(entry + 172)
            << ",\"packed_bytes\":" << Fingerprint::Read<unsigned>(entry + 176) << "}\n";
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 3 && !strcmp(argv[1], "--index")) return Index(argv[2]);
    if (argc != 2) { std::cerr << "AssetInspector file-list.txt\n"; return 2; }
    std::ifstream list(argv[1]);
    if (!list) return 3;
    std::string path;
    while (std::getline(list, path))
    {
        if (!path.empty() && path.back() == '\r') path.pop_back();
        granny_file* file = nullptr;
        try
        {
            LARGE_INTEGER start{}, end{}, frequency{};
            QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
            file = GrannyReadEntireFile(path.c_str());
            QueryPerformanceCounter(&end);
            auto* info = file ? GrannyGetFileInfo(file) : nullptr;
            if (!info) throw std::runtime_error("Granny could not load file");
            Fingerprint fingerprint; fingerprint.Object(GrannyFileInfoType, info);
            const auto sha = fingerprint.Finish();
            long long vertices = 0, bones = 0;
            for (int i = 0; i < info->MeshCount; ++i) vertices += GrannyGetMeshVertexCount(info->Meshes[i]);
            for (int i = 0; i < info->SkeletonCount; ++i) bones += info->Skeletons[i]->BoneCount;
            std::cout << "{\"path\":" << Quote(path.c_str()) << ",\"semantic_sha256\":" << Quote(sha.c_str())
                << ",\"decode_ms\":" << (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart
                << ",\"models\":" << info->ModelCount << ",\"meshes\":" << info->MeshCount
                << ",\"vertices\":" << vertices << ",\"bones\":" << bones << ",\"animations\":" << info->AnimationCount
                << ",\"textures\":[";
            for (int i = 0; i < info->TextureCount; ++i)
            {
                if (i) std::cout << ',';
                std::cout << Quote(info->Textures[i]->FromFileName);
            }
            std::cout << "]}" << std::endl;
        }
        catch (const std::exception& error)
        {
            std::cout << "{\"path\":" << Quote(path.c_str()) << ",\"error\":" << Quote(error.what()) << "}" << std::endl;
        }
        if (file) GrannyFreeFile(file);
    }
    return 0;
}
