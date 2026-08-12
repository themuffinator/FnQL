from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(
        rf"(?:static\s+)?[A-Za-z_][A-Za-z0-9_:]*"
        rf"(?:\s*[*&])?\s*{name}\s*\([^)]*\)"
        rf"\s*(?:noexcept\s*)?\{{",
        source,
    )
    if not match:
        raise AssertionError(f"Missing function {name}")

    depth = 1
    for index in range(match.end(), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end() : index]
    raise AssertionError(f"Unterminated function {name}")


def section(source: str, start: str, end: str) -> str:
    start_index = source.index(start)
    return source[start_index : source.index(end, start_index)]


def assert_order(
    testcase: unittest.TestCase, source: str, *fragments: str
) -> None:
    cursor = -1
    for fragment in fragments:
        next_cursor = source.find(fragment, cursor + 1)
        testcase.assertNotEqual(
            next_cursor,
            -1,
            f"Missing ordered fragment {fragment!r}",
        )
        testcase.assertGreater(next_cursor, cursor)
        cursor = next_cursor


class SharedEventQueueRecoveryTests(unittest.TestCase):
    def test_reset_events_are_append_only_and_consumed_as_ordered_barriers(
        self,
    ) -> None:
        header = read_text("code/qcommon/qcommon.h")
        common = read_text("code/qcommon/common.c")
        event_loop = function_body(common, "Com_EventLoop")

        self.assertRegex(
            header,
            r"SE_CONSOLE,[^\n]*\n\s*"
            r"SE_INPUT_RESET,[^\n]*\n\s*"
            r"SE_MOUSE_RESET,[^\n]*\n\s*SE_MAX,",
        )
        self.assertIn('"SE_INPUT_RESET"', common)
        self.assertIn('"SE_MOUSE_RESET"', common)
        assert_order(
            self,
            event_loop,
            "case SE_INPUT_RESET:",
            "CL_ResetInputState();",
            "break;",
        )
        assert_order(
            self,
            event_loop,
            "case SE_MOUSE_RESET:",
            "CL_ResetMouseInputState( (unsigned int)ev.evValue );",
            "break;",
        )

    def test_system_queue_recovers_from_lost_transitions_before_new_event(
        self,
    ) -> None:
        common = read_text("code/qcommon/common.c")
        queue = function_body(common, "Sys_QueEvent")
        overflow = queue[queue.index("eventHead - eventTail >= MAX_QUED_EVENTS") :]

        self.assertIn(
            "lastEvent->evValue = Sys_SaturatingAddInt("
            " lastEvent->evValue, value );",
            queue,
        )
        self.assertIn(
            "lastEvent->evValue2 = Sys_SaturatingAddInt("
            " lastEvent->evValue2, value2 );",
            queue,
        )
        self.assertEqual(overflow.count("Sys_DiscardOldestEvent();"), 2)
        assert_order(
            self,
            overflow,
            "Sys_DiscardOldestEvent();",
            "if ( evType != SE_INPUT_RESET )",
            "Sys_DiscardOldestEvent();",
            "Sys_QueEvent( evTime, SE_INPUT_RESET",
            "ev = &eventQue[",
        )

    def test_pushed_queue_has_the_same_reset_before_new_event_contract(
        self,
    ) -> None:
        common = read_text("code/qcommon/common.c")
        push = function_body(common, "Com_PushEvent")
        self.assertIn(
            "static unsigned int com_pushedEventsHead = 0;",
            common,
        )
        self.assertIn(
            "static unsigned int com_pushedEventsTail = 0;",
            common,
        )
        overflow = push[
            push.index(
                "com_pushedEventsHead - com_pushedEventsTail "
                ">= MAX_PUSHED_EVENTS"
            ) :
        ]

        self.assertEqual(
            overflow.count("Com_DiscardOldestPushedEvent();"), 2
        )
        assert_order(
            self,
            overflow,
            "Com_DiscardOldestPushedEvent();",
            "if ( event->evType != SE_INPUT_RESET )",
            "Com_DiscardOldestPushedEvent();",
            "resetEvent.evType = SE_INPUT_RESET;",
            "*ev = resetEvent;",
            "*ev = *event;",
        )


