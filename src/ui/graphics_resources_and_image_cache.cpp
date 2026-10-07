// OWNER: Compiled QuickSiftApplication feature module.
#include "app/quicksift_application_internal.h"
#include <shobjidl.h>
#include <shellapi.h>

namespace quicksift::app {

// CODE GUIDE: See CODE_GUIDE.md -> "Scrolling thumbnails".
// OWNER: Graphics devices and bitmap-cache admission/eviction; UI thread owns device resources.

// Graphics/cache controller: Direct2D/Direct3D resources, bitmap admission and
// eviction, GPU pressure handling, WIC image sources, and view-resource retention.

    bool QuickSiftApplicationImpl::CreateSwapChainTarget() {
        if (!deviceContext2_ || !swapChain_) return false;
        deviceContext2_->SetTarget(nullptr);
        swapChainTarget_.Reset();
        ComPtr<IDXGISurface> surface;
        if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
        const float dpi = CanvasDpi();
        D2D1_BITMAP_PROPERTIES1 properties{};
        properties.pixelFormat = D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
        properties.dpiX = dpi;
        properties.dpiY = dpi;
        properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        properties.colorContext = nullptr;
        if (FAILED(deviceContext2_->CreateBitmapFromDxgiSurface(surface.Get(), &properties,
            &swapChainTarget_))) return false;
        deviceContext2_->SetTarget(swapChainTarget_.Get());
        deviceContext2_->SetDpi(dpi, dpi);
        deviceContext2_.As(&renderTarget_);
        return renderTarget_ != nullptr;
    }


    bool QuickSiftApplicationImpl::EnsureModernDeviceResources() {
        if (!systemProfile_.enableModernImageSources || !canvas_) return false;
        EnsureGpuBudgetDevice();
        if (!d3dDevice_) return false;
        if (!d2dFactory1_) {
            D2D1_FACTORY_OPTIONS options{};
#ifndef NDEBUG
            options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                __uuidof(ID2D1Factory1), &options,
                reinterpret_cast<void**>(d2dFactory1_.ReleaseAndGetAddressOf())))) return false;
        }
        if (!d2dDevice_) {
            ComPtr<IDXGIDevice> dxgiDevice;
            if (FAILED(d3dDevice_.As(&dxgiDevice)) ||
                FAILED(d2dFactory1_->CreateDevice(dxgiDevice.Get(), &d2dDevice_))) return false;
        }
        if (!deviceContext2_) {
            ComPtr<ID2D1DeviceContext> context;
            if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context)) ||
                FAILED(context.As(&deviceContext2_))) return false;
        }
        if (!swapChain_) {
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> factory;
            if (FAILED(d3dDevice_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
                FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;
            RECT client{};
            GetClientRect(canvas_, &client);
            DXGI_SWAP_CHAIN_DESC1 description{};
            description.Width = std::max<UINT>(1, static_cast<UINT>(client.right - client.left));
            description.Height = std::max<UINT>(1, static_cast<UINT>(client.bottom - client.top));
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.Stereo = FALSE;
            description.SampleDesc.Count = 1;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = 2;
            description.Scaling = DXGI_SCALING_STRETCH;
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            if (FAILED(factory->CreateSwapChainForHwnd(d3dDevice_.Get(), canvas_, &description,
                nullptr, nullptr, &swapChain_))) return false;
            factory->MakeWindowAssociation(canvas_, DXGI_MWA_NO_ALT_ENTER);
        }
        return swapChainTarget_ ? true : CreateSwapChainTarget();
    }


    void QuickSiftApplicationImpl::ResizeCanvasTarget(UINT width, UINT height) {
        if (swapChain_) {
            renderTarget_.Reset();
            if (deviceContext2_) deviceContext2_->SetTarget(nullptr);
            swapChainTarget_.Reset();
            const HRESULT result = swapChain_->ResizeBuffers(0, std::max(1u, width),
                std::max(1u, height), DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(result) || !CreateSwapChainTarget()) DiscardDeviceResources();
        } else if (hwndRenderTarget_) {
            hwndRenderTarget_->Resize(D2D1::SizeU(std::max(1u, width), std::max(1u, height)));
        }
    }


    void QuickSiftApplicationImpl::ResetModernDeviceResources() {
        renderTarget_.Reset();
        if (deviceContext2_) deviceContext2_->SetTarget(nullptr);
        swapChainTarget_.Reset();
        swapChain_.Reset();
        deviceContext2_.Reset();
        d2dDevice_.Reset();
        d2dFactory1_.Reset();
        ClearWicImageSources();
    }


    void QuickSiftApplicationImpl::EnsureDeviceResources() {
        if (!dwriteFactory_) {
            DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf()));
        }
        if (!renderTarget_) {
            if (!EnsureModernDeviceResources()) {
                ResetModernDeviceResources();
                if (!d2dFactory_) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                    d2dFactory_.ReleaseAndGetAddressOf());
                if (d2dFactory_ && canvas_) {
                    RECT rect{};
                    GetClientRect(canvas_, &rect);
                    D2D1_SIZE_U size = D2D1::SizeU(std::max(1L, rect.right), std::max(1L, rect.bottom));
                    const float dpi = CanvasDpi();
                    HRESULT createHr = d2dFactory_->CreateHwndRenderTarget(
                        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
                            D2D1::PixelFormat(), dpi, dpi),
                        D2D1::HwndRenderTargetProperties(canvas_, size), &hwndRenderTarget_);
                    if (FAILED(createHr)) {
                        d2dFactory_->CreateHwndRenderTarget(
                            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                D2D1::PixelFormat(), dpi, dpi),
                            D2D1::HwndRenderTargetProperties(canvas_, size), &hwndRenderTarget_);
                    }
                    if (hwndRenderTarget_) hwndRenderTarget_.As(&renderTarget_);
                }
            }
        }
        bool sharedResourcesReady = renderTarget_ != nullptr && dwriteFactory_ != nullptr;
        if (renderTarget_) {
            const ThemePalette palette = Palette();
            auto ensureBrush = [&](ComPtr<ID2D1SolidColorBrush>& brush,
                const D2D1_COLOR_F& color, std::wstring_view name) {
                if (brush) return true;
                const HRESULT result = renderTarget_->CreateSolidColorBrush(color, &brush);
                if (FAILED(result)) {
                    QS_LOG_WARNING(L"Rendering", L"Could not create the " + std::wstring(name) +
                        L" brush (HRESULT " + std::to_wstring(static_cast<long>(result)) + L")");
                    return false;
                }
                return true;
            };
            sharedResourcesReady = ensureBrush(textBrush_, ToD2D(palette.text), L"text") && sharedResourcesReady;
            sharedResourcesReady = ensureBrush(panelBrush_, ToD2D(palette.panel,
                theme_ == AppTheme::Dark ? 0.92f : 0.97f), L"panel") && sharedResourcesReady;
            sharedResourcesReady = ensureBrush(accentBrush_, ToD2D(palette.accent), L"accent") && sharedResourcesReady;
            sharedResourcesReady = ensureBrush(dangerBrush_, ToD2D(RGB(235, 64, 64)), L"danger") && sharedResourcesReady;
            sharedResourcesReady = ensureBrush(mutedBrush_, ToD2D(palette.muted), L"muted") && sharedResourcesReady;
            sharedResourcesReady = ensureBrush(backgroundBrush_, ToD2D(palette.windowBottom), L"background") && sharedResourcesReady;
            sharedResourcesReady = ensureBrush(overlayBrush_, ToD2D(palette.panelStrong,
                theme_ == AppTheme::Dark ? 0.94f : 0.985f), L"overlay") && sharedResourcesReady;
            sharedResourcesReady = ensureBrush(glassBorderBrush_, ToD2D(palette.border, 0.78f), L"glass border") && sharedResourcesReady;
            sharedResourcesReady = ensureBrush(glassHighlightBrush_, ToD2D(RGB(255, 255, 255),
                theme_ == AppTheme::Dark ? 0.055f : 0.28f), L"glass highlight") && sharedResourcesReady;
        }
        if (dwriteFactory_) {
            auto ensureTextFormat = [&](ComPtr<IDWriteTextFormat>& format, const wchar_t* family,
                DWRITE_FONT_WEIGHT weight, float size, std::wstring_view name) {
                if (format) return true;
                const HRESULT result = dwriteFactory_->CreateTextFormat(family, nullptr, weight,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &format);
                if (FAILED(result)) {
                    QS_LOG_WARNING(L"Rendering", L"Could not create the " + std::wstring(name) +
                        L" text format (HRESULT " + std::to_wstring(static_cast<long>(result)) + L")");
                    return false;
                }
                return true;
            };
            sharedResourcesReady = ensureTextFormat(smallText_, L"Segoe UI Variable Text",
                DWRITE_FONT_WEIGHT_NORMAL, 12.0f, L"small") && sharedResourcesReady;
            sharedResourcesReady = ensureTextFormat(mediumText_, L"Segoe UI Variable Display",
                DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.0f, L"medium") && sharedResourcesReady;
            sharedResourcesReady = ensureTextFormat(largeText_, L"Segoe UI Variable Display",
                DWRITE_FONT_WEIGHT_NORMAL, 16.0f, L"large") && sharedResourcesReady;
        }
        if (!sharedResourcesReady) {
            smallText_.Reset();
            mediumText_.Reset();
            largeText_.Reset();
            DiscardDeviceResources();
        }
    }


    void QuickSiftApplicationImpl::ClearWicImageSources() {
        wicImageSources_.clear();
        uiWicFactory_.Reset();
    }


    void QuickSiftApplicationImpl::DiscardDeviceResources() {
        ClearBitmapCache();
        ClearWicImageSources();
        textBrush_.Reset();
        panelBrush_.Reset();
        accentBrush_.Reset();
        dangerBrush_.Reset();
        mutedBrush_.Reset();
        backgroundBrush_.Reset();
        overlayBrush_.Reset();
        glassBorderBrush_.Reset();
        glassHighlightBrush_.Reset();
        hwndRenderTarget_.Reset();
        ResetModernDeviceResources();
    }


    void QuickSiftApplicationImpl::EnsureGpuBudgetDevice() {
        if (d3dDevice_) return;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevel{};
        static constexpr D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
        };
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
            &d3dDevice_, &featureLevel, &d3dContext_);
