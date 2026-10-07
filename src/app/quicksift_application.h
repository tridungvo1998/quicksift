// OWNER: Stable public application facade; implementation details live behind PIMPL.
#pragma once

#include <memory>
#include <windows.h>

namespace quicksift::app {

class QuickSiftApplicationImpl;

class QuickSiftApplication {
public:
    QuickSiftApplication();
    ~QuickSiftApplication();
    QuickSiftApplication(const QuickSiftApplication&) = delete;
    QuickSiftApplication& operator=(const QuickSiftApplication&) = delete;
    QuickSiftApplication(QuickSiftApplication&&) = delete;
    QuickSiftApplication& operator=(QuickSiftApplication&&) = delete;

    int Run(HINSTANCE instance, int showCommand);

private:
    std::unique_ptr<QuickSiftApplicationImpl> implementation_;
};

} // namespace quicksift::app
