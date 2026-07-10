import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class QuakeLiveFilesystemSourceTests(unittest.TestCase):
    def test_windows_profile_uses_active_steam_account(self):
        source = (ROOT / "code/win32/win_shared.cpp").read_text(encoding="utf-8")

        self.assertIn('"Software\\\\Valve\\\\Steam\\\\ActiveProcess"', source)
        self.assertIn('"ActiveUser"', source)
        self.assertIn("76561197960265728ull", source)
        self.assertIn("Sys_QuakeLiveProfilePath", source)

    def test_filesystem_prefers_retail_profile_default(self):
        source = (ROOT / "code/qcommon/files.c").read_text(encoding="utf-8")

        self.assertIn("FS_DefaultHomePath( fs_basepath->string )", source)
        self.assertIn("Sys_QuakeLiveProfilePath( basePath )", source)
        self.assertIn("Retail QL filesystem:", source)

    def test_retail_ui_defaults_native_and_fails_once_with_guidance(self):
        vm = (ROOT / "code/qcommon/vm.c").read_text(encoding="utf-8")
        ui = (ROOT / "code/client/cl_ui.cpp").read_text(encoding="utf-8")

        self.assertIn('Cvar_Get( "vm_ui", "0"', vm)
        self.assertIn("retail Quake Live on Windows requires the Win32 FnQL build", ui)
        self.assertNotIn('Com_Error( ERR_DROP, "VM_Create on UI failed"', ui)

    def test_retail_configs_use_profile_root_without_widening_search_paths(self):
        header = (ROOT / "code/qcommon/qcommon.h").read_text(encoding="utf-8")
        files = (ROOT / "code/qcommon/files.c").read_text(encoding="utf-8")
        common = (ROOT / "code/qcommon/common.c").read_text(encoding="utf-8")
        command = (ROOT / "code/qcommon/cmd.c").read_text(encoding="utf-8")

        self.assertIn('#define QL_CONFIG_HARDWARE_FILE "qzconfig.cfg"', header)
        self.assertIn('#define QL_CONFIG_REPLICATE_FILE "repconfig.cfg"', header)
        self.assertIn("FS_FOpenProfileFileRead", files)
        self.assertIn("FS_FOpenProfileFileWrite", files)
        self.assertIn("FS_ProfileQpathIsValid", files)
        self.assertIn("Sys_ReplaceFile", files)
        self.assertIn("FS_ReadProfileFile( filename, &f.v )", command)
        self.assertIn("Com_WriteQLConfigurationFiles", common)
        self.assertIn("Cmd_WriteAliases( hardwareFile )", common)
        self.assertNotIn('FS_AddGameDirectory( fs_homepath->string, "" )', files)
        self.assertNotIn("FS_ReorderSearchPaths", files)

    def test_profile_paths_are_private_and_never_persisted(self):
        cvars = (ROOT / "code/qcommon/cvar.c").read_text(encoding="utf-8")
        files = (ROOT / "code/qcommon/files.c").read_text(encoding="utf-8")

        self.assertIn("CVAR_PRIVATE | CVAR_INIT", cvars)
        self.assertIn("CVAR_INIT | CVAR_PROTECTED | CVAR_PRIVATE", files)

    def test_retail_alias_commands_are_supported_and_bounded(self):
        command = (ROOT / "code/qcommon/cmd.c").read_text(encoding="utf-8")

        self.assertIn("MAX_CMD_ALIASES 256", command)
        self.assertIn('Cmd_AddCommand ("unaliasall", Cmd_UnAliasAll_f)', command)
        self.assertIn("cmd_aliasExpansions > 64", command)
        self.assertIn('FS_Printf( file, "unaliasall" Q_NEWLINE )', command)


if __name__ == "__main__":
    unittest.main()
