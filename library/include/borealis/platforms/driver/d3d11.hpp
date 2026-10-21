/*
    Copyright 2023 zeromake

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once
#include <nanovg.h>
#include <d3d11.h>
#include <d3d11_1.h>
#ifdef BOREALIS_USE_WINRT_COMPOSITION
#include <winrt/Windows.UI.Composition.h>
#endif

#ifdef __GLFW__
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#elif defined(__SDL2__)
#include <SDL2/SDL.h>
#endif

namespace brls
{

class D3D11Context
{
  public:
#ifdef __GLFW__
    D3D11Context(GLFWwindow* window, int width, int height);
#elif defined(__WINRT_NEW__)
    D3D11Context(void* window, int width, int height, bool enableTearing = false);
#elif defined(__SDL2__)
    D3D11Context(SDL_Window* window, int width, int height);
#endif
    ~D3D11Context();

    double getScaleFactor();
    void clear(NVGcolor color);
    void beginFrame();
    void endFrame();
    void setSwapInterval(int value);

    bool onFramebufferSize(int width, int height, bool init = false);

    ID3D11Device* getDevice() { return this->device; }

    IDXGISwapChain* getSwapChain() { return this->swapChain; }

    ID3D11DeviceContext* getDeviceContext() { return this->deviceContext; }

#ifdef BOREALIS_USE_WINRT_COMPOSITION
    winrt::Windows::UI::Composition::Compositor getCompositor() { return compositor; }
    winrt::Windows::UI::Composition::ContainerVisual getCompositionRoot() { return compositionRoot; }
    auto getCompositionSize() { return uiVisual.Size(); }
    double getPresentationRefreshRate() const { return presentationRefreshRate; }
    bool isTransparentPresentation() const { return transparentPresentation; }
    bool setTransparentPresentation(bool enabled);
#endif

  private:
    ID3D11Device* device                     = nullptr;
    ID3D11DeviceContext* deviceContext       = nullptr;
    IDXGISwapChain1* swapChain               = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    ID3D11DepthStencilView* depthStencilView = nullptr;
    int interval                             = 1;

    UINT(WINAPI* GetDpiForWindow)(HWND);

    HWND hWnd = nullptr;

#ifdef BOREALIS_USE_WINRT_COMPOSITION
    winrt::Windows::UI::Composition::Compositor compositor{nullptr};
    winrt::Windows::UI::Composition::CompositionTarget compositionTarget{nullptr};
    winrt::Windows::UI::Composition::ContainerVisual compositionRoot{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual uiVisual{nullptr};
    UINT compositionSwapChainFlags = 0;
    bool useTearingPresent = false;
    double presentationRefreshRate = 0;
    UINT previousSyncRefresh = 0;
    LONGLONG previousSyncQpc = 0;
    bool transparentPresentation = false;
    HRESULT createPresentationSwapChain(IDXGIFactory2* factory, int width, int height, bool transparent);
    void releaseSwapChain();
#endif

    bool initDX(HWND window, IUnknown* coreWindow, int width, int height);
    void unInitDX();
};

}
