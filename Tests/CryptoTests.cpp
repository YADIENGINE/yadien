#include <catch2/catch_test_macros.hpp>
#include <string>
#include "Developer/DeveloperAccess.h"
#include "Platform/Crypto.h"
#include "Support/TempFolder.h"

namespace {
// Reference values from Python's hashlib (the script that writes DeveloperAccess.json uses it).
constexpr const char* AbcSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr const char* Password = "correct horse";
constexpr const char* PasswordHash = "57f2c2f0739748d516419b062a884666323c583ea4ae165504a81f7b53c62a09";
constexpr const char* Salt = "000102030405060708090a0b0c0d0e0f";
constexpr int Iterations = 100000;

std::string Record(int iterations) {
    return R"({"format": 1, "algorithm": "PBKDF2-SHA256", "iterations": )" + std::to_string(iterations) +
           R"(, "salt": ")" + Salt + R"(", "hash": ")" + PasswordHash + R"("})";
}
} // namespace

TEST_CASE("SHA-256 matches the reference for bytes and files", "[crypto]") {
    const std::string abc = "abc";
    CHECK(Sha256Hex({reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()}) == AbcSha256);
    TempFolder temp;
    temp.Write("abc.txt", abc);
    CHECK(Sha256FileHex(temp.Path() / "abc.txt").Value() == AbcSha256);
    CHECK_FALSE(Sha256FileHex(temp.Path() / "missing.txt").Ok());
}

TEST_CASE("The developer password is checked against a script-written record", "[crypto]") {
    Result<DeveloperAccessRecord> record = ParseDeveloperAccess(Record(Iterations));
    REQUIRE(record.Ok());
    CHECK(CheckDeveloperPassword(record.Value(), Password));
    CHECK_FALSE(CheckDeveloperPassword(record.Value(), "Correct horse"));
    CHECK_FALSE(CheckDeveloperPassword(record.Value(), ""));
}

TEST_CASE("Weak or malformed access records are refused", "[crypto]") {
    CHECK_FALSE(ParseDeveloperAccess(Record(1000)).Ok());
    CHECK_FALSE(ParseDeveloperAccess("{}").Ok());
    CHECK_FALSE(ParseDeveloperAccess("not json").Ok());
}

TEST_CASE("Hex round-trips and rejects malformed text", "[crypto]") {
    std::vector<std::uint8_t> bytes;
    REQUIRE(FromHex("00ff10", bytes));
    CHECK(ToHex(bytes) == "00ff10");
    CHECK_FALSE(FromHex("abc", bytes));
    CHECK_FALSE(FromHex("zz", bytes));
}