class ClientInputStateTests(unittest.TestCase):
    def test_key_entry_rejects_unmapped_and_out_of_range_keys(self) -> None:
        keys = read_text("code/client/cl_keys.cpp")
        key_event = function_body(keys, "CL_KeyEvent")

        assert_order(
            self,
            key_event,
            "if ( key == 0 )",
            "return;",
            "if ( key < 0 || key >= MAX_KEYS )",
            "return;",
            "CL_KeyDownEvent( key, time )",
        )

    def test_duplicate_key_release_cannot_underflow_or_redispatch(self) -> None:
        keys = read_text("code/client/cl_keys.cpp")
        key_up = function_body(keys, "CL_KeyUpEvent")

        assert_order(
            self,
            key_up,
            "const bool bound = keys[key].bound != qfalse;",
            "const bool wasDown = keys[key].down != qfalse;",
            "keys[key].down = qfalse;",
            "if ( wasDown )",
            "if ( anykeydown > 0 )",
            "--anykeydown;",
            "} else {",
            "return;",
            "Key_ParseBinding(",
        )

    def test_clear_states_releases_live_keys_and_resets_text_decoder(
        self,
    ) -> None:
        keys = read_text("code/client/cl_keys.cpp")
        clear = function_body(keys, "Key_ClearStates")

        self.assertIn("textInputDecoder.Reset();", clear)
        self.assertIn("anykeydown = 0;", clear)
        self.assertIn("if ( keys[i].down )", clear)
        self.assertIn("CL_KeyEvent( i, qfalse, 0 );", clear)
        self.assertIn("keys[i].repeats = 0;", clear)
        assert_order(
            self,
            clear,
            "CL_KeyEvent( i, qfalse, 0 );",
            "CL_ClearKeyCommandInputState();",
        )

    def test_per_source_generation_rejects_only_stale_engine_commands(
        self,
    ) -> None:
        bindings = read_text("code/qcommon/keys.c")
        key_header = read_text("code/client/keys.h")
        common_header = read_text("code/qcommon/qcommon.h")
        client_header = read_text("code/client/client.h")
        client = read_text("code/client/cl_input.cpp")
        parse = function_body(bindings, "Key_ParseBinding")
        current = function_body(bindings, "Key_GetBindingGeneration")
        advance = function_body(bindings, "Key_AdvanceBindingGeneration")
        advance_all = function_body(
            bindings, "Key_AdvanceAllBindingGenerations"
        )
        canonical = function_body(
            client, "CL_IsEngineStatefulInputCommand"
        )
        validate = function_body(client, "CL_ValidateInputCommandSource")
        key_down = function_body(client, "IN_KeyDown")
        key_up = function_body(client, "IN_KeyUp")
        mlook_down = function_body(client, "IN_MLookDown")
        mlook_up = function_body(client, "IN_MLookUp")
        command_buttons = section(
            client,
            "static std::array<kbutton_t *, 13> IN_CommandButtons",
            "static void IN_CenterView",
        )
        clear_commands = function_body(client, "IN_ClearCommandInputState")
        remove_source = function_body(
            client, "IN_RemoveCommandInputSource"
        )
        clear_key_commands = function_body(
            client, "CL_ClearKeyCommandInputState"
        )

        self.assertIn(
            "unsigned Key_GetBindingGeneration( int keynum );", key_header
        )
        self.assertIn(
            "void Key_AdvanceBindingGeneration( int keynum );",
            key_header,
        )
        self.assertIn(
            "void Key_AdvanceAllBindingGenerations( void );", key_header
        )
        self.assertIn(
            "qboolean CL_IsEngineStatefulInputCommand("
            " const char *command );",
            common_header,
        )
        self.assertIn(
            "int CL_ValidateInputCommandSource( void );", client_header
        )
        self.assertIn(
            "void CL_ClearKeyCommandInputState( void );", client_header
        )
        assert_order(
            self,
            parse,
            "CL_IsEngineStatefulInputCommand( p )",
            '"%c%s %d %u fnql-gen:%u\\n"',
            "Key_GetBindingGeneration( key )",
            "Cbuf_AddInputText( cmd )",
            "else",
            '"%c%s %d %d\\n"',
        )
        self.assertIn("qboolean Cbuf_AddInputText( const char *text );", common_header)
        self.assertIn("keyBindingGenerations[MAX_KEYS]", bindings)
        self.assertIn("keynum < 0 || keynum >= MAX_KEYS", current)
        self.assertIn("return keyBindingGenerations[keynum];", current)
        assert_order(
            self,
            advance,
            "keynum < 0 || keynum >= MAX_KEYS",
            "++keyBindingGenerations[keynum];",
            "if ( keyBindingGenerations[keynum] == 0u )",
            "keyBindingGenerations[keynum] = 1u;",
        )
        assert_order(
            self,
            advance_all,
            "keynum = 0",
            "keynum < MAX_KEYS",
            "Key_AdvanceBindingGeneration( keynum );",
        )
        self.assertIn("kInputCommandBindings", canonical)
        self.assertIn("binding.name[0] == '+'", canonical)
        self.assertIn("IsCanonicalCommandSegment(", canonical)
        self.assertIn("command, binding.name", canonical)
        self.assertIn('command, "+voice"', canonical)

        assert_order(
            self,
            validate,
            "ParseInputCommandGenerationTag( Cmd_Argv( 3 ) )",
            "if ( !tag.tagged )",
            "return CL_INPUT_COMMAND_LEGACY;",
            "ParseUnsignedInputCommandArgument( Cmd_Argv( 1 ) )",
            "if ( !tag.valid",
            "*sourceKey >= MAX_KEYS",
            "return CL_INPUT_COMMAND_STALE;",
            "const int key = static_cast<int>( *sourceKey );",
            "Key_GetBindingGeneration( key )",
        )

        for command in (key_down, key_up):
            with self.subTest(command=command[:40]):
                assert_order(
                    self,
                    command,
                    "const int commandSource = CL_ValidateInputCommandSource();",
                    "if ( commandSource == CL_INPUT_COMMAND_STALE )",
                    "return;",
                )
                self.assertIn("ParseSignedInputCommandArgument", command)
                self.assertIn("ParseUnsignedInputCommandArgument( Cmd_Argv( 2 ) )", command)
        self.assertIn("IN_KeyDown( &in_mlook );", mlook_down)
        assert_order(
            self,
            mlook_up,
            "const bool wasActive = in_mlook.active;",
            "IN_KeyUp( &in_mlook );",
            "wasActive && !in_mlook.active",
            "IN_CenterView",
        )

        for button in (
            "in_left",
            "in_right",
            "in_forward",
            "in_back",
            "in_lookup",
            "in_lookdown",
            "in_moveleft",
            "in_moveright",
            "in_strafe",
            "in_speed",
            "in_up",
            "in_down",
            "in_mlook",
        ):
            self.assertIn(f"&{button}", command_buttons)
        self.assertIn("*button = kbutton_t{};", clear_commands)
        self.assertIn("in_buttons.fill( kbutton_t{} );", clear_commands)
        self.assertIn("recenterMlook", clear_commands)
        self.assertIn("IN_CenterView();", clear_commands)

        assert_order(
            self,
            remove_source,
            "RemoveHeldInputSource(",
            "button->down, sourceKey",
            "if ( button->down[0] || button->down[1] )",
            "button->active = true;",
            "button->msec = fnql::input::SaturatingAddUnsigned(",
            "button->active = false;",
            "button->downtime = 0;",
        )
        self.assertIn("without per-source provenance", remove_source)
        self.assertNotIn("button->wasPressed =", remove_source)
        self.assertIn("for ( kbutton_t& button : in_buttons )", remove_source)
        self.assertIn(
            "mlookWasActive && !in_mlook.active", remove_source
        )
        assert_order(
            self,
            clear_key_commands,
            "Key_AdvanceAllBindingGenerations();",
            "IN_ClearCommandInputState();",
            "CL_ClearGeneratedVoiceInputState();",
        )

    def test_reset_barrier_clears_client_state_without_mutating_producer(
        self,
    ) -> None:
        client = read_text("code/client/cl_input.cpp")
        reset = function_body(client, "CL_ResetInputState")

        assert_order(
            self,
            reset,
            "Key_ClearStates();",
            "CL_ResetVoiceInputState();",
            "for ( int i = 0; i < 2; ++i )",
            "cl.mouseDx[i] = 0;",
            "cl.mouseDy[i] = 0;",
            "for ( int& axis : cl.joystickAxis )",
            "axis = 0;",
            "retailMouseFilter.Reset(",
        )
        self.assertNotIn("IN_ResetInputState", reset)
        self.assertNotIn("Key_AdvanceAllBindingGenerations", reset)
        self.assertNotIn("IN_ClearCommandInputState", reset)

        for path in (
            "code/sdl/sdl_input.cpp",
            "code/win32/win_input.cpp",
            "code/unix/linux_glimp.cpp",
        ):
            with self.subTest(path=path):
                self.assertIn(
                    "void IN_ResetInputState( void )", read_text(path)
                )

    def test_mouse_only_reset_preserves_keyboard_joystick_and_backend(
        self,
    ) -> None:
        client = read_text("code/client/cl_input.cpp")
        header = read_text("code/qcommon/qcommon.h")
        reset = function_body(client, "CL_ResetMouseInputState")

        self.assertIn(
            "void CL_ResetMouseInputState( unsigned int auxiliaryKeyMask );",
            header,
        )
        assert_order(
            self,
            reset,
            "for ( int key = K_MOUSE1; key <= K_MWHEELUP; ++key )",
            "if ( keys[key].down )",
            "CL_KeyEvent( key, qfalse, 0 );",
            "for ( unsigned int bit = 0; bit < 16; ++bit )",
            "auxiliaryKeyMask & ( 1u << bit )",
            "keys[K_AUX1 + bit].down",
            "CL_KeyEvent( K_AUX1 + bit, qfalse, 0 );",
            "for ( int key = K_MOUSE1; key <= K_MWHEELUP; ++key )",
            "Key_AdvanceBindingGeneration( key );",
            "IN_RemoveCommandInputSource( key );",
            "CL_RemoveVoiceInputSource( key );",
            "if ( auxiliaryKeyMask & ( 1u << bit ) )",
            "Key_AdvanceBindingGeneration( key );",
            "IN_RemoveCommandInputSource( key );",
            "CL_RemoveVoiceInputSource( key );",
            "for ( int i = 0; i < 2; ++i )",
            "cl.mouseDx[i] = 0;",
            "cl.mouseDy[i] = 0;",
            "retailMouseFilter.Reset(",
        )
        self.assertNotIn("Key_ClearStates", reset)
        self.assertNotIn("IN_ResetInputState", reset)
        self.assertNotIn("cl.joystickAxis", reset)

    def test_unsampled_mouse_motion_is_not_replayed_on_resume(self) -> None:
        client = read_text("code/client/cl_input.cpp")
        suspend = function_body(client, "CL_SuspendUsercmdInputSampling")
        create_new = function_body(client, "CL_CreateNewCommands")
        send = function_body(client, "CL_SendCmd")
        create = function_body(client, "CL_CreateCmd")

        assert_order(
            self,
            suspend,
            "cl.mouseDx[i] = 0;",
            "cl.mouseDy[i] = 0;",
            "retailMouseFilter.Reset(",
            "old_com_frameTime = static_cast<unsigned>( com_frameTime );",
        )
        self.assertIn("CL_SuspendUsercmdInputSampling();", create_new)
        self.assertEqual(send.count("CL_SuspendUsercmdInputSampling();"), 2)
        assert_order(
            self,
            create,
            "CL_MouseMove( &cmd );",
            "CL_FinishMove( &cmd );",
            "if ( pitchLimited )",
            "retailMouseFilter.Reset( finalView );",
            "else",
            "retailMouseFilter.Synchronize(",
        )

    def test_stateful_input_has_reserved_command_capacity(self) -> None:
        command_buffer = read_text("code/qcommon/cmd.c")
        common_header = read_text("code/qcommon/qcommon.h")
        bindings = read_text("code/qcommon/keys.c")
        add_text = function_body(command_buffer, "Cbuf_AddText")
        add_input = function_body(command_buffer, "Cbuf_AddInputText")
        can_append = function_body(command_buffer, "Cbuf_CanAppend")

        self.assertIn("#define MAX_CMD_TEXT            65536", command_buffer)
        self.assertIn("#define MAX_INPUT_CMD_RESERVE   65536", command_buffer)
        self.assertIn("Cbuf_CanAppend( text, MAX_CMD_TEXT", add_text)
        self.assertIn("Cbuf_CanAppend( text, cmd_text.maxsize", add_input)
        self.assertIn("*length = strlen( text );", can_append)
        self.assertIn("*length >= (size_t)( limit - cmd_text.cursize )", can_append)
        self.assertIn("qboolean Cbuf_AddInputText( const char *text );", common_header)
        self.assertIn("Cbuf_AddInputText( cmd )", bindings)

    def test_native_directinput_buffer_matches_retail_depth(self) -> None:
        native = read_text("code/win32/win_input.cpp")
        self.assertRegex(native, r"#define DINPUT_BUFFERSIZE\s+0x200")

    def test_usercmd_counters_and_clock_deltas_are_wrap_safe(self) -> None:
        client = read_text("code/client/cl_input.cpp")
        messages = read_text("code/qcommon/msg.cpp")
        server = read_text("code/server/sv_client.cpp")
        create_new = function_body(client, "CL_CreateNewCommands")
        write_packet = function_body(client, "CL_WritePacket")
        write_delta = function_body(messages, "MSG_WriteDeltaUsercmdKey")
        read_delta = function_body(messages, "MSG_ReadDeltaUsercmdKey")
        user_move = section(server, "static void SV_UserMove", "SV_ExecuteClientMessage")

        self.assertIn("fnql::net::NextCounter( cl.cmdNumber )", create_new)
        self.assertIn("fnql::net::PendingCounterCount(", write_packet)
        self.assertIn("fnql::net::CounterAdd(", write_packet)
        self.assertIn("fnql::net::CounterSubtract(", write_packet)
        self.assertIn("fnql::net::CounterDistance(", write_delta)
        self.assertIn("fnql::net::CounterAdd(", read_delta)
        self.assertEqual(user_move.count("fnql::net::IsNewerCounter("), 2)

    def test_protocol_91_uses_the_retail_usercmd_hash_on_both_ends(self) -> None:
        client = function_body(
            read_text("code/client/cl_input.cpp"), "CL_WritePacket"
        )
        server = section(
            read_text("code/server/sv_client.cpp"),
            "static void SV_UserMove",
            "SV_ExecuteClientMessage",
        )
        messages = read_text("code/qcommon/msg.cpp")
        profile_hash = function_body(messages, "MSG_HashKeyForWireProfile")
        profile_string = function_body(
            messages, "MSG_ReadStringForWireProfile"
        )

        self.assertIn("key ^= clc.serverCommandHashes[", client)
        self.assertIn(
            "MSG_HashKeyForWireProfile( cl->netchan.wireProfile,", server
        )
        self.assertIn("profile != NETCHAN_WIRE_QL_RETAIL", profile_hash)
        self.assertIn("return MSG_HashKey( string, maxlen );", profile_hash)
        self.assertNotIn("string[i] == '%'", profile_hash)
        self.assertIn("profile == NETCHAN_WIRE_QL_RETAIL", profile_string)
        self.assertIn(
            "MSG_ReadCommandStringForWireProfile( msg, clc.netchan.wireProfile )",
            read_text("code/client/cl_parse.cpp"),
        )
        self.assertIn(
            "clc.serverCommandHashes[ index ] = wireHash;",
            read_text("code/client/cl_parse.cpp"),
        )
        self.assertIn(
            "MSG_ReadStringForWireProfile( msg, cl->netchan.wireProfile )",
            read_text("code/server/sv_client.cpp"),
        )
        self.assertIn(
            "MSG_WriteStringForWireProfile( &buf,", client
        )
        self.assertIn(
            "MSG_WriteStringForWireProfile( msg,",
            read_text("code/server/sv_snapshot.cpp"),
        )
        add_server_command = function_body(
            read_text("code/server/sv_main.cpp"), "SV_AddServerCommand"
        )
        self.assertIn(
            "client->netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL",
            add_server_command,
        )
        self.assertIn("cmd[offset] == '%' ? '.' : cmd[offset]", add_server_command)

    def test_voice_binding_has_source_ownership_and_synchronous_resets(
        self,
    ) -> None:
        webui = read_text("code/client/cl_webui.cpp")
        header = read_text("code/client/client.h")
        start = function_body(webui, "CL_Steam_VoiceStart_f")
        stop = function_body(webui, "CL_Steam_VoiceStop_f")
        reset = function_body(webui, "CL_ResetVoiceInputState")
        clear_generated = function_body(
            webui, "CL_ClearGeneratedVoiceInputState"
        )
        remove = function_body(webui, "CL_RemoveVoiceInputSource")
        register = function_body(webui, "QLWebHost_RegisterCommands")
        unregister = function_body(webui, "QLWebHost_UnregisterCommands")
        disconnect = function_body(
            read_text("code/client/cl_main.cpp"), "CL_Disconnect"
        )

        self.assertIn(
            "std::array<qboolean, MAX_KEYS> cl_steamVoiceInputSources;",
            webui,
        )
        self.assertIn("qboolean cl_steamVoiceManualOwner;", webui)
        self.assertIn("void CL_ResetVoiceInputState( void );", header)
        self.assertIn(
            "void CL_ClearGeneratedVoiceInputState( void );", header
        )
        self.assertIn(
            "void CL_RemoveVoiceInputSource( int sourceKey );", header
        )

        assert_order(
            self,
            start,
            "CL_ValidateInputCommandSource()",
            "source == CL_INPUT_COMMAND_STALE",
            "return;",
            "source == CL_INPUT_COMMAND_LEGACY",
            "cl_steamVoiceManualOwner = qtrue;",
            "cl_steamVoiceInputSources[",
            "= qtrue;",
            "CL_Steam_StartVoiceRecording();",
        )
        assert_order(
            self,
            stop,
            "CL_ValidateInputCommandSource()",
            "source == CL_INPUT_COMMAND_STALE",
            "return;",
            "source == CL_INPUT_COMMAND_LEGACY",
            "cl_steamVoiceManualOwner = qfalse;",
            "cl_steamVoiceInputSources.fill( qfalse );",
            "CL_Steam_StopVoiceRecording();",
            "cl_steamVoiceInputSources[",
            "= qfalse;",
            "if ( !CL_Steam_HasVoiceInputOwner() )",
            "CL_Steam_StopVoiceRecording();",
        )
        assert_order(
            self,
            reset,
            "cl_steamVoiceManualOwner = qfalse;",
            "cl_steamVoiceInputSources.fill( qfalse );",
            "CL_Steam_StopVoiceRecording();",
        )
        assert_order(
            self,
            clear_generated,
            "cl_steamVoiceInputSources.fill( qfalse );",
            "if ( !cl_steamVoiceManualOwner )",
            "CL_Steam_StopVoiceRecording();",
        )
        self.assertNotIn(
            "cl_steamVoiceManualOwner =", clear_generated
        )
        assert_order(
            self,
            remove,
            "sourceKey < 0 || sourceKey >= MAX_KEYS",
            "return;",
            "cl_steamVoiceInputSources[",
            "= qfalse;",
            "if ( !CL_Steam_HasVoiceInputOwner() )",
            "CL_Steam_StopVoiceRecording();",
        )
        for command in (
            '"+voice"',
            '"-voice"',
            '"steam_voice_start"',
            '"steam_voice_stop"',
        ):
            self.assertIn(command, register)
        self.assertIn("CL_ResetVoiceInputState();", unregister)
        assert_order(
            self,
            disconnect,
            "Key_ClearStates();",
            "CL_ResetVoiceInputState();",
        )

    def test_clipboard_paste_inserts_data_without_editor_recursion(
        self,
    ) -> None:
        keys = read_text("code/client/cl_keys.cpp")
        paste = function_body(keys, "Field_Paste")

        self.assertIn(
            "ScopedZoneMemory clipboardText( Sys_GetClipboardData() );",
            paste,
        )
        self.assertIn("fnql::input::DecodeUtf8(", paste)
        self.assertIn("replacementCharacter", paste)
        self.assertGreaterEqual(paste.count("Field_InsertUtf8Scalar("), 3)
        self.assertIn("byte >= ' ' && byte != 0x7fu", paste)
        self.assertNotIn("Field_CharEvent", paste)
        self.assertNotIn("Field_KeyDownEvent", paste)
        self.assertNotIn("CTRL(", paste)

    def test_relative_mouse_events_never_become_overlay_positions(self) -> None:
        client = read_text("code/client/cl_input.cpp")
        mouse = function_body(client, "CL_MouseEvent")
        overlay_guard = (
            "KEYCATCH_BROWSER | KEYCATCH_UI | KEYCATCH_CGAME"
        )

        self.assertIn(overlay_guard, mouse)
        self.assertLess(
            mouse.index(overlay_guard),
            mouse.index("cl.mouseDx[cl.mouseIndex]"),
        )
        self.assertIn("return;", mouse[mouse.index(overlay_guard) :])

    def test_all_input_sources_bound_float_conversions_and_view_deltas(
        self,
    ) -> None:
        client = read_text("code/client/cl_input.cpp")
        safe_move = function_body(client, "CL_SafeMoveValue")
        add_view = function_body(client, "CL_AddViewAngleDelta")
        legacy = function_body(client, "CL_MouseMove")
        retail = function_body(client, "CL_RetailMouseMove")
        keys = function_body(client, "CL_KeyMove")
        key_angles = function_body(client, "CL_AdjustAngles")
        joystick = function_body(client, "CL_JoystickMove")
        finish = function_body(client, "CL_FinishMove")

        assert_order(
            self,
            safe_move,
            "fnql::input::FiniteOr( delta, 0.0f )",
            "fnql::input::FiniteOr(",
            "static_cast<float>( current ) + finiteDelta",
            "fnql::input::TruncateFiniteFloatToInt( combined )",
        )
        assert_order(
            self,
            add_view,
            "fnql::input::FiniteOr( cl.viewangles[axis], 0.0f )",
            "fnql::input::FiniteOr( delta, 0.0f )",
            "fnql::input::FiniteOr( current + finiteDelta, current )",
        )

        self.assertIn("cl_mouseAccelStyle->integer == 2", legacy)
        self.assertIn("cl_mouseAccelStyle->integer == 0", legacy)
        for name, body in (("legacy styles 0/1", legacy), ("retail style 2", retail)):
            with self.subTest(style=name):
                self.assertEqual(body.count("CL_SafeMoveValue("), 2)
                self.assertEqual(body.count("CL_AddViewAngleDelta("), 2)
                self.assertNotIn("ClampCharMove( cmd->", body)
                self.assertNotIn("cl.viewangles[YAW] -=", body)
                self.assertNotIn("cl.viewangles[PITCH] +=", body)

        self.assertEqual(keys.count("CL_SafeMoveValue("), 8)
        self.assertEqual(key_angles.count("CL_AddViewAngleDelta("), 4)
        self.assertEqual(joystick.count("CL_AddViewAngleDelta("), 2)
        self.assertNotRegex(
            key_angles, r"cl\.viewangles\[(?:YAW|PITCH)\]\s*[+-]="
        )
        assert_order(
            self,
            finish,
            "const float safeAngle = fnql::input::FiniteAngleForShort(",
            "fnql::input::FiniteOr( cl.viewangles[i], 0.0f )",
            "cl.viewangles[i] = safeAngle;",
            "cmd->angles[i] = ANGLE2SHORT( safeAngle );",
        )

        self.assertIn(
            "fnql::input::FiniteOr( view.yaw, unfiltered.yaw )",
            retail,
        )
        self.assertIn(
            "fnql::input::FiniteOr( view.pitch, unfiltered.pitch )",
            retail,
        )

    def test_joystick_events_validate_axis_before_bounded_storage(
        self,
    ) -> None:
        client = read_text("code/client/cl_input.cpp")
        header = read_text("code/qcommon/qcommon.h")
        event = function_body(client, "CL_JoystickEvent")

        assert_order(
            self,
            event,
            "axis < 0 || axis >= MAX_JOYSTICK_AXIS",
            "Com_DPrintf(",
            "return;",
            "cl.joystickAxis[axis] = std::clamp( value, -32768, 32767 );",
        )
        self.assertNotIn("Com_Error", event)
        self.assertIn(
            "evValue2 is a signed producer value (-32768 to 32767)",
            header,
        )


class UnicodeFieldAndCompletionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.keys = read_text("code/client/cl_keys.cpp")
        cls.console = read_text("code/client/cl_console.cpp")
        cls.common = read_text("code/qcommon/common.c")

    def test_engine_field_scalar_insert_is_capacity_atomic(self) -> None:
        insert = function_body(self.keys, "Field_InsertUtf8Scalar")

        assert_order(
            self,
            insert,
            "constexpr int fieldCapacity = MAX_EDIT_LINE - 2;",
            "byteCount == 0 || byteCount > 4",
            "edit->cursor < 0 || edit->cursor > len",
            "key_overstrikeMode",
            "Field_IsUtf8ContinuationByte",
            "const int newLength =",
            "if ( newLength > fieldCapacity )",
            "return false;",
            "std::memmove(",
            "std::memcpy(",
        )
        self.assertIn("len - overwriteEnd + 1", insert)
        self.assertLess(
            insert.index("if ( newLength > fieldCapacity )"),
            insert.index("std::memmove("),
        )

    def test_char_dispatch_keeps_engine_scalars_atomic_and_module_abi_bytes(
        self,
    ) -> None:
        char_event = function_body(self.keys, "CL_CharEvent")

        assert_order(
            self,
            char_event,
            "textInputDecoder.Consume",
            "fnql::input::EncodeUtf8",
            "catcher & KEYCATCH_CONSOLE",
            "Con_CharEventUtf8(",
            "catcher & KEYCATCH_UI",
            "if ( uivm )",
            "for ( std::size_t i = 0; i < encoded.size; ++i )",
            "UI_KEY_EVENT, utf8Byte | K_CHAR_FLAG",
            "catcher & KEYCATCH_MESSAGE",
            "Field_InsertUtf8Scalar( &chatField",
            "cls.state == CA_DISCONNECTED",
            "Field_InsertUtf8Scalar( &g_consoleField",
        )

    def test_console_scalar_and_text_insertion_are_strict_transactions(
        self,
    ) -> None:
        scalar = function_body(
            self.console, "Con_InsertInputUtf8Scalar"
        )
        text = function_body(self.console, "Con_InsertInputTextAt")

        assert_order(
            self,
            scalar,
            "fnql::input::DecodeUtf8( bytes, byteCount )",
            "!decoded.valid || decoded.size != byteCount",
            "ClampUtf8Boundary",
            "Con_HasInputSelection()",
            "NextUtf8Boundary",
            "const int newLength =",
            "if ( newLength > inputCapacity )",
            "return false;",
            "std::memmove(",
            "std::memcpy(",
            "Con_ClearInputSelection();",
        )
        self.assertIn("len - replaceEnd + 1", scalar)
        assert_order(
            self,
            text,
            "fnql::input::DecodeUtf8( bytes, textLength - i )",
            "if ( !decoded.valid )",
            "Con_InsertInputUtf8Scalar( conUtf8ReplacementCharacter",
            "if ( decoded.size > 1 )",
            "Con_InsertInputUtf8Scalar( bytes, decoded.size, false )",
            "Con_InsertInputChar( ch, false )",
        )

    def test_popup_completion_preflights_exact_result_length(self) -> None:
        apply = function_body(
            self.console, "Con_ApplySelectedCompletion"
        )

        assert_order(
            self,
            apply,
            "suffixLen = len - suffixOffset;",
            "requiredLength =",
            "replaceOffset + matchLen",
            "suffixLen;",
            "if ( requiredLength > MAX_EDIT_LINE - 2 )",
            "return;",
            "std::copy_n( buffer",
            "std::copy_n( match",
            "assert( outLen == requiredLength );",
            "completed[ outLen ] = '\\0';",
        )
        self.assertIn(
            "g_consoleField.cursor = "
            "( con.completionPrependSlash ? 1 : 0 ) +",
            apply,
        )

    def test_completion_matching_preserves_utf8_boundaries_and_ascii_fold(
        self,
    ) -> None:
        fold = section(
            self.common,
            "static unsigned char Field_FoldCompletionByte",
            "/*\n===============\nFindMatches",
        )
        matches = function_body(self.common, "FindMatches")

        self.assertIn("value >= 'A' && value <= 'Z'", fold)
        self.assertIn("return value;", fold)
        self.assertIn("Field_CopyUtf8Prefix(", matches)
        assert_order(
            self,
            matches,
            "Field_FoldCompletionByte(",
            "Field_FoldCompletionByte(",
            "Field_ClampUtf8Boundary( shortestMatch",
            "shortestMatch[i] = '\\0';",
        )

    def test_field_completion_and_queries_reject_partial_results(self) -> None:
        complete = function_body(self.common, "Field_Complete")
        command = function_body(self.common, "Field_CompleteCommand")

        assert_order(
            self,
            complete,
            "bufferLength = strlen( completionField->buffer );",
            "completionLength = strlen( completionString );",
            "if ( completionLength > bufferLength )",
            "completionOffset = bufferLength - completionLength;",
            "replacementLength = strlen( shortestMatch );",
            "completionOffset + replacementLength >",
            "sizeof( completionField->buffer ) - 2",
            "return qtrue;",
            "memcpy(",
            "replacementLength + 1",
            "completionField->cursor = "
            "(int)( completionOffset + replacementLength );",
        )
        assert_order(
            self,
            command,
            'cmd = Com_SkipCharset( cmd, " \\"" );',
            "if ( !cmd[0] )",
            "return;",
            "Cmd_TokenizeStringIgnoreQuotes( cmd );",
            "strlen( cmd ) - 1",
        )

        for name in (
            "Field_QueryCompletionMatches",
            "Field_QueryCompletionCandidates",
        ):
            with self.subTest(function=name):
                query = function_body(self.common, name)
                assert_order(
                    self,
                    query,
                    "commandLength = strlen( cmd );",
                    "commandLength > sizeof( queryField.buffer ) - 2",
                    "return 0;",
                    "memcpy( queryField.buffer, cmd, commandLength + 1 );",
                )


class SDLInputProducerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = read_text("code/sdl/sdl_input.cpp")
        cls.events = function_body(cls.source, "HandleEvents")

    def test_window_hit_testing_reads_physical_alt_state(self) -> None:
        glimp = read_text("code/sdl/sdl_glimp.cpp")
        hit_test = function_body(glimp, "SDL_HitTestFunc")

        self.assertIn("SDL_GetModState()", hit_test)
        self.assertIn("SDL_KMOD_ALT", hit_test)
        self.assertNotIn("keys[K_ALT].down", hit_test)

    def test_gl_and_vulkan_shutdown_never_warp_the_global_pointer(
        self,
    ) -> None:
        glimp = read_text("code/sdl/sdl_glimp.cpp")

        for shutdown in ("GLimp_Shutdown", "VKimp_Shutdown"):
            with self.subTest(shutdown=shutdown):
                self.assertNotIn(
                    "SDL_WarpMouseGlobal",
                    function_body(glimp, shutdown),
                )

    def test_mode_transitions_discard_only_synthetic_motion(self) -> None:
        gobble = function_body(self.source, "IN_GobbleMotionEvents")
        motion = section(
            self.events,
            "case SDL_EVENT_MOUSE_MOTION:",
            "case SDL_EVENT_MOUSE_BUTTON_DOWN:",
        )

        self.assertIn("SDL_PumpEvents();", gobble)
        self.assertIn(
            "SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_MOTION", gobble
        )
        self.assertNotIn("SDL_EVENT_MOUSE_BUTTON", gobble)
        self.assertNotIn("SDL_EVENT_MOUSE_WHEEL", gobble)
        assert_order(
            self,
            motion,
            "s_pointerModeValid",
            "s_pointerMode.driveInput",
            "s_pointerMode.relativeMotion",
            "e.motion.xrel || e.motion.yrel",
            "SE_MOUSE",
        )

    def test_absolute_dedup_is_consumer_aware_with_retail_precedence(
        self,
    ) -> None:
        consumer = function_body(
            self.source, "IN_PointerConsumerIdentity"
        )
        queue = function_body(
            self.source, "IN_QueueAbsolutePointerPosition"
        )

        assert_order(
            self,
            consumer,
            "catcher & KEYCATCH_CONSOLE",
            "catcher & KEYCATCH_BROWSER",
            "catcher & KEYCATCH_UI",
            "catcher & KEYCATCH_CGAME",
        )
        self.assertIn(
            "const int consumer = IN_PointerConsumerIdentity();", queue
        )
        self.assertIn("consumer == s_absLastConsumer", queue)
        self.assertIn("s_absLastConsumer = consumer;", queue)

    def test_absolute_float_coordinates_scale_before_single_truncation(
        self,
    ) -> None:
        queue = function_body(
            self.source, "IN_QueueAbsolutePointerPosition"
        )
        compat = read_text("code/client/input_compat.hpp")

        self.assertIn(
            "ProjectPointerToDrawable(\n"
            "\t\t\twindowX, windowY, IN_PointerProjection() )",
            queue,
        )
        self.assertNotIn("TruncateFiniteFloatToInt", queue)
        self.assertIn(
            "float value, int hostExtent, int drawableExtent", compat
        )
        self.assertIn(
            "scaled = scaled * static_cast<double>( drawableExtent )",
            compat,
        )

    def test_event_time_uses_modular_arithmetic_and_bounded_age(self) -> None:
        event_time = function_body(self.source, "IN_EventTime")

        self.assertIn("timestamp <= nowNanoseconds", event_time)
        self.assertIn("std::numeric_limits<std::uint32_t>::max", event_time)
        self.assertIn(
            "static_cast<std::uint32_t>( now ) - elapsed", event_time
        )
        self.assertIn(
            "static_cast<std::int64_t>( 1 ) << 32", event_time
        )
        self.assertNotIn("static_cast<int>( elapsed", event_time)
        self.assertLess(
            self.events.index(
                "in_eventTime = IN_EventTime( e.common.timestamp );"
            ),
            self.events.index("switch( e.type )"),
        )

    def test_repeat_gate_translates_first_and_suppresses_one_shots(
        self,
    ) -> None:
        key_down = section(
            self.events,
            "case SDL_EVENT_KEY_DOWN:",
            "case SDL_EVENT_KEY_UP:",
        )

        assert_order(
            self,
            key_down,
            "key = IN_TranslateSDLToQ3Key",
            "if ( e.key.repeat )",
            "if ( key == K_CONSOLE )",
            "s_lastKeyDown = K_CONSOLE;",
            "if ( key == K_ESCAPE ||",
            "!fnql::input::CatcherBlocksGameplayInput(",
            "Key_GetCatcher(), KEYCATCH_RETAIL_MOUSEPASS",
            "cls.state != CA_DISCONNECTED",
            "break;",
        )
        alt_enter = key_down[key_down.index("if ( key == K_ENTER") :]
        assert_order(
            self,
            alt_enter,
            "if ( !e.key.repeat )",
            'Cvar_SetIntegerValue( "r_fullscreen"',
            'Cbuf_AddText( "vid_restart fast\\n" );',
            "break;",
        )
        self.assertNotIn("Key_GetCatcher() == 0", key_down)

    def test_character_console_repeat_suppression_preserves_other_text(
        self,
    ) -> None:
        queue_text = function_body(self.source, "IN_QueueTextInput")
        text_event = section(
            self.events,
            "case SDL_EVENT_TEXT_INPUT:",
            "case SDL_EVENT_MOUSE_MOTION:",
        )

        self.assertIn(
            "qboolean suppressRepeatedConsoleKey", self.source
        )
        assert_order(
            self,
            queue_text,
            "if ( IN_IsConsoleKey( 0, utf32 ) )",
            "if ( !suppressRepeatedConsoleKey )",
            "SE_KEY, K_CONSOLE, qtrue",
            "else\n",
            "SE_CHAR, utf32",
        )
        self.assertIn(
            "e.text.text, s_lastKeyDownWasRepeat", text_event
        )
        self.assertIn("s_lastKeyDown = 0;", text_event)
        self.assertIn("s_lastKeyDownWasRepeat = qfalse;", text_event)
        self.assertLess(
            text_event.index("IN_QueueTextInput("),
            text_event.index("s_lastKeyDown = 0;"),
        )
        self.assertRegex(
            self.events,
            r"if \( e\.type != SDL_EVENT_KEY_DOWN &&\s*"
            r"e\.type != SDL_EVENT_KEY_UP &&\s*"
            r"e\.type != SDL_EVENT_TEXT_EDITING &&\s*"
            r"e\.type != SDL_EVENT_TEXT_EDITING_CANDIDATES &&\s*"
            r"e\.type != SDL_EVENT_TEXT_INPUT \) \{\s*"
            r"s_lastKeyDown = 0;\s*"
            r"s_lastKeyDownWasRepeat = qfalse;",
        )
        assert_order(
            self,
            self.events,
            "key = IN_TranslateSDLToQ3Key",
            "s_lastKeyDownWasRepeat =",
            "e.key.repeat ? qtrue : qfalse",
        )

    def test_click_position_is_speculative_ordered_and_capture_is_verified(
        self,
    ) -> None:
        button = section(
            self.events,
            "case SDL_EVENT_MOUSE_BUTTON_DOWN:",
            "case SDL_EVENT_MOUSE_WHEEL:",
        )

        self.assertIn("e.button.y, in_eventTime, qtrue", button)
        self.assertLess(
            button.index("IN_QueueAbsolutePointerPosition( owner,"),
            button.index("Com_QueueEvent( in_eventTime, SE_KEY"),
        )
        assert_order(
            self,
            button,
            "s_absCaptureButtons |= buttonMask;",
            "IN_UpdateTemporaryMouseCapture();",
            "} else if ( buttonMask )",
            "s_absCaptureButtons &= ~buttonMask;",
            "IN_UpdateTemporaryMouseCapture();",
        )

        update = function_body(
            self.source, "IN_UpdateTemporaryMouseCapture"
        )
        failure = update.index(
            "if ( !SDL_CaptureMouse( requested != qfalse ) )"
        )
        assert_order(
            self,
            update,
            "const qboolean requested = s_absCaptureButtons ? qtrue : qfalse;",
            "if ( requested == s_absCaptureActive )",
            "if ( !SDL_CaptureMouse( requested != qfalse ) )",
            "return;",
            "s_absCaptureActive = requested;",
        )
        self.assertNotIn(
            "s_absCaptureActive = requested;",
            update[failure : update.index("return;", failure)],
        )

        release = function_body(
            self.source, "IN_EndTemporaryMouseCapture"
        )
        assert_order(
            self,
            release,
            "s_absCaptureButtons = 0;",
            "IN_UpdateTemporaryMouseCapture();",
        )

    def test_temporary_capture_is_reconciled_and_retried_every_frame(
        self,
    ) -> None:
        apply_mode = function_body(self.source, "IN_ApplyPointerMode")
        reset = function_body(self.source, "IN_ResetInputState")

        assert_order(
            self,
            apply_mode,
            "const Uint32 reconciledButtons",
            "s_absCaptureButtons & SDL_GetMouseState( nullptr, nullptr )",
            "if ( reconciledButtons != s_absCaptureButtons )",
            "IN_QueueMouseReset();",
            "IN_EndTemporaryMouseCapture();",
            "} else {",
            "IN_UpdateTemporaryMouseCapture();",
            "mode == s_pointerMode && !in_nograb->modified",
        )
        mismatch = apply_mode[
            apply_mode.index(
                "if ( reconciledButtons != s_absCaptureButtons )"
            ) :
            apply_mode.index("} else {", apply_mode.index(
                "if ( reconciledButtons != s_absCaptureButtons )"
            ))
        ]
        self.assertNotIn("s_absCaptureButtons = reconciledButtons", mismatch)
        self.assertIn("s_absCaptureActive", self.source)
        self.assertIn("s_absCaptureFailureReported", self.source)
        self.assertIn("IN_EndTemporaryMouseCapture();", reset)

    def test_outside_absolute_drag_stops_polling_after_final_release(
        self,
    ) -> None:
        finish = function_body(
            self.source, "IN_FinishOutsideCaptureRelease"
        )
        apply_mode = function_body(self.source, "IN_ApplyPointerMode")
        window = function_body(self.source, "IN_HandleWindowEvent")
        leave = section(
            window,
            "case SDL_EVENT_WINDOW_MOUSE_LEAVE:",
            "default:",
        )
        button = section(
            self.events,
            "case SDL_EVENT_MOUSE_BUTTON_DOWN:",
            "case SDL_EVENT_MOUSE_WHEEL:",
        )

        assert_order(
            self,
            leave,
            "if ( s_absCaptureActive && s_absCaptureButtons )",
            "s_absPointerOutside = qtrue;",
            "break;",
        )
        assert_order(
            self,
            finish,
            "s_absPointerOutside && !s_absCaptureButtons",
            "!s_absCaptureActive",
            "s_absPointerOutside = qfalse;",
            "mouse_focus = qfalse;",
            "s_absHaveLast = qfalse;",
        )
        self.assertIn("IN_FinishOutsideCaptureRelease();", button)
        self.assertIn(
            "if ( s_absCaptureActive && !s_absCaptureButtons )",
            apply_mode,
        )
        self.assertIn("IN_FinishOutsideCaptureRelease();", apply_mode)

    def test_text_input_is_owned_only_by_character_consumers(self) -> None:
        owner = function_body(self.source, "IN_TextInputOwnerActive")
        transition = function_body(self.source, "IN_SetTextInputActive")
        frame = function_body(self.source, "IN_Frame")
        initialize = function_body(self.source, "IN_Init")
        shutdown = function_body(self.source, "IN_Shutdown")
        layout_console = function_body(
            self.source, "IN_IsLayoutConsoleKey"
        )
        translate = function_body(
            self.source, "IN_TranslateSDLToQ3Key"
        )

        self.assertIn("!gw_active || gw_minimized", owner)
        self.assertIn("cls.state == CA_DISCONNECTED", owner)
        for catcher in (
            "KEYCATCH_CONSOLE",
            "KEYCATCH_UI",
            "KEYCATCH_MESSAGE",
            "KEYCATCH_BROWSER",
        ):
            self.assertIn(catcher, self.source)
        text_mask = section(
            self.source,
            "static constexpr int kTextInputCatcherMask",
            "static qboolean     s_textInputActive",
        )
        self.assertNotIn("KEYCATCH_CGAME", text_mask)
        assert_order(
            self,
            transition,
            "if ( s_textInputActive == active )",
            "SDL_StartTextInput( SDL_window )",
            "SDL_ClearComposition( SDL_window )",
            "SDL_StopTextInput( SDL_window )",
            "s_textInputActive = active;",
        )
        self.assertIn("IN_ReconcileTextInput();", frame)
        assert_order(
            self,
            self.events,
            "IN_ReconcileTextInput();",
            "while ( SDL_PollEvent( &e ) )",
        )
        window_dispatch = section(
            self.events, "case SDL_EVENT_WINDOW_MOVED:", "default:"
        )
        assert_order(
            self,
            window_dispatch,
            "IN_HandleWindowEvent( e.type, &e.window, &s_lastKeyDown );",
            "IN_ReconcileTextInput();",
            "break;",
        )
        assert_order(
            self,
            initialize,
            "SDL_TextInputActive( SDL_window )",
            "IN_ReconcileTextInput();",
        )
        self.assertIn("IN_SetTextInputActive( qfalse );", shutdown)
        self.assertIn("s_textInputActive", section(
            self.events,
            "case SDL_EVENT_TEXT_INPUT:",
            "case SDL_EVENT_MOUSE_MOTION:",
        ))

        assert_order(
            self,
            layout_console,
            "SDL_KMOD_GUI | SDL_KMOD_LALT",
            "( modifiers & SDL_KMOD_CTRL ) && !rightAlt && !mode",
            "SDL_GetKeyFromScancode(",
            "keyinfo->scancode, modifiers, false",
            "modifiers & ~( SDL_KMOD_CTRL | SDL_KMOD_RALT | SDL_KMOD_MODE )",
            "normalCandidate",
            "altGrCandidate",
            "if ( altGrCandidate == normalCandidate )",
            "candidate = altGrCandidate;",
            "SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK",
            "candidate > 0x10ffffu",
            "candidate >= 0xd800u && candidate <= 0xdfffu",
            "IN_IsConsoleKey( 0, static_cast<int>( candidate ) )",
        )
        self.assertNotIn("SDL_KMOD_SHIFT", layout_console)
        self.assertIn("IN_IsConsoleKey( key, 0 ) ||", translate)
        self.assertIn(
            "( down && IN_IsLayoutConsoleKey( keyinfo ) )", translate
        )
        for editing_event in (
            "SDL_EVENT_TEXT_EDITING",
            "SDL_EVENT_TEXT_EDITING_CANDIDATES",
        ):
            self.assertIn(editing_event, self.events)

    def test_wheel_preserves_position_direction_and_magnitude(self) -> None:
        wheel = section(
            self.events,
            "case SDL_EVENT_MOUSE_WHEEL:",
            "#ifdef USE_JOYSTICK",
        )

        self.assertIn(
            "IN_WheelRemainder( e.wheel.which, consumer )", wheel
        )
        self.assertIn("fnql::input::FiniteOr( e.wheel.y, 0.0f )", wheel)
        self.assertIn(
            "fnql::input::TruncateFiniteFloatToInt( combinedY )", wheel
        )
        self.assertNotIn("e.wheel.integer_y", wheel)
        self.assertIn("SDL_MOUSEWHEEL_FLIPPED", wheel)
        self.assertIn("std::clamp( steps, -32, 32 )", wheel)
        self.assertIn(
            "e.wheel.mouse_y,\n\t\t\t\t\t\tin_eventTime, qtrue", wheel
        )
        self.assertLess(
            wheel.index("IN_QueueAbsolutePointerPosition( owner,"),
            wheel.index("Com_QueueEvent( in_eventTime, SE_KEY"),
        )
        self.assertIn(
            "for ( int i = 0; i < std::abs( steps ); ++i )", wheel
        )

    def test_wheel_fraction_state_is_bounded_by_device_and_consumer(
        self,
    ) -> None:
        remainder = function_body(self.source, "IN_WheelRemainder")
        reset = function_body(self.source, "IN_ResetWheelAccumulator")

        self.assertIn(
            "std::array<sdlWheelAccumulator_t, 8> s_wheelAccumulators",
            self.source,
        )
        self.assertIn("slot.device == device", remainder)
        self.assertIn("slot.consumer == consumer", remainder)
        self.assertIn("if ( !slot.valid )", remainder)
        self.assertIn("slot.lastUse < replacement->lastUse", remainder)
        self.assertIn(
            "device, consumer, 0.0f, s_wheelAccumulatorUse, true",
            remainder,
        )
        self.assertIn(
            "s_wheelAccumulators.fill( sdlWheelAccumulator_t{} );", reset
        )
        self.assertIn("s_wheelAccumulatorUse = 0;", reset)

    def test_mouse_buttons_are_unique_and_aux_state_tracks_its_source(
        self,
    ) -> None:
        button = section(
            self.events,
            "case SDL_EVENT_MOUSE_BUTTON_DOWN:",
            "case SDL_EVENT_MOUSE_WHEEL:",
        )
        mouse_reset = function_body(self.source, "IN_QueueMouseReset")
        input_reset = function_body(self.source, "IN_QueueInputReset")
        backend_reset = function_body(self.source, "IN_ResetInputState")

        self.assertIn("int b = 0;", button)
        self.assertIn("e.button.button <= SDL_BUTTON_X2 + 4", button)
        self.assertIn(
            "b = K_MOUSE6 +\n"
            "\t\t\t\t\t\t\t\t\t( e.button.button - "
            "( SDL_BUTTON_X2 + 1 ) );",
            button,
        )
        self.assertIn("e.button.button >= SDL_BUTTON_X2 + 5", button)
        self.assertIn("e.button.button <= SDL_BUTTON_X2 + 20", button)
        self.assertIn(
            "b = K_AUX1 +\n"
            "\t\t\t\t\t\t\t\t\t( e.button.button - "
            "( SDL_BUTTON_X2 + 5 ) );",
            button,
        )
        self.assertIn("if ( !b )", button)
        self.assertNotIn("% 16", button)

        assert_order(
            self,
            button,
            "if ( b >= K_AUX1 && b < K_AUX1 + 16 )",
            "s_mouseAuxButtonState |= mask;",
            "} else {",
            "s_mouseAuxButtonState &= ~mask;",
            "Com_QueueEvent( in_eventTime, SE_KEY, b,",
        )
        assert_order(
            self,
            mouse_reset,
            "SE_MOUSE_RESET",
            "static_cast<int>( s_mouseAuxButtonState )",
            "s_mouseAuxButtonState = 0;",
        )
        assert_order(
            self,
            input_reset,
            "SE_INPUT_RESET",
            "s_mouseAuxButtonState = 0;",
        )
        self.assertNotIn("s_mouseAuxButtonState", backend_reset)

    def test_window_and_device_losses_use_the_narrowest_ordered_reset(
        self,
    ) -> None:
        window = function_body(self.source, "IN_HandleWindowEvent")
        mouse_leave = section(
            window,
            "case SDL_EVENT_WINDOW_MOUSE_LEAVE:",
            "default:",
        )
        keyboard_removed = section(
            self.events,
            "case SDL_EVENT_KEYBOARD_REMOVED:",
            "case SDL_EVENT_MOUSE_REMOVED:",
        )
        mouse_removed = section(
            self.events,
            "case SDL_EVENT_MOUSE_REMOVED:",
            "case SDL_EVENT_QUIT:",
        )

        assert_order(
            self,
            mouse_leave,
            "if ( s_absCaptureActive && s_absCaptureButtons )",
            "break;",
            "if ( s_absCaptureButtons || s_absCaptureActive )",
            "IN_QueueMouseReset();",
            "IN_EndTemporaryMouseCapture();",
            "mouse_focus = qfalse;",
            "break;",
            "if ( glw_state.isFullscreen ||",
        )
        self.assertIn("s_pointerMode.relativeMotion", mouse_leave)
        self.assertIn("s_pointerMode.confineToWindow", mouse_leave)
        self.assertIn("IN_QueueMouseReset();", mouse_leave)
        self.assertNotIn("SE_INPUT_RESET", mouse_leave)
        self.assertIn("IN_ResetWheelAccumulator();", mouse_leave)
        self.assertIn("IN_EndTemporaryMouseCapture();", mouse_leave)
        self.assertIn("s_pointerModeValid = qfalse;", mouse_leave)
        self.assertTrue(
            mouse_leave.rstrip().endswith("mouse_focus = qfalse;\n\t\t\tbreak;")
        )

        self.assertIn("IN_QueueInputReset(", keyboard_removed)
        self.assertIn(
            "gw_active && !gw_minimized ? qtrue : qfalse",
            keyboard_removed,
        )
        self.assertNotIn("SE_MOUSE_RESET", keyboard_removed)

        self.assertIn("IN_QueueMouseReset();", mouse_removed)
        self.assertNotIn("SE_INPUT_RESET", mouse_removed)
        self.assertIn("IN_ResetWheelAccumulator();", mouse_removed)
        self.assertIn("s_absHaveLast = qfalse;", mouse_removed)
        self.assertIn("IN_EndTemporaryMouseCapture();", mouse_removed)

    def test_window_routing_handles_occlusion_exposure_and_old_windows(
        self,
    ) -> None:
        window = function_body(self.source, "IN_HandleWindowEvent")
        occluded = section(
            window,
            "case SDL_EVENT_WINDOW_OCCLUDED:",
            "case SDL_EVENT_WINDOW_EXPOSED:",
        )
        exposed = section(
            window,
            "case SDL_EVENT_WINDOW_EXPOSED:",
            "case SDL_EVENT_WINDOW_FOCUS_LOST:",
        )
        routing = section(
            self.events,
            "case SDL_EVENT_WINDOW_MOVED:",
            "default:",
        )

        self.assertIn("if ( glw_state.isFullscreen )", occluded)
        self.assertIn("IN_QueueInputReset( qfalse );", occluded)
        self.assertIn("gw_minimized = qtrue;", occluded)
        self.assertIn("s_fullscreenOcclusionReset = qtrue;", occluded)
        self.assertIn("case SDL_EVENT_WINDOW_EXPOSED:", exposed)
        assert_order(
            self,
            exposed,
            "glw_state.isFullscreen && s_fullscreenOcclusionReset",
            "gw_minimized = qfalse;",
            "mouse_focus =",
            "IN_QueueHeldModifiers( in_eventTime );",
            "s_fullscreenOcclusionReset = qfalse;",
        )
        self.assertIn("gw_minimized = qfalse;", exposed)
        self.assertIn("case SDL_EVENT_WINDOW_OCCLUDED:", routing)
        self.assertIn("case SDL_EVENT_WINDOW_EXPOSED:", routing)
        self.assertIn(
            "IN_HandleWindowEvent( e.type, &e.window, &s_lastKeyDown );",
            routing,
        )

        assert_order(
            self,
            self.events,
            "Sys_ConsoleHandleEvent( &e )",
            "const SDL_WindowID eventWindowID = IN_EventWindowID( e );",
            "eventWindowID != currentWindowID",
            "switch( e.type )",
        )

    def test_altgr_does_not_synthesize_ctrl_text(self) -> None:
        key_down = section(
            self.events,
            "case SDL_EVENT_KEY_DOWN:",
            "case SDL_EVENT_KEY_UP:",
        )

        assert_order(
            self,
            key_down,
            "keyinfo.mod & SDL_KMOD_CTRL",
            "!( keyinfo.mod & ( SDL_KMOD_ALT |",
            "kModeModifierFamily ) )",
            "key >= 'a' && key <= 'z'",
            "SE_CHAR, CTRL(key)",
        )

    def test_restart_drains_retained_events_before_reset_and_modifiers(
        self,
    ) -> None:
        restart = function_body(self.source, "IN_Restart")

        assert_order(
            self,
            restart,
            "IN_Shutdown();",
            "IN_Init();",
            "HandleEvents();",
            "IN_QueueInputReset(",
            "gw_active && !gw_minimized ? qtrue : qfalse",
        )

    def test_focus_reset_is_queued_before_reasserting_held_modifiers(
        self,
    ) -> None:
        window = function_body(self.source, "IN_HandleWindowEvent")
        lost = section(
            window,
            "case SDL_EVENT_WINDOW_FOCUS_LOST:",
            "case SDL_EVENT_WINDOW_FOCUS_GAINED:",
        )
        gained = section(
            window,
            "case SDL_EVENT_WINDOW_FOCUS_GAINED:",
            "case SDL_EVENT_WINDOW_MOUSE_ENTER:",
        )

        self.assertIn("IN_QueueInputReset( qfalse );", lost)
        assert_order(
            self,
            gained,
            "IN_QueueInputReset( qtrue );",
            "gw_active = qtrue;",
            "mouse_focus =",
            "SDL_GetMouseFocus() == SDL_window",
            "glw_state.isFullscreen",
        )
        self.assertNotIn("mouse_focus = qtrue;", gained)
        self.assertNotIn("Key_ClearStates", window)

    def test_gamepad_and_raw_joystick_state_is_bounded_and_rebuilt(
        self,
    ) -> None:
        gamepad = function_body(self.source, "IN_GamepadMove")
        raw = function_body(self.source, "IN_JoyMove")
        frame = function_body(self.source, "IN_Frame")

        for fragment in (
            "kRawJoystickButtonCount = 16",
            "SDL_GAMEPAD_BUTTON_TOUCHPAD",
            "kRawDigitalAxisCount",
            "gamepadButtons[kSupportedGamepadButtonCount]",
            "rawButtons[kRawJoystickButtonCount]",
            "gamepadDigitalDirections[SDL_GAMEPAD_AXIS_COUNT * 2]",
        ):
            self.assertIn(fragment, self.source)
        self.assertIn(
            "i < kSupportedGamepadButtonCount", gamepad
        )
        self.assertNotIn("SDL_GAMEPAD_BUTTON_COUNT", gamepad)
        self.assertIn("fnql::input::ApplyJoystickDeadzone(", gamepad)
        self.assertIn("fnql::input::StrongerJoystickAxis(", gamepad)
        self.assertIn("oldTranslatedAxes[i]", gamepad)
        self.assertIn(
            "std::array<qboolean, SDL_GAMEPAD_AXIS_COUNT * 2>",
            gamepad,
        )
        assert_order(
            self,
            gamepad,
            "stick_state.gamepadDigitalDirections[i] &&",
            "qfalse, 0, NULL",
            "!stick_state.gamepadDigitalDirections[i] &&",
            "qtrue, 0, NULL",
            "stick_state.gamepadDigitalDirections[i] = digitalDirections[i];",
        )

        self.assertIn(
            "fnql::input::SaturatingAddInt( balldx, dx )", raw
        )
        self.assertIn(
            "static_cast<std::int64_t>( balldx ) * 2", raw
        )
        self.assertIn(
            "if ( total > kRawJoystickButtonCount )", raw
        )
        self.assertIn(
            "if ( total > kRawDigitalAxisCount )", raw
        )
        self.assertIn("1u << ( i * 2 )", raw)
        self.assertNotIn("abs( balldx )", raw)
        self.assertIn(
            '"in_joystickUseAnalog", "0", CVAR_ARCHIVE | CVAR_LATCH',
            self.source,
        )
        self.assertIn(
            'Cvar_CheckRange( in_joystickThreshold, "0", "1", CV_FLOAT )',
            self.source,
        )
        assert_order(
            self,
            frame,
            "if ( gw_active && !gw_minimized )",
            "IN_JoyMove();",
        )

    def test_joystick_hotplug_resets_before_reopen_without_background_modifiers(
        self,
    ) -> None:
        hotplug = section(
            self.events,
            "case SDL_EVENT_JOYSTICK_ADDED:",
            "case SDL_EVENT_KEYBOARD_REMOVED:",
        )

        assert_order(
            self,
            hotplug,
            "IN_QueueInputReset(",
            "gw_active && !gw_minimized ? qtrue : qfalse",
            "IN_InitJoystick();",
        )

    def test_joystick_hotplug_is_deduplicated_and_refreshes_the_ui_list(
        self,
    ) -> None:
        initialize = function_body(self.source, "IN_InitJoystick")
        hotplug = section(
            self.events,
            "case SDL_EVENT_JOYSTICK_ADDED:",
            "case SDL_EVENT_KEYBOARD_REMOVED:",
        )

        assert_order(
            self,
            initialize,
            'Cvar_Get( "in_availableJoysticks", buf, CVAR_ROM )',
            'Cvar_Set( "in_availableJoysticks", buf );',
            "if( !in_joystick->integer )",
        )
        self.assertIn("topologyTransitions", self.events)
        self.assertIn("topologyTransitionCount", self.events)
        self.assertIn("topologyTransitions[i].device == device", hotplug)
        self.assertIn("topologyTransitions[i].kind == kind", hotplug)
        self.assertIn("if ( duplicate )", hotplug)
        assert_order(
            self,
            hotplug,
            "if ( in_joystick )",
            "if ( in_joystick->integer )",
            "IN_QueueInputReset(",
            "IN_InitJoystick();",
        )

    def test_input_barriers_clear_joystick_snapshots_and_full_deadzone(
        self,
    ) -> None:
        reset = function_body(self.source, "IN_ResetInputState")
        queue_reset = function_body(self.source, "IN_QueueInputReset")
        raw = function_body(self.source, "IN_JoyMove")
        window = function_body(self.source, "IN_HandleWindowEvent")

        self.assertIn("stick_state = {};", reset)
        assert_order(
            self,
            queue_reset,
            "IN_ResetInputState();",
            "SE_INPUT_RESET",
        )
        for event in (
            "SDL_EVENT_WINDOW_HIDDEN",
            "SDL_EVENT_WINDOW_OCCLUDED",
            "SDL_EVENT_WINDOW_FOCUS_LOST",
            "SDL_EVENT_WINDOW_FOCUS_GAINED",
        ):
            self.assertIn(event, window)
        self.assertGreaterEqual(window.count("IN_QueueInputReset("), 4)
        self.assertIn(
            "if ( deadzone >= 1.0f || f < deadzone ) axis = 0;",
            raw,
        )
        self.assertIn("if ( deadzone >= 1.0f )", raw)

    def test_joystick_subsystem_refs_are_owned_once_and_released_exactly(
        self,
    ) -> None:
        acquire = function_body(
            self.source, "IN_AcquireJoystickSubsystem"
        )
        initialize = function_body(self.source, "IN_InitJoystick")
        shutdown = function_body(self.source, "IN_ShutdownJoystick")

        assert_order(
            self,
            acquire,
            "if ( *acquired )",
            "return qtrue;",
            "SDL_InitSubSystem( flags )",
            "*acquired = qtrue;",
        )
        assert_order(
            self,
            initialize,
            "SDL_INIT_JOYSTICK, &s_joystickSubsystemAcquired",
            "SDL_INIT_GAMEPAD, &s_gamepadSubsystemAcquired",
        )
        self.assertNotIn("SDL_WasInit", initialize)
        self.assertNotIn("ScopedSubSystem", initialize)
        assert_order(
            self,
            shutdown,
            "IN_CloseJoystickHandles();",
            "if ( s_gamepadSubsystemAcquired )",
            "SDL_QuitSubSystem( SDL_INIT_GAMEPAD );",
            "s_gamepadSubsystemAcquired = qfalse;",
            "if ( s_joystickSubsystemAcquired )",
            "SDL_QuitSubSystem( SDL_INIT_JOYSTICK );",
            "s_joystickSubsystemAcquired = qfalse;",
        )
        self.assertNotIn("SDL_WasInit", shutdown)

    def test_sdl_modifier_rebuild_and_keypad_identity_are_complete(
        self,
    ) -> None:
        translate = function_body(self.source, "IN_TranslateSDLToQ3Key")
        modifier_bit = function_body(self.source, "IN_ModifierBit")
        modifier_family = function_body(
            self.source, "IN_ModifierFamily"
        )
        held = function_body(self.source, "IN_QueueHeldModifiers")

        self.assertIn(
            "case SDLK_KP_MULTIPLY:  key = '*';", translate
        )
        self.assertIn(
            "case SDLK_KP_EQUALS:    key = K_KP_EQUALS;", translate
        )
        self.assertIn(
            "case SDLK_LEVEL5_SHIFT: key = K_MODE;", translate
        )
        self.assertIn(
            "case SDLK_MULTI_KEY_COMPOSE: key = K_COMPOSE;", translate
        )
        self.assertIn("K_COMMAND", held)
        self.assertIn("K_SUPER", held)
        assert_order(
            self,
            modifier_bit,
            "keycode == SDLK_LEVEL5_SHIFT",
            "return SDL_KMOD_LEVEL5;",
        )
        self.assertIn("kModeModifierFamily", modifier_family)
        self.assertIn("kModeModifierFamily", held)
        self.assertIn("K_MODE", held)


