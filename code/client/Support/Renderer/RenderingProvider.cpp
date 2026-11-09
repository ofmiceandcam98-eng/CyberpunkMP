#include "RenderingProvider.hpp"

#include <tchar.h>

#include "Core/Facades/Runtime.hpp"
#include "Core/Rendering/RenderingAgent.hpp"

#include "Game/Rendering.h"

#include <RED4ext/GpuApi/DeviceData.hpp>

using namespace std::chrono_literals;

static BOOL CALLBACK EnumWindowsProcCP77(HWND ahWnd, LPARAM alParam)
{
    DWORD lpdwProcessId;
    GetWindowThreadProcessId(ahWnd, &lpdwProcessId);
    if (lpdwProcessId == GetCurrentProcessId())
    {
        wchar_t name[512] = {0};
        GetWindowTextW(ahWnd, name, 511);
        if (_tcscmp(L"Cyberpunk 2077 (C) 2020 by CD Projekt RED", name) == 0)
        {
            *reinterpret_cast<HWND*>(alParam) = ahWnd;
            return FALSE;
        }
    }
    return TRUE;
}

void Support::RenderingProvider::OnInitialize()
{
    if (!HookBefore<Game::Rendering::CRenderNode_Present_DoInternal>(&PresentHook))
        throw std::runtime_error("Failed to hook CRenderNode_Present_DoInternal.");

    if (!HookBefore<Game::Rendering::CRenderGlobal_Resize>(&ResizeHook))
        throw std::runtime_error("Failed to hook CRenderNode_Present_DoInternal.");

    if (!HookBefore<Game::Rendering::CRenderGlobal_Shutdown>(&ShutdownHook))
        throw std::runtime_error("Failed to hook CRenderNode_Present_DoInternal.");

    std::thread t(
        [this]
        {
            while (m_window == nullptr)
            {
                // Wait extra long to be sure we are the last ones to hooks wndproc
                std::this_thread::sleep_for(10000ms);
                if (EnumWindows(EnumWindowsProcCP77, reinterpret_cast<LPARAM>(&m_window)))
                    continue;

                m_wndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(m_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));

            }
        });

    t.detach();

    SetDefault(*this);
}

void Support::RenderingProvider::Present(int32_t* apDeviceIndex, uint8_t aSomeSync, UINT aSyncInterval)
{
    if (!m_initialized)
    {
        // From the SDK's hash-resolved device data, which upstream keeps current per patch. This
        // used to be a hand-located render context - a displacement read 0x1F bytes into the
        // present function, then fixed field offsets - and 2.5 moved all of it: the first frame
        // after the scripts compiled read garbage and the game died.
        auto* pDeviceData = RED4ext::GpuApi::GetDeviceData();
        const auto swapChainId = static_cast<uint32_t>(*apDeviceIndex);
        if (!pDeviceData || !pDeviceData->swapChains.IsValidID(swapChainId) || !pDeviceData->swapChains.IsUsedID(swapChainId))
            return;

        m_pCommandQueue = pDeviceData->directCommandQueue;
        m_pdxgiSwapChain = pDeviceData->swapChains.GetData(swapChainId).swapChain;
        Initialize();
        return;
    }

    TriggerPresent();
}

void Support::RenderingProvider::Resize(uint32_t aWidth, uint32_t aHeight, uint32_t a3, uint8_t a4, int* apDeviceIndex) const
{
    TriggerReset();
}

void Support::RenderingProvider::Shutdown(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) const
{
    TriggerReset(true);
}

void Support::RenderingProvider::PresentHook(int32_t* apDeviceIndex, uint8_t aSomeSync, UINT aSyncInterval)
{
    static_cast<RenderingProvider&>(GetDefault()).Present(apDeviceIndex, aSomeSync, aSyncInterval);
}

void Support::RenderingProvider::ResizeHook(uint32_t aWidth, uint32_t aHeight, uint32_t a3, uint8_t a4, int* apDeviceIndex)
{
    static_cast<RenderingProvider&>(GetDefault()).Resize(aWidth, aHeight, a3, a4, apDeviceIndex);
}

void Support::RenderingProvider::ShutdownHook(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
    static_cast<RenderingProvider&>(GetDefault()).Shutdown(a1, a2, a3, a4);
}

LRESULT Support::RenderingProvider::WndProc(HWND ahWnd, UINT auMsg, WPARAM awParam, LPARAM alParam)
{
    if(static_cast<RenderingProvider&>(GetDefault()).GetWindow())
    {
        if (static_cast<RenderingProvider&>(GetDefault()).TriggerWindowProc(ahWnd, auMsg, awParam, alParam))
            return 0;

        return CallWindowProc(static_cast<RenderingProvider&>(GetDefault()).m_wndProc, ahWnd, auMsg, awParam, alParam);
    }
    return 0;
}

void Support::RenderingProvider::Initialize()
{
    if (!GetWindow())
        return;

    Microsoft::WRL::ComPtr<ID3D12Device> pDevice;
    if (FAILED(m_pdxgiSwapChain->GetDevice(IID_PPV_ARGS(&pDevice))))
    {
        TriggerReset();
        return;
    }

    m_initialized = true;

    TriggerRenderInit();
}
