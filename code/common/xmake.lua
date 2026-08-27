add_requires("hopscotch-map", "snappy", "gamenetworkingsockets", "catch2 2.13.9", "libuv", "openssl", "spdlog")
add_requireconfs("*.protobuf*", { build = true })
-- secure = true builds mimalloc with guard pages and encoded free lists.
--
-- Turned on to find the corruption behind the 27 August crashes: the client dies a few
-- seconds after a new character is created, inside this DLL, holding values that cannot be
-- pointers (0x11, 0x13) or a null. The faulting address is wherever the damaged block was
-- next touched, not where it was damaged, so reasoning backwards from it kept producing
-- plausible wrong answers.
--
-- Secure mode makes mimalloc notice the write itself - an overflow runs into a guard page,
-- and a corrupted free-list link fails its check - so the report names the moment instead
-- of the aftermath. Paired with mi_register_error in MimallocAllocator.cpp, which routes
-- that report into the log.
--
-- It costs some speed. Revert this line once the corruption is found and fixed.
add_requireconfs("mimalloc", {configs = {rltgenrandom = true, secure = true}})

if is_plat("windows") then
    add_requires("minhook", "mem", "xbyak")
end

target("Common")
    set_kind("static")
    add_files("**.cpp")
    remove_files("Tests/**")
    set_group("Libraries")
    add_headerfiles("**.h", "**.hpp", "**.inl")

    set_pcxxheader("CommonPCH.h")
    add_includedirs(".", {public = true})
    add_includedirs(
        "../../build", 
        "../../vendor"
    )

    add_packages("hopscotch-map", "snappy", "gamenetworkingsockets", "libuv")
    if is_plat("windows") then
        add_packages("minhook", "mem", "xbyak")
    else
        remove_files("Reverse/**")
    end

    add_cxflags("-fPIC")
    add_defines("STEAMNETWORKINGSOCKETS_STATIC_LINK")

    add_packages(
        "spdlog",
        "glm",
        "hopscotch-map",
        "mimalloc",
        "gamenetworkingsockets",
        "snappy",
        "openssl",
        "libuv")