#ifndef NDEBUG
        // The debug layer is optional on end-user Windows installations. Retry
        // the hardware adapter without it before falling all the way back to
        // WARP; otherwise a perfectly capable GPU can be replaced by software
        // rendering merely because the SDK debug component is absent.
        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
            d3dDevice_.Reset();
            d3dContext_.Reset();
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                &d3dDevice_, &featureLevel, &d3dContext_);
        }
#endif
        if (FAILED(hr)) {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            d3dDevice_.Reset();
            d3dContext_.Reset();
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                &d3dDevice_, &featureLevel, &d3dContext_);
        }
        if (FAILED(hr) || !d3dDevice_) return;
        d3dDevice_.As(&dxgiDevice3_);
        ComPtr<IDXGIDevice> dxgiDevice;
        if (SUCCEEDED(d3dDevice_.As(&dxgiDevice))) {
            ComPtr<IDXGIAdapter> adapter;
            if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) adapter.As(&dxgiAdapter3_);
        }
        resourcePressureMonitor_.SetAdapter(dxgiAdapter3_.Get());
        UpdateGpuBudget();
    }


    void QuickSiftApplicationImpl::UpdateGpuBudget() {
        if (!dxgiAdapter3_) return;
        DXGI_QUERY_VIDEO_MEMORY_INFO local{};
        DXGI_QUERY_VIDEO_MEMORY_INFO nonLocal{};
        const HRESULT localResult = dxgiAdapter3_->QueryVideoMemoryInfo(
            0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
        const HRESULT nonLocalResult = dxgiAdapter3_->QueryVideoMemoryInfo(
            0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal);
        if (FAILED(localResult) && FAILED(nonLocalResult)) return;
        if (FAILED(localResult)) local = {};
        if (FAILED(nonLocalResult)) nonLocal = {};
        const std::uint64_t localAvailable = local.Budget > local.CurrentUsage ?
            local.Budget - local.CurrentUsage : 0;
        const std::uint64_t sharedAvailable = nonLocal.Budget > nonLocal.CurrentUsage ?
            nonLocal.Budget - nonLocal.CurrentUsage : 0;
        const std::uint64_t reclaimable = static_cast<std::uint64_t>(TotalCacheBytes());
        const std::uint64_t localUseful = localAvailable + std::min(local.CurrentUsage, reclaimable);
        const std::uint64_t sharedUseful = sharedAvailable + std::min(nonLocal.CurrentUsage, reclaimable);
        const std::uint64_t useful = std::max(localUseful, sharedUseful / 2);
        const std::uint64_t normalMinimum = systemProfile_.lowMemory ?
            96ull * kMebibyte : 192ull * kMebibyte;
        const std::uint64_t emergencyMinimum = systemProfile_.lowMemory ?
            32ull * kMebibyte : 64ull * kMebibyte;
        const std::uint64_t maximum = systemProfile_.lowMemory ? 320ull * kMebibyte :
            (systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte ? 640ull * kMebibyte : 1536ull * kMebibyte);
        const std::uint64_t proportional = useful / (systemProfile_.lowMemory ? 5ull : 4ull);
        const std::uint64_t minimum = useful >= normalMinimum * 2 ? normalMinimum : emergencyMinimum;
        systemProfile_.gpuBudgetTarget = static_cast<std::size_t>(std::clamp<std::uint64_t>(
            proportional, minimum, maximum));
        hardCacheLimit_ = std::min(memoryHardCacheLimit_, systemProfile_.gpuBudgetTarget);
    }


    void QuickSiftApplicationImpl::OfferGraphicsResources(bool release) {
        if (release) {
            for (auto& [path, entry] : wicImageSources_) {
                if (entry.imageSource && !entry.offered && SUCCEEDED(entry.imageSource->OfferResources())) {
                    entry.offered = true;
                }
            }
            if (IsIconic(hwnd_)) {
                DiscardDeviceResources();
                return;
            }
        } else {
            for (auto& [path, entry] : wicImageSources_) {
                if (!entry.imageSource || !entry.offered) continue;
                BOOL discarded = FALSE;
                if (SUCCEEDED(entry.imageSource->TryReclaimResources(&discarded))) {
                    entry.offered = false;
                    if (discarded) entry.hasTrim = false;
                }
            }
        }
        // Trim is a release boundary, not a reclaim operation. Calling it after
        // TryReclaimResources would immediately throw away driver allocations
        // that the foreground renderer is about to use again.
        if (release && dxgiDevice3_) dxgiDevice3_->Trim();
    }


    void QuickSiftApplicationImpl::ConfigureCacheBudgets() {
        MEMORYSTATUSEX memory{ sizeof(memory) };
        const bool haveMemory = GlobalMemoryStatusEx(&memory) != FALSE;
        const std::uint64_t available64 = haveMemory ? memory.ullAvailPhys :
            systemProfile_.availablePhysicalBytes;
        const size_t available = static_cast<size_t>(std::min<std::uint64_t>(
            available64, static_cast<std::uint64_t>(SIZE_MAX)));
        const size_t mib = static_cast<size_t>(kMebibyte);

        // CPU workers and bitmap budgets are derived from the same machine profile.
        // Eight-gigabyte systems keep a compact working set and rely on the disk cache;
        // roomier systems retain more comparison panes and zoom refinements in memory.
        if (systemProfile_.lowMemory) {
            thumbnailCacheLimit_ = std::clamp<size_t>(available / 72, 64 * mib, 128 * mib);
            previewCacheLimit_ = std::clamp<size_t>(available / 36, 80 * mib, 160 * mib);
            fullCacheLimit_ = std::clamp<size_t>(available / 64, 48 * mib, 96 * mib);
            tileCacheLimit_ = std::clamp<size_t>(available / 48, 64 * mib, 128 * mib);
            memoryHardCacheLimit_ = std::clamp<size_t>(available / 12, 224 * mib, 384 * mib);
        } else if (systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte) {
            thumbnailCacheLimit_ = std::clamp<size_t>(available / 56, 80 * mib, 160 * mib);
            previewCacheLimit_ = std::clamp<size_t>(available / 22, 128 * mib, 320 * mib);
            fullCacheLimit_ = std::clamp<size_t>(available / 36, 64 * mib, 192 * mib);
            tileCacheLimit_ = std::clamp<size_t>(available / 28, 96 * mib, 256 * mib);
            memoryHardCacheLimit_ = std::clamp<size_t>(available / 7, 320 * mib, 640 * mib);
        } else {
            thumbnailCacheLimit_ = std::clamp<size_t>(available / 48, 96 * mib, 256 * mib);
            previewCacheLimit_ = std::clamp<size_t>(available / 18, 192 * mib, 512 * mib);
            fullCacheLimit_ = std::clamp<size_t>(available / 28, 96 * mib, 320 * mib);
            tileCacheLimit_ = std::clamp<size_t>(available / 22, 128 * mib, 384 * mib);
            memoryHardCacheLimit_ = std::clamp<size_t>(available / 6, 512 * mib, 1024 * mib);
        }
        hardCacheLimit_ = std::min(memoryHardCacheLimit_, systemProfile_.gpuBudgetTarget);
    }


    void QuickSiftApplicationImpl::AddCacheBytes(CacheClass cacheClass, size_t bytes) {
        switch (cacheClass) {
        case CacheClass::Thumbnail: thumbnailCacheBytes_ += bytes; break;
        case CacheClass::Preview: previewCacheBytes_ += bytes; break;
        case CacheClass::Full: fullCacheBytes_ += bytes; break;
        case CacheClass::Tile: tileCacheBytes_ += bytes; break;
        }
    }


    void QuickSiftApplicationImpl::RemoveCacheBytes(CacheClass cacheClass, size_t bytes) {
        auto subtract = [bytes](size_t& value) { value = bytes > value ? 0 : value - bytes; };
        switch (cacheClass) {
        case CacheClass::Thumbnail: subtract(thumbnailCacheBytes_); break;
        case CacheClass::Preview: subtract(previewCacheBytes_); break;
        case CacheClass::Full: subtract(fullCacheBytes_); break;
        case CacheClass::Tile: subtract(tileCacheBytes_); break;
        }
    }


    void QuickSiftApplicationImpl::UnindexBitmapSize(const CacheKey& key) {
        // Multiple cache classes may share the same path/size bucket. Keep the
        // size indexed until the last bitmap for that pair is gone.
        const bool anotherEntry = std::any_of(bitmapCache_.begin(), bitmapCache_.end(),
            [&](const auto& item) {
                return item.first.path == key.path && item.first.size == key.size &&
                    !(item.first.cacheClass == key.cacheClass);
            });
        if (anotherEntry) return;
        auto it = pathToBitmapSizes_.find(key.path);
        if (it == pathToBitmapSizes_.end()) return;
        it->second.erase(key.size);
        if (it->second.empty()) pathToBitmapSizes_.erase(it);
    }


    size_t QuickSiftApplicationImpl::TotalCacheBytes() const {
        size_t total = thumbnailCacheBytes_;
        total = SaturatingSizeAdd(total, previewCacheBytes_);
        total = SaturatingSizeAdd(total, fullCacheBytes_);
        return SaturatingSizeAdd(total, tileCacheBytes_);
    }


    size_t QuickSiftApplicationImpl::FrequencyHash(const CacheKey& key) const {
        return CacheKeyHash{}(key);
    }


    std::uint8_t QuickSiftApplicationImpl::CacheFrequency(const CacheKey& key) const {
        const auto it = cacheFrequency_.find(FrequencyHash(key));
        return it == cacheFrequency_.end() ? 0 : it->second;
    }


    void QuickSiftApplicationImpl::RecordCacheFrequency(const CacheKey& key) {
        const size_t hash = FrequencyHash(key);
        auto [it, inserted] = cacheFrequency_.try_emplace(hash, 0);
        if (it->second < 255) ++it->second;
        ++cacheFrequencySamples_;
        const size_t limit = systemProfile_.lowMemory ? 8192 :
            (systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte ? 16384 : 32768);
        if (cacheFrequency_.size() > limit || cacheFrequencySamples_ > limit * 8ull) {
            for (auto frequency = cacheFrequency_.begin(); frequency != cacheFrequency_.end();) {
                frequency->second = static_cast<std::uint8_t>(frequency->second / 2);
                if (frequency->second == 0) frequency = cacheFrequency_.erase(frequency);
                else ++frequency;
            }
            cacheFrequencySamples_ = 0;
        }
    }


    void QuickSiftApplicationImpl::TouchBitmapEntry(const CacheKey& key, BitmapEntry& entry) {
        entry.lastUse = ++useCounter_;
        entry.lastUseTime = std::chrono::steady_clock::now();
        if (entry.hitCount < 3) ++entry.hitCount;
        if (entry.hitCount >= 2) entry.frequent = true;
        RecordCacheFrequency(key);
    }


    bool QuickSiftApplicationImpl::ShouldAdmitBitmap(const CacheKey& key, const WorkResult& result, size_t additionalBytes) {
        RecordCacheFrequency(key);
        if ((result.priority != JobPriority::Idle && result.priority != JobPriority::Analysis) ||
            IsProtectedBitmap(key) || (reviewState_.FaceLockEnabled() && PathHasDetectedFace(key.path))) return true;
        const size_t projected = additionalBytes > SIZE_MAX - TotalCacheBytes() ? SIZE_MAX :
            TotalCacheBytes() + additionalBytes;
        if (projected <= hardCacheLimit_) return true;

        const std::uint8_t incomingFrequency = CacheFrequency(key);
        const auto victim = std::min_element(bitmapCache_.begin(), bitmapCache_.end(),
            [&](const auto& left, const auto& right) {
                const bool leftProtected = IsProtectedBitmap(left.first);
                const bool rightProtected = IsProtectedBitmap(right.first);
                if (leftProtected != rightProtected) return !leftProtected;
                const bool leftFace = reviewState_.FaceLockEnabled() && PathHasDetectedFace(left.first.path);
                const bool rightFace = reviewState_.FaceLockEnabled() && PathHasDetectedFace(right.first.path);
                if (leftFace != rightFace) return !leftFace;
                if (left.second.frequent != right.second.frequent) return !left.second.frequent;
                return left.second.lastUse < right.second.lastUse;
            });
        if (victim == bitmapCache_.end() || IsProtectedBitmap(victim->first)) return false;
        const std::uint8_t victimFrequency = CacheFrequency(victim->first);
        return !victim->second.frequent || incomingFrequency > victimFrequency;
    }


    void QuickSiftApplicationImpl::PruneSupersededViewBitmaps(const std::wstring& path) {
        int bestFullEdge = 0;
        int bestPreviewEdge = 0;
        for (const auto& [key, entry] : bitmapCache_) {
            if (key.path != path || key.cacheClass == CacheClass::Thumbnail) continue;
            const int edge = BitmapMaximumEdge(&entry);
            if (entry.previewOnly) bestPreviewEdge = std::max(bestPreviewEdge, edge);
            else bestFullEdge = std::max(bestFullEdge, edge);
        }
        if (bestFullEdge <= 0 && bestPreviewEdge <= 0) return;
        for (auto iterator = bitmapCache_.begin(); iterator != bitmapCache_.end();) {
            const bool sameView = iterator->first.path == path &&
                iterator->first.cacheClass != CacheClass::Thumbnail;
            const int edge = BitmapMaximumEdge(&iterator->second);
            const bool supersededFull = sameView && !iterator->second.previewOnly && edge < bestFullEdge;
            const bool supersededPreview = sameView && iterator->second.previewOnly &&
                (edge < bestPreviewEdge || edge <= bestFullEdge);
            if (supersededFull || supersededPreview) {
                RemoveCacheBytes(iterator->second.cacheClass, iterator->second.bytes);
                UnindexBitmapSize(iterator->first);
                iterator = bitmapCache_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }


    std::chrono::milliseconds QuickSiftApplicationImpl::ViewResourceRetention() const {
        // View images are performance-critical. Do not shorten retention because of
        // CPU/RAM pressure; the retired runtime pressure governor caused visible reload churn.
        return std::chrono::minutes(30);
    }


    std::chrono::milliseconds QuickSiftApplicationImpl::RecentViewGracePeriod() const {
        const auto retention = ViewResourceRetention();
        if (retention <= std::chrono::milliseconds(0)) return std::chrono::milliseconds(0);
        const auto cap = systemProfile_.lowMemory ? std::chrono::seconds(10) :
            (systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte ?
                std::chrono::minutes(1) : std::chrono::minutes(2));
        return std::min(retention, std::chrono::duration_cast<std::chrono::milliseconds>(cap));
    }


    size_t QuickSiftApplicationImpl::RetainedWicImageSourceLimit() const {
        const auto retention = ViewResourceRetention();
        if (retention <= std::chrono::milliseconds(0)) return 1;
        if (systemProfile_.lowMemory && retention <= std::chrono::seconds(10)) return 1;
        if (systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte &&
            retention <= std::chrono::minutes(1)) return 2;
        if (systemProfile_.totalPhysicalBytes > 18ull * kGibibyte &&
            retention <= std::chrono::minutes(2)) return 4;
        if (systemProfile_.lowMemory) return 2;
        if (systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte) return 8;
        return systemProfile_.logicalProcessors >= 16 ? 16u : 12u;
    }


    bool QuickSiftApplicationImpl::PathHasDetectedFace(const std::wstring& path) const {
        const auto index = CatalogIndexForPath(path);
        return index && catalog_.PhotoAt(*index).facesScanned && !catalog_.PhotoAt(*index).faces.empty();
    }


    bool QuickSiftApplicationImpl::MemoryPressureActive() const {
        return false;
    }


    std::chrono::milliseconds QuickSiftApplicationImpl::ViewResourceRetentionForPath(
        const std::wstring& path, bool memoryPressure) const {
        const auto base = ViewResourceRetention();
        if (!reviewState_.FaceLockEnabled() || !PathHasDetectedFace(path) || memoryPressure ||
            base <= std::chrono::milliseconds(0)) return base;
        return std::min(base * 2,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(30)));
    }


    std::unordered_set<std::wstring> QuickSiftApplicationImpl::ViewResourceKeepSet() const {
        std::unordered_set<std::wstring> keep;
        if (reviewState_.Mode() == ViewMode::Compare) {
            keep.insert(reviewState_.ComparePaths().begin(), reviewState_.ComparePaths().end());
            return keep;
        }
        if (reviewState_.Mode() != ViewMode::Single || reviewState_.SinglePath().empty()) return keep;
        keep.insert(reviewState_.SinglePath());
        const int radius = systemProfile_.totalPhysicalBytes <= 18ull * kGibibyte ? 0 :
            (systemProfile_.totalPhysicalBytes <= 32ull * kGibibyte ? 1 : 2);
        if (radius <= 0) return keep;
        const auto current = IndexForPath(reviewState_.SinglePath());
        if (!current) return keep;
        for (int offset = -radius; offset <= radius; ++offset) {
            const std::int64_t candidate = static_cast<std::int64_t>(*current) + offset;
            if (candidate < 0 || candidate >= static_cast<std::int64_t>(catalog_.VisibleCount())) continue;
            keep.insert(VisiblePhoto(static_cast<size_t>(candidate)).path.wstring());
        }
        return keep;
    }


    void QuickSiftApplicationImpl::TrimInactiveViewResources(bool force ) {
        const auto keep = ViewResourceKeepSet();
        const auto now = std::chrono::steady_clock::now();
        const bool memoryPressure = !force && MemoryPressureActive();
        auto nextDelay = std::chrono::milliseconds::max();
        auto expired = [&](const std::wstring& path,
            const std::chrono::steady_clock::time_point& lastUseTime) {
            const auto retention = force ? std::chrono::milliseconds(0) :
                ViewResourceRetentionForPath(path, memoryPressure);
            if (retention <= std::chrono::milliseconds(0)) return true;
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUseTime);
            if (age >= retention) return true;
            nextDelay = std::min(nextDelay, retention - age);
            return false;
        };

        size_t released = 0;
        bool removedAny = false;
        for (auto iterator = bitmapCache_.begin(); iterator != bitmapCache_.end();) {
            if (iterator->first.cacheClass != CacheClass::Thumbnail &&
                !keep.contains(iterator->first.path) && expired(iterator->first.path, iterator->second.lastUseTime)) {
                released += iterator->second.bytes;
                removedAny = true;
                RemoveCacheBytes(iterator->second.cacheClass, iterator->second.bytes);
                UnindexBitmapSize(iterator->first);
                iterator = bitmapCache_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        for (auto iterator = tileCache_.begin(); iterator != tileCache_.end();) {
            if (!keep.contains(iterator->first.path) && expired(iterator->first.path, iterator->second.lastUseTime)) {
                released += iterator->second.bytes;
                removedAny = true;
                tileCacheBytes_ = iterator->second.bytes > tileCacheBytes_ ?
                    0 : tileCacheBytes_ - iterator->second.bytes;
                iterator = tileCache_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        for (auto iterator = wicImageSources_.begin(); iterator != wicImageSources_.end();) {
            if (!keep.contains(iterator->first) && expired(iterator->first, iterator->second.lastUseTime)) {
                removedAny = true;
                iterator = wicImageSources_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        for (auto iterator = displayedViewKeys_.begin(); iterator != displayedViewKeys_.end();) {
            if (!bitmapCache_.contains(iterator->second)) iterator = displayedViewKeys_.erase(iterator);
            else ++iterator;
        }
        if (removedAny) worker_.RequestDecoderSessionTrim();
        if (released >= (systemProfile_.lowMemory ? 16ull : 48ull) * kMebibyte) {
            if (renderTarget_) renderTarget_->Flush();
            if (d3dContext_) d3dContext_->Flush();
            if (dxgiDevice3_) dxgiDevice3_->Trim();
            UpdateGpuBudget();
        }

        if (!force && nextDelay != std::chrono::milliseconds::max()) {
            const auto bounded = std::clamp<std::int64_t>(nextDelay.count(), 1000,
                static_cast<std::int64_t>(std::numeric_limits<UINT>::max()));
            StartUiTimer(ID_TIMER_VIEW_RESOURCE_TRIM, static_cast<UINT>(bounded), L"view-resource trim");
        }
    }


    void QuickSiftApplicationImpl::ScheduleViewResourceTrim() {
        // Re-evaluate shortly after navigation, then TrimInactiveViewResources schedules
        // the exact next expiry. Cache byte ceilings still evict immediately when needed.
        StartUiTimer(ID_TIMER_VIEW_RESOURCE_TRIM, 1000u, L"view-resource trim");
    }


    void QuickSiftApplicationImpl::ClearBitmapCache() {
        decltype(bitmapCache_) emptyBitmaps;
        bitmapCache_.swap(emptyBitmaps);
        decltype(tileCache_) emptyTiles;
        tileCache_.swap(emptyTiles);
        decltype(failedDecodes_) emptyFailures;
        failedDecodes_.swap(emptyFailures);
        failedThumbnailPaths_.clear();
        thumbnailRetryAttempts_.clear();
        thumbnailRetryCooldown_.clear();
        thumbnailFailureRetryViewportEpochs_.clear();
        KillTimer(hwnd_, ID_TIMER_THUMBNAIL_RETRY_COOLDOWN);
        decltype(cacheFrequency_) emptyFrequency;
        cacheFrequency_.swap(emptyFrequency);
        decltype(displayedViewKeys_) emptyDisplayed;
        displayedViewKeys_.swap(emptyDisplayed);
        cacheFrequencySamples_ = 0;
        thumbnailCacheBytes_ = previewCacheBytes_ = fullCacheBytes_ = tileCacheBytes_ = 0;
    }


    bool QuickSiftApplicationImpl::IsProtectedBitmap(const CacheKey& key) const {
        if (key.cacheClass == CacheClass::Thumbnail) {
            return protectedThumbnailPaths_.contains(key.path);
        }
        return protectedViewPaths_.contains(key.path);
    }


    void QuickSiftApplicationImpl::EvictBitmapCache(bool forceProtected ,
        std::optional<CacheClass> incomingClass , size_t incomingBytes ) {
        auto baseLimitFor = [&](CacheClass cacheClass) -> size_t {
            switch (cacheClass) {
            case CacheClass::Thumbnail: return thumbnailCacheLimit_;
            case CacheClass::Preview: return previewCacheLimit_;
            case CacheClass::Full: return fullCacheLimit_;
            case CacheClass::Tile: return tileCacheLimit_;
            }
            return previewCacheLimit_;
        };
        auto limitFor = [&](CacheClass cacheClass) -> size_t {
            const size_t limit = baseLimitFor(cacheClass);
            if (!incomingClass || *incomingClass != cacheClass) return limit;
            return incomingBytes >= limit ? 0 : limit - incomingBytes;
        };
        auto bytesFor = [&](CacheClass cacheClass) -> size_t {
            switch (cacheClass) {
            case CacheClass::Thumbnail: return thumbnailCacheBytes_;
            case CacheClass::Preview: return previewCacheBytes_;
            case CacheClass::Full: return fullCacheBytes_;
            case CacheClass::Tile: return tileCacheBytes_;
            }
            return 0;
        };
        const size_t adjustedHardLimit = incomingBytes >= hardCacheLimit_ ?
            0 : hardCacheLimit_ - incomingBytes;
        const auto now = std::chrono::steady_clock::now();
        const auto recentGrace = forceProtected ? std::chrono::milliseconds(0) : RecentViewGracePeriod();
        auto isRecent = [&](const std::chrono::steady_clock::time_point& lastUseTime) {
            return recentGrace > std::chrono::milliseconds(0) && now - lastUseTime < recentGrace;
        };
        const bool classOver = thumbnailCacheBytes_ > limitFor(CacheClass::Thumbnail) ||
            previewCacheBytes_ > limitFor(CacheClass::Preview) ||
            fullCacheBytes_ > limitFor(CacheClass::Full) ||
            tileCacheBytes_ > limitFor(CacheClass::Tile);
        if (!classOver && TotalCacheBytes() <= adjustedHardLimit) return;

        try {
        using BitmapIterator = decltype(bitmapCache_)::iterator;
        struct BitmapCandidate {
            BitmapIterator iterator;
            uint64_t lastUse = 0;
            CacheClass cacheClass = CacheClass::Preview;
            bool frequent = false;
            bool facePriority = false;
            bool recent = false;
            bool erased = false;
        };
        using TileIterator = decltype(tileCache_)::iterator;
        struct TileCandidate {
            TileIterator iterator;
            uint64_t lastUse = 0;
            bool recent = false;
            bool erased = false;
        };

        std::vector<BitmapCandidate> bitmaps;
        bitmaps.reserve(bitmapCache_.size());
        for (auto it = bitmapCache_.begin(); it != bitmapCache_.end(); ++it) {
            if (!forceProtected && IsProtectedBitmap(it->first)) continue;
            const bool recent = it->first.cacheClass != CacheClass::Thumbnail &&
                isRecent(it->second.lastUseTime);
            bitmaps.push_back(BitmapCandidate{ it, it->second.lastUse, it->second.cacheClass,
                it->second.frequent, reviewState_.FaceLockEnabled() && PathHasDetectedFace(it->first.path), recent, false });
        }
        std::sort(bitmaps.begin(), bitmaps.end(), [](const BitmapCandidate& left, const BitmapCandidate& right) {
            if (left.recent != right.recent) return !left.recent;
            if (left.facePriority != right.facePriority) return !left.facePriority;
            if (left.frequent != right.frequent) return !left.frequent;
            return left.lastUse < right.lastUse;
        });

        std::vector<TileCandidate> tiles;
        tiles.reserve(tileCache_.size());
        for (auto it = tileCache_.begin(); it != tileCache_.end(); ++it) {
            tiles.push_back(TileCandidate{ it, it->second.lastUse, isRecent(it->second.lastUseTime), false });
        }
        std::sort(tiles.begin(), tiles.end(), [](const TileCandidate& left, const TileCandidate& right) {
            if (left.recent != right.recent) return !left.recent;
            return left.lastUse < right.lastUse;
        });

        for (CacheClass cacheClass : { CacheClass::Thumbnail, CacheClass::Preview, CacheClass::Full }) {
            if (bytesFor(cacheClass) <= limitFor(cacheClass)) continue;
            for (BitmapCandidate& candidate : bitmaps) {
                if (bytesFor(cacheClass) <= limitFor(cacheClass)) break;
                if (candidate.erased || candidate.cacheClass != cacheClass) continue;
                RemoveCacheBytes(candidate.iterator->second.cacheClass, candidate.iterator->second.bytes);
                UnindexBitmapSize(candidate.iterator->first);
                bitmapCache_.erase(candidate.iterator);
                candidate.erased = true;
            }
        }
        if (tileCacheBytes_ > limitFor(CacheClass::Tile)) {
            for (TileCandidate& candidate : tiles) {
                if (tileCacheBytes_ <= limitFor(CacheClass::Tile)) break;
                if (candidate.erased) continue;
                tileCacheBytes_ = candidate.iterator->second.bytes > tileCacheBytes_ ?
                    0 : tileCacheBytes_ - candidate.iterator->second.bytes;
                tileCache_.erase(candidate.iterator);
                candidate.erased = true;
            }
        }

        size_t bitmapIndex = 0;
        size_t tileIndex = 0;
        auto skipErased = [&] {
            while (bitmapIndex < bitmaps.size() && bitmaps[bitmapIndex].erased) ++bitmapIndex;
            while (tileIndex < tiles.size() && tiles[tileIndex].erased) ++tileIndex;
        };
        skipErased();
        while (TotalCacheBytes() > adjustedHardLimit) {
            if (bitmapIndex >= bitmaps.size() && tileIndex >= tiles.size()) break;
            const bool removeTile = tileIndex < tiles.size() &&
                (bitmapIndex >= bitmaps.size() ||
                    (tiles[tileIndex].recent != bitmaps[bitmapIndex].recent ? !tiles[tileIndex].recent :
                        tiles[tileIndex].lastUse < bitmaps[bitmapIndex].lastUse));
            if (removeTile) {
                TileCandidate& candidate = tiles[tileIndex];
                tileCacheBytes_ = candidate.iterator->second.bytes > tileCacheBytes_ ?
                    0 : tileCacheBytes_ - candidate.iterator->second.bytes;
                tileCache_.erase(candidate.iterator);
                candidate.erased = true;
            } else {
                BitmapCandidate& candidate = bitmaps[bitmapIndex];
                RemoveCacheBytes(candidate.iterator->second.cacheClass, candidate.iterator->second.bytes);
                UnindexBitmapSize(candidate.iterator->first);
                bitmapCache_.erase(candidate.iterator);
                candidate.erased = true;
            }
            skipErased();
        }
        return;
        } catch (...) {
            // Candidate arrays are an optimization. Under extreme allocation
            // pressure, fall back to the allocation-free LRU scan rather than
            // failing the UI thread while trying to free memory.
        }

        auto evictOneBitmap = [&](std::optional<CacheClass> onlyClass) -> bool {
            auto oldest = bitmapCache_.end();
            for (auto it = bitmapCache_.begin(); it != bitmapCache_.end(); ++it) {
                if (onlyClass && it->second.cacheClass != *onlyClass) continue;
                if (!forceProtected && IsProtectedBitmap(it->first)) continue;
                const bool candidateRecent = it->first.cacheClass != CacheClass::Thumbnail &&
                    isRecent(it->second.lastUseTime);
                const bool oldestRecent = oldest != bitmapCache_.end() &&
                    oldest->first.cacheClass != CacheClass::Thumbnail && isRecent(oldest->second.lastUseTime);
                const bool candidateFace = reviewState_.FaceLockEnabled() && PathHasDetectedFace(it->first.path);
                const bool oldestFace = oldest != bitmapCache_.end() && reviewState_.FaceLockEnabled() && PathHasDetectedFace(oldest->first.path);
                if (oldest == bitmapCache_.end() ||
                    (oldestRecent && !candidateRecent) ||
                    (oldestRecent == candidateRecent && oldestFace && !candidateFace) ||
                    (oldestRecent == candidateRecent && oldestFace == candidateFace && oldest->second.frequent && !it->second.frequent) ||
                    (oldestRecent == candidateRecent && oldestFace == candidateFace && oldest->second.frequent == it->second.frequent &&
                        it->second.lastUse < oldest->second.lastUse)) oldest = it;
            }
            if (oldest == bitmapCache_.end()) return false;
            RemoveCacheBytes(oldest->second.cacheClass, oldest->second.bytes);
            UnindexBitmapSize(oldest->first);
            bitmapCache_.erase(oldest);
            return true;
        };
        for (CacheClass cacheClass : { CacheClass::Thumbnail, CacheClass::Preview, CacheClass::Full }) {
            while (bytesFor(cacheClass) > limitFor(cacheClass) && evictOneBitmap(cacheClass)) {}
        }
        while (tileCacheBytes_ > limitFor(CacheClass::Tile) && !tileCache_.empty()) {
            auto oldest = tileCache_.begin();
            for (auto it = tileCache_.begin(); it != tileCache_.end(); ++it) {
                const bool candidateRecent = isRecent(it->second.lastUseTime);
                const bool oldestRecent = isRecent(oldest->second.lastUseTime);
                if ((oldestRecent && !candidateRecent) ||
                    (oldestRecent == candidateRecent && it->second.lastUse < oldest->second.lastUse)) oldest = it;
            }
            tileCacheBytes_ = oldest->second.bytes > tileCacheBytes_ ?
                0 : tileCacheBytes_ - oldest->second.bytes;
            tileCache_.erase(oldest);
        }
        while (TotalCacheBytes() > adjustedHardLimit) {
            uint64_t bitmapUse = UINT64_MAX, tileUse = UINT64_MAX;
            bool bitmapRecent = true, tileRecent = true;
            auto oldestBitmap = bitmapCache_.end();
            auto oldestTile = tileCache_.end();
            for (auto it = bitmapCache_.begin(); it != bitmapCache_.end(); ++it) {
                if (!forceProtected && IsProtectedBitmap(it->first)) continue;
                const bool recent = it->first.cacheClass != CacheClass::Thumbnail &&
                    isRecent(it->second.lastUseTime);
                if (oldestBitmap == bitmapCache_.end() || (bitmapRecent && !recent) ||
                    (bitmapRecent == recent && it->second.lastUse < bitmapUse)) {
                    bitmapUse = it->second.lastUse;
                    bitmapRecent = recent;
                    oldestBitmap = it;
                }
            }
            for (auto it = tileCache_.begin(); it != tileCache_.end(); ++it) {
                const bool recent = isRecent(it->second.lastUseTime);
                if (oldestTile == tileCache_.end() || (tileRecent && !recent) ||
                    (tileRecent == recent && it->second.lastUse < tileUse)) {
                    tileUse = it->second.lastUse;
                    tileRecent = recent;
                    oldestTile = it;
                }
            }
            if (oldestBitmap == bitmapCache_.end() && oldestTile == tileCache_.end()) break;
            if (oldestTile != tileCache_.end() &&
                (oldestBitmap == bitmapCache_.end() ||
                    (tileRecent != bitmapRecent ? !tileRecent : tileUse < bitmapUse))) {
                tileCacheBytes_ = oldestTile->second.bytes > tileCacheBytes_ ?
                    0 : tileCacheBytes_ - oldestTile->second.bytes;
                tileCache_.erase(oldestTile);
            } else if (oldestBitmap != bitmapCache_.end()) {
                RemoveCacheBytes(oldestBitmap->second.cacheClass, oldestBitmap->second.bytes);
                UnindexBitmapSize(oldestBitmap->first);
                bitmapCache_.erase(oldestBitmap);
            }
        }
    }


    void QuickSiftApplicationImpl::TrimCachesForMemoryPressure(bool forced ) {
        RefreshPerformancePolicy(forced);
        MEMORYSTATUSEX memory{ sizeof(memory) };
        if (!GlobalMemoryStatusEx(&memory)) return;
        const size_t mib = static_cast<size_t>(kMebibyte);
        const size_t available = static_cast<size_t>(std::min<ULONGLONG>(
            memory.ullAvailPhys, static_cast<ULONGLONG>(SIZE_MAX)));
        // Runtime pressure trimming is intentionally disabled. Forced cleanup is
        // retained as an explicit/emergency path only.
        if (!forced) return;
        const bool critical = available < 512ull * mib;
        worker_.TrimForMemoryPressure(critical);
        persistentCache_.TrimMemory(critical);
        // These are regeneration-only text/failure caches. Releasing them early is
        // cheaper than evicting an on-screen bitmap or letting Windows compress the
        // process working set. Undo history is intentionally never discarded here.
        decltype(exifCache_) emptyExif;
        exifCache_.swap(emptyExif);
        if (critical) {
            decltype(failedDecodes_) emptyFailures;
            failedDecodes_.swap(emptyFailures);
            // The blurred background is decorative and can be regenerated. Its
            // 4K-sized CPU surface is a better pressure victim than user photos.
            std::vector<std::uint32_t>().swap(backgroundPixels_);
            backgroundCacheWidth_ = backgroundCacheHeight_ = 0;
            backgroundSurfaceWidth_ = backgroundSurfaceHeight_ = 0;
            if (selection_.Empty()) selection_.Clear(true);
            ClearWicImageSources();
        } else {
            for (auto& [path, entry] : wicImageSources_) {
                if (entry.imageSource) entry.imageSource->TrimCache(nullptr);
                entry.hasTrim = false;
            }
        }

        // Normal pressure evicts only off-screen entries. Under genuinely critical
        // pressure, protected visible entries may also be released to avoid paging.
        const size_t normalHardLimit = hardCacheLimit_;
        const size_t minimumPressureCache = critical ? 16 * mib : 96 * mib;
        hardCacheLimit_ = std::min(normalHardLimit, std::clamp<size_t>(
            available / (critical ? 24 : 16), minimumPressureCache,
            critical ? 192 * mib : 256 * mib));
        EvictBitmapCache(critical);
        hardCacheLimit_ = normalHardLimit;
    }


    void QuickSiftApplicationImpl::OnResourcePressure(quicksift::ResourcePressureEvent event) {
        if (event == quicksift::ResourcePressureEvent::LowMemory) {
            RefreshPerformancePolicy(true);
            TrimCachesForMemoryPressure(true);
            TrimInactiveViewResources(true);
            OfferGraphicsResources(true);
        } else if (event == quicksift::ResourcePressureEvent::HighMemory) {
            RefreshPerformancePolicy(false);
            ConfigureCacheBudgets();
            OfferGraphicsResources(false);
        } else {
            UpdateGpuBudget();
            RefreshPerformancePolicy(false);
            EvictBitmapCache(false);
        }
    }


    int QuickSiftApplicationImpl::BucketFor(int desired) const {
        static constexpr int buckets[] = { 128, 256, 512, 1024, 2048, 4096, 8192, 12288 };
        for (int bucket : buckets) {
            if (bucket > systemProfile_.maximumDecodeBucket) break;
            if (desired <= bucket) return bucket;
        }
        return systemProfile_.maximumDecodeBucket;
    }


    int QuickSiftApplicationImpl::IntermediateViewDecodeTarget(int baseDesired,
        int finalDesired) const {
        static constexpr int buckets[] = { 128, 256, 512, 1024, 2048, 4096, 8192, 12288 };
        const int baseBucket = BucketFor(std::max(1, baseDesired));
        const int finalBucket = BucketFor(std::max(1, finalDesired));
        if (finalBucket <= baseBucket) return baseBucket;

        int baseIndex = 0;
        int finalIndex = 0;
        for (int index = 0; index < static_cast<int>(std::size(buckets)); ++index) {
            if (buckets[index] <= baseBucket) baseIndex = index;
            if (buckets[index] <= finalBucket) finalIndex = index;
        }
        if (finalIndex - baseIndex < 2) return baseBucket;
        const int middleIndex = baseIndex + (finalIndex - baseIndex + 1) / 2;
        return buckets[middleIndex];
    }


    int QuickSiftApplicationImpl::ThumbnailDecodeTarget() const {
        const float physicalEdge = static_cast<float>(reviewState_.Thumbnails().sizeDip) * CanvasPixelScale();
        const int desired = std::max(1, static_cast<int>(std::lround(
            physicalEdge * systemProfile_.thumbnailQualityScale)));
        return BucketFor(desired);
    }


    int QuickSiftApplicationImpl::BitmapMaximumEdge(const BitmapEntry* bitmap) {
        return bitmap ? std::max(bitmap->width, bitmap->height) : 0;
    }


    bool QuickSiftApplicationImpl::BitmapSatisfiesDisplay(const BitmapEntry* bitmap, int desiredEdge,
        bool requireFullQuality) {
        if (!bitmap || !bitmap->bitmap || bitmap->width <= 0 || bitmap->height <= 0) return false;
        if (requireFullQuality && bitmap->previewOnly) return false;
        const int edge = BitmapMaximumEdge(bitmap);
        const int threshold = std::max(1, desiredEdge - desiredEdge / 10);
        return edge >= threshold;
    }


    BitmapEntry* QuickSiftApplicationImpl::FindBitmapNoTouch(const fs::path& path, int desired, CacheClass preferredClass) const {
        const std::wstring keyPath = path.wstring();
        static constexpr int buckets[] = { 128, 256, 512, 1024, 2048, 4096, 8192, 12288 };
        static constexpr CacheClass classes[] = {
            CacheClass::Thumbnail, CacheClass::Preview, CacheClass::Full
        };

        BitmapEntry* bestAbove = nullptr;
        BitmapEntry* bestBelow = nullptr;
        int bestAboveSize = INT_MAX;
        int bestBelowSize = INT_MIN;
        int bestAboveClassDistance = INT_MAX;
        int bestBelowClassDistance = INT_MAX;
        for (int bucket : buckets) {
            for (CacheClass cacheClass : classes) {
                const CacheKey candidate{ keyPath, bucket, cacheClass };
                const auto it = bitmapCache_.find(candidate);
                if (it == bitmapCache_.end()) continue;
                const int classDistance = std::abs(static_cast<int>(cacheClass) -
                    static_cast<int>(preferredClass));
                if (bucket >= desired) {
                    if (bucket < bestAboveSize ||
                        (bucket == bestAboveSize && classDistance < bestAboveClassDistance) ||
                        (bucket == bestAboveSize && classDistance == bestAboveClassDistance &&
                            bestAbove && bestAbove->previewOnly && !it->second.previewOnly)) {
                        bestAbove = const_cast<BitmapEntry*>(&it->second);
                        bestAboveSize = bucket;
                        bestAboveClassDistance = classDistance;
                    }
                } else if (bucket > bestBelowSize ||
                    (bucket == bestBelowSize && classDistance < bestBelowClassDistance) ||
                    (bucket == bestBelowSize && classDistance == bestBelowClassDistance &&
                        bestBelow && bestBelow->previewOnly && !it->second.previewOnly)) {
                    bestBelow = const_cast<BitmapEntry*>(&it->second);
                    bestBelowSize = bucket;
                    bestBelowClassDistance = classDistance;
                }
            }
        }
        return bestAbove ? bestAbove : bestBelow;
    }


    BitmapEntry* QuickSiftApplicationImpl::FindBitmap(const fs::path& path, int desired, CacheClass preferredClass) {
        const std::wstring keyPath = path.wstring();
        static constexpr int buckets[] = { 128, 256, 512, 1024, 2048, 4096, 8192, 12288 };
        static constexpr CacheClass classes[] = {
            CacheClass::Thumbnail, CacheClass::Preview, CacheClass::Full
        };

        BitmapEntry* bestAbove = nullptr;
        BitmapEntry* bestBelow = nullptr;
        const CacheKey* bestAboveKey = nullptr;
        const CacheKey* bestBelowKey = nullptr;
        int bestAboveSize = INT_MAX;
        int bestBelowSize = INT_MIN;
        int bestAboveClassDistance = INT_MAX;
        int bestBelowClassDistance = INT_MAX;
        for (int bucket : buckets) {
            for (CacheClass cacheClass : classes) {
                const CacheKey candidate{ keyPath, bucket, cacheClass };
                const auto it = bitmapCache_.find(candidate);
                if (it == bitmapCache_.end()) continue;
                const int classDistance = std::abs(static_cast<int>(cacheClass) -
                    static_cast<int>(preferredClass));
                if (bucket >= desired) {
                    if (bucket < bestAboveSize ||
                        (bucket == bestAboveSize && classDistance < bestAboveClassDistance) ||
                        (bucket == bestAboveSize && classDistance == bestAboveClassDistance &&
                            bestAbove && bestAbove->previewOnly && !it->second.previewOnly)) {
                        bestAbove = &it->second;
                        bestAboveKey = &it->first;
                        bestAboveSize = bucket;
                        bestAboveClassDistance = classDistance;
                    }
                } else if (bucket > bestBelowSize ||
                    (bucket == bestBelowSize && classDistance < bestBelowClassDistance) ||
                    (bucket == bestBelowSize && classDistance == bestBelowClassDistance &&
                        bestBelow && bestBelow->previewOnly && !it->second.previewOnly)) {
                    bestBelow = &it->second;
                    bestBelowKey = &it->first;
                    bestBelowSize = bucket;
                    bestBelowClassDistance = classDistance;
                }
            }
        }
        BitmapEntry* best = bestAbove ? bestAbove : bestBelow;
        const CacheKey* bestKey = bestAbove ? bestAboveKey : bestBelowKey;
        if (best && bestKey) TouchBitmapEntry(*bestKey, *best);
        return best;
    }



    ComPtr<ID2D1Bitmap> QuickSiftApplicationImpl::GetOrCreateShellFallback(const fs::path& path, int targetSize) {
        const std::wstring key = path.wstring();
        const auto now = std::chrono::steady_clock::now();
        if (shellFallbackLastUse_ != std::chrono::steady_clock::time_point{} &&
            now - shellFallbackLastUse_ > std::chrono::seconds(5)) shellFallbackCache_.clear();
        shellFallbackLastUse_ = now;
        if (auto it = shellFallbackCache_.find(key); it != shellFallbackCache_.end()) return it->second;
        ComPtr<IShellItem> item;
        if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) return nullptr;
        ComPtr<IShellItemImageFactory> factory;
        if (FAILED(item.As(&factory))) return nullptr;
        HBITMAP hbmp = nullptr;
        const SIZE size{ std::max(16, targetSize), std::max(16, targetSize) };
        if (FAILED(factory->GetImage(size, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &hbmp)) || !hbmp) return nullptr;
        ComPtr<IWICBitmap> wic;
        if (!uiWicFactory_ || FAILED(uiWicFactory_->CreateBitmapFromHBITMAP(hbmp, nullptr, WICBitmapUsePremultipliedAlpha, &wic))) { DeleteObject(hbmp); return nullptr; }
        DeleteObject(hbmp);
        ComPtr<ID2D1Bitmap> bitmap;
        if (!renderTarget_ || FAILED(renderTarget_->CreateBitmapFromWicBitmap(wic.Get(), nullptr, &bitmap))) return nullptr;
        shellFallbackCache_[key] = bitmap;
        return bitmap;
    }


    BitmapEntry* QuickSiftApplicationImpl::FindBestViewBitmap(const fs::path& path, int desiredEdge,
        CacheClass preferredClass) {
        const std::wstring keyPath = path.wstring();
        const auto sizesIt = pathToBitmapSizes_.find(keyPath);
        if (sizesIt == pathToBitmapSizes_.end() || sizesIt->second.empty()) return nullptr;

        const int threshold = std::max(1, desiredEdge - desiredEdge / 10);
        auto chooseAtSize = [&](int size) -> std::pair<BitmapEntry*, const CacheKey*> {
            static constexpr CacheClass classes[] = { CacheClass::Full, CacheClass::Preview, CacheClass::Thumbnail };
            BitmapEntry* best = nullptr; const CacheKey* bestKey = nullptr;
            int bestDistance = INT_MAX; int bestTier = -1;
            for (CacheClass c : classes) {
                CacheKey key{ keyPath, size, c };
                auto it = bitmapCache_.find(key);
                if (it == bitmapCache_.end() || !it->second.bitmap) continue;
                const int tier = c == CacheClass::Thumbnail ? 0 : (it->second.previewOnly ? 1 : 2);
                const int distance = std::abs(static_cast<int>(c) - static_cast<int>(preferredClass));
                if (!best || tier > bestTier || (tier == bestTier && distance < bestDistance)) {
                    best = &it->second; bestKey = &it->first; bestTier = tier; bestDistance = distance;
                }
            }
            return {best, bestKey};
        };
        const auto lb = sizesIt->second.lower_bound(threshold);
        std::vector<int> candidates;
        if (lb != sizesIt->second.end()) candidates.push_back(*lb);
        if (lb != sizesIt->second.begin()) candidates.push_back(*std::prev(lb));
        if (candidates.empty()) candidates.push_back(*sizesIt->second.rbegin());
        BitmapEntry* best = nullptr; const CacheKey* bestKey = nullptr; int bestScore = INT_MIN;
        for (int size : candidates) {
            auto [entry, key] = chooseAtSize(size); if (!entry) continue;
            const int edge = BitmapMaximumEdge(entry); if (edge <= 0) continue;
            const int score = (edge >= threshold ? 100000 : 0) + edge;
            if (score > bestScore) { best=entry; bestKey=key; bestScore=score; }
        }
        if (best && bestKey) TouchBitmapEntry(*bestKey, *best);
        return best;
    }

    bool QuickSiftApplicationImpl::SupportsModernWicImageSource(const fs::path& path) const {
        if (!deviceContext2_ || !systemProfile_.enableModernImageSources) return false;
        const std::wstring extension = ExtensionLower(path);
        if (IsRawExtension(extension) || extension == L".webp" || extension == L".avif") return false;
        return extension == L".jpg" || extension == L".jpeg" || extension == L".jpe" ||
            extension == L".png" || extension == L".tif" || extension == L".tiff" ||
            extension == L".bmp" || extension == L".gif" || extension == L".jxr" ||
            extension == L".wdp" || extension == L".heic" || extension == L".heif";
    }


    WicImageSourceEntry* QuickSiftApplicationImpl::GetOrCreateWicImageSource(const fs::path& path) {
        if (!SupportsModernWicImageSource(path)) return nullptr;
        const std::wstring key = path.wstring();
        const std::uint64_t identity = WicFileIdentity(path);
        if (identity == 0) return nullptr;
        if (auto existing = wicImageSources_.find(key); existing != wicImageSources_.end()) {
            if (existing->second.identity == identity && existing->second.imageSource) {
                existing->second.lastUse = ++useCounter_;
                existing->second.lastUseTime = std::chrono::steady_clock::now();
                if (existing->second.offered) {
                    BOOL discarded = FALSE;
                    if (SUCCEEDED(existing->second.imageSource->TryReclaimResources(&discarded))) {
                        existing->second.offered = false;
                        if (discarded) existing->second.hasTrim = false;
                    }
                }
                return &existing->second;
            }
            wicImageSources_.erase(existing);
        }

        try {
            if (!uiWicFactory_) {
                if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&uiWicFactory_)))) return nullptr;
            }
            WicImageSourceEntry entry;
            if (FAILED(uiWicFactory_->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, &entry.decoder)) ||
                FAILED(entry.decoder->GetFrame(0, &entry.frame))) return nullptr;
            // The Direct2D image source preserves native JPEG Y/Cb/Cr planes and tiles
            // only when it receives the original frame. Oriented source files stay on
            // the existing transform path until orientation-aware source composition is
            // available, avoiding silent rotation errors.
            if (ReadOrientationTransform(entry.frame.Get(), path) != WICBitmapTransformRotate0) return nullptr;
            if (FAILED(entry.frame->GetSize(&entry.width, &entry.height)) ||
                entry.width == 0 || entry.height == 0) return nullptr;
            if (FAILED(deviceContext2_->CreateImageSourceFromWic(entry.frame.Get(),
                D2D1_IMAGE_SOURCE_LOADING_OPTIONS_CACHE_ON_DEMAND, &entry.imageSource))) return nullptr;
            entry.identity = identity;
            entry.lastUse = ++useCounter_;
            entry.lastUseTime = std::chrono::steady_clock::now();

            const size_t maximumEntries = RetainedWicImageSourceLimit();
            while (wicImageSources_.size() >= maximumEntries && !wicImageSources_.empty()) {
                auto victim = wicImageSources_.end();
                for (auto iterator = wicImageSources_.begin(); iterator != wicImageSources_.end(); ++iterator) {
                    if (protectedViewPaths_.contains(iterator->first)) continue;
                    if (victim == wicImageSources_.end() || iterator->second.lastUse < victim->second.lastUse)
                        victim = iterator;
                }
                if (victim == wicImageSources_.end()) break;
                wicImageSources_.erase(victim);
            }
            auto [iterator, inserted] = wicImageSources_.insert_or_assign(key, std::move(entry));
            (void)inserted;
            return &iterator->second;
        } catch (...) {
            return nullptr;
        }
    }


    bool QuickSiftApplicationImpl::DrawWicImageSourceFit(WicImageSourceEntry* entry,
        D2D1_RECT_F area, const quicksift::review::transform::ResolvedView& view, bool clip ) {
        if (!entry || !entry->imageSource || !deviceContext2_ || entry->width == 0 ||
            entry->height == 0 || !view.valid) return false;
        const float scale = view.scaleDip;
        const float drawWidth = static_cast<float>(entry->width) * scale;
        const float drawHeight = static_cast<float>(entry->height) * scale;
        const float areaCenterX = (area.left + area.right) * 0.5f;
        const float areaCenterY = (area.top + area.bottom) * 0.5f;
        const float left = areaCenterX - view.state.centerX * drawWidth;
        const float top = areaCenterY - view.state.centerY * drawHeight;

        if (clip) deviceContext2_->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        D2D1_MATRIX_3X2_F previous{};
        deviceContext2_->GetTransform(&previous);
        D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Scale(scale, scale) *
            D2D1::Matrix3x2F::Translation(left, top);
        if (view.state.rotation != 0) {
            transform = transform * D2D1::Matrix3x2F::Rotation(
                static_cast<float>(view.state.rotation),
                D2D1::Point2F(areaCenterX, areaCenterY));
        }
        deviceContext2_->SetTransform(transform * previous);
        const D2D1_INTERPOLATION_MODE interpolation = IsActivelyInteracting() ?
            D2D1_INTERPOLATION_MODE_LINEAR : D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
        const D2D1_POINT_2F targetOffset = D2D1::Point2F(0.0f, 0.0f);
        deviceContext2_->DrawImage(entry->imageSource.Get(), &targetOffset, nullptr,
            interpolation, D2D1_COMPOSITE_MODE_SOURCE_OVER);
        deviceContext2_->SetTransform(previous);
        if (clip) deviceContext2_->PopAxisAlignedClip();

        if (view.state.rotation == 0) {
            auto clampCoordinate = [](float value, UINT maximum, bool upper) -> UINT {
                if (!std::isfinite(value) || value <= 0.0f) return 0;
                if (value >= static_cast<float>(maximum)) return maximum;
                return static_cast<UINT>(upper ? std::ceil(value) : std::floor(value));
            };
            D2D1_RECT_U visible{
                clampCoordinate((area.left - left) / scale, entry->width, false),
                clampCoordinate((area.top - top) / scale, entry->height, false),
                clampCoordinate((area.right - left) / scale, entry->width, true),
                clampCoordinate((area.bottom - top) / scale, entry->height, true)
            };
            visible.right = std::max(visible.left +
                (visible.left < entry->width ? 1u : 0u), visible.right);
            visible.bottom = std::max(visible.top +
                (visible.top < entry->height ? 1u : 0u), visible.bottom);
            visible.right = std::min(visible.right, entry->width);
            visible.bottom = std::min(visible.bottom, entry->height);
            const auto materiallyDifferent = [&](const D2D1_RECT_U& previousRect) {
                constexpr UINT threshold = 64;
                return !entry->hasTrim ||
                    (visible.left > previousRect.left ? visible.left - previousRect.left : previousRect.left - visible.left) > threshold ||
                    (visible.top > previousRect.top ? visible.top - previousRect.top : previousRect.top - visible.top) > threshold ||
                    (visible.right > previousRect.right ? visible.right - previousRect.right : previousRect.right - visible.right) > threshold ||
                    (visible.bottom > previousRect.bottom ? visible.bottom - previousRect.bottom : previousRect.bottom - visible.bottom) > threshold;
            };
            if (visible.right > visible.left && visible.bottom > visible.top &&
                materiallyDifferent(entry->lastTrim)) {
                entry->imageSource->TrimCache(&visible);
                entry->lastTrim = visible;
                entry->hasTrim = true;
            }
        }
        entry->lastUse = ++useCounter_;
        entry->lastUseTime = std::chrono::steady_clock::now();
        return true;
    }


    BitmapEntry* QuickSiftApplicationImpl::GetOrRequestBitmap(const fs::path& path, int desired, JobPriority priority, bool requestFull,
        CacheClass cacheClass, uint64_t viewportEpoch, bool ignoreThumbnailViewport) {
        const bool raw = IsRawExtension(ExtensionLower(path));
        const bool rawJpegOnly = raw && loadOnlyRawJpegPreviews_;
        if (rawJpegOnly) requestFull = false;
        // Thumbnails have a strict grid target. Embedded previews can report a tiny
        // source dimension (for example 160 px), which must not clamp the requested
        // thumbnail bucket and cause an immediate cancellation/retry loop.
        if (cacheClass != CacheClass::Thumbnail && !requestFull) {
            if (const auto photo = IndexForPath(path.wstring()); photo) {
                const PhotoItem& item = VisiblePhoto(*photo);
                if (item.sourceWidth > 0 && item.sourceHeight > 0) {
                    desired = std::min(desired, std::max(item.sourceWidth, item.sourceHeight));
                }
            }
        }
        const int desiredEdge = std::max(1, desired);
        const int bucket = BucketFor(desiredEdge);
        const CacheClass requestedClass = requestFull ? (bucket >= 6144 ? CacheClass::Full : cacheClass) : cacheClass;
        const CacheKey exact{ path.wstring(), bucket, requestedClass };
        const auto exactIt = bitmapCache_.find(exact);
        const bool gridThumbnailRequest = !requestFull && cacheClass == CacheClass::Thumbnail;
        BitmapEntry* existing = nullptr;
        if (gridThumbnailRequest && exactIt != bitmapCache_.end()) {
            TouchBitmapEntry(exactIt->first, exactIt->second);
            existing = &exactIt->second;
        } else if (gridThumbnailRequest) {
            existing = FindBitmap(path, bucket, requestedClass);
        } else {
            existing = FindBestViewBitmap(path, desiredEdge, requestedClass);
        }
        const uint64_t epoch = (requestFull || cacheClass != CacheClass::Thumbnail) ? reviewState_.Epochs().navigation : 0;
        const uint64_t thumbnailViewportEpoch = viewportEpoch ? viewportEpoch : reviewState_.Epochs().thumbnailViewport;

        if (gridThumbnailRequest && (ThumbnailScrollPrefersPlaceholders() ||
            thumbnailScrollMotionActive_ || thumbnailScrollDragging_)) {
            // Virtualized grid: paint never enqueues. Decode admission belongs to the
            // scroll frame / settle path so flings cannot build a paint-driven backlog.
            return existing;
        }

        if (gridThumbnailRequest) {
            const std::wstring thumbnailRetryKey = NormalizedPathKey(path);
            if (failedThumbnailPaths_.contains(thumbnailRetryKey)) return existing;
            const bool resolutionSatisfied = BitmapSatisfiesDisplay(existing, desiredEdge, false);
            if (exactIt == bitmapCache_.end()) {
                const std::wstring previewFailed = DecodeFailureKey(path.wstring(), bucket, JobKind::DecodePreview, gridThumbnailRequest ? CacheClass::Thumbnail : cacheClass);
                if (!HasRecentDecodeFailure(previewFailed))
                    worker_.EnqueuePreview(path, bucket, generation_, priority, CacheClass::Thumbnail, epoch,
                        thumbnailViewportEpoch, 0, false, 0, ignoreThumbnailViewport);
            } else if (!resolutionSatisfied && !rawJpegOnly) {
                // Keep an undersized embedded preview as an immediate stand-in, but
                // never let its bucket identity suppress the real target-sized decode
                // unless the user explicitly forbids RAW decoding.
                const std::wstring fullFailed = DecodeFailureKey(path.wstring(), bucket, JobKind::DecodeFull, gridThumbnailRequest ? CacheClass::Thumbnail : requestedClass);
                if (!HasRecentDecodeFailure(fullFailed))
                    worker_.EnqueueFull(path, bucket, generation_, priority, CacheClass::Thumbnail, epoch,
                        thumbnailViewportEpoch);
            }
        } else if (raw) {
            const CacheKey previewKey{ path.wstring(), bucket, cacheClass };
            const auto previewIt = bitmapCache_.find(previewKey);
            const bool previewSatisfied = BitmapSatisfiesDisplay(existing, desiredEdge, false);
            const std::wstring previewFailed = DecodeFailureKey(path.wstring(), bucket, JobKind::DecodePreview, gridThumbnailRequest ? CacheClass::Thumbnail : cacheClass);
            if (previewIt == bitmapCache_.end() && !previewSatisfied && !HasRecentDecodeFailure(previewFailed)) {
                worker_.EnqueuePreview(path, bucket, generation_, priority, cacheClass, epoch);
            }
            const bool fullSatisfied = BitmapSatisfiesDisplay(existing, desiredEdge, true);
            if (requestFull && !fullSatisfied) {
                const std::wstring fullFailed = DecodeFailureKey(path.wstring(), bucket, JobKind::DecodeFull, gridThumbnailRequest ? CacheClass::Thumbnail : requestedClass);
                if (!HasRecentDecodeFailure(fullFailed))
                    worker_.EnqueueFull(path, bucket, generation_, priority, requestedClass, epoch, 0, false,
                        reviewState_.Mode() == ViewMode::Single && requestedClass == CacheClass::Full,
                        viewInteractionEpoch_.load(std::memory_order_acquire));
            }
        } else if (exactIt == bitmapCache_.end() &&
            !BitmapSatisfiesDisplay(existing, desiredEdge, requestFull)) {
            const std::wstring fullFailed = DecodeFailureKey(path.wstring(), bucket, JobKind::DecodeFull, gridThumbnailRequest ? CacheClass::Thumbnail : requestedClass);
            if (!HasRecentDecodeFailure(fullFailed))
                worker_.EnqueueFull(path, bucket, generation_, priority, requestedClass, epoch);
        }
        return existing;
    }


    BitmapEntry* QuickSiftApplicationImpl::GetOrRequestIntermediateViewBitmap(
        const fs::path& path, int desired, JobPriority priority) {
        if (loadOnlyRawJpegPreviews_ && IsRawExtension(ExtensionLower(path))) {
            return GetOrRequestBitmap(path, desired, priority, false, CacheClass::Preview);
        }
        if (const auto photo = IndexForPath(path.wstring()); photo) {
            const PhotoItem& item = VisiblePhoto(*photo);
            if (item.sourceWidth > 0 && item.sourceHeight > 0)
                desired = std::min(desired, std::max(item.sourceWidth, item.sourceHeight));
        }
        const int desiredEdge = std::max(1, desired);
        const int bucket = BucketFor(desiredEdge);
        BitmapEntry* existing = FindBestViewBitmap(path, desiredEdge, CacheClass::Preview);
        if (!BitmapSatisfiesDisplay(existing, desiredEdge, true)) {
            const std::wstring failed = DecodeFailureKey(path.wstring(), bucket, JobKind::DecodeFull, CacheClass::Preview);
            if (!HasRecentDecodeFailure(failed)) {
                // This is deliberately DecodeFull in the Preview cache lane. An
                // embedded-thumbnail decode can return the same tiny stand-in,
                // while this request guarantees a real scaled middle-resolution frame.
                worker_.EnqueueFull(path, bucket, generation_, priority,
                    CacheClass::Preview, reviewState_.Epochs().navigation);
            }
        }
        return existing;
    }

} // namespace quicksift::app
