set_project("mw169proxy")
set_xmakever("2.8.0")

set_allowedplats("windows")
set_allowedarchs("x64")
set_allowedmodes("debug", "release", "protect")
set_defaultmode("release")
set_languages("c++20", "c17")

local DEBUG = is_mode("debug")
local PROTECT = is_mode("protect")
local CONFIG = DEBUG and "Debug" or (PROTECT and "Protect" or "Release")

option("shifting_headless")
    set_default(false)
    set_showmenu(true)
    set_description("Use the tested Shifting.Codes preset without the GUI")
option_end()

option("minimal_proxy")
    set_default(false)
    set_showmenu(true)
    set_description("Build the import-free three-export loader probe")
option_end()

option("proxy_only")
    set_default(false)
    set_showmenu(true)
    set_description("Build the full Discord forwarder without the proxy worker")
option_end()

local SHIFTING_HEADLESS = has_config("shifting_headless")
local MINIMAL_PROXY = has_config("minimal_proxy")
local PROXY_ONLY = has_config("proxy_only")
local SHIFTING_DIRECTORY = path.join(os.projectdir(), ".tools", "shifting")
local SHIFTING_SOURCE = path.join(SHIFTING_DIRECTORY, "source")
local SHIFTING_PYTHON = path.join(SHIFTING_SOURCE, ".venv", "Scripts", "python.exe")
local SHIFTING_OUTPUT_DIRECTORY = path.join(os.projectdir(), "x64", CONFIG, "Shifting")
local SHIFTING_UNIT_DIRECTORY = path.join(SHIFTING_OUTPUT_DIRECTORY, "units")
local SHIFTING_OBJECT_DIRECTORY = path.join(SHIFTING_OUTPUT_DIRECTORY, "objects")
local LUA_MENU_GENERATED_DIRECTORY = path.join(os.projectdir(), "build", "generated")

-- Add first-party source files here. Protect mode opens one Shifting.Codes
-- window for each file and links one COFF object for each protected file.
local SHIFTING_SOURCES = {
    "src/protected_build.cpp",
    "src/hook.cpp",
    "src/auth.cpp",
    "src/online_fences.cpp",
}

local SHIFTING_OBJECTS = {}
for _, source in ipairs(SHIFTING_SOURCES) do
    table.insert(
        SHIFTING_OBJECTS,
        path.join(SHIFTING_OBJECT_DIRECTORY, path.basename(source) .. ".obj"))
end

local function shifting_msvc_files()
    local names = {}
    for _, source in ipairs(SHIFTING_SOURCES) do
        table.insert(names, path.filename(source))
    end
    return "src/*.cpp|" .. table.concat(names, "|")
end

local THIRD_PARTY_DEFINES = {
    "ASMJIT_STATIC",
    "ASMJIT_NO_FOREIGN",
    "ASMTK_STATIC",
    "ZYDIS_STATIC_BUILD",
    "ZYCORE_STATIC_BUILD",
    "WINDOWS_IGNORE_PACKING_MISMATCH",
    "_FILE_STAT_INFORMATION_DEFINED",
    "_FILE_STAT_LX_INFORMATION_DEFINED",
    "_FILE_CASE_SENSITIVE_INFORMATION_DEFINED"
}

local THIRD_PARTY_INCLUDES = {
    "thirdparty/cwhook",
    "thirdparty/cwhook/src",
    "thirdparty/cwhook/include/NT",
    "thirdparty/cwhook/libs/asmjit/src",
    "thirdparty/cwhook/libs/minhook/include",
    "thirdparty/cwhook/libs/patterns",
    "thirdparty/polyhook2",
    "thirdparty/polyhook2/zydis",
    "thirdparty/polyhook2/asmtk/src",
    "thirdparty/asmjit/src",
}

