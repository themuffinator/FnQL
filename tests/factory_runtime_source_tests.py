from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start + len(signature))
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function: {signature}")


class FactoryRuntimeSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.runtime = read("code/server/sv_factory.cpp")
        cls.server_h = read("code/server/server.h")
        cls.commands = read("code/server/sv_ccmds.cpp")
        cls.game = read("code/server/sv_game.cpp")
        cls.init = read("code/server/sv_init.cpp")
        cls.server_main = read("code/server/sv_main.cpp")
        cls.common = read("code/qcommon/common.c")
        cls.files = read("code/qcommon/files.c")
        cls.client_main = read("code/client/cl_main.cpp")
        cls.client_workshop = read("code/client/cl_workshop.cpp")
        cls.webui = read("code/client/cl_webui.cpp")

    def test_registry_uses_retail_sources_and_bounded_shared_catalog(self) -> None:
        self.assertIn("constexpr std::size_t FactoryFileByteLimit = 0x8000;", self.runtime)
        self.assertIn("constexpr std::size_t FactoryFileListBytes = 0x400;", self.runtime)
        self.assertIn('AppendFactoryFile( replacement, "scripts/factories.txt", true );', self.runtime)
        self.assertIn('ForEachScriptFile( ".factories"', self.runtime)
        self.assertIn('ForEachScriptFile( ".factory"', self.runtime)
        self.assertLess(
            self.runtime.index('ForEachScriptFile( ".factories"'),
            self.runtime.index('ForEachScriptFile( ".factory"'),
        )
        self.assertIn("factoryCatalog.FindFirst( factoryName )", self.runtime)
        self.assertIn("replacement.InsertRetained( activeFactory->definition )", self.runtime)
        self.assertIn("definition.enumerated", self.runtime)

    def test_factory_switch_owns_snapshot_restore_and_identity_cvars(self) -> None:
        restore = self.runtime.index("RestoreActiveFactorySettings();")
        save = self.runtime.index("Cvar_SaveFactoryValue( setting.name.c_str() );")
        apply = self.runtime.index("Cvar_Set( setting.name.c_str(), setting.value.c_str() );")
        self.assertLess(restore, save)
        self.assertLess(save, apply)
        restore_body = function_body(
            self.runtime, "void RestoreActiveFactorySettings()"
        )
        prepare_body = function_body(
            self.runtime,
            "qboolean SV_FactoryPrepareMap( const char *mapName, const char *factoryName,",
        )
        self.assertNotIn("setting.hasValue", restore_body)
        self.assertIn("if ( !setting.hasValue )", prepare_body)
        self.assertIn('Cvar_SetIntegerValue( "g_gametype"', self.runtime)
        self.assertIn('Cvar_Set( "g_factory", selectedCopy.id.c_str() );', self.runtime)
        self.assertIn('Cvar_Set( "g_factoryTitle", selectedCopy.title.c_str() );', self.runtime)
        self.assertIn("activeFactory->generation != selectedGeneration", self.runtime)

    def test_map_command_requires_or_reuses_factory_and_honors_hidden_rules(self) -> None:
        self.assertIn("SV_FactoryPrintMapUsage( cmd );", self.commands)
        map_command = function_body(self.commands, "static void SV_Map_f( void )")
        self.assertIn("SV_FactoryPrepareMap( map, Cmd_Argv( 2 )", map_command)
        self.assertIn("SV_QBool( Cmd_Argc() >= 3 )", map_command)
        self.assertLess(
            map_command.index("Cmd_Argc() < 3 && !SV_FactoryHasActive()"),
            map_command.index('Com_sprintf( expanded.data()'),
        )
        self.assertIn("} else if ( activeFactory ) {", self.runtime)
        self.assertIn("FactoryIsHidden( *selected ) && svServerType", self.runtime)
        self.assertIn("const bool explicitHiddenFactory = explicitFactory", self.runtime)
        self.assertIn("!developerMap && !explicitHiddenFactory", self.runtime)
        self.assertIn('Cvar_Set( "ui_singlePlayerActive", "1" );', self.commands)
        self.assertIn('Cvar_Set( "ui_priv", "3" );', self.commands)

    def test_ammo_pack_and_restart_transition_match_retail(self) -> None:
        self.assertIn("extern\tcvar_t\t*sv_ammoPack;", self.server_h)
        self.assertIn("cvar_t\t*sv_ammoPack;", self.server_main)
        self.assertIn('sv_ammoPack = Cvar_Get( "g_ammoPack", "1", CVAR_LATCH );', self.init)
        self.assertIn('(void)Cvar_Get( "g_ammoPack", "0", CVAR_LATCH );', self.commands)
        self.assertIn("sv_ammoPack->modified = qfalse;", self.init)

        restart = function_body(self.commands, "static void SV_MapRestart_f( void )")
        self.assertIn('Cvar_VariableValue( "g_restarted" ) == 0.0f', restart)
        self.assertIn("( sv_ammoPack && sv_ammoPack->modified ) || sv_gametype->modified", restart)
        self.assertIn("SV_ChangeMaxClients();", restart)
        self.assertNotIn('Cvar_VariableIntegerValue( "sv_pure" )', restart)

    def test_operator_lifecycle_rotation_and_startup_are_connected(self) -> None:
        for command in (
            "reload_arenas",
            "reload_factories",
            "reload_mappool",
            "startRandomMap",
        ):
            self.assertIn(f'{{ "{command}",', self.commands)
        self.assertIn('Cvar_Get( "sv_mapPoolFile", "mappool.txt", CVAR_ARCHIVE )', self.runtime)
        self.assertIn('Cvar_Get( "sv_includeCurrentMapInVote", "0",', self.runtime)
        self.assertIn('Cvar_Set( "nextmap",', self.runtime)
        self.assertIn('Cvar_Set( "nextmaps", nextMaps.data() );', self.runtime)
        self.assertIn("SV_MapPoolRefreshCvars();", self.init)
        self.assertIn('Cbuf_AddText( "vstr serverstartup\\n" );', self.common)

        for signature in (
            "void SV_FactoryReload_f( void )",
            "void SV_ArenaReload_f( void )",
            "void SV_MapPoolReload_f( void )",
        ):
            body = function_body(self.runtime, signature)
            self.assertNotIn("UpdateRotationCvars();", body)

        factory_reload = function_body(self.runtime, "void SV_FactoryReload_f( void )")
        arena_reload = function_body(self.runtime, "void SV_ArenaReload_f( void )")
        pool_reload = function_body(self.runtime, "void SV_MapPoolReload_f( void )")
        self.assertNotIn("LoadMapPool();", factory_reload)
        self.assertNotIn("LoadMapPool();", arena_reload)
        self.assertIn("LoadMapPool();", pool_reload)

    def test_shutdown_restores_overrides_without_forgetting_retail_selection(self) -> None:
        body = function_body(self.runtime, "void SV_FactoryShutdown( void )")
        self.assertIn("RestoreActiveFactorySettings();", body)
        self.assertNotIn("activeFactory.reset", body)

        shutdown = function_body(self.init, "void SV_Shutdown( const char *finalmsg )")
        lifecycle = (
            "SV_ShutdownGameProgs();",
            "SV_ClearServer();",
            "SV_ZFree( svs.clients );",
            "SV_FactoryShutdown();",
            "svs = {};",
        )
        positions = [shutdown.index(marker) for marker in lifecycle]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("settingsApplied = false;", self.runtime)
        self.assertIn("selectionChanged || !activeFactory->settingsApplied", self.runtime)

    def test_single_player_alias_deactivates_factory_before_sp_overrides(self) -> None:
        deactivate = function_body(self.runtime, "void SV_FactoryDeactivate( void )")
        self.assertLess(
            deactivate.index("RestoreActiveFactorySettings();"),
            deactivate.index("activeFactory.reset();"),
        )
        self.assertIn('Cvar_Set( "g_factory", "" );', deactivate)
        self.assertIn('Cvar_Set( "g_factoryTitle", "" );', deactivate)

        map_command = function_body(self.commands, "static void SV_Map_f( void )")
        self.assertLess(
            map_command.index("SV_FactoryDeactivate();"),
            map_command.index(
                'Cvar_SetIntegerValue( "g_gametype", GT_SINGLE_PLAYER );'
            ),
        )

    def test_empty_pool_fallback_is_split_between_nextmap_and_start(self) -> None:
        refresh = function_body(self.runtime, "void UpdateRotationCvars()")
        start = function_body(self.runtime, "void SV_StartRandomMap_f( void )")
        fallback = function_body(
            self.runtime,
            "std::optional<rotation::RotationEntry> FallbackRotationEntry()",
        )
        self.assertIn('Cvar_Set( "nextmap", "map_restart 0" );', refresh)
        self.assertIn('entry.requestedMap = entry.map = "campgrounds";', fallback)
        self.assertIn("SelectStartRotationEntry();", start)
        self.assertNotIn("LoadMapPool();", start)

    def test_native_module_and_webui_share_the_authoritative_registry(self) -> None:
        self.assertIn("return SV_FactoryExists( factoryName );", self.game)
        self.assertNotIn("SV_FactoryJsonHasId", self.game)
        self.assertIn("static char *CL_WebHost_AllocateFactoryListJson", self.webui)
        self.assertIn("SV_FactoryWebCatalogJsonSize()", self.webui)
        self.assertIn("SV_FactoryBuildWebCatalogJson( buffer, bufferSize )", self.webui)
        self.assertNotIn("CL_WebHost_AppendFactoryDefinitionsFromFile", self.webui)
        self.assertIn("factoryCatalog.SerializeWebUi", self.runtime)
        self.assertIn("CL_WebHost_InvalidateFactoryCatalog();", self.runtime)
        self.assertIn("CL_WebHost_InvalidateDocumentSnapshots();", self.webui)
        self.assertIn("!CL_WebHost_HasLiveView() || CL_Awesomium_IsLoading()", self.webui)

    def test_rotation_rng_build_modes_and_mount_refresh_are_wired(self) -> None:
        random_index = function_body(self.runtime, "std::size_t RuntimeRandomIndex()")
        self.assertEqual(random_index.count("std::rand()"), 2)
        self.assertIn("Com_Milliseconds()", random_index)
        self.assertIn("? 0u - mixed : mixed", random_index)

        resolver = function_body(self.runtime, "rotation::RotationResolver BuildRotationResolver()")
        self.assertIn('Cvar_VariableIntegerValue( "com_build" )', resolver)
        self.assertIn('Cvar_VariableIntegerValue( "com_buildScript" )', resolver)

        restart_source = self.files[self.files.rindex("void FS_Restart( int checksumFeed )") :]
        restart_body = function_body(restart_source, "void FS_Restart( int checksumFeed )")
        self.assertLess(
            restart_body.index("FS_Startup();"),
            restart_body.index("SV_FactoryRefreshMountedContent();"),
        )
        conditional_restart = function_body(
            self.files,
            "qboolean FS_ConditionalRestart( int checksumFeed, qboolean clientRestart )",
        )
        pure_reorder = conditional_restart.index("FS_ReorderPurePaks();")
        self.assertLess(
            pure_reorder,
            conditional_restart.index(
                "SV_FactoryRefreshMountedContent();", pure_reorder
            ),
        )
        self.assertLess(
            self.common.index("Com_WorkshopInit();"),
            self.common.index("SV_FactoryRefreshMountedContent();", self.common.index("Com_WorkshopInit();")),
        )
        refresh = function_body(self.runtime, "void SV_FactoryRefreshMountedContent( void )")
        self.assertIn("if ( !factoryRuntimeInitialized )", refresh)
        self.assertNotIn("SV_FactoryInit();", refresh)

    def test_every_supported_build_frontend_compiles_the_runtime(self) -> None:
        cmake = read("CMakeLists.txt")
        makefile = read("Makefile")
        meson = read("meson.build")
        for source in ("factory_catalog.cpp", "factory_rotation.cpp", "sv_factory.cpp"):
            self.assertIn(f"code/server/{source}", cmake)
            self.assertIn(f"code/server/{source}", meson)
            stem = source.removesuffix(".cpp")
            self.assertIn(f"$(B)/client/{stem}.o", makefile)
            self.assertIn(f"$(B)/ded/{stem}.o", makefile)

        for target in ("fnql_factory_catalog", "fnql_factory_rotation"):
            self.assertIn(target, cmake)
            self.assertIn(target, meson)

        for source_test in (
            "fnql_cvar_factory_snapshot_source",
            "fnql_factory_runtime_source",
            "fnql_retail_factory_probe",
        ):
            self.assertIn(source_test, cmake)
            self.assertIn(source_test, meson)

        for project in (
            "code/win32/msvc2005/fnql.vcproj",
            "code/win32/msvc2005/fnql-ded.vcproj",
            "code/win32/msvc2017/fnql.vcxproj",
            "code/win32/msvc2017/fnql-ded.vcxproj",
        ):
            source = read(project).replace("\\", "/")
            self.assertIn("../../server/factory_catalog.cpp", source)
            self.assertIn("../../server/factory_rotation.cpp", source)
            self.assertIn("../../server/sv_factory.cpp", source)


if __name__ == "__main__":
    unittest.main()
