#include <borealis/platforms/driver/d3d11.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/util.hpp>
#define NANOVG_D3D11_IMPLEMENTATION
#include <nanovg_d3d11.h>
#include <versionhelpers.h>
#if defined(BOREALIS_USE_WINRT_COMPOSITION)
#include <chrono>
#endif
#if defined(__ALLOW_TEARING__) || defined(BOREALIS_USE_WINRT_COMPOSITION)
#include <dxgi1_6.h>
#endif
#ifdef __GLFW__
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#elif defined(__SDL2__)
#include <SDL_syswm.h>
#endif
#ifdef __WINRT__
#include <windows.ui.core.h>
#include <winrt/Windows.Graphics.Display.h>
#endif
#ifdef BOREALIS_USE_WINRT_COMPOSITION
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Core.h>
#endif

namespace brls
{

static const int SwapChainBufferCount    = 2;
static const DXGI_SAMPLE_DESC sampleDesc = { 1, 0 };

#ifdef __GLFW__
D3D11Context::D3D11Context(GLFWwindow* window, int width, int height)
{
    this->hWnd = glfwGetWin32Window(window);
    this->initDX(this->hWnd, nullptr, width, height);
}
#elif defined(__WINRT_NEW__)
D3D11Context::D3D11Context(void* window, int width, int height, bool enableTearing)
{
    this->interval = enableTearing ? 0 : 1;
    this->initDX(nullptr, static_cast<IUnknown*>(window), width, height);
}
#elif defined(__SDL2__)
D3D11Context::D3D11Context(SDL_Window* window, int width, int height)
{
    SDL_SysWMinfo wi;
    SDL_GetVersion(&wi.version);
    SDL_GetWindowWMInfo(window, &wi);
#ifdef __WINRT__
    // winrt 代码需要特别编译
    ABI::Windows::UI::Core::ICoreWindow* coreWindow = nullptr;
    if (FAILED(wi.info.winrt.window->QueryInterface(&coreWindow)))
    {
        return;
    }
    this->initDX(nullptr, coreWindow, width, height);
    coreWindow->Release();
#else
    this->hWnd = wi.info.win.window;
    this->initDX(this->hWnd, nullptr, width, height);
#endif
}
#endif

D3D11Context::~D3D11Context()
{
    this->unInitDX();
}

bool D3D11Context::initDX(HWND hWnd, IUnknown* coreWindow, int width, int height)
{
    HRESULT hr = S_OK;

    IDXGIDevice* dxgiDevice    = nullptr;
    IDXGIAdapter* dxgiAdapter  = nullptr;
    IDXGIFactory2* dxgiFactory = nullptr;

    static const D3D_DRIVER_TYPE driverAttempts[] = {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };

    static const D3D_FEATURE_LEVEL levelAttempts[] = {
        D3D_FEATURE_LEVEL_11_1, // Direct3D 11.1 SM 6
        D3D_FEATURE_LEVEL_11_0, // Direct3D 11.0 SM 5
        D3D_FEATURE_LEVEL_10_1, // Direct3D 10.1 SM 4
        D3D_FEATURE_LEVEL_10_0, // Direct3D 10.0 SM 4
        D3D_FEATURE_LEVEL_9_3, // Direct3D 9.3  SM 3
        D3D_FEATURE_LEVEL_9_2, // Direct3D 9.2  SM 2
        D3D_FEATURE_LEVEL_9_1, // Direct3D 9.1  SM 2
    };

    for (size_t driver = 0; driver < ARRAYSIZE(driverAttempts); driver++)
    {
        hr = D3D11CreateDevice(
            nullptr,
            driverAttempts[driver],
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levelAttempts,
            ARRAYSIZE(levelAttempts),
            D3D11_SDK_VERSION,
            &this->device,
            nullptr,
            &this->deviceContext);

        if (SUCCEEDED(hr))
        {
            break;
        }
    }
    if (SUCCEEDED(hr))
    {
        hr = this->device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    }
    if (SUCCEEDED(hr))
    {
        hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    }
    if (SUCCEEDED(hr))
    {
        hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    }
#ifdef BOREALIS_USE_WINRT_COMPOSITION
    if (SUCCEEDED(hr))
    {
        IDXGIFactory5* featureFactory = nullptr;
        const auto factoryResult = dxgiFactory->QueryInterface(IID_PPV_ARGS(&featureFactory));
        BOOL allowTearing = FALSE;
        const auto featureResult = SUCCEEDED(factoryResult)
            ? featureFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))
            : factoryResult;
        Logger::info("Xbox DXGI tearing capability: factory={:#010x}, query={:#010x}, supported={}",
            static_cast<unsigned>(factoryResult), static_cast<unsigned>(featureResult),
            SUCCEEDED(featureResult) && allowTearing != FALSE);
        compositionSwapChainFlags = SUCCEEDED(featureResult) && allowTearing
            ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        D3D_API_RELEASE(featureFactory);
    }