if PROTECT then
    target("shifting_object")
        set_kind("phony")
        set_default(false)

        on_build(function ()
            local setup = path.join(os.projectdir(), "tools", "shifting", "setup.ps1")
            local wrapper = path.join(os.projectdir(), "tools", "shifting", "protect.py")
            local llvm_root = os.getenv("SHIFTING_LLVM_ROOT") or "C:\\llvm\\21"
            local clang = path.join(llvm_root, "bin", "clang-cl.exe")

            if not os.isfile(clang) then
                raise("LLVM 21 clang-cl is absent at " .. clang)
            end
            os.vrunv("C:\\Program Files\\PowerShell\\7\\pwsh.exe", {
                "-NoProfile",
                "-File", setup,
                "-ToolRoot", SHIFTING_DIRECTORY,
                "-LLVMRoot", llvm_root
            })
            if not os.isfile(SHIFTING_PYTHON) then
                raise("The Shifting.Codes Python environment is incomplete")
            end

            os.mkdir(SHIFTING_OUTPUT_DIRECTORY)
            os.mkdir(SHIFTING_UNIT_DIRECTORY)
            os.mkdir(SHIFTING_OBJECT_DIRECTORY)

            if #SHIFTING_SOURCES == 0 then
                raise("SHIFTING_SOURCES is empty")
            end

            for index, source in ipairs(SHIFTING_SOURCES) do
                local abs_source = path.absolute(path.join(os.projectdir(), source))
                if not os.isfile(abs_source) then
                    raise("Protected source is absent: " .. source)
                end

                local bitcode = path.join(SHIFTING_UNIT_DIRECTORY, path.basename(source) .. ".bc")
                local object = SHIFTING_OBJECTS[index]
                os.rm(bitcode)
                os.rm(object)

                local clang_args = {
                    "/nologo",
                    "/c",
                    "/std:c++20",
                    "/Od",
                    "/MT",
                    "/EHsc",
                    "/guard:cf",
                    "/DNDEBUG",
                    "/DWIN32",
                    "/D_WINDOWS",
                    "/D_USRDLL",
                    "/DNOMINMAX",
                    "/DWIN32_LEAN_AND_MEAN",
                    "/D_CRT_SECURE_NO_WARNINGS",
                    "/DUNICODE",
                    "/D_UNICODE",
                    "/DMW169_PROTECT_BUILD"
                }
                for _, define in ipairs(THIRD_PARTY_DEFINES) do
                    table.insert(clang_args, "/D" .. define)
                end
                table.insert(clang_args, "/I" .. path.join(os.projectdir(), "src"))
                for _, include in ipairs(THIRD_PARTY_INCLUDES) do
                    table.insert(clang_args, "/I" .. path.join(os.projectdir(), include))
                end
                table.insert(clang_args, "/clang:-emit-llvm")
                table.insert(clang_args, "/clang:-o")
                table.insert(clang_args, "/clang:" .. bitcode)
                table.insert(clang_args, abs_source)
                os.vrunv(clang, clang_args, {curdir = SHIFTING_UNIT_DIRECTORY})

                if not os.isfile(bitcode) then
                    raise("clang-cl did not create bitcode for " .. source)
                end

                local arguments = {
                    wrapper,
                    "--input", bitcode,
                    "--output", object,
                    "--shifting-root", SHIFTING_SOURCE,
                    "--source", abs_source
                }
                if SHIFTING_HEADLESS then
                    table.insert(arguments, "--headless")
                end
                os.vrunv(SHIFTING_PYTHON, arguments, {
                    envs = {
                        PATH = path.join(llvm_root, "bin") .. ";" .. os.getenv("PATH")
                    }
                })
                if not os.isfile(object) then
                    raise("Shifting.Codes did not create an object for " .. source)
                end
            end
        end)
end