class Win32InputProducerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.input = read_text("code/win32/win_input.cpp")
        cls.wndproc = read_text("code/win32/win_wndproc.cpp")
        cls.glimp = read_text("code/win32/win_glimp.cpp")

    def test_window_and_clipboard_paths_are_unicode_native(self) -> None:
        self.assertIn("RegisterClassW( &wc )", self.glimp)
        self.assertIn("CreateWindowExW(", self.glimp)
        self.assertIn("DefWindowProcW(", self.wndproc)
        self.assertIn("case WM_UNICHAR:", self.wndproc)
        self.assertIn("UNICODE_NOCHAR", self.wndproc)
        self.assertIn("GetClipboardData( CF_UNICODETEXT )", self.wndproc)
        self.assertIn("SetClipboardData( CF_UNICODETEXT", self.wndproc)
        self.assertIn("WideCharToMultiByte(\n\t\tCP_UTF8", self.wndproc)
        self.assertIn(
            "MultiByteToWideChar(\n\t\tCP_UTF8, MB_ERR_INVALID_CHARS",
            self.wndproc,
        )
        unichar = section(
            self.wndproc, "case WM_UNICHAR:", "#ifdef USE_MIDI"
        )
        assert_order(
            self,
            unichar,
            "wParam == UNICODE_NOCHAR",
            "wParam > 0 && wParam <= 0x10ffff",
            "!( wParam >= 0xd800 && wParam <= 0xdfff )",
            "SE_CHAR",
        )

    def test_focus_gate_covers_keyboard_text_mouse_and_raw_input(self) -> None:
        accepts = function_body(self.wndproc, "WIN_WindowAcceptsInput")
        activate = function_body(self.wndproc, "VID_AppActivate")

        self.assertIn("gw_active && !gw_minimized", accepts)
        self.assertIn("windowFocused", accepts)
        self.assertGreaterEqual(
            self.wndproc.count("WIN_WindowAcceptsInput()"), 6
        )
        self.assertIn("case WM_KEYDOWN:", self.wndproc)
        self.assertIn("case WM_KEYUP:", self.wndproc)
        self.assertIn("case WM_CHAR:", self.wndproc)
        self.assertIn("case WM_UNICHAR:", self.wndproc)
        self.assertIn("case WM_MOUSEMOVE:", self.wndproc)
        self.assertIn("IN_RawMouseDrivesInput()", self.wndproc)
        self.assertIn("!windowInputSuspended", accepts)
        assert_order(
            self,
            activate,
            "WIN_QueueInputReset( active );",
            "CL_WebHost_NotifyAppActivation( active );",
            "IN_Activate( active );",
        )
        self.assertNotIn("Key_ClearStates", activate)

    def test_input_reset_helper_uses_fresh_time_and_orders_modifiers(
        self,
    ) -> None:
        helper = function_body(self.wndproc, "WIN_QueueInputReset")
        restart = function_body(self.input, "IN_Restart_f")
        local = read_text("code/win32/win_local.h")

        assert_order(
            self,
            helper,
            "const int eventTime = Sys_Milliseconds();",
            "Sys_QueEvent( eventTime, SE_INPUT_RESET",
            "windowWheelRemainder = 0;",
            "windowWheelOwnerValid = qfalse;",
            "windowWheelFlip = qtrue;",
            "windowWheelPendingKey = 0;",
            "if ( rebuildModifiers && gw_active && !gw_minimized &&",
            "!windowInputSuspended",
            "windowFocused",
            "WIN_QueueHeldModifiers( eventTime );",
            "} else {",
            "physicalModifierState = 0;",
        )
        self.assertNotIn("g_wv.sysMsgTime", helper)
        self.assertIn(
            "void\tWIN_QueueInputReset( qboolean rebuildModifiers );", local
        )
        assert_order(
            self,
            restart,
            "WIN_QueueInputReset( qtrue );",
            "IN_Shutdown();",
            "IN_Init();",
        )

    def test_shared_window_focus_gates_every_polled_input_path(self) -> None:
        local = read_text("code/win32/win_local.h")
        accepts = function_body(self.wndproc, "WIN_WindowAcceptsInput")
        reset = function_body(self.wndproc, "WIN_QueueInputReset")
        update = function_body(self.wndproc, "WIN_UpdateWindowFocus")
        focus_messages = section(
            self.wndproc, "case WM_SETFOCUS:", "case WM_CAPTURECHANGED:"
        )
        resolve = function_body(self.input, "IN_ResolvePointerMode")
        mouse_active = function_body(self.input, "IN_MouseActive")
        frame = function_body(self.input, "IN_Frame")
        midi = function_body(self.input, "MIDI_WindowAcceptsInput")
        main = function_body(self.wndproc, "MainWndProc")

        self.assertIn("qboolean WIN_WindowFocused( void );", local)
        self.assertIn("return windowFocused;", function_body(
            self.wndproc, "WIN_WindowFocused"
        ))
        self.assertIn("windowFocused", accepts)
        self.assertIn("windowFocused", reset)
        self.assertNotIn("static qboolean focused", main)
        assert_order(
            self,
            update,
            "if ( windowFocused == focused )",
            "windowFocused = focused;",
            "if ( !windowFocused )",
            "WIN_QueueInputReset( qfalse );",
            "IN_Activate( qfalse );",
            "WIN_ReleaseTemporaryMouseCapture();",
            "const qboolean wasSuspended = windowInputSuspended;",
            "WIN_UpdateInputSuspension();",
            "WIN_QueueInputReset( qtrue );",
        )
        assert_order(
            self,
            focus_messages,
            "case WM_SETFOCUS:",
            "WIN_UpdateWindowFocus( qtrue );",
            "case WM_KILLFOCUS:",
            "WIN_UpdateWindowFocus( qfalse );",
        )
        for body in (resolve, mouse_active, frame, midi):
            self.assertIn("WIN_WindowFocused()", body)

    def test_altgr_suppresses_only_the_adjacent_synthetic_left_ctrl(
        self,
    ) -> None:
        helper = function_body(
            self.wndproc, "WIN_SkipAltGrLeftControl"
        )
        key_down = section(
            self.wndproc, "case WM_SYSKEYDOWN:", "case WM_SYSKEYUP:"
        )
        key_up = section(
            self.wndproc, "case WM_SYSKEYUP:", "case WM_SYSCHAR:"
        )

        assert_order(
            self,
            helper,
            "key != VK_CONTROL",
            "lParam & ( 1L << 24 )",
            "PeekMessageW( &nextMessage, NULL, 0, 0, PM_NOREMOVE )",
            "nextMessage.message == WM_KEYDOWN",
            "nextMessage.message == WM_SYSKEYDOWN",
            "nextMessage.wParam == VK_MENU",
            "nextMessage.lParam & ( 1L << 24 )",
            "nextMessage.time == messageTime",
            "return qtrue;",
        )
        self.assertIn(
            "WIN_SkipAltGrLeftControl( wParam, lParam )", key_down
        )
        self.assertIn(
            "WIN_SkipAltGrLeftControl( wParam, lParam )", key_up
        )
        self.assertEqual(
            self.wndproc.count(
                "WIN_SkipAltGrLeftControl( wParam, lParam )"
            ),
            2,
        )

    def test_raw_input_is_gated_by_registration_source_and_activation(
        self,
    ) -> None:
        drives = function_body(self.input, "IN_RawMouseDrivesInput")
        activate = function_body(self.input, "IN_ActivateRawMouse")
        raw_event = function_body(self.input, "IN_RawMouseEvent")

        for fragment in (
            "in_mouse->integer == 2",
            "raw_driving",
            "raw_activated",
            "IN_MouseActive()",
        ):
            self.assertIn(fragment, drives)
        self.assertIn("if( !RRID( &Rid, 1, sizeof( Rid ) ) )", activate)
        self.assertIn("raw_driving = qfalse;", activate)
        self.assertIn("raw_activated = TRUE;", activate)
        self.assertIn("raw_driving = qtrue;", activate)
        self.assertIn("if ( !IN_RawMouseDrivesInput() )", raw_event)
        self.assertIn(
            "u.raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE", raw_event
        )
        self.assertNotIn(
            "usFlags == MOUSE_MOVE_RELATIVE", raw_event
        )

    def test_raw_packet_read_failure_queues_one_mouse_recovery_barrier(
        self,
    ) -> None:
        recovery = function_body(
            self.input, "IN_QueueRawMouseReadReset"
        )
        raw_event = function_body(self.input, "IN_RawMouseEvent")
        activate = function_body(self.input, "IN_ActivateRawMouse")
        deactivate = function_body(self.input, "IN_DeactivateRawMouse")
        reset = function_body(self.input, "IN_ResetInputState")

        assert_order(
            self,
            recovery,
            "IN_ClearRawMouseDeltas();",
            "if ( rawReadResetQueued )",
            "SE_MOUSE_RESET",
            "rawReadResetQueued = qtrue;",
        )
        assert_order(
            self,
            raw_event,
            "err == static_cast<UINT>( -1 ) || err != dwSize",
            "IN_QueueRawMouseReadReset();",
            "if ( u.raw.header.dwType != RIM_TYPEMOUSE )",
            "return;",
            "rawReadResetQueued = qfalse;",
        )
        self.assertIn("rawReadResetQueued = qfalse;", activate)
        self.assertIn("rawReadResetQueued = qfalse;", deactivate)
        self.assertIn("rawReadResetQueued = qfalse;", reset)

    def test_capture_and_confinement_latches_follow_api_results(self) -> None:
        capture = function_body(self.input, "IN_CaptureMouse")
        deactivate = function_body(
            self.input, "IN_DeactivateWin32Mouse"
        )
        confine = function_body(self.input, "IN_SetPointerConfinement")
        temporary = function_body(
            self.wndproc, "WIN_UpdateTemporaryMouseCapture"
        )
        release_temporary = function_body(
            self.wndproc, "WIN_ReleaseTemporaryMouseCapture"
        )

        self.assertIn("ClipCursor( clipRect ) ? qtrue : qfalse", capture)
        self.assertIn("GetCapture() == g_wv.hWnd ? qtrue : qfalse", capture)
        self.assertIn("!clipApplied || !s_gameplayCaptureOwned", capture)
        self.assertIn("s_gameplayClipNeedsRefresh = qtrue;", capture)
        self.assertIn("return qfalse;", capture)
        self.assertIn("return qtrue;", capture)
        self.assertIn(
            "!s_gameplayCaptureOwned || GetCapture() != g_wv.hWnd ||",
            deactivate,
        )
        self.assertIn("ReleaseCapture() )", deactivate)
        self.assertIn(
            "if ( s_gameplayClipActive && ClipCursor( NULL ) )",
            deactivate,
        )
        self.assertIn("s_gameplayClipActive = qfalse;", deactivate)
        self.assertIn("if ( ClipCursor( &window_rect ) )", confine)
        self.assertIn("if ( ClipCursor( NULL ) )", confine)
        self.assertIn("GetCapture() == hWnd ? qtrue : qfalse", temporary)
        assert_order(
            self,
            release_temporary,
            "if ( !ReleaseCapture() )",
            "return;",
            "temporaryMouseCapture = qfalse;",
        )

    def test_cursor_visibility_and_nograb_are_transition_safe(self) -> None:
        cursor = function_body(self.input, "IN_SetSystemCursorVisible")
        frame = function_body(self.input, "IN_Frame")
        mouse_active = function_body(self.input, "IN_MouseActive")
        activate_mouse = function_body(self.input, "IN_ActivateMouse")

        assert_order(
            self,
            cursor,
            "s_cursorVisibilityRequestValid &&",
            "s_cursorVisibilityRequested == visible",
            "return;",
            "ShowCursor(",
            "s_cursorVisibilityRequestValid = qtrue;",
            "s_cursorVisibilityRequested = visible;",
        )

        absolute_start = frame.index("PointerOwnerReportsAbsolute( owner )")
        gameplay_start = frame.rindex("WIN_ReleaseTemporaryMouseCapture();")
        absolute = frame[absolute_start:gameplay_start]
        gameplay = frame[gameplay_start:]
        self.assertIn("IN_SetSystemCursorVisible( qtrue );", absolute)
        self.assertNotIn("in_nograb->integer", absolute)
        assert_order(
            self,
            gameplay,
            "!mode.driveInput || in_nograb->integer",
            "IN_DeactivateMouse();",
            "return;",
            "IN_ActivateMouse();",
        )
        self.assertIn("in_nograb->integer == 0", mouse_active)
        self.assertIn("gw_active && WIN_WindowFocused()", mouse_active)
        self.assertIn("!gw_minimized", mouse_active)
        self.assertIn(
            "s_wmv.mouseActive = IN_ActivateWin32Mouse();",
            activate_mouse,
        )

    def test_directinput_loss_uses_mouse_only_reset(self) -> None:
        direct_input = function_body(self.input, "IN_DIMouse")

        assert_order(
            self,
            direct_input,
            "const auto queueLossReset",
            "if ( !directInputLossResetQueued )",
            "SE_MOUSE_RESET",
            "directInputLossResetQueued = qtrue;",
            "DIERR_INPUTLOST",
            "queueLossReset();",
            "IDirectInputDevice_Acquire( g_pMouse );",
        )
        self.assertIn("if ( FAILED(hr) )", direct_input)
        self.assertGreaterEqual(direct_input.count("queueLossReset();"), 3)
        self.assertIn(
            "directInputLossResetQueued = qfalse;", direct_input
        )
        self.assertNotIn("SE_INPUT_RESET", direct_input)
        self.assertIn("DIERR_INPUTLOST", direct_input)
        self.assertIn("DIERR_NOTACQUIRED", direct_input)
        self.assertIn("DI_BUFFEROVERFLOW", direct_input)

    def test_window_wheel_compatibility_never_strands_a_pending_key(
        self,
    ) -> None:
        queue_step = function_body(self.wndproc, "WIN_QueueWheelStep")
        reset_message = function_body(
            self.wndproc, "WIN_ResetMessageInputState"
        )
        wheel = section(
            self.wndproc,
            "case WM_MOUSEWHEEL:",
            "case WM_CREATE:",
        )

        normal = section(
            queue_step,
            "if ( !in_logitechbug->integer )",
            "// The legacy Logitech workaround",
        )
        assert_order(
            self,
            normal,
            "if ( windowWheelPendingKey )",
            "windowWheelPendingKey, qfalse",
            "windowWheelPendingKey = 0;",
            "SE_KEY, key, qtrue",
            "SE_KEY, key, qfalse",
        )
        assert_order(
            self,
            queue_step,
            "windowWheelPendingKey && windowWheelPendingKey != key",
            "windowWheelPendingKey, qfalse",
            "windowWheelPendingKey = 0;",
            "Sys_QueEvent( eventTime, SE_KEY, key, windowWheelFlip",
            "windowWheelPendingKey = key;",
        )
        self.assertIn("windowWheelPendingKey = 0;", reset_message)

        owner_change = wheel.index(
            "windowWheelOwner != pointerOwner"
        )
        pending_release = wheel.index(
            "windowWheelPendingKey, qfalse", owner_change
        )
        remainder_reset = wheel.index(
            "windowWheelRemainder = 0;", pending_release
        )
        self.assertLess(pending_release, remainder_reset)
        self.assertGreaterEqual(
            wheel.count("windowWheelPendingKey, qfalse"), 2
        )

    def test_backend_reset_discards_all_persistent_mouse_state(self) -> None:
        reset = function_body(self.input, "IN_ResetInputState")

        for fragment in (
            "WIN_ReleaseTemporaryMouseCapture();",
            "IN_ClearRawMouseDeltas();",
            "directInputWheelRemainder = 0;",
            "s_wmv.oldButtonState = 0;",
            "s_wmv.cursorPositionValid = qfalse;",
            "WIN_ResetMessageInputState();",
        ):
            self.assertIn(fragment, reset)

    def test_backend_reset_discards_winmm_controller_snapshot(self) -> None:
        reset = function_body(self.input, "IN_ResetInputState")
        clear = function_body(
            self.input, "IN_ClearWinMMJoystickState"
        )

        self.assertIn("IN_ClearWinMMJoystickState( qfalse );", reset)
        for fragment in (
            "joy.oldbuttonstate = 0;",
            "joy.oldpovstate = 0;",
            "for ( int& axis : joy.oldmoveaxisstate )",
            "Com_Memset( &joy.ji, 0, sizeof( joy.ji ) );",
            "joy.consecutiveReadFailures = 0;",
        ):
            self.assertIn(fragment, clear)

        joy_move = function_body(self.input, "IN_JoyMove")
        self.assertIn(
            "kRawButtonCount = K_JOY16 - K_JOY1 + 1", joy_move
        )

    def test_capture_loss_always_queues_an_ordered_mouse_barrier(
        self,
    ) -> None:
        helper = function_body(self.wndproc, "WIN_QueueMouseReset")
        capture_loss = section(
            self.wndproc, "case WM_CAPTURECHANGED:", "case WM_MOVE:"
        )

        self.assertIn("SE_MOUSE_RESET", helper)
        self.assertNotIn("keys[", helper)
        assert_order(
            self,
            capture_loss,
            "case WM_CAPTURECHANGED:",
            "WIN_QueueMouseReset();",
            "temporaryMouseCapture = qfalse;",
            "case WM_CANCELMODE:",
            "WIN_QueueMouseReset();",
            "WIN_ReleaseTemporaryMouseCapture();",
        )

    def test_minimize_and_hide_have_an_independent_input_suspension_latch(
        self,
    ) -> None:
        update = function_body(
            self.wndproc, "WIN_UpdateInputSuspension"
        )
        window_messages = section(
            self.wndproc, "case WM_SIZE:", "case WM_TIMER:"
        )
        show_window = section(
            self.wndproc,
            "case WM_SHOWWINDOW:",
            "case WM_INPUT_DEVICE_CHANGE:",
        )
        resolve = function_body(self.input, "IN_ResolvePointerMode")
        frame = function_body(self.input, "IN_Frame")
        local = read_text("code/win32/win_local.h")

        assert_order(
            self,
            update,
            "windowHidden || gw_minimized",
            "if ( shouldSuspend )",
            "if ( windowInputSuspended )",
            "windowInputSuspended = qtrue;",
            "WIN_QueueInputReset( qfalse );",
            "IN_Activate( qfalse );",
            "WIN_ReleaseTemporaryMouseCapture();",
            "if ( !windowInputSuspended || !gw_active || !windowFocused )",
            "windowInputSuspended = qfalse;",
            "WIN_QueueInputReset( qtrue );",
            "IN_Activate( qtrue );",
        )
        assert_order(
            self,
            window_messages,
            "case WM_SIZE:",
            "wParam == SIZE_MINIMIZED",
            "WIN_UpdateInputSuspension();",
        )
        assert_order(
            self,
            show_window,
            "case WM_SHOWWINDOW:",
            "windowHidden = wParam ? qfalse : qtrue;",
            "WIN_UpdateInputSuspension();",
        )
        self.assertIn(
            "WIN_UpdateInputSuspension();",
            section(
                self.wndproc, "case WM_ACTIVATE:", "case WM_CAPTURECHANGED:"
            ),
        )
        self.assertGreaterEqual(
            self.wndproc.count("windowInputSuspended = qfalse;"), 3
        )
        self.assertIn(
            "qboolean WIN_InputSuspended( void );", local
        )
        self.assertIn("WIN_InputSuspended()", resolve)
        self.assertIn("!WIN_InputSuspended()", frame)

    def test_key_repeat_suppresses_one_shots_and_alt_enter_restarts_once(
        self,
    ) -> None:
        key_down = section(
            self.wndproc,
            "case WM_SYSKEYDOWN:",
            "case WM_SYSKEYUP:",
        )
        key_up = section(
            self.wndproc,
            "case WM_SYSKEYUP:",
            "case WM_CHAR:",
        )

        assert_order(
            self,
            key_down,
            "lParam & ( 1L << 30 )",
            "wParam == VK_RETURN",
            "suppressedAltEnter = qtrue;",
            "if ( !isRepeat )",
            'Cvar_SetIntegerValue( "r_fullscreen"',
            'Cbuf_AddText( "vid_restart\\n" );',
        )
        self.assertIn(
            "mappedKey == K_CONSOLE || mappedKey == K_ESCAPE",
            key_down,
        )
        assert_order(
            self,
            key_up,
            "wParam == VK_RETURN && suppressedAltEnter",
            "suppressedAltEnter = qfalse;",
            "return 0;",
        )
        syschar = section(
            self.wndproc,
            "case WM_SYSCHAR:",
            "case WM_CHAR:",
        )
        self.assertIn("if ( wParam == VK_RETURN )", syschar)
        self.assertIn("return 0;", syschar)

    def test_controller_and_midi_polling_respects_window_activation(
        self,
    ) -> None:
        frame = function_body(self.input, "IN_Frame")
        accepts = function_body(self.input, "MIDI_WindowAcceptsInput")
        note_on = function_body(self.input, "MIDI_NoteOn")
        midi_start = function_body(self.input, "IN_StartupMIDI")

        assert_order(
            self,
            frame,
            "if ( gw_active && WIN_WindowFocused() && !gw_minimized &&",
            "!WIN_InputSuspended() )",
            "IN_JoyMove();",
        )
        self.assertIn("GetForegroundWindow() == window", accepts)
        self.assertIn("!IsIconic( window )", accepts)
        assert_order(
            self,
            note_on,
            "if ( velocity == 0 )",
            "MIDI_NoteOff( note );",
            "return;",
        )
        midi_message = function_body(self.input, "IN_MIDIMessage")
        assert_order(
            self,
            midi_message,
            "device != s_midiInfo.hMidiIn",
            "!MIDI_WindowAcceptsInput()",
            "return;",
            "const int message = packedMessage & 0xff;",
            "channel != in_midichannel->integer",
            "MIDI_NoteOn(",
        )
        self.assertNotIn("MidiInProc", self.input)
        self.assertIn(
            "( std::min )( systemDeviceCount, MAX_MIDIIN_DEVICES )",
            midi_start,
        )
        self.assertIn(
            "selectedDevice >= s_midiInfo.numDevices", midi_start
        )
        self.assertIn(
            'Cvar_CheckRange( in_midichannel, "1", "16", CV_INTEGER )',
            self.input,
        )
        self.assertIn(
            "reinterpret_cast<DWORD_PTR>( g_wv.hWnd )", midi_start
        )
        self.assertIn("CALLBACK_WINDOW", midi_start)
        self.assertNotIn("CALLBACK_FUNCTION", midi_start)
        midi_window = section(
            self.wndproc,
            "case MM_MIM_DATA:",
            "case WM_NCHITTEST:",
        )
        self.assertIn("IN_MIDIMessage(", midi_window)
        self.assertIn(
            "reinterpret_cast<HMIDIIN>( wParam )", midi_window
        )
        local = read_text("code/win32/win_local.h")
        self.assertIn(
            "IN_MIDIMessage( HMIDIIN device, DWORD packedMessage );",
            local,
        )

    def test_legacy_trackball_float_conversion_is_saturating(self) -> None:
        joystick = function_body(self.input, "IN_JoyMove")
        trackball = joystick[
            joystick.index("} else if ( !retailProfile )") :
        ]

        self.assertEqual(
            trackball.count("fnql::input::TruncateFiniteFloatToInt("),
            2,
        )
        self.assertNotRegex(
            trackball, r"\bx\s*=\s*JoyToI|\by\s*=\s*JoyToI"
        )

    def test_winmm_joystick_bounds_deadzone_and_loss_recovery(self) -> None:
        joystick = function_body(self.input, "IN_JoyMove")
        axis_position = function_body(
            self.input, "IN_WinMMAxisPosition"
        )
        clear = function_body(
            self.input, "IN_ClearWinMMJoystickState"
        )

        self.assertIn(
            "kRawButtonCount = K_JOY16 - K_JOY1", joystick
        )
        self.assertIn(
            "( std::min<int> )( joy.jc.wNumButtons, kRawButtonCount )",
            joystick,
        )
        self.assertIn(
            "fnql::input::FiniteJoystickDeadzone( joy_threshold->value )",
            joystick,
        )
        self.assertIn("JOYERR_UNPLUGGED", joystick)
        self.assertIn("kMaximumReadFailures", joystick)
        for axis, member, capability in (
            ("Z", "dwZpos", "JOYCAPS_HASZ"),
            ("R", "dwRpos", "JOYCAPS_HASR"),
            ("U", "dwUpos", "JOYCAPS_HASU"),
            ("V", "dwVpos", "JOYCAPS_HASV"),
        ):
            with self.subTest(axis=axis):
                self.assertIn(
                    f"case WINMM_AXIS_{axis}:", axis_position
                )
                self.assertIn(capability, axis_position)
                self.assertIn(member, axis_position)
        self.assertNotIn("(&joy.ji.dwXpos)[i]", joystick)
        self.assertGreaterEqual(
            joystick.count("IN_WinMMAxisPosition("), 6
        )
        assert_order(
            self,
            joystick,
            "IN_ClearWinMMJoystickState( qtrue );",
            "WIN_QueueInputReset( qtrue );",
        )
        for fragment in (
            "joy.oldbuttonstate = 0;",
            "joy.oldpovstate = 0;",
            "axis = 0;",
            "joy.consecutiveReadFailures = 0;",
            "joy.avail = qfalse;",
        ):
            self.assertIn(fragment, clear)
        self.assertIn(
            'Cvar_CheckRange( joy_threshold, "0", "1", CV_FLOAT )',
            self.input,
        )

    def test_absolute_position_dedup_includes_overlay_consumer(self) -> None:
        identity = function_body(
            self.input, "IN_PointerConsumerIdentity"
        )
        poll = function_body(self.input, "IN_WindowMouse")

        assert_order(
            self,
            identity,
            "catcher & KEYCATCH_CONSOLE",
            "catcher & KEYCATCH_BROWSER",
            "catcher & KEYCATCH_UI",
            "catcher & KEYCATCH_CGAME",
        )
        self.assertIn(
            "consumer == s_wmv.oldCursorConsumer", poll
        )
        self.assertIn("s_wmv.oldCursorConsumer = consumer;", poll)


class LinuxJoystickProducerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = read_text("code/unix/linux_joystick.cpp")

    def test_device_lifecycle_clears_descriptor_and_snapshot(self) -> None:
        close = function_body(self.source, "IN_CloseJoystickDevice")
        producer_reset = function_body(
            self.source, "IN_ResetJoystickState"
        )
        startup = function_body(self.source, "IN_StartupJoystick")
        shutdown = function_body(self.source, "IN_ShutdownJoystick")
        glimp = read_text("code/unix/linux_glimp.cpp")
        glimp_shutdown = function_body(glimp, "IN_Shutdown")
        glimp_reset = function_body(glimp, "IN_ResetInputState")
        local = read_text("code/unix/linux_local.h")

        assert_order(
            self,
            close,
            "if( joy_fd != -1 )",
            "close( joy_fd );",
            "joy_fd = -1;",
            "memset( axes_state, 0",
            "IN_ResetJoystickState();",
            "if( queueReset )",
            "X11_QueueInputReset( qtrue );",
        )
        self.assertIn("old_axes = 0;", producer_reset)
        self.assertNotIn("axes_state", producer_reset)
        self.assertLess(
            startup.index("IN_CloseJoystickDevice( qfalse );"),
            startup.index("if( !in_joystick->integer )"),
        )
        self.assertIn("IN_CloseJoystickDevice( qfalse );", shutdown)
        self.assertIn("IN_ShutdownJoystick();", glimp_shutdown)
        self.assertIn("IN_ResetJoystickState();", glimp_reset)
        self.assertIn(
            "#ifdef USE_JOYSTICK\nvoid IN_ResetJoystickState( void );\n#endif",
            local,
        )

    def test_read_loop_is_bounded_unsigned_and_recovers_device_loss(
        self,
    ) -> None:
        move = function_body(self.source, "IN_JoyMove")
        retry = function_body(self.source, "IN_JoystickRetryDue")
        open_device = function_body(self.source, "IN_TryOpenJoystick")

        self.assertIn(
            "kDigitalAxisCount =\n  static_cast<int>( ARRAY_LEN( joy_keys ) ) / 2",
            self.source,
        )
        self.assertIn(
            "kRawButtonCount = K_JOY16 - K_JOY1 + 1", self.source
        )
        assert_order(
            self,
            move,
            "n = read( joy_fd, &event, sizeof( event ) );",
            "errno == EINTR",
            "errno == EAGAIN || errno == EWOULDBLOCK",
            "n != static_cast<ssize_t>( sizeof( event ) )",
            "IN_CloseJoystickDevice( qtrue );",
            "IN_ScheduleJoystickRetry( now );",
            "return;",
        )
        assert_order(
            self,
            move,
            "if( joy_fd == -1 )",
            "!IN_JoystickRetryDue( now )",
            "!IN_TryOpenJoystick( now, qfalse )",
            "return;",
        )
        self.assertIn("event.number < kRawButtonCount", move)
        self.assertIn("event.number >= kDigitalAxisCount", move)
        self.assertIn(
            "fnql::input::FiniteJoystickDeadzone( joy_threshold->value )",
            move,
        )
        self.assertIn("if( deadzone < 1.0f )", move)
        self.assertIn("1u << ( i * 2 )", move)
        self.assertNotRegex(move, r"(?<!u)1 <<")
        assert_order(
            self,
            open_device,
            '"/dev/input/js%d"',
            '"/dev/js%d"',
            "i < kJoystickDeviceCount",
            "O_RDONLY | O_NONBLOCK | O_CLOEXEC",
        )
        assert_order(
            self,
            retry,
            "!joystick_open_attempted",
            "now - joystick_last_open_attempt",
            "kJoystickRetryIntervalMilliseconds",
        )
        self.assertIn(
            "kJoystickRetryIntervalMilliseconds = 1000u", self.source
        )

        glimp = read_text("code/unix/linux_glimp.cpp")
        self.assertIn(
            'Cvar_CheckRange( joy_threshold, "0", "1", CV_FLOAT )',
            glimp,
        )

    def test_native_linux_builds_compile_the_joystick_only_without_sdl(
        self,
    ) -> None:
        meson = read_text("meson.build")
        cmake = read_text("CMakeLists.txt")
        makefile = read_text("Makefile")

        self.assertIn("linux = host_machine.system() == 'linux'", meson)
        self.assertIn(
            "if linux\n      q3_ui_src += 'code/unix/linux_joystick.cpp'",
            meson,
        )
        self.assertIn(
            "if linux and build_client and not use_sdl\n"
            "  client_cpp_args += ['-DUSE_JOYSTICK']",
            meson,
        )
        self.assertIn(
            'IF(CMAKE_SYSTEM_NAME STREQUAL "Linux")\n'
            "\t\t\tlist(APPEND Q3_UI_SRCS code/unix/linux_joystick.cpp)",
            cmake,
        )
        self.assertIn(
            'IF(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT USE_SDL)\n'
            "\tTARGET_COMPILE_DEFINITIONS(q3ui PRIVATE USE_JOYSTICK)",
            cmake,
        )
        self.assertIn(
            "ifeq ($(PLATFORM),linux)\n"
            "    Q3OBJ += $(B)/client/linux_joystick.o\n"
            "    $(B)/client/linux_glimp.o $(B)/client/linux_joystick.o: "
            "CXXFLAGS += -DUSE_JOYSTICK",
            makefile,
        )


