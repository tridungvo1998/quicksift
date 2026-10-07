// OWNER: Process initialization and native entry point only.
#include "app/quicksift_application.h"
#include "app/application_support.h"
#include "diagnostics/diagnostic_log.h"
#include "runtime_support.h"
#include "platform/application_data_paths.h"

#include <exception>
#include <filesystem>
#include <gdiplus.h>
#include <objbase.h>
#include <string>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    if (!quicksift::diagnostics::Initialize()) {
        const std::filesystem::path dataRoot = quicksift::diagnostics::DataRootPath();
        const bool packaged = quicksift::platform::RunningWithPackageIdentity();
        std::wstring message = packaged
            ? L"QuickSift could not create or write its required MSIX LocalState data folder. QuickSift will not start.\n\n"
            : L"QuickSift could not create or write its required QuickSiftData folder next to the application executable. QuickSift will not start.\n\n";
        if (!dataRoot.empty()) {
            message += L"Required folder:\n" + dataRoot.wstring() + L"\n\n";
        }
        message += packaged
            ? L"Repair or reinstall the QuickSift package, or check permissions for the package LocalState folder, then try again."
            : L"Move QuickSift to a writable folder or grant write access to the application folder, then try again.";
        MessageBoxW(nullptr, message.c_str(), L"QuickSift startup error", MB_OK | MB_ICONERROR);
        return 1;
    }
    quicksift::diagnostics::InstallCrashHandlers();
    QS_LOG_INFO(L"Lifecycle", L"QuickSift process starting");
    quicksift::diagnostics::WriteSystemSnapshot(L"startup");
    quicksift::InitializeProcessSecurity();

    if (auto setDpi = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))) {
        setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        QS_LOG_CRITICAL(L"Startup", L"GDI+ initialization failed");
        quicksift::diagnostics::WriteSystemSnapshot(L"startup_failure_gdiplus");
        quicksift::diagnostics::Shutdown();
        return 1;
    }

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com)) {
        QS_LOG_CRITICAL(L"Startup", L"COM apartment initialization failed");
        Gdiplus::GdiplusShutdown(gdiplusToken);
        quicksift::diagnostics::Shutdown();
        return 1;
    }
    int result = 1;
    try {
        quicksift::app::QuickSiftApplication application;
        result = application.Run(instance, showCommand);
    } catch (const std::exception& error) {
        QS_LOG_CRITICAL(L"Runtime", L"A standard C++ exception reached wWinMain");
        try {
            const char* what = error.what();
            if (what) QS_LOG_CRITICAL(L"Runtime", quicksift::app::Utf8ToWide(what));
        } catch (...) {
            // The original failure may be allocation exhaustion. Diagnostics must
            // not throw a second exception while handling it.
        }
        MessageBoxW(nullptr, L"QuickSift encountered an unexpected startup or runtime failure. A diagnostic log was saved.",
            L"QuickSift", MB_OK | MB_ICONERROR);
        result = 1;
    } catch (...) {
        QS_LOG_CRITICAL(L"Runtime", L"Unhandled non-standard C++ exception reached wWinMain");
        MessageBoxW(nullptr, L"QuickSift encountered an unexpected startup or runtime failure. A diagnostic log was saved.",
            L"QuickSift", MB_OK | MB_ICONERROR);
        result = 1;
    }
    try {
        QS_LOG_INFO(L"Lifecycle", L"QuickSift process exiting with code " + std::to_wstring(result));
    } catch (...) {
        QS_LOG_INFO(L"Lifecycle", L"QuickSift process exiting");
    }
    quicksift::diagnostics::WriteSystemSnapshot(L"shutdown");
    CoUninitialize();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    quicksift::diagnostics::Shutdown();
    return result;
}
