#include "Developer/DeveloperAccess.h"
#include <nlohmann/json.hpp>
#include "EmbeddedDeveloperAccess.h"
#include "Platform/Crypto.h"

namespace {
constexpr std::string_view Algorithm = "PBKDF2-SHA256";
constexpr std::uint64_t MinIterations = 100'000; // Records below this were not written by the script
} // namespace

Result<DeveloperAccessRecord> ParseDeveloperAccess(std::string_view json) {
    using Parsed = Result<DeveloperAccessRecord>;
    const nlohmann::json data = nlohmann::json::parse(json, nullptr, false);
    if (data.is_discarded() || !data.is_object()) return Parsed::Failure("DeveloperAccess.json is not valid JSON");
    if (data.value("format", 0) != 1 || data.value("algorithm", "") != Algorithm)
        return Parsed::Failure("DeveloperAccess.json: unsupported format or algorithm");
    DeveloperAccessRecord record;
    record.iterations = data.value("iterations", std::uint64_t{0});
    if (record.iterations < MinIterations || !FromHex(data.value("salt", ""), record.salt) ||
        !FromHex(data.value("hash", ""), record.hash) || record.salt.empty() || record.hash.empty())
        return Parsed::Failure("DeveloperAccess.json: iterations, salt or hash is invalid");
    return record;
}

std::optional<DeveloperAccessRecord> EmbeddedDeveloperAccess() {
    const std::string_view json = EmbeddedDeveloperAccessJson;
    if (json.find_first_not_of(" \t\r\n") == std::string_view::npos) return std::nullopt;
    Result<DeveloperAccessRecord> record = ParseDeveloperAccess(json);
    if (!record.Ok()) return std::nullopt;
    return std::move(record).Value();
}

bool CheckDeveloperPassword(const DeveloperAccessRecord& record, std::string_view password) {
    const std::vector<std::uint8_t> derived =
        Pbkdf2Sha256(password, record.salt, record.iterations, record.hash.size());
    return !derived.empty() && EqualConstantTime(derived, record.hash);
}
