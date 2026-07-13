from __future__ import annotations

import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def extract_braced_definition(source: str, name: str) -> str:
    """Return a function/type definition without depending on line layout."""
    type_match = re.search(
        rf"\b(?:struct|class|enum(?:\s+class)?)\s+{re.escape(name)}\s*\{{",
        source,
    )
    matches = [type_match] if type_match else list(
        re.finditer(rf"\b{re.escape(name)}\s*\(", source)
    )
    for match in matches:
        if match is None:
            continue
        if type_match:
            brace = source.find("{", match.start(), match.end())
        else:
            paren = source.find("(", match.start(), match.end())
            paren_depth = 0
            close_paren = -1
            for index in range(paren, len(source)):
                if source[index] == "(":
                    paren_depth += 1
                elif source[index] == ")":
                    paren_depth -= 1
                    if paren_depth == 0:
                        close_paren = index
                        break
            if close_paren < 0:
                continue
            brace = close_paren + 1
            while brace < len(source) and source[brace].isspace():
                brace += 1
            if brace >= len(source) or source[brace] != "{":
                continue

        depth = 0
        for index in range(brace, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    end = index + 1
                    if end < len(source) and source[end] == ";":
                        end += 1
                    line_start = source.rfind("\n", 0, match.start()) + 1
                    return source[line_start:end]
        break
    raise AssertionError(f"definition for {name} was not found")


def assert_in_order(test: unittest.TestCase, source: str, *needles: str) -> None:
    positions = []
    for needle in needles:
        position = source.find(needle)
        test.assertNotEqual(position, -1, needle)
        positions.append(position)
    test.assertEqual(positions, sorted(positions), needles)


def compile_and_run_cpp(
    test: unittest.TestCase, name: str, source: str
) -> None:
    """Compile and execute a small harness containing production definitions."""
    compiler_text = os.environ.get("CXX")
    if compiler_text:
        compiler = shlex.split(compiler_text, posix=os.name != "nt")
    else:
        executable = next(
            (shutil.which(candidate) for candidate in ("clang++", "g++", "c++")
            if shutil.which(candidate)),
            None,
        )
        if executable is None:
            test.skipTest("a C++17 compiler is required for the behavior harness")
        compiler = [executable]

    with tempfile.TemporaryDirectory(prefix=f"fnql-{name}-") as temp:
        temp_path = Path(temp)
        source_path = temp_path / f"{name}.cpp"
        executable_path = temp_path / (f"{name}.exe" if os.name == "nt" else name)
        source_path.write_text(source, encoding="utf-8")

        # The Win32 VS Code task activates x86 MSVC libraries.  Clang defaults
        # to its host architecture, which is normally x64, and then mixes the
        # two CRTs at link time.  Keep the standalone harness architecture in
        # step with the active MSVC environment.
        target_args: list[str] = []
        compiler_name = Path(compiler[0]).name.lower()
        if (
            os.name == "nt"
            and compiler_name.startswith("clang")
            and os.environ.get("VSCMD_ARG_TGT_ARCH", "").lower() == "x86"
        ):
            target_args.append("--target=i686-pc-windows-msvc")
        compiled = subprocess.run(
            [*compiler, *target_args, "-std=c++17", str(source_path), "-o", str(executable_path)],
            check=False,
            capture_output=True,
            text=True,
        )
        test.assertEqual(
            compiled.returncode,
            0,
            f"{name} harness did not compile:\n{compiled.stdout}\n{compiled.stderr}",
        )
        executed = subprocess.run(
            [str(executable_path)], check=False, capture_output=True, text=True
        )
        test.assertEqual(
            executed.returncode,
            0,
            f"{name} contract failed with code {executed.returncode}:\n"
            f"{executed.stdout}\n{executed.stderr}",
        )


class WorkshopParserBehaviorTests(unittest.TestCase):
    def test_client_and_console_id_parsers_enforce_the_retail_contract(self) -> None:
        """Compile the production parser bodies and execute boundary cases."""
        client = read_source("code/client/cl_workshop.cpp")
        server = read_source("code/server/sv_ccmds.cpp")
        parser_fragments = "\n\n".join(
            (
                re.search(
                    r"constexpr\s+std::size_t\s+kMaxRequiredItems\s*=\s*256\s*;",
                    client,
                ).group(0),
                extract_braced_definition(client, "ParseReport"),
                extract_braced_definition(client, "IsAsciiWhitespace"),
                extract_braced_definition(client, "ParseRequiredItems"),
                extract_braced_definition(client, "CL_ParseRequiredWorkshopItems"),
                extract_braced_definition(server, "SV_ParseWorkshopItemId"),
            )
        )

        harness = f"""
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <system_error>

using uint64_t = std::uint64_t;
using qboolean = int;
constexpr qboolean qfalse = 0;
constexpr qboolean qtrue = 1;

{parser_fragments}

static int Require(bool condition, int code) {{
    return condition ? 0 : code;
}}

int main() {{
    std::array<unsigned long long, 300> items{{}};

    int count = CL_ParseRequiredWorkshopItems(
        " \\t1\\n2\\r\\n18446744073709551615\\f2\\v3 ",
        items.data(), static_cast<int>(items.size()));
    if (int result = Require(count == 4, 10)) return result;
    if (int result = Require(items[0] == 1 && items[1] == 2
        && items[2] == UINT64_MAX && items[3] == 3, 11)) return result;

    count = CL_ParseRequiredWorkshopItems(
        "0 -1 +1 0x10 12x 18446744073709551616 4", items.data(), 300);
    if (int result = Require(count == 1 && items[0] == 4, 12)) return result;

    count = CL_ParseRequiredWorkshopItems("10 20 30 40", items.data(), 2);
    if (int result = Require(count == 2 && items[0] == 10 && items[1] == 20, 13))
        return result;

    std::ostringstream manifest;
    for (unsigned int id = 1; id <= 300; ++id) manifest << id << ' ';
    count = CL_ParseRequiredWorkshopItems(manifest.str().c_str(), items.data(), 300);
    if (int result = Require(count == 256 && items[255] == 256, 14)) return result;
    if (int result = Require(CL_ParseRequiredWorkshopItems(nullptr, items.data(), 4) == 0, 15))
        return result;
    if (int result = Require(CL_ParseRequiredWorkshopItems("1", nullptr, 4) == 0, 16))
        return result;

    uint64_t itemId = 99;
    if (int result = Require(SV_ParseWorkshopItemId("18446744073709551615", &itemId)
        && itemId == UINT64_MAX, 20)) return result;
    if (int result = Require(SV_ParseWorkshopItemId("00042", &itemId) && itemId == 42, 21))
        return result;
    const char *invalid[] = {{"", "0", "-1", "+1", " 1", "1 ", "0x10",
        "12x", "18446744073709551616"}};
    for (const char *text : invalid) {{
        itemId = 99;
        if (SV_ParseWorkshopItemId(text, &itemId) || itemId != 0) return 22;
    }}
    itemId = 99;
    if (SV_ParseWorkshopItemId(nullptr, &itemId) || itemId != 0) return 23;
    return 0;
}}
"""

        compile_and_run_cpp(self, "workshop_parser_harness", harness)


class WorkshopFilesystemSourceTests(unittest.TestCase):
    def test_registration_is_bounded_transactional_and_keeps_transient_items(self) -> None:
        source = read_source("code/qcommon/files.c")
        header = read_source("code/qcommon/qcommon.h")
        register = extract_braced_definition(source, "FS_RegisterWorkshopInstall")
        upsert = extract_braced_definition(source, "FS_UpsertWorkshopInstall")
        commit = extract_braced_definition(source, "FS_EndWorkshopUpdate")
        normalize = extract_braced_definition(source, "FS_NormalizeWorkshopPath")

        self.assertRegex(source, r"#define\s+MAX_WORKSHOP_INSTALLS\s+256")
        self.assertRegex(
            source,
            r"#define\s+MAX_WORKSHOP_INSTALL_PATH\s+1024",
        )
        self.assertRegex(
            source,
            r"#define\s+MAX_WORKSHOP_OSPATH\s+"
            r"\(\s*MAX_WORKSHOP_INSTALL_PATH\s*\+\s*MAX_OSPATH\s*\+\s*1\s*\)",
        )
        self.assertIn("installFolder[MAX_WORKSHOP_INSTALL_PATH]", source)
        self.assertIn("fs_workshopSubscribedStage[MAX_WORKSHOP_INSTALLS]", source)
        self.assertIn("fs_workshopTransient[MAX_WORKSHOP_INSTALLS]", source)
        for name, value in (
            ("REJECTED", "-1"),
            ("UNCHANGED", "0"),
            ("CHANGED", "1"),
        ):
            self.assertRegex(
                header,
                rf"FS_WORKSHOP_REGISTER_{name}\s*=\s*{value}",
            )
        self.assertIn("fsWorkshopRegisterResult_t FS_RegisterWorkshopInstall", header)
        self.assertIn("fsWorkshopRegisterResult_t FS_RegisterWorkshopInstall", register)
        self.assertIn("FS_WORKSHOP_REGISTER_UNCHANGED", upsert)
        self.assertIn("FS_WORKSHOP_REGISTER_CHANGED", upsert)
        self.assertIn("FS_WORKSHOP_REGISTER_REJECTED", upsert)
        self.assertIn("itemId == 0", register)
        self.assertIn("FS_NormalizeWorkshopPath", register)
        self.assertIn("FS_WorkshopPathOwnedByOther", register)
        self.assertIn("FS_WORKSHOP_REGISTER_REJECTED", register)
        self.assertIn("fs_workshopSubscribedStage", register)
        self.assertIn("fs_workshopTransient", register)
        self.assertNotIn("fs_workshopTransient", commit)
        self.assertIn("fs_workshopSubscribedStage", commit)
        self.assertIn("fs_workshopUpdateActive = qfalse", commit)

        self.assertIn("FS_WorkshopPathIsAbsolute", normalize)
        self.assertIn("FS_WorkshopPathHasTraversal", normalize)
        self.assertIn("FS_WorkshopPathHasEmptyComponent", normalize)
        self.assertIn("FS_WorkshopPathIsFilesystemRoot", normalize)
        self.assertIn("(unsigned char)*p < 0x20", normalize)
        self.assertIn("(unsigned char)*p == 0x7f", normalize)
        self.assertIn("length >= MAX_WORKSHOP_INSTALL_PATH", normalize)

    def test_registration_tristate_executes_change_duplicate_and_capacity_cases(self) -> None:
        source = read_source("code/qcommon/files.c")
        header = read_source("code/qcommon/qcommon.h")
        enum_definition = re.search(
            r"typedef\s+enum\s+fsWorkshopRegisterResult_e\s*\{.*?\}\s*"
            r"fsWorkshopRegisterResult_t\s*;",
            header,
            re.DOTALL,
        )
        self.assertIsNotNone(enum_definition)
        fragments = "\n\n".join(
            (
                enum_definition.group(0),
                extract_braced_definition(source, "FS_WorkshopPathEqual"),
                extract_braced_definition(source, "FS_UpsertWorkshopInstall"),
            )
        )
        harness = r"""
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

using uint64_t = std::uint64_t;
using qboolean = int;
#define MAX_WORKSHOP_INSTALLS 256
#define MAX_WORKSHOP_INSTALL_PATH 1024
#define S_COLOR_YELLOW ""

typedef struct {
    uint64_t itemId;
    char installFolder[MAX_WORKSHOP_INSTALL_PATH];
} workshopInstall_t;

static int Q_stricmp(const char *a, const char *b) {
    while (*a && *b) {
        const int difference = std::tolower(static_cast<unsigned char>(*a))
            - std::tolower(static_cast<unsigned char>(*b));
        if (difference) return difference;
        ++a;
        ++b;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

static void Q_strncpyz(char *destination, const char *source, std::size_t size) {
    if (!size) return;
    const std::size_t length = std::strlen(source) < size - 1
        ? std::strlen(source) : size - 1;
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}

static void Com_Printf(const char *, ...) {}

""" + fragments + r"""

int main() {
    workshopInstall_t installs[MAX_WORKSHOP_INSTALLS]{};
    int count = 0;
    if (FS_UpsertWorkshopInstall(installs, &count, 1, "/ugc/1")
        != FS_WORKSHOP_REGISTER_CHANGED || count != 1) return 10;
    if (FS_UpsertWorkshopInstall(installs, &count, 1, "/ugc/1")
        != FS_WORKSHOP_REGISTER_UNCHANGED || count != 1) return 11;
    if (FS_UpsertWorkshopInstall(installs, &count, 1, "/ugc/moved-1")
        != FS_WORKSHOP_REGISTER_CHANGED || count != 1) return 12;
    if (std::strcmp(installs[0].installFolder, "/ugc/moved-1") != 0) return 13;

    char path[64];
    for (std::uint64_t item = 2; item <= MAX_WORKSHOP_INSTALLS; ++item) {
        std::snprintf(path, sizeof(path), "/ugc/%llu",
            static_cast<unsigned long long>(item));
        if (FS_UpsertWorkshopInstall(installs, &count, item, path)
            != FS_WORKSHOP_REGISTER_CHANGED) return 20;
    }
    if (count != MAX_WORKSHOP_INSTALLS) return 21;
    if (FS_UpsertWorkshopInstall(installs, &count, 257, "/ugc/257")
        != FS_WORKSHOP_REGISTER_REJECTED || count != MAX_WORKSHOP_INSTALLS) return 22;
    if (FS_UpsertWorkshopInstall(installs, &count, 1, "/ugc/final-1")
        != FS_WORKSHOP_REGISTER_CHANGED || count != MAX_WORKSHOP_INSTALLS) return 23;
    return 0;
}
"""
        compile_and_run_cpp(self, "workshop_registration", harness)

    def test_install_path_normalization_executes_cross_platform_safety_cases(self) -> None:
        source = read_source("code/qcommon/files.c")
        fragments = "\n\n".join(
            extract_braced_definition(source, name)
            for name in (
                "FS_WorkshopPathIsAbsolute",
                "FS_WorkshopPathHasTraversal",
                "FS_WorkshopPathHasEmptyComponent",
                "FS_WorkshopPathIsFilesystemRoot",
                "FS_NormalizeWorkshopPath",
            )
        )
        harness = r"""
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using qboolean = int;
constexpr qboolean qfalse = 0;
constexpr qboolean qtrue = 1;
#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif
#define MAX_WORKSHOP_INSTALL_PATH 1024

static void Q_strncpyz(char *destination, const char *source, int size) {
    if (size <= 0) return;
    const std::size_t capacity = static_cast<std::size_t>(size);
    const std::size_t length = std::strlen(source) < capacity - 1
        ? std::strlen(source) : capacity - 1;
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}

static void FS_ReplaceSeparators(char *path) {
    for (; *path; ++path) {
        if (*path == '/' || *path == '\\') *path = PATH_SEP;
    }
}

""" + fragments + r"""

int main() {
    std::vector<char> normalized(MAX_WORKSHOP_INSTALL_PATH);
#ifdef _WIN32
    const char *valid = "C:\\Steam\\steamapps\\workshop\\content\\282440\\42";
    const char *root = "C:\\";
    const char *traversal = "C:\\Steam\\..\\escape";
    const char *uncRoot = "\\\\server\\share";
    const char *driveDoubled = "C:\\\\Steam\\workshop";
    const char *uncServerDoubled = "\\\\server\\\\share\\item";
    const char *uncChildDoubled = "\\\\server\\share\\\\item";
#else
    const char *valid = "/steam/steamapps/workshop/content/282440/42";
    const char *root = "/";
    const char *traversal = "/steam/../escape";
    const char *posixDoubled = "/steam//workshop/item";
#endif
    if (!FS_NormalizeWorkshopPath(valid, normalized.data(),
        static_cast<int>(normalized.size()))) return 10;
    if (FS_NormalizeWorkshopPath(root, normalized.data(),
        static_cast<int>(normalized.size()))) return 11;
    if (FS_NormalizeWorkshopPath(traversal, normalized.data(),
        static_cast<int>(normalized.size()))) return 12;
    if (FS_NormalizeWorkshopPath("relative/workshop", normalized.data(),
        static_cast<int>(normalized.size()))) return 13;
#ifdef _WIN32
    if (FS_NormalizeWorkshopPath(uncRoot, normalized.data(),
        static_cast<int>(normalized.size()))) return 14;
    if (FS_NormalizeWorkshopPath(driveDoubled, normalized.data(),
        static_cast<int>(normalized.size()))) return 18;
    if (FS_NormalizeWorkshopPath(uncServerDoubled, normalized.data(),
        static_cast<int>(normalized.size()))) return 19;
    if (FS_NormalizeWorkshopPath(uncChildDoubled, normalized.data(),
        static_cast<int>(normalized.size()))) return 20;
#else
    if (FS_NormalizeWorkshopPath(posixDoubled, normalized.data(),
        static_cast<int>(normalized.size()))) return 18;
#endif
    std::string control(valid);
    control.push_back(static_cast<char>(0x7f));
    if (FS_NormalizeWorkshopPath(control.c_str(), normalized.data(),
        static_cast<int>(normalized.size()))) return 15;

#ifdef _WIN32
    std::string providerMaximum("C:\\");
#else
    std::string providerMaximum("/");
#endif
    providerMaximum.resize(1023, 'x');
    if (!FS_NormalizeWorkshopPath(providerMaximum.c_str(), normalized.data(),
        static_cast<int>(normalized.size()))) return 16;

#ifdef _WIN32
    std::string oversized("C:\\");
#else
    std::string oversized("/");
#endif
    oversized.resize(MAX_WORKSHOP_INSTALL_PATH, 'x');
    if (FS_NormalizeWorkshopPath(oversized.c_str(), normalized.data(),
        static_cast<int>(normalized.size()))) return 17;
    return 0;
}
"""
        compile_and_run_cpp(self, "workshop_path_normalization", harness)

    def test_workshop_mounts_precede_normal_roots_and_preserve_retail_priority(self) -> None:
        source = read_source("code/qcommon/files.c")
        startup = extract_braced_definition(source, "FS_Startup")
        add_directory = extract_braced_definition(source, "FS_AddGameDirectoryInternal")
        add_install = extract_braced_definition(source, "FS_AddWorkshopInstall")
        add_all = extract_braced_definition(source, "FS_AddWorkshopDirectories")
        trusted_marker = extract_braced_definition(source, "FS_HasTrustedBasePakOnDisk")
        loaded_marker = extract_braced_definition(source, "FS_HasQuakeLiveBasePak")

        # Search paths are prepended. Mounting Workshop first therefore leaves
        # retail/base/home sources dominant, while later Workshop IDs dominate
        # earlier IDs just as retail does.
        assert_in_order(
            self,
            startup,
            "scanWorkshopArchives = FS_HasTrustedBasePakOnDisk();",
            'WARNING: Skipping workshop PK3s since pak00 doesn\'t exist.',
            "FS_AddWorkshopDirectories( scanWorkshopArchives );",
            "FS_AddGameDirectory( fs_steampath->string",
            "FS_AddGameDirectory( fs_basepath->string",
        )
        self.assertIn("search->next = fs_searchpaths", add_directory)
        self.assertIn("fs_searchpaths = search", add_directory)
        self.assertIn("workshopItemId", add_directory)
        self.assertIn("rawPath", add_directory)
        self.assertIn("scanArchives", add_directory)
        assert_in_order(
            self,
            add_directory,
            "fs_searchpaths = search",
            "if ( !scanArchives )",
            "Sys_ListFiles( curpath",
        )
        assert_in_order(
            self,
            add_install,
            'FS_AddGameDirectoryInternal( install->installFolder, "", install->itemId, qtrue,',
            "Q_stricmp( directories[i], BASEGAME )",
            'FS_AddGameDirectoryInternal( childPath, "", install->itemId, qtrue,',
        )
        self.assertGreaterEqual(add_install.count("scanArchives"), 3)
        assert_in_order(
            self,
            add_all,
            "fs_numWorkshopSubscribed",
            "fs_numWorkshopTransient",
        )
        self.assertIn("fs_skipWorkshop->integer", add_all)
        self.assertIn('Cvar_VariableIntegerValue( "com_buildScript" )', add_all)
        self.assertIn("FS_WorkshopItemAlreadyMounted", add_all)
        self.assertIn(
            'FS_BuildOSPath( fs_basepath->string, BASEGAME, "pak00.pk3" )',
            trusted_marker,
        )
        self.assertNotIn("fs_searchpaths", trusted_marker)
        self.assertIn("path->pack->workshopItemId == 0", loaded_marker)

    def test_trusted_base_marker_executes_missing_spoof_and_valid_cases(self) -> None:
        source = read_source("code/qcommon/files.c")
        fragments = "\n\n".join(
            (
                extract_braced_definition(source, "FS_HasTrustedBasePakOnDisk"),
                extract_braced_definition(source, "FS_HasQuakeLiveBasePak"),
            )
        )
        harness = r"""
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

using uint64_t = std::uint64_t;
using qboolean = int;
constexpr qboolean qfalse = 0;
constexpr qboolean qtrue = 1;
#define BASEGAME "baseq3"
#define MAX_OSPATH 256

struct cvar_t { const char *string; };
struct pack_t {
    const char *pakGamename;
    const char *pakBasename;
    uint64_t workshopItemId;
};
struct searchpath_t {
    searchpath_t *next;
    pack_t *pack;
};

static cvar_t basePath{"/retail"};
static cvar_t *fs_basepath = &basePath;
static searchpath_t *fs_searchpaths;
static bool markerReadable;
static int markerOpenCount;
static int markerCloseCount;

static char *FS_BuildOSPath(const char *base, const char *game, const char *qpath) {
    static char path[512];
    std::snprintf(path, sizeof(path), "%s/%s/%s", base, game, qpath);
    return path;
}

static FILE *Sys_FOpen(const char *path, const char *mode) {
    ++markerOpenCount;
    if (!markerReadable || std::strcmp(path, "/retail/baseq3/pak00.pk3") != 0
        || std::strcmp(mode, "rb") != 0) return nullptr;
    return reinterpret_cast<FILE *>(static_cast<std::uintptr_t>(1));
}

static int MockClose(FILE *) {
    ++markerCloseCount;
    return 0;
}
#define fclose MockClose

static int Q_stricmpn(const char *left, const char *right, int count) {
    for (int i = 0; i < count; ++i) {
        const unsigned char a = static_cast<unsigned char>(left[i]);
        const unsigned char b = static_cast<unsigned char>(right[i]);
        const int difference = std::tolower(a) - std::tolower(b);
        if (difference || !a || !b) return difference;
    }
    return 0;
}

""" + fragments + r"""

int main() {
    pack_t workshopPak{"baseq3", "pak00", 42};
    searchpath_t workshopPath{nullptr, &workshopPak};
    fs_searchpaths = &workshopPath;

    markerReadable = false;
    if (FS_HasTrustedBasePakOnDisk()) return 10;
    if (markerOpenCount != 1 || markerCloseCount != 0) return 11;
    if (FS_HasQuakeLiveBasePak()) return 12;

    markerReadable = true;
    if (!FS_HasTrustedBasePakOnDisk()) return 20;
    if (markerOpenCount != 2 || markerCloseCount != 1) return 21;

    pack_t normalPak{"baseq3", "pak00", 0};
    searchpath_t normalPath{&workshopPath, &normalPak};
    fs_searchpaths = &normalPath;
    if (!FS_HasQuakeLiveBasePak()) return 30;

    basePath.string = "";
    if (FS_HasTrustedBasePakOnDisk()) return 40;
    if (markerOpenCount != 2) return 41;
    return 0;
}
"""
        compile_and_run_cpp(self, "workshop_trusted_base_marker", harness)

    def test_referenced_items_are_numeric_deduplicated_and_retail_formatted(self) -> None:
        source = read_source("code/qcommon/files.c")
        referenced = extract_braced_definition(source, "FS_ReferencedWorkshopItems")

        self.assertIn("for ( search = fs_searchpaths", referenced)
        self.assertIn("search->pack == NULL || search->pack->referenced == 0", referenced)
        self.assertIn("itemId = search->pack->workshopItemId", referenced)
        self.assertIn("if ( itemId == 0 )", referenced)
        self.assertIn("seen[i] == itemId", referenced)
        self.assertIn('"%llu "', referenced)
        self.assertNotIn("FNQL_Steam_", source)


class WorkshopCommonBehaviorTests(unittest.TestCase):
    def test_subscription_scheduler_executes_independent_bounded_updates(self) -> None:
        source = read_source("code/platform/fnql_workshop.cpp")
        constants = []
        for name in ("kRetailWorkshopLimit", "kSubscriptionSettleMsec"):
            match = re.search(
                rf"constexpr\s+[^;]+\s+{name}\s*=\s*[^;]+;",
                source,
            )
            self.assertIsNotNone(match, name)
            constants.append(match.group(0))
        type_fragments = "\n\n".join(
            (
                *constants,
                extract_braced_definition(source, "SubscriptionAction"),
                extract_braced_definition(source, "PendingSubscriptionAction"),
            )
        )
        function_fragments = "\n\n".join(
            (
                extract_braced_definition(source, "CanTrackSubscriptionAction"),
                extract_braced_definition(source, "ScheduleSubscriptionRefresh"),
            )
        )
        harness = r"""
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

static int fakeMilliseconds;
static int Com_Milliseconds() { return fakeMilliseconds; }

""" + type_fragments + r"""

std::array<PendingSubscriptionAction, kRetailWorkshopLimit>
    pendingSubscriptionActions;

""" + function_fragments + r"""

static PendingSubscriptionAction *Find(std::uint64_t itemId) {
    for (auto &pending : pendingSubscriptionActions) {
        if (pending.active && pending.itemId == itemId) return &pending;
    }
    return nullptr;
}

static std::size_t ActiveCount() {
    return static_cast<std::size_t>(std::count_if(
        pendingSubscriptionActions.begin(), pendingSubscriptionActions.end(),
        [](const PendingSubscriptionAction &pending) { return pending.active; }));
}

int main() {
    fakeMilliseconds = 100;
    ScheduleSubscriptionRefresh(11, SubscriptionAction::Subscribe);
    ScheduleSubscriptionRefresh(22, SubscriptionAction::Unsubscribe);
    if (ActiveCount() != 2 || !Find(11) || !Find(22)) return 10;
    if (Find(11)->action != SubscriptionAction::Subscribe
        || Find(22)->action != SubscriptionAction::Unsubscribe) return 11;
    if (Find(11)->nextAt != 100 + kSubscriptionSettleMsec) return 12;

    for (std::uint64_t itemId = 100;
        itemId < 100 + (kRetailWorkshopLimit - 2); ++itemId) {
        if (!CanTrackSubscriptionAction(itemId)) return 20;
        ScheduleSubscriptionRefresh(itemId, SubscriptionAction::Subscribe);
    }
    if (ActiveCount() != kRetailWorkshopLimit) return 21;
    if (CanTrackSubscriptionAction(999)) return 22;
    if (!CanTrackSubscriptionAction(11)) return 23;

    fakeMilliseconds = 500;
    ScheduleSubscriptionRefresh(11, SubscriptionAction::Unsubscribe);
    if (ActiveCount() != kRetailWorkshopLimit) return 24;
    if (!Find(22) || Find(22)->action != SubscriptionAction::Unsubscribe) return 25;
    if (!Find(11) || Find(11)->action != SubscriptionAction::Unsubscribe
        || Find(11)->startedAt != 500) return 26;
    return 0;
}
"""
        compile_and_run_cpp(self, "workshop_subscription_scheduler", harness)

    def test_state_machine_separates_actions_snapshot_retries_and_download_owners(self) -> None:
        source = read_source("code/platform/fnql_workshop.cpp")
        header = read_source("code/qcommon/qcommon.h")
        client = read_source("code/client/cl_workshop.cpp")
        frame = extract_braced_definition(source, "Com_WorkshopFrame")
        provider_changed = extract_braced_definition(source, "Com_WorkshopProviderChanged")
        claim = extract_braced_definition(source, "Com_WorkshopClaimConnectionDownloads")
        begin = extract_braced_definition(client, "CL_Workshop_BeginRequiredDownloads")
        manual_download = extract_braced_definition(source, "Com_WorkshopDownloadItem")
        subscribe = extract_braced_definition(source, "Com_WorkshopSubscribeItem")
        unsubscribe = extract_braced_definition(source, "Com_WorkshopUnsubscribeItem")
        event = extract_braced_definition(source, "WorkshopProviderEvent")

        self.assertIn("std::array<PendingSubscriptionAction, kRetailWorkshopLimit>", source)
        self.assertIn("pendingSubscriptionActions", source)
        self.assertIn("SnapshotRefreshState snapshotRefresh", source)
        self.assertIn("for (PendingSubscriptionAction &pending", frame)
        self.assertIn("pending = {}", frame)
        self.assertIn("ScheduleSnapshotRefresh(restartFilesystem, 1u)", frame)
        self.assertIn("snapshotRefresh.active", frame)
        self.assertIn('Cvar_VariableIntegerValue("cl_workshopDownloadActive")', frame)
        self.assertIn("snapshotRefresh.nextAt = now + kSubscriptionSettleMsec", frame)
        self.assertIn("kSnapshotPollMsec", frame)
        self.assertIn("SnapshotRefreshResult::WaitingForInstall", frame)

        self.assertIn("ResetRefreshState();", provider_changed)
        self.assertIn("ScheduleSnapshotRefresh(true, 1u);", provider_changed)
        self.assertNotIn("RefreshSubscribedSnapshot", provider_changed)

        self.assertIn("void Com_WorkshopClaimConnectionDownloads", header)
        self.assertIn("manualDownload.active", claim)
        self.assertIn("ResetManualDownload(true)", claim)
        assert_in_order(
            self,
            begin,
            "FNQL_Steam_Available",
            "Com_WorkshopClaimConnectionDownloads();",
            "workshopQueue.itemCount",
            "SetQueueActive( true )",
            "PreflightQueue()",
        )
        automatic_owner_guard = manual_download[
            manual_download.index('Cvar_VariableIntegerValue("cl_workshopDownloadActive")') :
        ]
        assert_in_order(
            self,
            automatic_owner_guard,
            'Cvar_VariableIntegerValue("cl_workshopDownloadActive")',
            "return qfalse",
            "manualDownload.active",
            "FNQL_Steam_GetItemState",
        )

        for action, provider_call in (
            (subscribe, "FNQL_Steam_SubscribeItem"),
            (unsubscribe, "FNQL_Steam_UnsubscribeItem"),
        ):
            assert_in_order(
                self,
                action,
                "CanTrackSubscriptionAction",
                provider_call,
                "ScheduleSubscriptionRefresh",
            )
            self.assertNotIn("RegisterInstalledItem", action)
            self.assertNotIn("FS_Reload", action)

        installed_branch = event[event.index("FNQL_STEAM_EVENT_UGC_ITEM_INSTALLED") :]
        assert_in_order(
            self,
            installed_branch,
            "manualDownload.active",
            "manualDownload.completionHint = true",
            "return;",
            "ScheduleSnapshotRefresh(true, 1u)",
        )

    def test_registration_callers_distinguish_rejection_change_and_no_change(self) -> None:
        common = read_source("code/platform/fnql_workshop.cpp")
        client = read_source("code/client/cl_workshop.cpp")
        refresh = extract_braced_definition(common, "RefreshSubscribedSnapshot")
        common_register = extract_braced_definition(common, "RegisterInstalledItem")
        finish_manual = extract_braced_definition(common, "FinishManualDownload")
        client_register = extract_braced_definition(client, "RegisterInstalledItem")

        assert_in_order(
            self,
            refresh,
            "FS_BeginWorkshopUpdate();",
            "== FS_WORKSHOP_REGISTER_REJECTED",
            "FS_CancelWorkshopUpdate();",
            "FS_EndWorkshopUpdate()",
        )
        self.assertIn("InstalledRegistrationStatus::Rejected", common_register)
        self.assertIn("InstalledRegistrationStatus::Changed", common_register)
        self.assertIn("InstalledRegistrationStatus::Unchanged", common_register)
        self.assertIn("registration == InstalledRegistrationStatus::Changed", finish_manual)
        self.assertEqual(finish_manual.count("FS_Reload();"), 1)

        self.assertIn("registration == FS_WORKSHOP_REGISTER_REJECTED", client_register)
        self.assertIn("registration == FS_WORKSHOP_REGISTER_CHANGED", client_register)
        registration_branch = client_register[
            client_register.index("const fsWorkshopRegisterResult_t registration") :
        ]
        assert_in_order(
            self,
            registration_branch,
            "FS_WORKSHOP_REGISTER_REJECTED",
            "return false",
            "FS_WORKSHOP_REGISTER_CHANGED",
            "filesystemRestartRequired = true",
            "return true",
        )


class WorkshopServerSourceTests(unittest.TestCase):
    def test_references_publish_to_02cb_without_gameserver_identity_dependency(self) -> None:
        platform = read_source("code/server/sv_platform.cpp")
        init = read_source("code/server/sv_init.cpp")
        publish = extract_braced_definition(platform, "SV_PublishWorkshopReferences")
        spawn = extract_braced_definition(init, "SV_SpawnServer")
        provider_event = extract_braced_definition(platform, "SteamProviderEvent")

        self.assertIn("QL_STEAM_REFERENCED_CONFIGSTRING = 0x2cb", platform)
        assert_in_order(
            self,
            publish,
            "FS_ReferencedWorkshopItems()",
            'Cvar_Set( "sv_referencedSteamworks", references )',
            "SV_SetConfigstring( QL_STEAM_REFERENCED_CONFIGSTRING, references )",
        )
        for forbidden in (
            "QL_STEAM_SERVER_ID_CONFIGSTRING",
            "FNQL_Steam_GetGameServerSteamId",
            "PublishSteamGameServerIdentity",
        ):
            self.assertNotIn(forbidden, publish)

        self.assertIn("SV_PublishWorkshopReferences();", spawn)
        self.assertIn("FNQL_STEAM_EVENT_GAME_SERVER_CONNECTED", provider_event)
        self.assertIn("SV_PublishWorkshopReferences();", provider_event)

    def test_commands_accept_one_strict_uint64_and_use_common_workshop_actions(self) -> None:
        source = read_source("code/server/sv_ccmds.cpp")
        run = extract_braced_definition(source, "SV_RunWorkshopCommand")
        parser = extract_braced_definition(source, "SV_ParseWorkshopItemId")

        self.assertIn("Cmd_Argc() != 2", run)
        self.assertIn("SV_ParseWorkshopItemId( Cmd_Argv( 1 ), &itemId )", run)
        self.assertIn("*cursor < '0' || *cursor > '9'", parser)
        self.assertIn("value > ( maxValue - digit ) / 10u", parser)
        self.assertIn("if ( !value )", parser)
        for command, action in (
            ("steam_downloadugc", "Com_WorkshopDownloadItem"),
            ("steam_subscribeugc", "Com_WorkshopSubscribeItem"),
            ("steam_unsubscribeugc", "Com_WorkshopUnsubscribeItem"),
        ):
            self.assertIn(f'"{command}"', source)
            self.assertIn(action, source)


class WorkshopClientSourceTests(unittest.TestCase):
    def test_configstring_02cb_preempts_legacy_downloads_only_for_workshop_protocol(self) -> None:
        source = read_source("code/client/cl_main.cpp")
        init_downloads = extract_braced_definition(source, "CL_InitDownloads")
        resume = extract_braced_definition(source, "CL_ResumeDownloadsAfterWorkshop")

        self.assertIn("QL_STEAM_REFERENCED_CONFIGSTRING = 0x2cb", init_downloads)
        assert_in_order(
            self,
            init_downloads,
            "ForWireProfile( clc.netchan.wireProfile )",
            "!clc.demoplaying",
            "activeContract.Has( fnql::protocol::Capability::WorkshopContent )",
            "CL_Workshop_BeginRequiredDownloads",
            "CL_InitLegacyDownloads();",
        )
        self.assertIn("CL_InitLegacyDownloads();", resume)

    def test_required_downloads_are_sequential_and_poll_authoritative_state(self) -> None:
        source = read_source("code/client/cl_workshop.cpp")
        start = extract_braced_definition(source, "StartNextDownload")
        event = extract_braced_definition(source, "WorkshopSteamEvent")
        frame = extract_braced_definition(source, "CL_Workshop_Frame")
        finish = extract_braced_definition(source, "FinishQueue")
        progress = extract_braced_definition(source, "UpdateProgress")

        self.assertEqual(start.count("FNQL_Steam_DownloadItem"), 1)
        assert_in_order(
            self,
            start,
            "FNQL_Steam_DownloadItem",
            "workshopQueue.activeIndex = i",
            "item.disposition = ItemDisposition::Downloading",
            "return true",
        )
        self.assertIn("FNQL_Steam_GetItemDownloadInfo", progress)
        self.assertIn("QueryItemState", frame)
        self.assertIn("FNQL_STEAM_UGC_ITEM_STATE_INSTALLED", frame)
        self.assertIn("StartNextDownload", frame)

        # Provider callbacks are hints; installation state is verified by the
        # per-frame poll before any item is registered or the FS is restarted.
        self.assertIn("callbackCompletionHint = true", event)
        self.assertNotIn("RegisterInstalledItem", event)
        self.assertNotIn("FS_Restart", event)
        self.assertEqual(finish.count("FS_Restart"), 1)
        assert_in_order(
            self,
            finish,
            "FS_Restart( clc.checksumFeed )",
            "QueuePhase::WaitAfterRestart",
        )

    def test_queue_lifecycle_resets_on_disconnect_and_shuts_down_cleanly(self) -> None:
        main = read_source("code/client/cl_main.cpp")
        init = extract_braced_definition(main, "CL_Init")
        disconnect = extract_braced_definition(main, "CL_Disconnect")
        shutdown = extract_braced_definition(main, "CL_Shutdown")
        frame = extract_braced_definition(main, "CL_Frame")

        self.assertIn("CL_Workshop_Init();", init)
        self.assertIn("CL_Workshop_Reset();", disconnect)
        self.assertIn("CL_Workshop_Shutdown();", shutdown)
        self.assertIn("CL_Workshop_Frame();", frame)

    def test_download_info_executes_live_progress_and_cvar_fallback_contract(self) -> None:
        source = read_source("code/client/cl_main.cpp")
        fragments = "\n\n".join(
            (
                extract_braced_definition(source, "CL_ParseUnsignedLongLongString"),
                extract_braced_definition(source, "CL_GetWorkshopDownloadInfo"),
            )
        )
        harness = r"""
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using qboolean = int;
constexpr qboolean qfalse = 0;
constexpr qboolean qtrue = 1;
using fnqlSteamResult_t = int;
constexpr fnqlSteamResult_t FNQL_STEAM_RESULT_OK = 0;
constexpr fnqlSteamResult_t FNQL_STEAM_RESULT_FAILED = 1;
constexpr std::uint64_t FNQL_STEAM_CAP_UGC = 1;

static bool providerAvailable;
static fnqlSteamResult_t progressResult = FNQL_STEAM_RESULT_FAILED;
static std::uint64_t liveDownloaded;
static std::uint64_t liveTotal;
static std::uint64_t queriedItem;
static std::string cvarItem;
static std::string cvarDownloaded;
static std::string cvarTotal;

static qboolean FNQL_Steam_Available(std::uint64_t) {
    return providerAvailable ? qtrue : qfalse;
}

static fnqlSteamResult_t FNQL_Steam_GetItemDownloadInfo(
    std::uint64_t itemId, std::uint64_t *downloaded, std::uint64_t *total) {
    queriedItem = itemId;
    if (progressResult == FNQL_STEAM_RESULT_OK) {
        *downloaded = liveDownloaded;
        *total = liveTotal;
    }
    return progressResult;
}

static void Cvar_VariableStringBuffer(const char *name, char *buffer, int size) {
    const std::string *value = &cvarItem;
    if (std::strcmp(name, "cl_downloadCount") == 0) value = &cvarDownloaded;
    if (std::strcmp(name, "cl_downloadSize") == 0) value = &cvarTotal;
    if (size <= 0) return;
    std::snprintf(buffer, static_cast<std::size_t>(size), "%s", value->c_str());
}

""" + fragments + r"""

int main() {
    cvarItem = "42";
    cvarDownloaded = "7";
    cvarTotal = "11";
    providerAvailable = true;
    progressResult = FNQL_STEAM_RESULT_OK;
    liveDownloaded = 5;
    liveTotal = 10;
    unsigned long long downloaded = 99;
    unsigned long long total = 99;
    if (!CL_GetWorkshopDownloadInfo(42, 0, &downloaded, &total)
        || downloaded != 5 || total != 10 || queriedItem != 42) return 10;

    progressResult = FNQL_STEAM_RESULT_FAILED;
    downloaded = total = 99;
    if (!CL_GetWorkshopDownloadInfo(42, 0, &downloaded, &total)
        || downloaded != 7 || total != 11) return 11;

    downloaded = total = 99;
    if (CL_GetWorkshopDownloadInfo(43, 0, &downloaded, &total)
        || downloaded != 0 || total != 0) return 12;

    downloaded = total = 99;
    if (!CL_GetWorkshopDownloadInfo(0, 0, &downloaded, &total)
        || downloaded != 7 || total != 11 || queriedItem != 42) return 13;

    providerAvailable = false;
    cvarDownloaded = "malformed";
    cvarTotal = "18446744073709551616";
    downloaded = total = 99;
    if (!CL_GetWorkshopDownloadInfo(42, 0, &downloaded, &total)
        || downloaded != 0 || total != 0) return 14;
    if (!CL_GetWorkshopDownloadInfo(42, 0, nullptr, nullptr)) return 15;

    providerAvailable = true;
    progressResult = FNQL_STEAM_RESULT_OK;
    liveDownloaded = 9;
    liveTotal = 12;
    cvarItem = "4294967298";
    downloaded = total = 0;
    if (!CL_GetWorkshopDownloadInfo(2, 1, &downloaded, &total)
        || queriedItem != 4294967298ULL || downloaded != 9 || total != 12) return 16;
    return 0;
}
"""
        compile_and_run_cpp(self, "workshop_download_info", harness)


class WorkshopLifecycleAndBuildSourceTests(unittest.TestCase):
    def test_provider_ugc_flags_limits_and_high_priority_requests_match_retail(self) -> None:
        api = read_source("code/platform/fnql_steam_api.h")
        common = read_source("code/platform/fnql_workshop.cpp")
        client = read_source("code/client/cl_workshop.cpp")

        for name, bit in (
            ("NONE", "0"),
            ("SUBSCRIBED", "1u << 0"),
            ("LEGACY", "1u << 1"),
            ("INSTALLED", "1u << 2"),
            ("NEEDS_UPDATE", "1u << 3"),
            ("DOWNLOADING", "1u << 4"),
            ("DOWNLOAD_PENDING", "1u << 5"),
        ):
            self.assertRegex(
                api,
                rf"FNQL_STEAM_UGC_ITEM_STATE_{name}\s*=\s*{re.escape(bit)}",
            )

        self.assertRegex(common, r"kRetailWorkshopLimit\s*=\s*256")
        self.assertIn("FNQL_STEAM_RESULT_BUFFER_TOO_SMALL", common)
        self.assertIn("complete.resize(kRetailWorkshopLimit)", common)
        self.assertIn("FNQL_Steam_DownloadItem(itemId, qtrue)", common)
        self.assertIn("FNQL_Steam_DownloadItem( item.id, qtrue )", client)

    def test_provider_lifecycle_orders_workshop_work_around_provider_calls(self) -> None:
        source = read_source("code/qcommon/common.c")
        header = read_source("code/qcommon/qcommon.h")
        loader = read_source("code/platform/fnql_steam.cpp")
        init = extract_braced_definition(source, "Com_Init")
        frame = extract_braced_definition(source, "Com_Frame")
        shutdown = extract_braced_definition(source, "Com_Shutdown")
        start_game_server = extract_braced_definition(loader, "FNQL_Steam_StartGameServer")

        assert_in_order(
            self,
            init,
            "SV_Init();",
            "SV_RegisterGameCvars();",
            "FNQL_Steam_Init(",
            'SV_SteamGameServerStart( "" );',
            "Com_WorkshopInit();",
        )
        self.assertEqual(source.count('SV_SteamGameServerStart( "" );'), 2)
        self.assertIn("void SV_SteamGameServerStart( const char *mapName );", header)
        assert_in_order(self, frame, "FNQL_Steam_Pump();", "Com_WorkshopFrame();")
        assert_in_order(
            self,
            frame,
            "FNQL_Steam_Reconfigure(",
            'SV_SteamGameServerStart( "" );',
            "Com_WorkshopProviderChanged();",
        )
        assert_in_order(
            self,
            start_game_server,
            "state.provider->start_game_server(config)",
            "result == FNQL_STEAM_RESULT_OK",
            'RefreshProviderCapabilities("GameServer startup")',
        )
        assert_in_order(self, shutdown, "Com_WorkshopShutdown();", "FNQL_Steam_Shutdown();")

    def test_dynamic_capabilities_refresh_after_callbacks_and_before_workshop_polling(self) -> None:
        loader = read_source("code/platform/fnql_steam.cpp")
        api = read_source("code/platform/fnql_steam_api.h")
        common = read_source("code/qcommon/common.c")
        refresh = extract_braced_definition(loader, "RefreshProviderCapabilities")
        validated = extract_braced_definition(loader, "ValidatedCapabilities")
        pump = extract_braced_definition(loader, "FNQL_Steam_Pump")
        host_event = extract_braced_definition(loader, "HostEvent")
        start = extract_braced_definition(loader, "FNQL_Steam_StartGameServer")
        stop = extract_braced_definition(loader, "FNQL_Steam_StopGameServer")
        frame = extract_braced_definition(common, "Com_Frame")

        self.assertIn("uint64_t reportedCapabilities", validated)
        ugc_validation = validated[
            validated.index("if (!provider->get_subscribed_items") :
            validated.index("if (!provider->request_user_stats")
        ]
        for provider_field in (
            "get_subscribed_items",
            "get_item_install_info",
            "get_item_download_info",
            "download_item",
            "subscribe_item",
            "unsubscribe_item",
            "get_item_state",
        ):
            self.assertIn(provider_field, ugc_validation)
        self.assertIn("capabilities &= ~FNQL_STEAM_CAP_UGC", ugc_validation)
        self.assertIn("For a GAME_SERVER-only startup", api)
        self.assertIn("get_status.capabilities is the", api)
        self.assertIn("must not initialize CLIENT", api)
        status_branch = refresh[refresh.index("if (state.provider->get_status)") :]
        assert_in_order(
            self,
            status_branch,
            "state.provider->get_status(&status)",
            "!= FNQL_STEAM_RESULT_OK",
            "return;",
            "reportedCapabilities = status.capabilities",
            "ValidatedCapabilities(",
            "state.capabilities = capabilities",
        )
        failed_probe_path = status_branch[
            : status_branch.index("reportedCapabilities = status.capabilities")
        ]
        self.assertNotIn("state.capabilities =", failed_probe_path)
        self.assertNotIn("ValidatedCapabilities(", failed_probe_path)
        assert_in_order(
            self,
            refresh,
            "state.capabilities = capabilities",
            'SetStatusCvars("active", state.provider->info.name',
            "SV_RefreshPlatformServiceCvars();",
        )
        assert_in_order(
            self,
            pump,
            "state.provider->run_callbacks();",
            "Com_Milliseconds()",
            "state.nextCapabilityRefresh",
            'RefreshProviderCapabilities("the callback pump")',
            "state.nextCapabilityRefresh = now + 1000u",
        )
        self.assertIn("FNQL_STEAM_EVENT_GAME_SERVER_CONNECTED", host_event)
        self.assertIn("FNQL_STEAM_EVENT_GAME_SERVER_DISCONNECTED", host_event)
        self.assertEqual(host_event.count("state.nextCapabilityRefresh = 0"), 2)
        assert_in_order(self, frame, "FNQL_Steam_Pump();", "Com_WorkshopFrame();")
        assert_in_order(
            self,
            start,
            "state.provider->start_game_server(config)",
            'RefreshProviderCapabilities("GameServer startup")',
        )
        assert_in_order(
            self,
            stop,
            "state.provider->stop_game_server();",
            'RefreshProviderCapabilities("GameServer shutdown")',
        )

    def test_ugc_query_contract_makes_synchronous_reentry_cleanup_explicit(self) -> None:
        api = read_source("code/platform/fnql_steam_api.h")
        docs = read_source("docs/fnql/STEAM_PROVIDER.md")

        event_contract = api[
            api.index("The completed result snapshot is already readable") :
            api.index("FNQL_STEAM_EVENT_UGC_QUERY_COMPLETE")
        ]
        assert_in_order(
            self,
            event_contract,
            "provider detaches",
            "pending call-result",
            "clears its shared in-flight ownership",
            "handler may re-enter request_ugc_query safely",
            "non-I/O completion",
            "captured native query remains live during delivery",
            "released exactly",
            "using that local handle",
        )
        assert_in_order(
            self,
            event_contract,
            "I/O failure",
            "releases the captured handle",
            "before emitting failure",
        )

        query_contract = api[
            api.index("Optional trailing asynchronous all-UGC query extension") :
            api.index("fnqlSteamResult_t (FNQL_STEAM_CALL *request_ugc_query)")
        ]
        assert_in_order(
            self,
            query_contract,
            "accepted",
            "returns PENDING",
            "captures",
            "detaches its call-result",
            "clears shared ownership",
            "branching solely on I/O failure",
            "copies the\n\t * bounded rows",
            "emits FNQL_STEAM_EVENT_UGC_QUERY_COMPLETE synchronously",
            "captured query remains live",
            "releases that exact local handle",
        )
        self.assertIn(
            "NULL payload without I/O failure is a successful empty completion",
            query_contract,
        )
        self.assertIn("non-OK native result", query_contract)
        assert_in_order(
            self,
            query_contract,
            "I/O failure it releases the captured handle first",
            "then emits failed",
        )
        self.assertIn("count-then-fetch", query_contract)
        self.assertIn("until the next accepted", query_contract)

        assert_in_order(
            self,
            docs,
            "captures the native query",
            "detaches its call-result",
            "clears the old shared in-flight ownership",
            "may start a replacement query re-entrantly",
            "captured local handle",
        )
        assert_in_order(
            self,
            docs,
            "A non-I/O",
            "publishes `FNQL_STEAM_EVENT_UGC_QUERY_COMPLETE`",
            "native query remains live",
            "releases that exact handle after event delivery",
        )
        self.assertIn("branches solely on the I/O-failure flag", docs)
        self.assertIn("null callback payload is terminal but is not a failure", docs)
        self.assertIn("zero-row successful completion", docs)
        self.assertIn("non-OK raw", docs)
        assert_in_order(
            self,
            docs,
            "Only the I/O-failure lane",
            "releases the captured query first",
            "then emits the",
            "failed completion",
        )
        self.assertIn("canceled request emits no ambiguous", docs)
        self.assertIn("stays client-owned unless", docs)

    def test_snapshot_failure_preserves_last_good_registration_and_polling_fallback(self) -> None:
        source = read_source("code/platform/fnql_workshop.cpp")
        refresh = extract_braced_definition(source, "RefreshSubscribedSnapshot")
        init = extract_braced_definition(source, "Com_WorkshopInit")
        frame = extract_braced_definition(source, "Com_WorkshopFrame")
        schedule = extract_braced_definition(source, "ScheduleSnapshotRefresh")
        fallback = extract_braced_definition(source, "ScheduleFallbackSnapshotPoll")

        assert_in_order(
            self,
            refresh,
            "!FNQL_Steam_Available",
            "return SnapshotRefreshResult::Retry",
            "FS_BeginWorkshopUpdate();",
            "FS_WORKSHOP_REGISTER_REJECTED",
            "FS_CancelWorkshopUpdate();",
            "FS_EndWorkshopUpdate()",
        )
        self.assertIn("waitingForInstall = true", refresh)
        self.assertIn("SnapshotRefreshResult::WaitingForInstall", refresh)
        self.assertIn("SnapshotRefreshResult::Complete", refresh)
        self.assertIn("FNQL_Steam_AddEventSink", init)
        self.assertIn("polling remains active", init)
        scheduling_branch = init[init.index("const SnapshotRefreshResult result") :]
        assert_in_order(
            self,
            scheduling_branch,
            "result != SnapshotRefreshResult::Complete",
            "ScheduleSnapshotRefresh(true, kSubscriptionSettleMsec)",
            "else",
            "ScheduleFallbackSnapshotPoll(kSnapshotPollMsec)",
        )
        self.assertNotIn("eventSinkRegistered", scheduling_branch)
        self.assertRegex(
            source,
            r"kSnapshotPollMsec\s*=\s*30u\s*\*\s*1000u",
        )
        self.assertIn("snapshotRefresh.fallbackPoll", schedule)
        self.assertIn("snapshotRefresh = {}", schedule)
        self.assertIn("if (snapshotRefresh.active)", fallback)
        self.assertIn("snapshotRefresh.fallbackPoll = true", fallback)
        self.assertIn("snapshotRefresh.nextAt = now + delayMsec", fallback)
        self.assertIn("FNQL_Steam_GetItemState", frame)
        self.assertIn("snapshotRefresh.active", frame)
        self.assertIn("SnapshotRefreshResult::Retry", source)
        self.assertIn("kSnapshotPollMsec", frame)
        completion_branch = frame[frame.index("if (result == SnapshotRefreshResult::Complete)") :]
        assert_in_order(
            self,
            completion_branch,
            "snapshotRefresh = {}",
            "ScheduleFallbackSnapshotPoll(kSnapshotPollMsec)",
        )
        self.assertIn("kManualDownloadTimeoutMsec", frame)

    def test_provider_install_paths_are_bounded_and_nul_terminated(self) -> None:
        api = read_source("code/platform/fnql_steam_api.h")
        loader = read_source("code/platform/fnql_steam.cpp")
        common = read_source("code/platform/fnql_workshop.cpp")
        client = read_source("code/client/cl_workshop.cpp")
        files = read_source("code/qcommon/files.c")
        get_install_info = extract_braced_definition(loader, "FNQL_Steam_GetItemInstallInfo")
        read_installed = extract_braced_definition(common, "ReadInstalledItem")
        client_register = extract_braced_definition(client, "RegisterInstalledItem")

        self.assertRegex(api, r"#define\s+FNQL_STEAM_PATH_CAPACITY\s+1024u")
        self.assertRegex(
            files,
            r"#define\s+MAX_WORKSHOP_INSTALL_PATH\s+1024",
        )
        self.assertIn("installFolder[MAX_WORKSHOP_INSTALL_PATH]", files)
        self.assertIn("static char ospath[2][MAX_WORKSHOP_OSPATH]", files)

        assert_in_order(
            self,
            get_install_info,
            "std::memset(folder, 0, folderCapacity)",
            "state.provider->get_item_install_info",
            "result != FNQL_STEAM_RESULT_OK",
            "folder[0] = '\\0'",
            "std::memchr(folder, '\\0', folderCapacity)",
            "return FNQL_STEAM_RESULT_FAILED",
        )
        self.assertEqual(get_install_info.count("folder[0] = '\\0'"), 2)
        self.assertGreaterEqual(get_install_info.count("*sizeOnDisk = 0"), 3)
        self.assertGreaterEqual(get_install_info.count("*timestamp = 0"), 3)
        assert_in_order(
            self,
            read_installed,
            "FNQL_Steam_GetItemInstallInfo",
            "folder[folderCapacity - 1] = '\\0'",
            "installResult != FNQL_STEAM_RESULT_OK",
        )
        self.assertGreaterEqual(client_register.count("folder.back() = '\\0'"), 2)
        assert_in_order(
            self,
            client_register,
            "FNQL_Steam_GetItemInstallInfo",
            "folder.back() = '\\0'",
            "FS_RegisterWorkshopInstall",
        )

    def test_provider_install_info_executes_unterminated_buffer_rejection(self) -> None:
        loader = read_source("code/platform/fnql_steam.cpp")
        get_install_info = extract_braced_definition(loader, "FNQL_Steam_GetItemInstallInfo")
        harness = r"""
#include <cstdint>
#include <cstdio>
#include <cstring>

using uint64_t = std::uint64_t;
using uint32_t = std::uint32_t;
using qboolean = int;
constexpr qboolean qfalse = 0;
constexpr qboolean qtrue = 1;
using fnqlSteamResult_t = int;
constexpr fnqlSteamResult_t FNQL_STEAM_RESULT_OK = 0;
constexpr fnqlSteamResult_t FNQL_STEAM_RESULT_FAILED = 1;
constexpr fnqlSteamResult_t FNQL_STEAM_RESULT_UNAVAILABLE = 2;
constexpr fnqlSteamResult_t FNQL_STEAM_RESULT_INVALID_ARGUMENT = 3;
constexpr uint64_t FNQL_STEAM_CAP_UGC = 1;

enum class ProviderMode { Good, Unterminated, Failed };
static ProviderMode providerMode;
static bool providerAvailable = true;

static fnqlSteamResult_t FakeGetInstallInfo(uint64_t, char *folder,
    uint32_t capacity, uint64_t *sizeOnDisk, uint32_t *timestamp) {
    std::memset(folder, 'x', capacity);
    if (sizeOnDisk) *sizeOnDisk = 123;
    if (timestamp) *timestamp = 456;
    if (providerMode == ProviderMode::Good && capacity) folder[capacity - 1] = '\0';
    return providerMode == ProviderMode::Failed
        ? FNQL_STEAM_RESULT_FAILED : FNQL_STEAM_RESULT_OK;
}

struct FakeProvider {
    fnqlSteamResult_t (*get_item_install_info)(
        uint64_t, char *, uint32_t, uint64_t *, uint32_t *);
};
struct FakeState { FakeProvider *provider; };
static FakeProvider provider{FakeGetInstallInfo};
static FakeState state{&provider};

#define FNQL_HAS_PROVIDER_FIELD(field) true
static qboolean FNQL_Steam_Available(uint64_t) {
    return providerAvailable ? qtrue : qfalse;
}
static fnqlSteamResult_t UnavailableResult() {
    return FNQL_STEAM_RESULT_UNAVAILABLE;
}

""" + get_install_info + r"""

int main() {
    char folder[8];
    uint64_t sizeOnDisk = 99;
    uint32_t timestamp = 99;

    providerMode = ProviderMode::Good;
    if (FNQL_Steam_GetItemInstallInfo(1, folder, sizeof(folder),
        &sizeOnDisk, &timestamp) != FNQL_STEAM_RESULT_OK) return 10;
    if (folder[sizeof(folder) - 1] != '\0' || sizeOnDisk != 123
        || timestamp != 456) return 11;

    providerMode = ProviderMode::Unterminated;
    if (FNQL_Steam_GetItemInstallInfo(1, folder, sizeof(folder),
        &sizeOnDisk, &timestamp) != FNQL_STEAM_RESULT_FAILED) return 12;
    if (folder[0] != '\0' || sizeOnDisk != 0 || timestamp != 0) return 13;

    providerMode = ProviderMode::Failed;
    if (FNQL_Steam_GetItemInstallInfo(1, folder, sizeof(folder),
        &sizeOnDisk, &timestamp) != FNQL_STEAM_RESULT_FAILED) return 14;
    if (folder[0] != '\0' || sizeOnDisk != 0 || timestamp != 0) return 15;

    providerAvailable = false;
    std::memset(folder, 'z', sizeof(folder));
    sizeOnDisk = 99;
    timestamp = 99;
    if (FNQL_Steam_GetItemInstallInfo(1, folder, sizeof(folder),
        &sizeOnDisk, &timestamp) != FNQL_STEAM_RESULT_UNAVAILABLE) return 16;
    for (char value : folder) if (value != '\0') return 17;
    if (sizeOnDisk != 0 || timestamp != 0) return 18;

    if (FNQL_Steam_GetItemInstallInfo(1, nullptr, 0, nullptr, nullptr)
        != FNQL_STEAM_RESULT_INVALID_ARGUMENT) return 19;
    return 0;
}
"""
        # Avoid Windows installer-detection heuristics on executable names.
        compile_and_run_cpp(self, "workshop_provider_path", harness)

    def test_all_supported_build_manifests_include_workshop_sources(self) -> None:
        meson = read_source("meson.build")
        makefile = read_source("Makefile")
        cmake = read_source("CMakeLists.txt")
        msvc_client = read_source("code/win32/msvc2017/fnql.vcxproj")
        msvc_ded = read_source("code/win32/msvc2017/fnql-ded.vcxproj")
        msvc_client_filters = read_source("code/win32/msvc2017/fnql.vcxproj.filters")
        msvc_ded_filters = read_source("code/win32/msvc2017/fnql-ded.vcxproj.filters")
        legacy_client = read_source("code/win32/msvc2005/fnql.vcproj")
        legacy_ded = read_source("code/win32/msvc2005/fnql-ded.vcproj")

        self.assertIn("'code/client/cl_workshop.cpp'", meson)
        self.assertIn("'code/platform/fnql_workshop.cpp'", meson)
        self.assertIn("$(B)/client/cl_workshop.o", makefile)
        self.assertIn("$(B)/client/fnql_workshop.o", makefile)
        self.assertIn("$(B)/ded/fnql_workshop.o", makefile)
        self.assertIn("code/platform/fnql_workshop.cpp", cmake)
        self.assertIn("AUX_SOURCE_DIRECTORY(code/client CLIENT_SRCS)", cmake)
        self.assertIn("fnql_workshop_source", meson)
        self.assertIn("tests/workshop_source_tests.py", meson)
        self.assertIn("fnql_workshop_source", cmake)
        self.assertIn("tests/workshop_source_tests.py", cmake)

        for source in (msvc_client, msvc_client_filters, legacy_client):
            self.assertIn("cl_workshop.cpp", source)
            self.assertIn("fnql_workshop.cpp", source)
        for source in (msvc_ded, msvc_ded_filters, legacy_ded):
            self.assertIn("fnql_workshop.cpp", source)
            self.assertNotIn("cl_workshop.cpp", source)


if __name__ == "__main__":
    unittest.main()
