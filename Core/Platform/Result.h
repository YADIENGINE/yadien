#pragma once

#include <optional>
#include <string>
#include <utility>

// The core's error channel: a value or a message the UI / CLI shows as is (English, one sentence, no trailing
// period). Exceptions do not cross the core's public API.
template <typename T> class Result {
public:
    Result(T value)
        : _value(std::move(value)) {}

    [[nodiscard]] static Result Failure(std::string error) {
        Result result;
        result._error = std::move(error);
        return result;
    }

    [[nodiscard]] bool Ok() const { return _value.has_value(); }
    [[nodiscard]] const T& Value() const& { return *_value; }
    [[nodiscard]] T& Value() & { return *_value; }
    [[nodiscard]] T&& Value() && { return std::move(*_value); }
    [[nodiscard]] const std::string& Error() const { return _error; }

private:
    Result() = default;

    std::optional<T> _value;
    std::string _error;
};

// Result without a value.
class Status {
public:
    [[nodiscard]] static Status Success() { return Status(); }
    [[nodiscard]] static Status Failure(std::string error) {
        Status status;
        status._error = std::move(error);
        status._ok = false;
        return status;
    }

    [[nodiscard]] bool Ok() const { return _ok; }
    [[nodiscard]] const std::string& Error() const { return _error; }

private:
    Status() = default;

    bool _ok = true;
    std::string _error;
};
