#include "ImGuiService.h"

#include "dx12.h"
#include "win32.h"
#include "App/Application.h"
#include "App/Debugging/DebugService.h"
#include "App/Settings.h"

// Once per session and call site. A healthy overlay says how far it got in a few lines, and a
// broken one stops saying so at the step that failed. These go to the client log on purpose:
// LogError lands in red4ext's own file, which never reaches the server.
#define OVERLAY_LOG_ONCE(...)         \
    do                                \
    {                                 \
        static bool s_said = false;   \
        if (!s_said)                  \
        {                             \
            s_said = true;            \
            spdlog::info(__VA_ARGS__); \
        }                             \
    } while (false)

namespace App
{

ImGuiService::ImGuiService()
{
}

ImGuiService::~ImGuiService()
{
}

void ImGuiService::OnRenderInit()
{
    // With the overlay off, take no part in rendering whatsoever.
    //
    // Returning here means no descriptor heap, no command allocators, no command list,
    // no ImGui context - the mod creates nothing on the game's D3D12 device and never
    // touches a frame. PrepareUpdate and OnPresent already skipped their work; this
    // removes the objects as well.
    //
    // The reason to go this far is Cyber Engine Tweaks. CET draws its own ImGui overlay
    // through the same present path, and two overlays sharing one swapchain is a known
    // way to hard-lock a GPU - which is exactly what was seen when both were installed.
    // Most Cyberpunk mods want CET, so "you must uninstall CET" is a real cost to anyone
    // playing here. A mod that renders nothing cannot fight another renderer.
    //
    // Safe to decide once: the overlay is set from the command line at launch and cannot
    // change mid-session, so there is no case where this is skipped and later needed.
    if (!Settings::Get().debug)
    {
        spdlog::info("[ImGuiService] developer overlay off - not initialising any rendering");
        return;
    }

    spdlog::info("[ImGuiService] developer overlay requested - starting");

    // The hard-lock above was seen with both overlays drawing, so they do not run together.
    // A developer's machine is not the place to find out whether 2.5 changed that.
    if (GetModuleHandleW(L"cyber_engine_tweaks.asi"))
    {
        spdlog::warn("[ImGuiService] Cyber Engine Tweaks is loaded, so the developer overlay was NOT started: "
                     "the two drawing together have hard-locked a GPU. Disable CET to use the overlay.");
        return;
    }

    spdlog::info("[ImGuiService] Cyber Engine Tweaks is not loaded");

    const auto fail = [this](const char* acpWhat)
    {
        spdlog::error("[ImGuiService] the overlay did not start: {}", acpWhat);
        LogError(acpWhat);
    };

    Microsoft::WRL::ComPtr<ID3D12Device> pDevice;
    if (FAILED(GetSwapChain()->GetDevice(IID_PPV_ARGS(&pDevice))))
    {
        fail("RenderingProvider::Initialize() - could not get the device from the swap chain!");
        return;
    }

    DXGI_SWAP_CHAIN_DESC sdesc;
    GetSwapChain()->GetDesc(&sdesc);

    spdlog::info("[ImGuiService] swap chain: {} buffer(s), format {}, {}x{}, flags {:#x}", sdesc.BufferCount,
                 static_cast<int>(sdesc.BufferDesc.Format), sdesc.BufferDesc.Width, sdesc.BufferDesc.Height, sdesc.Flags);

    const auto buffersCounts = std::min(sdesc.BufferCount, 3u);
    m_frameContexts.resize(buffersCounts);

    D3D12_DESCRIPTOR_HEAP_DESC srvdesc = {};
    srvdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvdesc.NumDescriptors = buffersCounts;
    srvdesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(pDevice->CreateDescriptorHeap(&srvdesc, IID_PPV_ARGS(&m_pSrvDescHeap))))
    {
        fail("RenderingProvider::Initialize() - failed to create SRV descriptor heap!");
        OnReset();
        return;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvdesc;
    rtvdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvdesc.NumDescriptors = buffersCounts;
    rtvdesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvdesc.NodeMask = 1;

    if (FAILED(pDevice->CreateDescriptorHeap(&rtvdesc, IID_PPV_ARGS(&m_pRtvDescHeap))))
    {
        fail("RenderingProvider::Initialize() - failed to create RTV descriptor heap!");
        OnReset();
        return;
    }

    const SIZE_T rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_pRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < buffersCounts; i++)
    {
        auto& context = m_frameContexts[i];
        context.MainRenderTargetDescriptor = rtvHandle;
        GetSwapChain()->GetBuffer(i, IID_PPV_ARGS(&context.BackBuffer));
        pDevice->CreateRenderTargetView(context.BackBuffer.Get(), nullptr, context.MainRenderTargetDescriptor);
        rtvHandle.ptr += rtvDescriptorSize;
    }

