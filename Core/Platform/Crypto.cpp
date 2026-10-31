#include "Platform/Crypto.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <fstream>
#include "Platform/Text.h"

namespace {
constexpr size_t Sha256Bytes = 32;
constexpr size_t FileChunkBytes = 1 << 20;

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

std::vector<std::uint8_t> Pbkdf2Sha256(std::string_view password, std::span<const std::uint8_t> salt,
                                       std::uint64_t iterations, size_t length) {
    std::vector<std::uint8_t> key(length);
    BCRYPT_ALG_HANDLE provider = nullptr;
    if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
        return {};
    const NTSTATUS status = BCryptDeriveKeyPBKDF2(
        provider, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()), iterations, key.data(),
        static_cast<ULONG>(key.size()), 0);
    BCryptCloseAlgorithmProvider(provider, 0);
    return status == 0 ? key : std::vector<std::uint8_t>{};
}

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

bool FromHex(std::string_view text, std::vector<std::uint8_t>& bytes) {
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (text.size() % 2 != 0) return false;
    bytes.clear();
    for (size_t i = 0; i < text.size(); i += 2) {
        const int high = digit(text[i]);
        const int low = digit(text[i + 1]);
        if (high < 0 || low < 0) return false;
        bytes.push_back(static_cast<std::uint8_t>(high * 16 + low));
    }
    return true;
}

bool EqualConstantTime(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    if (a.size() != b.size()) return false;
    std::uint8_t difference = 0;
    for (size_t i = 0; i < a.size(); ++i)
        difference |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    return difference == 0;
}