class NativeX11InputProducerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = read_text("code/unix/linux_glimp.cpp")
        cls.events = function_body(cls.source, "HandleEvents")

    def test_focus_loss_deactivates_before_ordered_reset(self) -> None:
        focus = section(self.events, "case FocusIn:", "case Expose:")

        assert_order(
            self,
            focus,
            "gw_active = qfalse;",
            "dowarp = qfalse;",
            "IN_DeactivateMouse();",
            "IN_EndTemporaryPointerCapture();",
            "IN_SetPointerConfinement( qfalse );",
            "IN_ShowWindowCursor( qtrue );",
            "X11_QueueInputReset(",
        )
        self.assertIn(
            "event.type == FocusIn ? qtrue : qfalse", focus
        )
        self.assertIn(
            "X11_SetInputContextFocus( X11_TextInputOwnerActive() );",
            focus,
        )
        self.assertNotIn("Key_ClearStates", focus)

    def test_background_events_are_ignored_without_resetting_on_configure(
        self,
    ) -> None:
        for start, end in (
            ("case KeyPress:", "case KeyRelease:"),
            ("case KeyRelease:", "case MotionNotify:"),
            ("case MotionNotify:", "case ButtonPress:"),
            ("case ButtonPress:", "case CreateNotify:"),
        ):
            with self.subTest(start=start):
                self.assertIn(
                    "if ( !gw_active || gw_minimized )",
                    section(self.events, start, end),
                )

        configure = section(
            self.events, "case ConfigureNotify:", "case FocusIn:"
        )
        self.assertIn(
            "WindowMinimized( dpy, win, &minimizedState )", configure
        )
        self.assertIn(
            "X11_UpdateMinimizedState( minimizedState, &dowarp );",
            configure,
        )
        self.assertNotIn("Key_ClearStates", configure)

    def test_window_minimize_transitions_recover_without_focus_events(
        self,
    ) -> None:
        query = function_body(self.source, "WindowMinimized")
        transition = function_body(
            self.source, "X11_UpdateMinimizedState"
        )
        uninstall = function_body(self.source, "uninstall_mouse_grab")
        notifications = section(
            self.events, "case CreateNotify:", "case FocusIn:"
        )

        self.assertIn("PropertyChangeMask", self.source)
        assert_order(
            self,
            notifications,
            "case MapNotify:",
            "X11_UpdateMinimizedState( qfalse, &dowarp );",
            "case UnmapNotify:",
            "!event.xunmap.from_configure",
            "X11_UpdateMinimizedState( qtrue, &dowarp );",
            "case PropertyNotify:",
            'XInternAtom( dpy, "_NET_WM_STATE", True )',
            "WindowMinimized( dpy, win, &minimizedState )",
            "case ConfigureNotify:",
            "WindowMinimized( dpy, win, &minimizedState )",
        )
        self.assertEqual(
            notifications.count("X11_UpdateMinimizedState("), 4
        )
        self.assertIn(
            "actualType == None && actualFormat == 0 && numItems == 0",
            query,
        )
        self.assertIn(
            "( numItems > 0 && !propertyData )", query
        )
        assert_order(
            self,
            transition,
            "if ( minimized == gw_minimized )",
            "return;",
            "gw_minimized = minimized;",
            "if ( minimized )",
            "*dowarp = qfalse;",
            "IN_DeactivateMouse();",
            "IN_EndTemporaryPointerCapture();",
            "IN_SetPointerConfinement( qfalse );",
            "IN_ShowWindowCursor( qtrue );",
            "X11_SetInputContextFocus( qfalse );",
            "X11_QueueInputReset( qfalse );",
            "return;",
            "if ( gw_active )",
            "X11_SetInputContextFocus( X11_TextInputOwnerActive() );",
            "X11_QueueInputReset( qtrue );",
        )
        self.assertIn(
            "gw_active && !gw_minimized && !IN_AbsolutePointerOwner()",
            uninstall,
        )

    def test_x11_input_reset_helper_preserves_barrier_order(self) -> None:
        helper = function_body(self.source, "X11_QueueInputReset")
        header = read_text("code/unix/linux_local.h")

        assert_order(
            self,
            helper,
            "X11_PrepareInputReset();",
            "Sys_QueEvent( 0, SE_INPUT_RESET",
            "rebuildModifiers && gw_active && !gw_minimized",
            "X11_QueueHeldModifiers( 0 );",
        )
        self.assertIn(
            "void X11_QueueInputReset( qboolean rebuildModifiers );",
            header,
        )

    def test_pointer_grabs_are_latched_only_after_success(self) -> None:
        shared_grab = function_body(self.source, "IN_ApplyPointerGrab")
        gameplay_grab = function_body(self.source, "install_mouse_grab")
        activate = function_body(self.source, "IN_ActivateMouse")

        assert_order(
            self,
            shared_grab,
            "result = XGrabPointer(",
            "if ( result != GrabSuccess )",
            "return;",
            "pointer_grab_reasons = reasons;",
        )
        assert_order(
            self,
            gameplay_grab,
            "res = XGrabPointer(",
            "if ( res != GrabSuccess )",
            "return qfalse;",
            "return qtrue;",
        )
        assert_order(
            self,
            activate,
            "if ( install_mouse_grab() )",
            "install_kb_grab();",
            "mouse_active = qtrue;",
        )

    def test_key_release_uses_press_time_translation_identity(self) -> None:
        press = section(self.events, "case KeyPress:", "case KeyRelease:")
        release = section(
            self.events, "case KeyRelease:", "case MotionNotify:"
        )
        translate = function_body(self.source, "XLateKey")

        self.assertIn("translated_key_by_keycode[256]", self.source)
        self.assertIn(
            "translated_key_by_keycode[event.xkey.keycode] = key;", press
        )
        assert_order(
            self,
            release,
            "translated_key_by_keycode[event.xkey.keycode]",
            "key = translated_key_by_keycode[event.xkey.keycode];",
            "translated_key_by_keycode[event.xkey.keycode] = 0;",
            "Sys_QueEvent( t, SE_KEY, key, qfalse",
        )
        self.assertIn(
            "lookupLength > 0 || unmodifiedLength > 0", translate
        )

    def test_clipboard_wait_is_filtered_bounded_and_non_destructive(
        self,
    ) -> None:
        clipboard = function_body(self.source, "Sys_GetClipboardData")
        predicate = function_body(
            self.source, "X11_MatchClipboardSelection"
        )

        for fragment in (
            "event->type == SelectionNotify",
            "event->xselection.requestor == match->requestor",
            "event->xselection.selection == match->selection",
            "event->xselection.target == match->target",
        ):
            self.assertIn(fragment, predicate)
        self.assertIn("XCheckIfEvent(", clipboard)
        self.assertIn("X11_MatchClipboardSelection", clipboard)
        self.assertIn("elapsed >= 250u", clipboard)
        self.assertIn("select(", clipboard)
        self.assertIn("XEventsQueued( dpy, QueuedAfterReading )", clipboard)
        self.assertNotIn("XNextEvent", clipboard)
        self.assertIn("XFree( data );", clipboard)
        self.assertIn("XFree( cutBuffer );", clipboard)

    def test_reset_and_restart_invalidate_native_backend_state(self) -> None:
        reset = function_body(self.source, "IN_ResetInputState")
        restart = function_body(self.source, "IN_Restart_f")
        create_context = function_body(
            self.source, "X11_CreateInputContext"
        )

        for fragment in (
            "IN_ResetJoystickState();",
            "absolute_position_valid = qfalse;",
            "mx = my = 0;",
            "mouseResetTime = Sys_Milliseconds();",
            "memset( translated_key_by_keycode, 0,",
            "IN_EndTemporaryPointerCapture();",
        ):
            self.assertIn(fragment, reset)
        assert_order(
            self,
            restart,
            "Sys_QueEvent( 0, SE_INPUT_RESET",
            "IN_DeactivateMouse();",
            "IN_Shutdown();",
            "IN_Init();",
        )
        self.assertIn(
            "X11_SetInputContextFocus( X11_TextInputOwnerActive() );",
            create_context,
        )

    def test_xim_focus_and_filtering_follow_text_input_ownership(
        self,
    ) -> None:
        owner = function_body(self.source, "X11_TextInputOwnerActive")
        event_filter = function_body(self.source, "X11_FilterInputEvent")
        events = function_body(self.source, "HandleEvents")
        frame = function_body(self.source, "IN_Frame")

        assert_order(
            self,
            owner,
            "if ( !gw_active || gw_minimized )",
            "return qfalse;",
            "cls.state == CA_DISCONNECTED",
            "Key_GetCatcher() & kTextInputCatcherMask",
        )
        for catcher in (
            "KEYCATCH_CONSOLE",
            "KEYCATCH_UI",
            "KEYCATCH_MESSAGE",
            "KEYCATCH_BROWSER",
        ):
            with self.subTest(catcher=catcher):
                self.assertIn(catcher, self.source)
        text_mask = section(
            self.source,
            "static constexpr int kTextInputCatcherMask",
            "using fnql::input::PointerMode",
        )
        self.assertNotIn("KEYCATCH_CGAME", text_mask)

        assert_order(
            self,
            event_filter,
            "!x11_input_context_focused",
            "event->type == FocusIn",
            "event->type == FocusOut",
            "!XFilterEvent( event, None )",
        )
        assert_order(
            self,
            events,
            "if ( !dpy )",
            "X11_SetInputContextFocus( X11_TextInputOwnerActive() );",
            "while( XPending( dpy ) )",
        )
        assert_order(
            self,
            frame,
            "X11_ReopenInputMethodIfNeeded();",
            "X11_SetInputContextFocus( X11_TextInputOwnerActive() );",
            "const PointerOwner pointerOwner",
        )

    def test_repeat_one_shots_keypad_and_super_are_native_complete(
        self,
    ) -> None:
        press = section(self.events, "case KeyPress:", "case KeyRelease:")
        release = section(
            self.events, "case KeyRelease:", "case MotionNotify:"
        )
        translate = function_body(self.source, "XLateKey")
        side_bit = function_body(self.source, "X11_ModifierSideBit")
        family = function_body(self.source, "X11_ModifierFamilyMask")
        physical = function_body(self.source, "X11_ReadPhysicalModifiers")
        held = function_body(self.source, "X11_QueueHeldModifiers")

        assert_order(
            self,
            press,
            "translated_key_by_keycode[event.xkey.keycode] != 0",
            "key == K_ENTER",
            "event.xkey.state & Mod1Mask",
            "if ( !isRepeat )",
            'Cvar_SetIntegerValue( "r_fullscreen"',
            "isRepeat && ( key == K_CONSOLE || key == K_ESCAPE )",
        )
        self.assertIn("suppressed_key_by_keycode", press)
        self.assertIn("suppressed_key_by_keycode", release)
        self.assertIn("case XK_KP_Multiply: *key = '*';", translate)
        self.assertIn("case XK_KP_Equal: *key = K_KP_EQUALS;", translate)
        for body in (side_bit, family, physical, held):
            with self.subTest(function=body[:30]):
                self.assertIn("SUPER", body.upper())
        self.assertIn("K_SUPER", held)
        for fragment in (
            "case XK_ISO_Level3_Shift:",
            "case XK_Mode_switch: *key = K_MODE;",
            "case XK_Multi_key: *key = K_COMPOSE;",
            "case XK_Help: *key = K_HELP;",
            "case XK_Sys_Req: *key = K_SYSREQ;",
            "case XK_Break: *key = K_BREAK;",
            "case XK_Undo: *key = K_UNDO;",
        ):
            self.assertIn(fragment, translate)
        self.assertIn("X11_MOD_LEVEL3 | X11_MOD_MODE_SWITCH", family)
        self.assertIn("K_MODE", held)

    def test_x11_controller_polling_is_inactive_in_background(self) -> None:
        frame = function_body(self.source, "IN_Frame")

        assert_order(
            self,
            frame,
            "if ( gw_active && !gw_minimized )",
            "IN_JoyMove();",
        )

    def test_absolute_dedup_is_consumer_aware_with_retail_precedence(
        self,
    ) -> None:
        consumer = function_body(
            self.source, "IN_PointerConsumerIdentity"
        )
        queue = function_body(
            self.source, "IN_QueueAbsolutePointerPosition"
        )

        assert_order(
            self,
            consumer,
            "catcher & KEYCATCH_CONSOLE",
            "catcher & KEYCATCH_BROWSER",
            "catcher & KEYCATCH_UI",
            "catcher & KEYCATCH_CGAME",
        )
        assert_order(
            self,
            queue,
            "const int consumer = IN_PointerConsumerIdentity();",
            "consumer == absolute_position_consumer",
            "absolute_position_valid = qtrue;",
            "absolute_position_consumer = consumer;",
        )
        self.assertEqual(
            self.source.count("absolute_position_valid = qfalse;"),
            self.source.count("absolute_position_consumer = 0;"),
        )

    def test_absolute_pointer_poll_reconciles_core_buttons_in_one_query(
        self,
    ) -> None:
        mouse_reset = function_body(self.source, "IN_QueueMouseReset")
        reconcile = function_body(
            self.source, "IN_ReconcileAbsolutePointerButtons"
        )
        poll = function_body(
            self.source, "IN_PollAbsolutePointerPosition"
        )
        frame = function_body(self.source, "IN_Frame")

        for mapping in (
            "{ Button1, Button1Mask, K_MOUSE1 }",
            "{ Button2, Button2Mask, K_MOUSE3 }",
            "{ Button3, Button3Mask, K_MOUSE2 }",
        ):
            with self.subTest(mapping=mapping):
                self.assertIn(mapping, reconcile)

        assert_order(
            self,
            mouse_reset,
            "SE_MOUSE_RESET",
            "static_cast<int>( mouse_aux_button_state )",
            "mouse_aux_button_state = 0;",
        )
        assert_order(
            self,
            reconcile,
            "const qboolean logicalDown = Key_IsDown( button.key );",
            "logicalDown && !physicalDown",
            "lostRelease = qtrue;",
            "if ( lostRelease )",
            "IN_QueueMouseReset( eventTime );",
            "IN_EndTemporaryPointerCapture();",
            "return;",
            "( physicalState & button.physicalMask )",
            "Key_IsDown( button.key )",
            "IN_BeginTemporaryPointerCapture( button.button );",
        )

        self.assertEqual(poll.count("XQueryPointer("), 1)
        self.assertNotIn("else", poll)
        assert_order(
            self,
            poll,
            "if ( dpy && win && XQueryPointer(",
            "const int eventTime = Sys_Milliseconds();",
            "IN_ReconcileAbsolutePointerButtons( mask, eventTime );",
            "IN_QueueAbsolutePointerPosition( eventTime, windowX, windowY );",
        )

        absolute = section(
            frame,
            "if ( fnql::input::PointerOwnerReportsAbsolute( pointerOwner ) )",
            "if ( absolute_pointer_owner != PointerOwner::Gameplay )",
        )
        self.assertEqual(absolute.count("IN_PollAbsolutePointerPosition();"), 1)
        assert_order(
            self,
            absolute,
            "IN_DeactivateMouse();",
            "if ( !mode.driveInput )",
            "return;",
            "IN_SetPointerConfinement(",
            "IN_PollAbsolutePointerPosition();",
        )

    def test_x11_mouse_aux_buttons_are_unique_and_reset_balanced(
        self,
    ) -> None:
        buttons = section(
            self.events, "case ButtonPress:", "case CreateNotify:"
        )
        prepare_reset = function_body(
            self.source, "X11_PrepareInputReset"
        )

        assert_order(
            self,
            buttons,
            "event.xbutton.button >= 12",
            "event.xbutton.button <= 27",
            "event.xbutton.button - 12 + K_AUX1",
            "btn_code >= K_AUX1 && btn_code < K_AUX1 + 16",
            "mouse_aux_button_state |= mask;",
            "} else {",
            "mouse_aux_button_state &= ~mask;",
            "Sys_QueEvent( t, SE_KEY, btn_code, btn_press",
        )
        self.assertNotIn("% 16", buttons)
        self.assertIn(
            "mouse_aux_button_state = 0;", prepare_reset
        )

    def test_x11_relative_capability_tracks_mouse_initialization(self) -> None:
        resolve = function_body(self.source, "IN_ResolvePointerMode")

        self.assertIn(
            "inputs.relativeAvailable = mouse_avail ? true : false;",
            resolve,
        )
        self.assertNotIn("inputs.relativeAvailable = true;", resolve)
        self.assertIn(
            '"in_mouse", "1", CVAR_ARCHIVE | CVAR_LATCH | CVAR_CLOUD',
            self.source,
        )
        self.assertIn(
            'Cvar_CheckRange( in_mouse, "0", "1", CV_INTEGER )',
            self.source,
        )


if __name__ == "__main__":
    unittest.main()
