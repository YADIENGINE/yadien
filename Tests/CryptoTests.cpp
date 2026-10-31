#include <catch2/catch_test_macros.hpp>
#include <string>
#include "Platform/Crypto.h"
#include "Support/TempFolder.h"

namespace {
// Reference value from Python's hashlib.
constexpr const char* AbcSha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
} // namespace

TEST_CASE("SHA-256 matches the reference for bytes and files", "[crypto]") {
    const std::string abc = "abc";
    CHECK(Sha256Hex({reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()}) == AbcSha256);
    TempFolder temp;
    temp.Write("abc.txt", abc);
    CHECK(Sha256FileHex(temp.Path() / "abc.txt").Value() == AbcSha256);
    CHECK_FALSE(Sha256FileHex(temp.Path() / "missing.txt").Ok());
}
