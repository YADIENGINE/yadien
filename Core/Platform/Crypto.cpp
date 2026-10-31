#include "Platform/Crypto.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <string_view>
#include <vector>
#include "Platform/Text.h"

namespace {
constexpr size_t Sha256Bytes = 32;
constexpr size_t FileChunkBytes = 1 << 20;

std::string ToHex(std::span<const std::uint8_t> bytes) {
    constexpr std::string_view Digits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        text.push_back(Digits[byte >> 4]);
        text.push_back(Digits[byte & 0xF]);
    }
    return text;
}

// One SHA-256 computation; the algorithm provider is opened once per process.
class Sha256 {
public:
    Sha256() { BCryptCreateHash(Provider(), &_hash, nullptr, 0, nullptr, 0, 0); }
    ~Sha256() {
        if (_hash != nullptr) BCryptDestroyHash(_hash);
    }

    void Add(const std::uint8_t* data, size_t size) {
        BCryptHashData(_hash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0);
    }

    [[nodiscard]] std::string FinishHex() {
        std::array<std::uint8_t, Sha256Bytes> digest{};
        BCryptFinishHash(_hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        return ToHex(digest);
    }

private:
    static BCRYPT_ALG_HANDLE Provider() {
        static BCRYPT_ALG_HANDLE provider = [] {
            BCRYPT_ALG_HANDLE handle = nullptr;
            BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
            return handle;
        }();
        return provider;
    }

    BCRYPT_HASH_HANDLE _hash = nullptr;

public:
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
};
} // namespace

std::string Sha256Hex(std::span<const std::uint8_t> bytes) {
    Sha256 hash;
    hash.Add(bytes.data(), bytes.size());
    return hash.FinishHex();
}

Result<std::string> Sha256FileHex(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return Result<std::string>::Failure("Cannot read " + PathToUtf8(file));
    Sha256 hash;
    std::vector<char> chunk(FileChunkBytes);
    while (stream) {
        stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        hash.Add(reinterpret_cast<const std::uint8_t*>(chunk.data()), static_cast<size_t>(stream.gcount()));
    }
    if (stream.bad()) return Result<std::string>::Failure("Cannot read " + PathToUtf8(file));
    return hash.FinishHex();
}
