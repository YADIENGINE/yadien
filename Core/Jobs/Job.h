#pragma once

#include "Jobs/BackgroundJob.h"
#include "Platform/Result.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

// A BackgroundJob whose body returns Result<T> (Status for Job<void>). The body starts at construction; the result
// is read once the job is finished:
//   auto job = std::make_unique<Job<SystemReport>>("System check", 1, [&](JobContext& context) { ... });
//   if (job->Finished()) { const auto& report = *job->Outcome(); ... }
template <typename T> class Job final : public BackgroundJob {
public:
    using ResultType = std::conditional_t<std::is_void_v<T>, Status, Result<T>>;
    using Body = std::function<ResultType(JobContext&)>;

    Job(std::string title, int stepCount, Body body)
        : BackgroundJob(std::move(title), stepCount) {
        Start([this, body = std::move(body)](JobContext& context) {
            ResultType outcome = body(context);
            std::string error = outcome.Ok() ? std::string() : outcome.Error();
            _outcome.emplace(std::move(outcome));
            return error;
        });
    }
    ~Job() override { Stop(); }

    // Only after Finished(): Succeeded holds the value; Failed / Canceled hold the error (empty if the body threw).
    [[nodiscard]] const std::optional<ResultType>& Outcome() const { return _outcome; }

private:
    std::optional<ResultType> _outcome; // Written by the job's thread before the state leaves Running
};
