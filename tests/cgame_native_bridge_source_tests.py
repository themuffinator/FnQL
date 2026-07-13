from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_repo_file(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class CGameNativeBridgeSourceTests(unittest.TestCase):
    def test_cgame_public_exposes_recovered_ql_native_abi(self) -> None:
        cg_public = read_repo_file("code/cgame/cg_public.h")

        self.assertIn("#define\tCGAME_NATIVE_API_VERSION\t8", cg_public)
        self.assertIn("#define CGAME_NATIVE_IMPORT_COUNT\tCG_QL_IMPORT_TOTAL_COUNT", cg_public)
        self.assertIn("CG_QL_IMPORT_R_RENDERSCENE = 76", cg_public)
        self.assertIn("CG_QL_IMPORT_R_SETCOLOR = 78", cg_public)
        self.assertIn("CG_QL_IMPORT_DRAW_SCALED_TEXT = 123", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_REGISTER_CVARS", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_COPY_CLIENT_IDENTITY", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_SET_CLIENT_SPEAKING_STATE", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_SHOW_1ST_TRACKED_PLAYER", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_SHOW_2ND_TRACKED_PLAYER", cg_public)
        self.assertIn("CG_NATIVE_EXPORT_GET_PHYSICS_TIME", cg_public)

    def test_client_cgame_uses_native_first_loader_and_startup_cvar_pass(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("static ql_import_f ql_cgame_imports[CGAME_NATIVE_IMPORT_COUNT];", cl_cgame)
        self.assertIn("static vm_t *CL_LoadCGameVM( vmInterpret_t interpret )", cl_cgame)
        self.assertIn("interpret == VMI_PINNED_NATIVE ? VMI_PINNED_NATIVE : VMI_NATIVE", cl_cgame)
        self.assertIn("ql_cgame_imports, CGAME_NATIVE_API_VERSION", cl_cgame)
        self.assertIn("if ( vm->dllHandle || interpret != VMI_BYTECODE || !vm->compiled )", cl_cgame)
        self.assertIn("registrationVm = CL_LoadCGameVM( interpret );", cl_cgame)
        self.assertIn("VM_CallCGameRegisterCvars( registrationVm );", cl_cgame)
        self.assertIn("cgvm = CL_LoadCGameVM( interpret );", cl_cgame)

    def test_every_non_null_cgame_import_slot_has_host_wiring(self) -> None:
        cg_public = read_repo_file("code/cgame/cg_public.h")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        import_names = {
            match.group(1)
            for match in re.finditer(r"\b(CG_QL_IMPORT_[A-Z0-9_]+)\b(?:\s*=|,)", cg_public)
            if match.group(1) not in {"CG_QL_IMPORT_COUNT", "CG_QL_IMPORT_TOTAL_COUNT"}
        }
        assigned_names = set(re.findall(r"ql_cgame_imports\[(CG_QL_IMPORT_[A-Z0-9_]+)\]", cl_cgame))
        expected_null_names = {
            "CG_QL_IMPORT_NULL_100",
            "CG_QL_IMPORT_NULL_118",
            "CG_QL_IMPORT_NULL_119",
        }

        self.assertEqual(import_names - assigned_names, expected_null_names)

    def test_vm_defaults_and_native_pointer_paths_support_retail_cgame(self) -> None:
        qcommon_h = read_repo_file("code/qcommon/qcommon.h")
        vm_c = read_repo_file("code/qcommon/vm.c")

        self.assertIn('Cvar_Get( "vm_cgame", "0"', vm_c)
        self.assertIn("VM_Free( vm );\n\t\treturn NULL;", vm_c)
        self.assertIn("vm->entryPoint || vm->dllExports", vm_c)
        self.assertIn("VM_CreateNative", qcommon_h)
        self.assertIn("VM_CallCGameRegisterCvars", qcommon_h)

    def test_retail_client_message_sideband_is_gated_and_wired(self) -> None:
        qcommon_h = read_repo_file("code/qcommon/qcommon.h")
        client_h = read_repo_file("code/client/client.h")
        cl_input = read_repo_file("code/client/cl_input.cpp")
        cl_main = read_repo_file("code/client/cl_main.cpp")
        sv_client = read_repo_file("code/server/sv_client.cpp")

        self.assertIn(
            "#define\tQL_RETAIL_PROTOCOL_VERSION\tNETCHAN_QL_RETAIL_PROTOCOL_VERSION",
            qcommon_h,
        )
        self.assertIn("void CL_SetRetailClientMessageViewangleDeltaFlag( void );", client_h)
        self.assertIn("void CL_SetRetailClientMessageCGameImportGuardFlag( void );", client_h)
        self.assertIn("void CL_SetRetailClientMessageRendererNodeCount( int nodeCount );", client_h)
        self.assertIn("void CL_CheckCGameNativeImportIntegrity( void );", client_h)
        self.assertIn("RETAIL_CLIENT_MESSAGE_FLAG_CGAME_IMPORT_GUARD", cl_input)
        self.assertIn("clc.netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL", cl_input)
        self.assertIn("MSG_WriteByte( &buf, CL_RetailClientMessageFlags() ^ ( clc.serverCommandSequence & 0xff ) );", cl_input)
        self.assertIn("CL_CheckCGameNativeImportIntegrity();", cl_main)
        self.assertIn("CL_SetRetailClientMessageViewangleDeltaFlag();", cl_main)
        self.assertIn("cl->netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL", sv_client)
        self.assertIn("MSG_ReadByte( msg );", sv_client)

    def test_cgame_usercmd_value_uses_ql_primary_and_fov_fields(self) -> None:
        q_shared = read_repo_file("code/qcommon/q_shared.h")
        client_h = read_repo_file("code/client/client.h")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")
        cl_input = read_repo_file("code/client/cl_input.cpp")
        msg_c = read_repo_file("code/qcommon/msg.cpp")

        self.assertIn("byte\t\t\tweaponPrimary;", q_shared)
        self.assertIn("byte\t\t\tfov;", q_shared)
        self.assertIn("int\t\t\tcgameUserCmdPrimary;", client_h)
        self.assertIn("int\t\t\tcgameUserCmdFov;", client_h)
        self.assertIn("static void CL_SetUserCmdValue( int userCmdValue, int userCmdPrimary, float sensitivityScale, int userCmdFov )", cl_cgame)
        self.assertIn("cl.cgameUserCmdPrimary = userCmdPrimary;", cl_cgame)
        self.assertIn("cl.cgameUserCmdFov = userCmdFov;", cl_cgame)
        self.assertIn("CL_SetUserCmdValue( args[1], args[2], VMF(3), args[4] );", cl_cgame)
        self.assertIn("CL_SetUserCmdValue( static_cast<int>( args[1] ), static_cast<int>( args[2] ), _vmf( args[3] ), static_cast<int>( args[4] ) );", cl_cgame)
        self.assertIn("cmd->weaponPrimary = cl.cgameUserCmdPrimary;", cl_input)
        self.assertIn("cmd->fov = cl.cgameUserCmdFov;", cl_input)
        self.assertIn("from->weaponPrimary == to->weaponPrimary", msg_c)
        self.assertIn("from->fov == to->fov", msg_c)
        self.assertIn("MSG_WriteDeltaKey( msg, key, from->weaponPrimary, to->weaponPrimary, 8 );", msg_c)
        self.assertIn("MSG_WriteDeltaKey( msg, key, from->fov, to->fov, 8 );", msg_c)
        self.assertIn("to->weaponPrimary = MSG_ReadDeltaKey( msg, key, from->weaponPrimary, 8);", msg_c)
        self.assertIn("to->fov = MSG_ReadDeltaKey( msg, key, from->fov, 8);", msg_c)
        self.assertIn("to->weaponPrimary = from->weaponPrimary;", msg_c)
        self.assertIn("to->fov = from->fov;", msg_c)

    def test_native_getglconfig_uses_retail_ql_layout(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("} qlRetailGlconfig_t;", cl_cgame)
        self.assertIn("qlRetailGlconfigSizeCheck[( sizeof( qlRetailGlconfig_t ) == 0x2c44 ) ? 1 : -1 ]", cl_cgame)
        self.assertIn("static void CL_GetRetailGlconfig( void *glconfig )", cl_cgame)
        self.assertIn("retailConfig.maxActiveTextures = cls.glconfig.numTextureUnits;", cl_cgame)
        self.assertIn("retailConfig.multitextureAvailable = ( cls.glconfig.numTextureUnits > 1 ) ? qtrue : qfalse;", cl_cgame)
        self.assertIn("Com_Memcpy( glconfig, &retailConfig, sizeof( retailConfig ) );", cl_cgame)
        self.assertIn("VM_CHECKBOUNDS( cgvm, args[1], sizeof( glconfig_t ) );\n\t\tCL_GetGlconfig( VMA(1) );", cl_cgame)
        self.assertIn("CL_GetRetailGlconfig( reinterpret_cast<void *>( args[1] ) );", cl_cgame)

    def test_snapshot_structs_and_deltas_use_ql_native_layout(self) -> None:
        q_shared = read_repo_file("code/qcommon/q_shared.h")
        msg_c = read_repo_file("code/qcommon/msg.cpp")

        self.assertIn("int\t\t\tlocation;\t\t// retail team location index", q_shared)
        self.assertIn("int\t\t\tweaponPrimary;\t// mirrored usercmd byte for retail follow/demo HUD widgets", q_shared)
        self.assertIn("int\t\t\tfov;\t\t\t// mirrored usercmd byte for retail prediction/HUD consumers", q_shared)
        self.assertIn("int\t\t\tjumpTime;", q_shared)
        self.assertIn("int\t\t\tdoubleJumped;", q_shared)
        self.assertIn("int\t\t\tcrouchTime;", q_shared)
        self.assertIn("int\t\t\tcrouchSlideTime;", q_shared)
        self.assertIn("signed char\tforwardmove;\t// mirrored usercmd byte for retail follow/demo HUD widgets", q_shared)
        self.assertIn("TR_QL_ACCEL", q_shared)
        self.assertIn("float\tgravity;\t\t\t// Quake Live trajectory acceleration scalar", q_shared)
        self.assertIn("int\t\thealth;\t\t\t// replicated player health for retail observers/HUD", q_shared)
        self.assertIn("int\t\tarmor;\t\t\t// replicated player armor for retail observers/HUD", q_shared)

        self.assertIn("{ NETF(pos.gravity), 32 }", msg_c)
        self.assertIn("{ NETF(apos.gravity), 32 }", msg_c)
        self.assertIn("{ NETF(jumpTime), 32 }", msg_c)
        self.assertIn("{ NETF(doubleJumped), 1 }", msg_c)
        self.assertIn("{ NETF(health), 16 }", msg_c)
        self.assertIn("{ NETF(armor), 16 }", msg_c)
        self.assertIn("{ NETF(location), 8 }", msg_c)
        self.assertIn("{ PSF(pm_flags), 24 }", msg_c)
        self.assertIn("{ PSF(weaponPrimary), 8 }", msg_c)
        self.assertIn("{ PSF(jumpTime), 32 }", msg_c)
        self.assertIn("{ PSF(crouchSlideTime), 32 }", msg_c)
        self.assertIn("{ PSF(forwardmove), 8 }", msg_c)
        self.assertIn("static qboolean MSG_PlayerStateFieldIsSignedByte( const netField_t *field )", msg_c)
        self.assertIn("MSG_PlayerStateFieldNetworkValue( to, field )", msg_c)
        self.assertIn("MSG_SetPlayerStateFieldValue( to, field, value );", msg_c)

    def test_renderer_reports_node_count_to_retail_sideband(self) -> None:
        tr_public = read_repo_file("code/renderercommon/tr_public.h")
        cl_main = read_repo_file("code/client/cl_main.cpp")
        tr_cmds = read_repo_file("code/renderer/tr_cmds.c")
        vk_cmds = read_repo_file("code/renderervk/tr_cmds.c")
        renderer2_cmds = read_repo_file("code/renderer2/tr_cmds.c")

        self.assertIn("SetClientMessageRendererNodeCount", tr_public)
        self.assertIn("rimp.SetClientMessageRendererNodeCount = CL_SetRetailClientMessageRendererNodeCount;", cl_main)
        for source in (tr_cmds, vk_cmds, renderer2_cmds):
            self.assertIn("ri.SetClientMessageRendererNodeCount( tr.pc.c_leafs );", source)

    def test_cgame_native_text_imports_use_shared_scaled_text_bridge(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_scrn = read_repo_file("code/client/cl_scrn.cpp")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("void\tRE_DrawScaledText(", client_h)
        self.assertIn("void\tRE_MeasureScaledText(", client_h)
        self.assertIn("void RE_DrawScaledText( int x, int y, const char *text, int fontHandle", cl_scrn)
        self.assertIn("void RE_MeasureScaledText( const char *text, const char *end, int fontHandle", cl_scrn)
        self.assertIn("re.GetScaledFontMetrics( fontHandle, scale", cl_scrn)
        self.assertIn("re.DrawScaledText( x, y, text, fontHandle, scale, limit, outMaxX", cl_scrn)
        self.assertIn("RE_DrawScaledText( x, y, text, fontHandle, scale, limit, maxX", cl_cgame)
        self.assertIn("RE_MeasureScaledText( text, end, fontHandle, scale, limit, bounds );", cl_cgame)
        self.assertIn("fnql::font::CopyMeasureBounds( outLeft, bounds );", cl_cgame)
        self.assertIn("width = bounds[2] - bounds[0];", cl_cgame)
        self.assertIn("height = bounds[4];", cl_cgame)
        self.assertNotIn("QL_CG_MeasureFallbackText", cl_cgame)

    def test_console_chat_field_uses_native_cgame_geometry_exports(self) -> None:
        cl_console = read_repo_file("code/client/cl_console.cpp")

        self.assertIn("static int Con_GetChatFieldY( void )", cl_console)
        self.assertIn("CG_GET_CHAT_FIELD_Y", cl_console)
        self.assertIn("static int Con_GetChatFieldPixelWidth( void )", cl_console)
        self.assertIn("CG_GET_CHAT_FIELD_PIXEL_WIDTH", cl_console)
        self.assertIn("static int Con_GetChatFieldWidthInChars( qboolean teamChat )", cl_console)
        self.assertIn("CG_GET_CHAT_FIELD_WIDTH_IN_CHARS", cl_console)
        self.assertIn("static void Con_ResetChatField( qboolean teamChat )", cl_console)
        self.assertIn("chatField.widthInChars = Con_GetChatFieldWidthInChars( teamChat );", cl_console)
        self.assertIn("chatFieldY = Con_GetChatFieldY();", cl_console)
        self.assertIn("chatFieldPixelWidth = Con_GetChatFieldPixelWidth();", cl_console)

    def test_console_timestamps_use_native_cgame_physics_time(self) -> None:
        cl_console = read_repo_file("code/client/cl_console.cpp")

        self.assertIn('con_timestamps = Cvar_Get( "con_timestamps", "0", CVAR_ARCHIVE_ND | CVAR_PROTECTED );', cl_console)
        self.assertIn("static int Con_GetTimestampTime( void )", cl_console)
        self.assertIn("const int physicsTime = CL_GetCGamePhysicsTime();", cl_console)
        self.assertIn("if ( physicsTime > 0 )", cl_console)
        self.assertIn("static void Con_FormatTimestamp( char *buffer, int bufferSize )", cl_console)
        self.assertIn('Com_sprintf( buffer, bufferSize, "[%d:%02d.%03d] ", minutes, seconds, millis );', cl_console)
        self.assertIn("static void Con_WriteTimestampPrefix( bool skipnotify, int colorIndex )", cl_console)
        self.assertIn("Con_WriteTimestampPrefix( skipnotify, colorIndex );", cl_console)
        self.assertIn("if ( con.newline )", cl_console)

    def test_recovered_cgame_exports_have_client_helpers(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")
        vm_c = read_repo_file("code/qcommon/vm.c")

        self.assertIn("void CL_ShowFirstTrackedPlayer( void );", client_h)
        self.assertIn("void CL_ShowSecondTrackedPlayer( void );", client_h)
        self.assertIn("int CL_GetCGamePhysicsTime( void );", client_h)
        self.assertIn("void CL_ShowFirstTrackedPlayer( void )", cl_cgame)
        self.assertIn("VM_Call( cgvm, 0, CG_SHOW_1ST_TRACKED_PLAYER );", cl_cgame)
        self.assertIn("void CL_ShowSecondTrackedPlayer( void )", cl_cgame)
        self.assertIn("VM_Call( cgvm, 0, CG_SHOW_2ND_TRACKED_PLAYER );", cl_cgame)
        self.assertIn("int CL_GetCGamePhysicsTime( void )", cl_cgame)
        self.assertIn("VM_Call( cgvm, 0, CG_GET_PHYSICS_TIME )", cl_cgame)
        self.assertIn("return physicsTime > 0 ? physicsTime : 0;", cl_cgame)
        self.assertIn("case CG_SHOW_1ST_TRACKED_PLAYER:", vm_c)
        self.assertIn("dllExports[CG_NATIVE_EXPORT_SHOW_1ST_TRACKED_PLAYER]", vm_c)
        self.assertIn("case CG_SHOW_2ND_TRACKED_PLAYER:", vm_c)
        self.assertIn("dllExports[CG_NATIVE_EXPORT_SHOW_2ND_TRACKED_PLAYER]", vm_c)
        self.assertIn("case CG_GET_PHYSICS_TIME:", vm_c)
        self.assertIn("dllExports[CG_NATIVE_EXPORT_GET_PHYSICS_TIME]", vm_c)

    def test_client_identity_and_speaking_state_exports_are_wired(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_main = read_repo_file("code/client/cl_main.cpp")

        self.assertIn("qboolean CL_CopyClientIdentity( int clientNum, cgameClientIdentity_t *identity );", client_h)
        self.assertIn("qboolean CL_GetClientSteamId( int clientNum, unsigned int *steamIdLow, unsigned int *steamIdHigh );", client_h)
        self.assertIn("qboolean CL_IsSteamIdentityMuted( unsigned int identityLow, unsigned int identityHigh );", client_h)
        self.assertIn("qboolean CL_ToggleSteamIdentityMute( unsigned int identityLow, unsigned int identityHigh );", client_h)
        self.assertIn("qboolean CL_IsVoiceSenderMuted( int clientNum );", client_h)
        self.assertIn("void CL_SetClientSpeakingState( int clientNum, qboolean speaking );", client_h)
        self.assertIn("void CL_SetLocalSpeakingState( qboolean speaking );", client_h)
        self.assertIn("static qboolean CL_ParseSteamIdString( const char *steamId, unsigned int *steamIdLow, unsigned int *steamIdHigh )", cl_main)
        self.assertIn("ULLONG_MAX", cl_main)
        self.assertIn("static const char *CL_GetConfigStringValue( int index )", cl_main)
        self.assertIn("VM_Call( cgvm, 2, CG_COPY_CLIENT_IDENTITY", cl_main)
        self.assertIn("CL_GetConfigStringValue( CS_PLAYERS + clientNum )", cl_main)
        self.assertIn('Info_ValueForKey( info, "steamid" )', cl_main)
        self.assertIn('Info_ValueForKey( info, "steam" )', cl_main)
        self.assertIn('Info_ValueForKey( info, "n" )', cl_main)
        self.assertIn('Info_ValueForKey( info, "cn" )', cl_main)
        self.assertIn("qboolean CL_GetClientSteamId( int clientNum, unsigned int *steamIdLow, unsigned int *steamIdHigh )", cl_main)
        self.assertIn("!identity.identityLow && !identity.identityHigh", cl_main)
        self.assertIn("qboolean CL_IsVoiceSenderMuted( int clientNum )", cl_main)
        self.assertIn("return CL_IsSteamIdentityMuted( steamIdLow, steamIdHigh );", cl_main)
        self.assertIn("!cgvm || !cgvm->dllExports || cls.state != CA_ACTIVE || !cl.snap.valid", cl_main)
        self.assertIn("VM_Call( cgvm, 2, CG_SET_CLIENT_SPEAKING_STATE", cl_main)
        self.assertIn("CL_SetClientSpeakingState( cl.snap.ps.clientNum, speaking );", cl_main)

    def test_cgame_mute_imports_use_shared_client_identity_mute_state(self) -> None:
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("qboolean CL_IsSteamIdentityMuted( unsigned int identityLow, unsigned int identityHigh )", cl_cgame)
        self.assertIn("qboolean CL_ToggleSteamIdentityMute( unsigned int identityLow, unsigned int identityHigh )", cl_cgame)
        self.assertIn("return ( identity && QL_CG_FindMutedIdentityIndex( identity ) >= 0 ) ? qtrue : qfalse;", cl_cgame)
        self.assertIn("ql_cgame_mutedIdentitySet[ql_cgame_mutedIdentityCount] = 0;", cl_cgame)
        self.assertIn("return CL_IsSteamIdentityMuted( identityLow, identityHigh ) ? 1 : 0;", cl_cgame)
        self.assertIn("return CL_ToggleSteamIdentityMute( identityLow, identityHigh ) ? 1 : 0;", cl_cgame)
        self.assertNotIn("ql_cgame_mutedIdentitySet.fill( 0 );", cl_cgame)
        self.assertNotIn("ql_cgame_mutedIdentityCount = 0;\n\n\tql_cgame_currentColor", cl_cgame)

    def test_cgame_service_imports_share_default_off_client_boundary(self) -> None:
        client_h = read_repo_file("code/client/client.h")
        cl_webui = read_repo_file("code/client/cl_webui.cpp")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("qboolean CL_IsSubscribedApp( int appId );", client_h)
        self.assertIn("qhandle_t CL_GetAvatarImageHandle( unsigned int identityLow, unsigned int identityHigh );", client_h)
        self.assertIn("qboolean CL_IsSubscribedApp( int appId )", cl_webui)
        self.assertIn('Com_DPrintf( "UI subscription bridge ignored for app %d: %s (%s [%s])\\n"', cl_webui)
        self.assertIn('"subscription bridge provider unavailable"', cl_webui)
        self.assertIn('"ui_subscriptionBridgePolicy"', cl_webui)
        self.assertIn("qhandle_t CL_GetAvatarImageHandle( unsigned int identityLow, unsigned int identityHigh )", cl_webui)
        self.assertIn("return qfalse;", cl_webui)
        self.assertIn("return 0;", cl_webui)
        self.assertIn("return CL_IsSubscribedApp( appId ) ? 1 : 0;", cl_cgame)
        self.assertIn("return CL_GetAvatarImageHandle( identityLow, identityHigh );", cl_cgame)

    def test_cgame_volume_sound_imports_reach_audio_backends(self) -> None:
        snd_public = read_repo_file("code/client/audio/snd_public.h")
        snd_local = read_repo_file("code/client/audio/snd_local.h")
        snd_main = read_repo_file("code/client/audio/snd_main.cpp")
        snd_dma = read_repo_file("code/client/audio/legacy/snd_dma.cpp")
        openal_backend = read_repo_file("code/client/audio/openal/AudioSystemBackend.inl")
        openal_world = read_repo_file("code/client/audio/openal/AudioSystemWorld.inl")
        cl_cgame = read_repo_file("code/client/cl_cgame.cpp")

        self.assertIn("void S_StartSoundVolume( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx, float volume );", snd_public)
        self.assertIn("void S_StartLocalSoundVolume( sfxHandle_t sfx, int channelNum, float volume );", snd_public)
        self.assertIn("void (*StartSound)( const vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx, float volume );", snd_local)
        self.assertIn("void (*StartLocalSound)( sfxHandle_t sfx, int channelNum, float volume );", snd_local)
        self.assertIn("S_StartSoundVolume( origin, entnum, entchannel, sfx, 1.0f );", snd_main)
        self.assertIn("S_StartLocalSoundVolume( sfx, channelNum, 1.0f );", snd_main)
        self.assertIn("si.StartSound( origin, entnum, entchannel, sfx, volume );", snd_main)
        self.assertIn("si.StartLocalSound( sfx, channelNum, volume );", snd_main)
        self.assertIn("static int S_VolumeToMasterVolume( float volume )", snd_dma)
        self.assertIn("static void S_Base_StartSound( const vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfxHandle, float volume )", snd_dma)
        self.assertIn("ch->master_vol = masterVolume;", snd_dma)
        self.assertIn("static void S_Base_StartLocalSound( sfxHandle_t sfxHandle, int channelNum, float volume )", snd_dma)
        self.assertIn("void StartSound( const float *origin, int entnum, int entchannel, sfxHandle_t sfxHandle, float volume );", openal_backend)
        self.assertIn("void StartLocalSound( sfxHandle_t sfxHandle, int channelNum, float volume );", openal_backend)
        self.assertIn("world_.StartSound( entnum, entchannel, sfxHandle, sample, origin, volume );", openal_backend)
        self.assertIn("float volumeScale = 1.0f;", openal_world)
        self.assertIn("voice->volumeScale = ClampFloat( volume, 0.0f, 2.0f );", openal_world)
        self.assertIn("gain = ClampFloat( gain * gainScale * voice.volumeScale, 0.0f, 2.0f );", openal_world)
        self.assertIn("S_StartSoundVolume( origin, entityNum, entchannel, sfx, volume );", cl_cgame)
        self.assertIn("S_StartLocalSoundVolume( sfx, channelNum, volume );", cl_cgame)


if __name__ == "__main__":
    unittest.main()
