#pragma once

// role: lightweight result wrapper for backend runtime helpers.
// revision: 2026-03-26 task7-ipc-runtime
// major changes: adds a small error/result type reused by IPC transport,
// control-socket helpers, and task-7 runtime plumbing.
// See docs/past-tasks.md for verification history.

#include <string>
#include <utility>

namespace insightio::backend {

struct Error {
    std::string code;
    std::string message;
};

template <typename T>
class Result {
public:
    Result(const T& value) : ok_(true), value_(value) {}
    Result(T&& value) : ok_(true), value_(std::move(value)) {}
    Result(const Error& error) : ok_(false), error_(error) {}
    Result(Error&& error) : ok_(false), error_(std::move(error)) {}

    static Result ok(T value) { return Result(std::move(value)); }
    static Result err(Error error) { return Result(std::move(error)); }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const T& value() const { return value_; }
    [[nodiscard]] T& value() { return value_; }
    [[nodiscard]] const Error& error() const { return error_; }

private:
    bool ok_{false};
    T value_{};
    Error error_{};
};

template <>
class Result<void> {
public:
    Result() : ok_(true) {}
    Result(const Error& error) : ok_(false), error_(error) {}
    Result(Error&& error) : ok_(false), error_(std::move(error)) {}

    static Result success() { return Result(); }
    static Result err(Error error) { return Result(std::move(error)); }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const Error& error() const { return error_; }

private:
    bool ok_{false};
    Error error_{};
};

}  // namespace insightio::backend
