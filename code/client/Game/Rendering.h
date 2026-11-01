namespace Game::Rendering
{
// The swapchain and command queue come from RED4ext::GpuApi::GetDeviceData() (see
// RenderingProvider::Present), not a hand-located context: that one read an instruction
// displacement and fixed offsets out of the 2.31 executable, and its asserts only checked
// our own struct against itself - so 2.5 broke it with nothing failing at compile time.

constexpr auto CRenderNode_Present_DoInternal = Core::RawFunc<
    /* hash = */ 2468877568UL,
    /* type = */ void* (*)(int32_t*, uint8_t, UINT)>();

constexpr auto CRenderGlobal_Resize = Core::RawFunc<
    /* hash = */ 239671909UL,
    /* type = */ void* (*)(uint32_t a1, uint32_t a2, uint32_t a3, uint8_t a4, int32_t* a5)>();

constexpr auto CRenderGlobal_Shutdown = Core::RawFunc<
    /* hash = */ 3192982283UL,
    /* type = */ void* (*)(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)>();
}