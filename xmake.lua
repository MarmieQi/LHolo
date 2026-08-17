add_rules("mode.debug", "mode.release")

option("levimc_repo")
    set_default("https://github.com/LiteLDev/xmake-repo.git")
    set_showmenu(true)
    set_description("Set the levimc-repo path or url")
option_end()

option("target_type")
    set_default("client")
    set_showmenu(true)
    set_values("server", "client")
option_end()

add_repositories("levimc-repo " .. (get_config("levimc_repo") or "https://github.com/LiteLDev/xmake-repo.git"))

add_requires("levilamina 26.20.7", {configs = {target_type = get_config("target_type") or "client"}})
add_requires("levibuildscript")
add_requires("imgui v1.91.9", {configs = {shared = false, win32 = true, dx11 = true, no_demo_windows = true}})
add_requires("minhook", {configs = {shared = false}})
add_requires("zlib")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

target("LHolo")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker", {modVersion = "26.20.3"})
    add_cxflags(
        "/utf-8",
        "/W4",
        "/wd4702",
        "/w44265",
        "/w44289",
        "/w44296",
        "/w45263",
        "/w44738",
        "/w45204"
    )
    add_defines("NOMINMAX", "UNICODE")
    add_syslinks("user32", "comdlg32", "d3d11", "d3d12", "dxgi")
    add_packages("levilamina", "imgui", "minhook", "zlib")

    set_kind("shared")
    set_languages("c++20")

    add_files("src/**.cpp")
    add_includedirs("src")