    for (auto& context : m_frameContexts)
        if (FAILED(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&context.CommandAllocator))))
        {
            fail("RenderingProvider::Initialize() - failed to create command allocator!");
            OnReset();
            return;
        }

    if (FAILED(pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frameContexts[0].CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_pCommandList))) ||
        FAILED(m_pCommandList->Close()))
    {
        fail("RenderingProvider::Initialize() - failed to create command list!");
        OnReset();
        return;
    }

    {
        std::lock_guard _(m_imguiLock);

        // TODO - scale also by DPI
        const auto [resx, resy] = std::tie(sdesc.BufferDesc.Width, sdesc.BufferDesc.Height);
        const auto scaleFromReference = std::min(static_cast<float>(resx) / 1920.0f, static_cast<float>(resy) / 1080.0f);

        auto& io = ImGui::GetIO();
       // io.Fonts->Clear();
        #if 0
        // tmp:
        struct FontSettings
        {
            std::string Path{};
            std::string Language{"Default"};
            float BaseSize{18.0f};
            int32_t OversampleHorizontal{3};
            int32_t OversampleVertical{1};
        } fontSettings;

        ImFontConfig config;
       // const auto& fontSettings = m_options.Font;
        config.SizePixels = std::floorf(fontSettings.BaseSize * scaleFromReference);
        config.OversampleH = fontSettings.OversampleHorizontal;
        config.OversampleV = fontSettings.OversampleVertical;
        if (config.OversampleH == 1 && config.OversampleV == 1)
            config.PixelSnapH = true;
        config.MergeMode = false;
        #endif

        if (ImGui::GetCurrentContext() == nullptr)
        {
            // do this once, do not repeat context creation!
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();

            // Keep our hands off the OS cursor.
            //
            // The Win32 backend re-applies a cursor shape every single NewFrame. Left
            // alone it calls SetCursor(IDC_ARROW) forever, which paints a plain white
            // Windows arrow in the middle of the screen - sitting on top of the crosshair
            // during play, and on top of the game's own cursor in menus. The game already
            // decides when a cursor should exist and what it should look like; this
            // overlay has no business overriding that.
            //
            // Our own windows stay usable because MouseDrawCursor below makes ImGui draw
            // its cursor INTO the overlay, so it exists only while ImGui wants the mouse.
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

            // No imgui.ini. By default ImGui writes one into the working directory, which here
            // is the game's own bin folder, and the footprint rule says we leave nothing behind.
            ImGui::GetIO().IniFilename = nullptr;

            // TODO - make this configurable eventually and overridable by mods for themselves easily
            // setup CET default style
            ImGui::StyleColorsDark(&m_styleReference);
            m_styleReference.WindowRounding = 6.0f;
            m_styleReference.WindowTitleAlign.x = 0.5f;
            m_styleReference.ChildRounding = 6.0f;
            m_styleReference.PopupRounding = 6.0f;
            m_styleReference.FrameRounding = 6.0f;
            m_styleReference.ScrollbarRounding = 12.0f;
            m_styleReference.GrabRounding = 12.0f;
            m_styleReference.TabRounding = 6.0f;
        }

        ImGui::GetStyle() = m_styleReference;
        ImGui::GetStyle().ScaleAllSizes(scaleFromReference);

        // Text at 18 authored units, the size of the selector's labels. Without this ImGui stays
        // at its 13 real pixels on every display, which on 1440p is a bar easy to look straight past.
        ImGui::GetStyle().FontScaleMain = (18.0f / 13.0f) * scaleFromReference;

        if (!ImGui_ImplWin32_Init(GetWindow()))
        {
            fail("RenderingProvider::Initialize() - ImGui_ImplWin32_Init call failed!");
            return;
        }

        if (!ImGui_ImplDX12_Init(
                pDevice.Get(), static_cast<int>(buffersCounts), sdesc.BufferDesc.Format, m_pSrvDescHeap.Get(), m_pSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
                m_pSrvDescHeap->GetGPUDescriptorHandleForHeapStart()))
        {
            fail("RenderingProvider::Initialize() - ImGui_ImplDX12_Init call failed!");
            ImGui_ImplWin32_Shutdown();
            return;
        }

        // ReloadFonts();

        if (!ImGui_ImplDX12_CreateDeviceObjects())
        {
            fail("RenderingProvider::Initialize() - ImGui_ImplDX12_CreateDeviceObjects call failed!");
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            return;
        }
    }

    m_initialized = true;
    spdlog::info("[ImGuiService] overlay started with {} frame context(s)", buffersCounts);
}