#endif

    if (SUCCEEDED(hr))
    {
        DXGI_SWAP_CHAIN_DESC1 swapDesc;
        ZeroMemory(&swapDesc, sizeof(swapDesc));
        swapDesc.SampleDesc.Count   = sampleDesc.Count;
        swapDesc.SampleDesc.Quality = sampleDesc.Quality;
        swapDesc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.Stereo             = FALSE;
        swapDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount        = SwapChainBufferCount;
        swapDesc.Flags              = 0;
        swapDesc.Scaling            = DXGI_SCALING_STRETCH;
#ifdef __WINRT__
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
#else
        if (IsWindows10OrGreater()) {
            swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        } else {
            swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        }
#endif
#ifdef __WINRT__
        swapDesc.Scaling = DXGI_SCALING_STRETCH;
#endif
#if WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP
        swapDesc.Scaling    = DXGI_SCALING_STRETCH;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
#endif
        // this->sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
#ifdef BOREALIS_USE_WINRT_COMPOSITION
        hr = createPresentationSwapChain(dxgiFactory, width, height, false);
#else
        if (coreWindow)
        {
            hr = dxgiFactory->CreateSwapChainForCoreWindow(
                this->device,
                coreWindow,
                &swapDesc,
                nullptr,
                &this->swapChain);
        }
        else
        {
            hr = dxgiFactory->CreateSwapChainForHwnd(
                this->device,
                hWnd,
                &swapDesc,
                nullptr,
                nullptr,
                &this->swapChain);
        }
#endif
    }
    D3D_API_RELEASE(dxgiDevice);
    D3D_API_RELEASE(dxgiAdapter);
    D3D_API_RELEASE(dxgiFactory);

    if (FAILED(hr))
    {
        Logger::error("Failed init D3D11 {:#010x}", hr);
        this->unInitDX();

        char errorText[256] = { 0 };
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      nullptr,
                      hr,
                      0,
                      errorText,
                      sizeof(errorText),
                      nullptr);
#ifndef __WINRT__
        MessageBoxA(hWnd, errorText, "Init D3D11 Failed", MB_ICONERROR);
#endif
        brls::fatal("Init D3D11 Failed");
        return false;
    }

#ifndef __WINRT__
    this->GetDpiForWindow = (UINT(WINAPI*)(HWND))GetProcAddress(GetModuleHandleW(L"USER32.DLL"), "GetDpiForWindow");
#endif

    return true;
}

#ifdef BOREALIS_USE_WINRT_COMPOSITION
HRESULT D3D11Context::createPresentationSwapChain(IDXGIFactory2* factory, int width, int height, bool transparent)
{
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = SwapChainBufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // On Xbox, a permanently transparent UI is composed at about 60 Hz even
    // on a 120 Hz output. Use an opaque surface unless native video is below it.
    desc.AlphaMode = transparent ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = compositionSwapChainFlags;
    const auto create = [&] {
        return factory->CreateSwapChainForComposition(device, &desc, nullptr, &swapChain);
    };
    auto hr = create();
    if (FAILED(hr) && desc.Flags) {
        Logger::warning("D3D11 tearing swap chain rejected: {:#010x}; using default presentation", static_cast<unsigned>(hr));
        compositionSwapChainFlags = desc.Flags = 0;
        hr = create();
    }
    Logger::info("Xbox UI presentation mode: transparent={}, size={}x{}, result={:#010x}",
        transparent, width, height, static_cast<unsigned>(hr));
    if (FAILED(hr)) return hr;
    useTearingPresent = compositionSwapChainFlags != 0;
    previousSyncQpc = 0;
    presentationRefreshRate = 0;
    try {
        if (!compositor) {
            compositor = winrt::Windows::UI::Composition::Compositor();
            compositionTarget = compositor.CreateTargetForCurrentView();
            compositionRoot = compositor.CreateContainerVisual();
            uiVisual = compositor.CreateSpriteVisual();
            compositionRoot.Children().InsertAtTop(uiVisual);
            compositionTarget.Root(compositionRoot);
        }
        winrt::Windows::UI::Composition::ICompositionSurface surface{nullptr};
        winrt::check_hresult(compositor.as<ABI::Windows::UI::Composition::ICompositorInterop>()
            ->CreateCompositionSurfaceForSwapChain(swapChain,
                reinterpret_cast<ABI::Windows::UI::Composition::ICompositionSurface**>(winrt::put_abi(surface))));
        uiVisual.Brush(compositor.CreateSurfaceBrush(surface));
        uiVisual.Size({static_cast<float>(width), static_cast<float>(height)});
        transparentPresentation = transparent;
    } catch (const winrt::hresult_error& error) {
        hr = error.code();
        Logger::error("D3D11 composition visual: {}", winrt::to_string(error.message()));
    }
    return hr;
}

