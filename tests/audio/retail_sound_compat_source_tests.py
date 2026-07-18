from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class RetailSoundCompatibilitySourceTests(unittest.TestCase):
    def test_remote_voice_has_a_distinct_validated_backend_contract(self) -> None:
        public = (ROOT / "code/client/audio/snd_public.h").read_text(encoding="utf-8")
        local = (ROOT / "code/client/audio/snd_local.h").read_text(encoding="utf-8")
        facade = (ROOT / "code/client/audio/snd_main.cpp").read_text(encoding="utf-8")

        self.assertIn("S_AddVoiceSamples( int clientNum, int samples, int rate, const short *data )", public)
        self.assertIn("S_ClearLoopingSoundsFrame( void )", public)
        self.assertIn("S_UpdateBackgroundTrack( void )", public)
        self.assertIn("void (*AddVoiceSamples)( int clientNum, int samples, int rate, const short *data );", local)
        self.assertIn("if( !s->AddVoiceSamples ) return false;", facade)
        self.assertIn("if( !s->ClearLoopingSoundsFrame ) return false;", facade)
        self.assertIn("if( !s->UpdateBackgroundTrack ) return false;", facade)
        self.assertIn("clientNum < 0 || clientNum >= MAX_CLIENTS", facade)
        self.assertIn("rate < 8000 || rate > 192000", facade)

    def test_steam_receive_does_not_share_cinematic_raw_stream(self) -> None:
        source = (ROOT / "code/client/cl_webui.cpp").read_text(encoding="utf-8")
        start = source.index("if ( !hasVoice ) return;")
        end = source.index("static void CL_Steam_PublishUserStats", start)
        receive = source[start:end]

        self.assertIn("S_AddVoiceSamples( sender", receive)
        self.assertNotIn("S_RawSamples(", receive)
        self.assertIn("short pcm[maxDecompressedVoice / sizeof( short )]", receive)

    def test_both_backends_implement_voice_lanes_and_volume(self) -> None:
        legacy = (ROOT / "code/client/audio/legacy/snd_dma.cpp").read_text(encoding="utf-8")
        streams = (ROOT / "code/client/audio/openal/AudioSystemStreams.inl").read_text(encoding="utf-8")
        backend = (ROOT / "code/client/audio/openal/AudioSystemBackend.inl").read_text(encoding="utf-8")

        self.assertIn("si->AddVoiceSamples = S_Base_AddVoiceSamples;", legacy)
        self.assertIn("si->ClearLoopingSoundsFrame = S_Base_ClearLoopingSoundsFrame;", legacy)
        self.assertIn("fnql_audio_voice::kMaxLanes", legacy)
        self.assertIn("S_PaintVoiceSamples( s_paintedtime, end", (ROOT / "code/client/audio/legacy/snd_mix.cpp").read_text(encoding="utf-8"))
        self.assertIn("class VoiceChatPlayer", streams)
        policy = (ROOT / "code/client/audio/shared/AudioVoiceQueue.h").read_text(encoding="utf-8")
        self.assertIn("Busy lanes are never stolen", policy)
        self.assertIn("si->AddVoiceSamples =", backend)
        self.assertIn("si->ClearLoopingSoundsFrame =", backend)
        self.assertIn("voicePlayer_.Update", backend)
        self.assertIn("s_voiceVolume->value", backend)

    def test_retail_controls_are_bounded_and_pvs_is_fail_open(self) -> None:
        facade = (ROOT / "code/client/audio/snd_main.cpp").read_text(encoding="utf-8")
        legacy = (ROOT / "code/client/audio/legacy/snd_dma.cpp").read_text(encoding="utf-8")
        world = (ROOT / "code/client/audio/openal/AudioSystemWorld.inl").read_text(encoding="utf-8")

        self.assertIn('Cvar_CheckRange( s_volume, "0", "2", CV_FLOAT )', facade)
        self.assertIn('Cvar_CheckRange( s_musicVolume, "0", "2", CV_FLOAT )', facade)
        self.assertIn('Cvar_CheckRange( s_voiceVolume, "0", "2", CV_FLOAT )', facade)
        self.assertIn('Cvar_Get( "s_pvs", "0"', facade)
        self.assertIn("return qtrue;", facade[facade.index("S_OriginInPVS"):])
        self.assertIn("!S_OriginInPVS( listener_origin, origin )", legacy)
        self.assertIn("!S_OriginInPVS( listenerOrigin_.Data(), voiceOrigin )", world)
        self.assertIn('Cvar_Get( "s_mixPreStep", "0.05"', legacy)

    def test_retail_loop_clear_abi_remains_mapped(self) -> None:
        source = (ROOT / "code/client/cl_cgame.cpp").read_text(encoding="utf-8")
        world = (ROOT / "code/client/audio/openal/AudioSystemWorld.inl").read_text(encoding="utf-8")
        self.assertIn("QL_CG_trap_S_StartSoundVolume", source)
        self.assertIn("S_StartSoundVolume( origin, entityNum, entchannel, sfx, volume );", source)
        self.assertIn("QL_CG_trap_S_StartLocalSoundVolume", source)
        self.assertIn("S_StartLocalSoundVolume( sfx, channelNum, volume );", source)
        self.assertIn("QL_CG_trap_S_ClearLoopingSoundsFrame", source)
        self.assertIn("S_ClearLoopingSoundsFrame();", source)
        self.assertIn("CG_QL_IMPORT_S_CLEARLOOPINGSOUNDS_FRAME", source)
        self.assertIn("loopFrameClearPending_ = true;", world)
        self.assertIn("( voice.killWhenUnrefreshed || loopFrameClearPending_ )", world)


if __name__ == "__main__":
    unittest.main()