void ImGuiService::OnPresent()
{
    if (!m_initialized)
        return;

    // Nothing to present when the overlay is off - see PrepareUpdate. Checked here too
    // rather than relying on the draw buffers being empty, because "no GPU work" should
    // not depend on a buffer happening to be in the right state.
    if (!Settings::Get().debug)
        return;

    // swap staging ImGui buffer with render ImGui buffer
    {
        std::lock_guard _(m_imguiLock);
        ImGui_ImplDX12_NewFrame();
        if (m_imguiDrawDataBuffers[1].Valid)
        {
            std::swap(m_imguiDrawDataBuffers[0], m_imguiDrawDataBuffers[1]);
            m_imguiDrawDataBuffers[1].Valid = false;
        }
    }

    OVERLAY_LOG_ONCE("[ImGuiService] first present reached the overlay");

    if (!m_imguiDrawDataBuffers[0].Valid)
        return;

    OVERLAY_LOG_ONCE("[ImGuiService] first frame with something to draw: {} draw list(s), {} vertices, display {}x{}",
                     m_imguiDrawDataBuffers[0].CmdLists.Size, m_imguiDrawDataBuffers[0].TotalVtxCount,
                     m_imguiDrawDataBuffers[0].DisplaySize.x, m_imguiDrawDataBuffers[0].DisplaySize.y);

    const auto bufferIndex = GetSwapChain()->GetCurrentBackBufferIndex();

    // Only three contexts are made however many buffers the swap chain has.
    if (bufferIndex >= m_frameContexts.size())
    {
        OVERLAY_LOG_ONCE("[ImGuiService] back buffer {} has no frame context ({} made) - that frame is not drawn",
                         bufferIndex, m_frameContexts.size());
        return;
    }

    auto& frameContext = m_frameContexts[bufferIndex];
    frameContext.CommandAllocator->Reset();

    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = frameContext.BackBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    ID3D12DescriptorHeap* heaps[] = {m_pSrvDescHeap.Get()};

    m_pCommandList->Reset(frameContext.CommandAllocator.Get(), nullptr);
    m_pCommandList->ResourceBarrier(1, &barrier);
    m_pCommandList->SetDescriptorHeaps(1, heaps);
    m_pCommandList->OMSetRenderTargets(1, &frameContext.MainRenderTargetDescriptor, FALSE, nullptr);

    ImGui_ImplDX12_RenderDrawData(&m_imguiDrawDataBuffers[0], m_pCommandList.Get());

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_pCommandList->ResourceBarrier(1, &barrier);
    m_pCommandList->Close();

    ID3D12CommandList* commandLists[] = {m_pCommandList.Get()};
    GetCommandQueue()->ExecuteCommandLists(1, commandLists);

    OVERLAY_LOG_ONCE("[ImGuiService] first overlay frame submitted, on back buffer {}", bufferIndex);
}