void D3D11Context::releaseSwapChain()
{
    if (deviceContext) {
        deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
        deviceContext->ClearState();
        deviceContext->Flush();
    }
    D3D_API_RELEASE(renderTargetView);
    D3D_API_RELEASE(depthStencilView);
    // Keep the target and shared visual root alive across mode switches.
    // Rebinding the Xbox CoreWindow after closing its target is not reliable.
    if (uiVisual) uiVisual.Brush(nullptr);
    D3D_API_RELEASE(swapChain);
    if (deviceContext) deviceContext->Flush();
}

bool D3D11Context::setTransparentPresentation(bool enabled)
{
    if (transparentPresentation == enabled) return true;
    DXGI_SWAP_CHAIN_DESC1 previous{};
    if (!swapChain || FAILED(swapChain->GetDesc1(&previous))) return false;
    winrt::com_ptr<IDXGIDevice> dxgi;
    winrt::com_ptr<IDXGIAdapter> adapter;
    winrt::com_ptr<IDXGIFactory2> factory;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), dxgi.put_void())) ||
        FAILED(dxgi->GetAdapter(adapter.put())) ||
        FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()))) return false;
    const bool oldMode = transparentPresentation;
    const auto open = [&](bool transparent) {
        const auto hr = createPresentationSwapChain(factory.get(), previous.Width, previous.Height, transparent);
        return SUCCEEDED(hr) && onFramebufferSize(previous.Width, previous.Height, true);
    };
    releaseSwapChain();
    if (open(enabled)) return true;
    releaseSwapChain();
    if (!open(oldMode)) brls::fatal("Unable to restore Xbox presentation");
    return false;
}
#endif

void D3D11Context::unInitDX()
{
#ifdef BOREALIS_USE_WINRT_COMPOSITION
    releaseSwapChain();
    if (compositionTarget) compositionTarget.Close();
    uiVisual = nullptr;
    compositionRoot = nullptr;
    compositionTarget = nullptr;
    compositor = nullptr;
#endif
    // Detach RTs
    if (this->deviceContext)
    {
        ID3D11RenderTargetView* viewList[1] = { nullptr };
        this->deviceContext->OMSetRenderTargets(1, viewList, nullptr);
    }
    D3D_API_RELEASE(this->deviceContext);
    D3D_API_RELEASE(this->device);
    D3D_API_RELEASE(this->swapChain);
    D3D_API_RELEASE(this->renderTargetView);
    D3D_API_RELEASE(this->depthStencilView);
}

double D3D11Context::getScaleFactor()
{
#ifdef __WINRT__
    static auto displayInformation = winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView();

    return (unsigned int)displayInformation.LogicalDpi() / 96.0f;
#else
    if (this->GetDpiForWindow)
    {
        return this->GetDpiForWindow(this->hWnd) / 96.0;
    }

    HDC hdc = GetDC(nullptr);
    if (hdc)
    {
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        return dpi / 96.0;
    }

    return 1.0;
#endif
}

bool D3D11Context::onFramebufferSize(int width, int height, bool init)
{
    HRESULT hr = S_OK;

    ID3D11Texture2D* backBuffer         = nullptr;
    ID3D11Texture2D* depthStencil       = nullptr;
    ID3D11RenderTargetView* viewList[1] = { nullptr };
    this->deviceContext->OMSetRenderTargets(1, viewList, nullptr);

    D3D_API_RELEASE(this->renderTargetView);
    D3D_API_RELEASE(this->depthStencilView);

    if (!init)
    {
        UINT resizeFlags = 0;
#ifdef BOREALIS_USE_WINRT_COMPOSITION
        resizeFlags = compositionSwapChainFlags;
#endif
        hr = this->swapChain->ResizeBuffers(SwapChainBufferCount, width, height, DXGI_FORMAT_UNKNOWN, resizeFlags);
        if (FAILED(hr))
        {
            return false;
        }
    }

    hr = this->swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr))
    {
        return false;
    }

#ifdef BOREALIS_USE_WINRT_COMPOSITION
    D3D11_TEXTURE2D_DESC actualBuffer{};
    backBuffer->GetDesc(&actualBuffer);
    if (uiVisual) {
        uiVisual.Size({static_cast<float>(actualBuffer.Width), static_cast<float>(actualBuffer.Height)});
        // The composition target still uses CoreWindow pixels on Xbox. Keep
        // both UI and native video in framebuffer coordinates, then map the
        // common root to the target once; a 4K buffer must not crop the UI.
        const auto bounds = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Bounds();
        const float viewWidth = bounds.Width * static_cast<float>(getScaleFactor());
        const float viewHeight = bounds.Height * static_cast<float>(getScaleFactor());
        if (viewWidth > 0 && viewHeight > 0) {
            compositionRoot.Scale({viewWidth / actualBuffer.Width, viewHeight / actualBuffer.Height, 1});
            Logger::info("Xbox UI composition: target={}x{}, scale={}x{}", viewWidth, viewHeight,
                viewWidth / actualBuffer.Width, viewHeight / actualBuffer.Height);
        }
    }
    Logger::info("Xbox UI framebuffer: {}x{}", actualBuffer.Width, actualBuffer.Height);
