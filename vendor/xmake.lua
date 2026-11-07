target("RED4ext.SDK")
    set_kind("static")
    set_group("vendor")
    add_files("RED4ext.SDK/src/**.cpp")
    add_headerfiles("RED4ext.SDK/include/**.hpp")
    add_includedirs("RED4ext.SDK/include/", { public = true })
    -- The SDK's GpuApi layer includes AMD's D3D12 Memory Allocator, which upstream ships
    -- inside its own repo and exposes publicly (CMakeLists: RED4EXT_VENDOR_DIR/D3D12MemAlloc).
    add_includedirs("RED4ext.SDK/vendor/D3D12MemAlloc/", { public = true })
    on_install(function() end)