void ImGuiService::OnReset(bool aCompleteReset)
{
    if (m_initialized)
    {
        std::lock_guard _(m_imguiLock);

        for (auto& drawData : m_imguiDrawDataBuffers)
        {
            for (auto i = 0; i < drawData.CmdLists.Size; ++i)
                IM_DELETE(drawData.CmdLists[i]);
            drawData.Clear();
        }

        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();

        if (aCompleteReset)
            ImGui::DestroyContext();
    }

    m_frameContexts.clear();
    
    m_pRtvDescHeap.Reset();
    m_pSrvDescHeap.Reset();
    m_pCommandList.Reset();

    m_initialized = false;
}

bool ImGuiService::OnWindowProc(HWND ahWnd, UINT auMsg, WPARAM awParam, LPARAM alParam)
{
    if (m_initialized)
    {
        // Whether a click reaches the overlay at all is the first question if a button will not press.
        if (auMsg == WM_LBUTTONDOWN)
            OVERLAY_LOG_ONCE("[ImGuiService] first left click reached the overlay at {}, {}",
                             static_cast<short>(LOWORD(alParam)), static_cast<short>(HIWORD(alParam)));

        if (const auto res = ImGui_ImplWin32_WndProcHandler(ahWnd, auMsg, awParam, alParam))
            return res != 0;
    }

    return false;
}

void ImGuiService::OnBootstrap()
{
    
}

void ImGuiService::OnGameUpdate(RED4ext::CGameApplication* apApp)
{
    PrepareUpdate();
}

void ImGuiService::PrepareUpdate()
{
    if (!m_pCommandList || !m_initialized)
        return;

    // With the overlay off, build no frame at all.
    //
    // Gating only DebugService::Draw() still ran the whole ImGui frame and still handed
    // OnPresent a valid (empty) draw list, so the mod kept resetting a command allocator,
    // raising a resource barrier and executing a command list inside the game's frame -
    // every frame, to draw nothing.
    //
    // That is worth removing on its own, and it matters more after a GPU crash was
    // reported with no other explanation: gpuApiDX12Error, "Gpu Crash for unknown
    // reasons". This mod injecting DX12 work into a frame is one of the few things about
    // the renderer we control, so for anyone not running the overlay it now injects none.
    // That is not a claim this caused it - it removes us from the list of suspects.
    if (!Settings::Get().debug)
        return;

    std::lock_guard _(m_imguiLock);

    DXGI_SWAP_CHAIN_DESC sdesc;
    GetSwapChain()->GetDesc(&sdesc);

    // Draw a cursor only while our own UI actually wants the mouse. WantCaptureMouse is
    // last frame's answer, which is exactly right here: it is set while the pointer is
    // over an ImGui window, and a single frame of lag entering or leaving one is
    // invisible. During normal play nothing is hovered, so nothing is drawn and the
    // crosshair is left alone.
    ImGui::GetIO().MouseDrawCursor = ImGui::GetIO().WantCaptureMouse;

    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // r
    Core::Container::Get<DebugService>()->Draw();
  
    ImGui::Render();

    auto& drawData = m_imguiDrawDataBuffers[2];

    for (auto i = 0; i < drawData.CmdLists.Size; ++i)
        IM_DELETE(drawData.CmdLists[i]);
    drawData.Clear();

    drawData = *ImGui::GetDrawData();

    ImVector<ImDrawList*> copiedDrawLists;
    for (auto i = 0; i < drawData.CmdLists.Size; ++i)
        copiedDrawLists.push_back(drawData.CmdLists[i]->CloneOutput());
    drawData.CmdLists = copiedDrawLists;

    std::swap(m_imguiDrawDataBuffers[1], m_imguiDrawDataBuffers[2]);
}
}