#endif

    hr = this->device->CreateRenderTargetView(backBuffer, nullptr, &this->renderTargetView);
    D3D_API_RELEASE(backBuffer);
    if (FAILED(hr))
    {
        return false;
    }

    D3D11_TEXTURE2D_DESC texDesc;
    texDesc.ArraySize          = 1;
    texDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
    texDesc.CPUAccessFlags     = 0;
    texDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    texDesc.Height             = (UINT)height;
    texDesc.Width              = (UINT)width;
    texDesc.MipLevels          = 1;
    texDesc.MiscFlags          = 0;
    texDesc.SampleDesc.Count   = sampleDesc.Count;
    texDesc.SampleDesc.Quality = sampleDesc.Quality;
    texDesc.Usage              = D3D11_USAGE_DEFAULT;

    hr = this->device->CreateTexture2D(&texDesc, nullptr, &depthStencil);
    if (FAILED(hr))
    {
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc;
    depthViewDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthViewDesc.ViewDimension      = (sampleDesc.Count > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    depthViewDesc.Flags              = 0;
    depthViewDesc.Texture2D.MipSlice = 0;

    hr = this->device->CreateDepthStencilView(depthStencil, &depthViewDesc, &this->depthStencilView);
    D3D_API_RELEASE(depthStencil);
    if (FAILED(hr))
    {
        return false;
    }

    D3D11_VIEWPORT viewport;
    viewport.Width    = (float)width;
    viewport.Height   = (float)height;
    viewport.MaxDepth = 1.0f;
    viewport.MinDepth = 0.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    this->deviceContext->RSSetViewports(1, &viewport);

    return true;
}

void D3D11Context::clear(NVGcolor color)
{
    float clearColor[4] = { color.r, color.g, color.b, color.a };
    this->deviceContext->ClearRenderTargetView(this->renderTargetView, clearColor);
    this->deviceContext->ClearDepthStencilView(this->depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);
}

void D3D11Context::beginFrame()
{
    this->deviceContext->OMSetRenderTargets(1, &this->renderTargetView, this->depthStencilView);
}

void D3D11Context::endFrame()
{
    // https://learn.microsoft.com/zh-cn/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
    DXGI_PRESENT_PARAMETERS presentParameters;
    ZeroMemory(&presentParameters, sizeof(DXGI_PRESENT_PARAMETERS));
    UINT flags = 0;
#ifdef BOREALIS_USE_WINRT_COMPOSITION
    if (useTearingPresent && interval == 0) flags = DXGI_PRESENT_ALLOW_TEARING;
#endif
    const auto result = this->swapChain->Present1(interval, flags, &presentParameters);
#ifdef BOREALIS_USE_WINRT_COMPOSITION
    if (FAILED(result) && flags)
    {
        Logger::warning("D3D11 tearing present rejected: {:#010x}; using default presentation", static_cast<unsigned>(result));
        useTearingPresent = false;
        this->swapChain->Present1(interval, 0, &presentParameters);
    }
    static auto nextStatistics = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now >= nextStatistics)
    {
        nextStatistics = now + std::chrono::seconds(1);
        DXGI_FRAME_STATISTICS statistics{};
        const auto statisticsResult = swapChain->GetFrameStatistics(&statistics);
        if (SUCCEEDED(statisticsResult))
        {
            LARGE_INTEGER frequency;
            if (previousSyncQpc && statistics.SyncQPCTime.QuadPart > previousSyncQpc && QueryPerformanceFrequency(&frequency))
            {
                const double rate = (statistics.SyncRefreshCount - previousSyncRefresh) * static_cast<double>(frequency.QuadPart) /
                    (statistics.SyncQPCTime.QuadPart - previousSyncQpc);
                if (rate >= 20 && rate <= 300) presentationRefreshRate = rate;
            }
            previousSyncRefresh = statistics.SyncRefreshCount;
            previousSyncQpc = statistics.SyncQPCTime.QuadPart;
        }
        else previousSyncQpc = 0; // A mode switch invalidates the previous sample.
    }
#endif
}

void D3D11Context::setSwapInterval(int value)
{
    if (value < 0 || value > 4)
        return;
    this->interval = value;
#ifdef BOREALIS_USE_WINRT_COMPOSITION
    Logger::info("D3D11 swap interval: {}, tearing present={}", value, useTearingPresent && value == 0);
#endif
}

}
