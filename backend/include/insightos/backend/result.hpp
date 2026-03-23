#pragma once

/// InsightOS backend — Simple Result<T> type for error propagation.
///
/// Adapted from donor iocontroller/result.hpp (commit 4032eb4).
/// Namespace: insightos::backend

#include <string>
#include <utility>

namespace insightos::backend {

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

    bool ok() const { return ok_; }
    const T& value() const { return value_; }
    T& value() { return value_; }
    const Error& error() const { return error_; }

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

    bool ok() const { return ok_; }
    const Error& error() const { return error_; }

private:
    bool ok_{false};
    Error error_{};
};

}  // namespace insightos::backend