target("mw169proxy")
    set_kind("shared")
    set_basename("discord_game_sdk")
    set_prefixname("")
    set_targetdir(path.join(os.projectdir(), "x64", CONFIG))
    set_objectdir(path.join(os.projectdir(), "x64", CONFIG, ".objs"))

    add_defines(
        "WIN32",
        "_WINDOWS",
        "_USRDLL",
        "NOMINMAX",
        "WIN32_LEAN_AND_MEAN",
        "_CRT_SECURE_NO_WARNINGS",
        "UNICODE",
        "_UNICODE"
    )
    add_defines(THIRD_PARTY_DEFINES)
    if PROXY_ONLY then
        add_defines("MW169_PROXY_ONLY")
    end
    add_includedirs("src")
    add_includedirs(LUA_MENU_GENERATED_DIRECTORY)
    add_includedirs(THIRD_PARTY_INCLUDES)
    add_syslinks(
        "kernel32",
        "user32",
        "advapi32",
        "ntdll",
        "dbghelp",
        "ws2_32"
    )

    if DEBUG then
        set_runtimes("MTd")
        set_symbols("debug")
        add_defines("_DEBUG")
    else
        set_runtimes("MT")
        set_optimize("fast")
        set_symbols("debug")
        add_defines("NDEBUG")
        add_shflags("/OPT:REF", "/OPT:ICF", "/PDBALTPATH:%_PDB%", {force = true})
    end

    if PROTECT then
        add_deps("shifting_object")
        add_shflags("/GUARD:CF", {force = true})
        for _, object in ipairs(SHIFTING_OBJECTS) do
            add_shflags(object, {force = true})
        end
        on_load(function (target)
            for _, object in ipairs(SHIFTING_OBJECTS) do
                target:data_add("linkdepfiles", object)
            end
        end)
        add_files(shifting_msvc_files(), {
            cxflags = {"/permissive-", "/sdl"},
            warnings = "more"
        })
        remove_files("src/minimal_proxy.cpp")
    elseif MINIMAL_PROXY then
        add_files("src/minimal_proxy.cpp", {
            cxflags = {"/permissive-", "/sdl"},
            warnings = "more"
        })
    else
        add_files("src/*.cpp", {
            cxflags = {"/permissive-", "/sdl"},
            warnings = "more"
        })
        remove_files("src/minimal_proxy.cpp")
    end

    if not MINIMAL_PROXY then add_files(
        "thirdparty/polyhook2/sources/ADetour.cpp",
        "thirdparty/polyhook2/sources/x64Detour.cpp",
        "thirdparty/polyhook2/sources/ZydisDisassembler.cpp",
        "thirdparty/polyhook2/sources/MemProtector.cpp",
        "thirdparty/polyhook2/sources/MemAccessor.cpp",
        "thirdparty/polyhook2/sources/RangeAllocator.cpp",
        "thirdparty/polyhook2/sources/ErrorLog.cpp",
        "thirdparty/polyhook2/sources/Misc.cpp",
        "thirdparty/polyhook2/sources/PolyHookOs.cpp",
        "thirdparty/polyhook2/sources/UID.cpp",
        "thirdparty/polyhook2/sources/FBAllocator.cpp",
        {
            cxflags = {"/bigobj"},
            warnings = "none",
            defines = THIRD_PARTY_DEFINES,
            includedirs = THIRD_PARTY_INCLUDES
        }
    ) end

    if not MINIMAL_PROXY then add_files("thirdparty/polyhook2/zydis/Zydis.c", {
        cflags = {"/TC"},
        cxflags = {"/TC"},
        warnings = "none",
        defines = THIRD_PARTY_DEFINES,
        includedirs = {"thirdparty/polyhook2/zydis"}
    }) end

    if not MINIMAL_PROXY then add_files(
        "thirdparty/polyhook2/asmtk/src/asmtk/asmparser.cpp",
        "thirdparty/polyhook2/asmtk/src/asmtk/asmtokenizer.cpp",
        {
            cxflags = {"/bigobj"},
            warnings = "none",
            defines = THIRD_PARTY_DEFINES,
            includedirs = THIRD_PARTY_INCLUDES
        }
    ) end

    if not MINIMAL_PROXY then add_files(
        "thirdparty/asmjit/src/asmjit/core/*.cpp",
        "thirdparty/asmjit/src/asmjit/x86/*.cpp",
        {
            cxflags = {"/bigobj"},
            warnings = "none",
            defines = THIRD_PARTY_DEFINES,
            includedirs = THIRD_PARTY_INCLUDES
        }
    ) end

    if not MINIMAL_PROXY then add_files(
        "thirdparty/cwhook/src/*.cpp",
        "thirdparty/cwhook/libs/patterns/Hooking.Patterns.cpp",
        {
            cxflags = {"/bigobj"},
            warnings = "none",
            defines = THIRD_PARTY_DEFINES,
            includedirs = THIRD_PARTY_INCLUDES
        }
    ) end

    if not MINIMAL_PROXY then add_files(
        "thirdparty/cwhook/libs/minhook/src/hook.c",
        "thirdparty/cwhook/libs/minhook/src/buffer.c",
        "thirdparty/cwhook/libs/minhook/src/trampoline.c",
        "thirdparty/cwhook/libs/minhook/src/hde/hde64.c",
        {
            warnings = "none",
            cflags = {"/TC"},
            cxflags = {"/TC"},
            defines = {"_CRT_SECURE_NO_WARNINGS"},
            includedirs = THIRD_PARTY_INCLUDES
        }
    ) end

    before_build(function ()
        local script = path.join(os.projectdir(), "tools", "gen_lua_menu_embed.py")
        os.mkdir(LUA_MENU_GENERATED_DIRECTORY)
        os.vrunv("python", {script}, {curdir = os.projectdir()})
    end)

target("load_smoke")
    set_kind("binary")
    set_default(false)
    set_targetdir(path.join(os.projectdir(), "x64", "Test"))
    set_objectdir(path.join(os.projectdir(), "x64", "Test", ".objs"))
    set_runtimes("MT")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "UNICODE", "_UNICODE")
    add_syslinks("kernel32", "user32")
    add_files("tests/load_smoke.cpp", {
        cxflags = {"/permissive-", "/sdl"},
        warnings = "more"
    })

target("network_policy_test")
    set_kind("binary")
    set_default(false)
    set_targetdir(path.join(os.projectdir(), "x64", "Test"))
    set_objectdir(path.join(os.projectdir(), "x64", "Test", ".policy_objs"))
    set_runtimes("MT")
    add_includedirs("src")
    add_syslinks("ws2_32")
    add_files("tests/network_policy_test.cpp", "src/network_policy.cpp", {
        cxflags = {"/permissive-", "/sdl"},
        warnings = "more"
    })

target("minidump_probe")
    set_kind("binary")
    set_default(false)
    set_targetdir(path.join(os.projectdir(), "x64", "Test"))
    set_objectdir(path.join(os.projectdir(), "x64", "Test", ".minidump_objs"))
    set_runtimes("MT")
    add_syslinks("dbghelp")
    add_files("tools/analysis/minidump_probe.cpp", {
        cxflags = {"/permissive-", "/sdl"},
        warnings = "more"
    })
