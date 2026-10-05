-- neosea -- a distraction-free word processor for books (a C++ port of NEO).
-- Generate: premake5 gmake && make -C build -j8 config=release

-- The build number and name come from git (scripts/version.sh) into a header,
-- rewritten only when it changes, at generation time and as a prebuild step.
local version_header = "build/generated/neosea_version.h"
os.mkdir("build/generated")
os.execute("./scripts/version.sh header " .. version_header)

local function refresh_version()
    prebuildcommands { '"%{wks.location}/../scripts/version.sh" header "%{wks.location}/generated/neosea_version.h"' }
end

-- Qt, ICU, libzip and hunspell through pkg-config: their paths move between
-- distributions, and a wrong guess fails at link time naming nothing useful.
local function pkg(what, mods)
    local out = os.outputof("pkg-config " .. what .. " " .. mods .. " 2>/dev/null") or ""
    -- system headers, so their warnings (Qt trips -Wsfinae-incomplete) stay theirs
    return (out:gsub("%-I/", "-isystem /"))
end
local CORE_PKGS = "Qt6Core Qt6Gui Qt6PrintSupport icu-uc icu-i18n libzip hunspell"
local APP_PKGS = CORE_PKGS .. " Qt6Widgets Qt6Svg Qt6Network"
local have_tts = os.outputof("pkg-config --exists Qt6TextToSpeech && echo yes") == "yes"
if have_tts then APP_PKGS = APP_PKGS .. " Qt6TextToSpeech" end

workspace "neosea"
    configurations { "Debug", "Release" }
    architecture "x86_64"
    location "build"
    startproject "neosea"

    language "C++"
    cppdialect "C++23"
    multiprocessorcompile "On"

    targetdir "bin/%{cfg.buildcfg}"
    objdir "obj/%{cfg.buildcfg}/%{prj.name}"

    includedirs { "src", "build/generated", "build/moc" }
    defines { 'NEOSEA_SOURCE_RESOURCES="' .. os.getcwd() .. '/resources"' }
    -- Qt6 exports its type_info with protected visibility, which a -fPIE object
    -- cannot copy-relocate against ("copy relocation against non-copyable
    -- protected symbol _ZTI7QObject").
    pic "On"

    filter "action:gmake*"
        buildoptions { "-Wall", "-Wextra", "-Wno-unused-parameter" }

    filter "configurations:Debug"
        defines { "NEOSEA_DEBUG" }
        symbols "On"
        optimize "Off"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "Speed"
        symbols "On"
    filter {}

-- Everything that decides something: storage, the book model, chapter HTML,
-- typing rules, import, screenplays, exports. QtGui (QTextDocument, QPainter)
-- but no widgets, so the tests link the real code.
project "neosea-core"
    kind "StaticLib"
    files { "src/core/**.h", "src/core/**.cpp" }
    -- moc output, generated into build/moc and #included by the matching .cpp
    prebuildcommands { '"%{wks.location}/../scripts/moc.sh" "%{wks.location}/moc"' }
    buildoptions { pkg("--cflags", CORE_PKGS) }
    refresh_version()

project "neosea"
    kind "WindowedApp"
    files { "src/app/**.h", "src/app/**.cpp" }
    links { "neosea-core" }
    prebuildcommands { '"%{wks.location}/../scripts/moc.sh" "%{wks.location}/moc"' }
    -- the spellcheck dictionaries, once; offline, the system's hunspell ones stand in
    prebuildcommands { '"%{wks.location}/../scripts/fetch-dictionaries.sh" || echo "neosea: dictionaries not fetched, the system ones will be used"' }
    buildoptions { pkg("--cflags", APP_PKGS) }
    linkoptions { pkg("--libs", APP_PKGS) }
    if have_tts then defines { "NEOSEA_HAVE_TTS=1" } end
    refresh_version()

project "neosea-tests"
    kind "ConsoleApp"
    files { "tests/**.h", "tests/**.cpp" }
    links { "neosea-core", "gtest" }
    defines { 'NEOSEA_TEST_FIXTURES="' .. os.getcwd() .. '/tests/fixtures"' }
    buildoptions { pkg("--cflags", CORE_PKGS) }
    linkoptions { pkg("--libs", CORE_PKGS) }
    refresh_version()
