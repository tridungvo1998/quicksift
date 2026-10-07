// CODE GUIDE: See CODE_GUIDE.md -> "Rules for asynchronous work".
// OWNER: Exception-safe temporary boolean state; always restore the previous value on scope exit.

#pragma once

namespace quicksift::core {

class ScopedBooleanFlag final {
public:
    explicit ScopedBooleanFlag(bool& flag, bool temporaryValue = true) noexcept
        : flag_(flag), previous_(flag) {
        flag_ = temporaryValue;
    }

    ~ScopedBooleanFlag() { flag_ = previous_; }

    ScopedBooleanFlag(const ScopedBooleanFlag&) = delete;
    ScopedBooleanFlag& operator=(const ScopedBooleanFlag&) = delete;

private:
    bool& flag_;
    bool previous_ = false;
};

} // namespace quicksift::core
