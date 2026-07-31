from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class AudioInitSourceTests(unittest.TestCase):
    def test_windows_com_accepts_existing_apartments_without_unbalanced_cleanup(self) -> None:
        raii = (ROOT / "code/win32/win_raii.h").read_text(encoding="utf-8")
        sound = (ROOT / "code/win32/win_snd.cpp").read_text(encoding="utf-8")

        self.assertIn("ownsReference_ = SUCCEEDED( result_ );", raii)
        self.assertIn("usable_ = ownsReference_ || result_ == RPC_E_CHANGED_MODE;", raii)
        reset = raii[raii.index("void reset() noexcept", raii.index("class ScopedComInitialization")) :]
        reset = reset[: reset.index("HRESULT result() const noexcept")]
        self.assertIn("if ( ownsReference_ )", reset)
        self.assertNotIn("if ( usable_ )", reset)
        self.assertIn("if ( !s_soundCom )", sound)
        self.assertIn("WASAPI initialization failed; trying DirectSound fallback", sound)

    def test_windows_optional_tuning_and_live_recovery_do_not_disable_audio(self) -> None:
        sound = (ROOT / "code/win32/win_snd.cpp").read_text(encoding="utf-8")

        priority = sound[sound.index("if ( th == NULL )") :]
        priority = priority[: priority.index("if ( com_developer->integer )")]
        self.assertNotIn("goto err_exit", priority)

        latency = sound[sound.index("GetStreamLatency") :]
        latency = latency[: latency.index("inPlay.store")]
        self.assertNotIn("goto err_exit", latency)
        self.assertIn("device reopen failed; trying DirectSound fallback", sound)
        self.assertIn("std::atomic_bool doSndRestart", sound)
        self.assertIn("std::atomic<UINT32>", sound)

    def test_sdl_uses_the_application_side_stream_format(self) -> None:
        sound = (ROOT / "code/sdl/sdl_snd.cpp").read_text(encoding="utf-8")

        self.assertIn("SDL_GetAudioStreamFormat", sound)
        self.assertIn("dma.channels = streamInput.channels;", sound)
        self.assertIn("dma.samplebits = SDL_AUDIO_BITSIZE( streamInput.format );", sound)
        self.assertNotIn("dma.channels = obtained.channels;", sound)
        self.assertIn("streamInput.channels > 2", sound)
        self.assertIn("SDL_GetSilenceValueForFormat", sound)
        self.assertIn("static_cast<int>( *ptr ) - 128", sound)
        self.assertIn("DmaConfigurationIsUsable", sound)
        self.assertLess(
            sound.index("dmaBuffer.reset( static_cast<byte *>( calloc"),
            sound.index("SDL_memset( dmaBuffer.get(), silenceValue"),
        )

    def test_alsa_failure_cleanup_is_bounded_and_retryable(self) -> None:
        sound = (ROOT / "code/unix/linux_snd.cpp").read_text(encoding="utf-8")
        alsa = sound[: sound.index("#else // legacy OSS code path")]

        self.assertNotIn("alloca(", alsa)
        self.assertIn("kMaximumAlsaParameterBytes", alsa)
        self.assertIn("if ( handle != NULL )", alsa)
        self.assertIn("if ( sync_initialized )", alsa)
        self.assertIn("if ( thread_started )", alsa)
        self.assertIn("snd_inited = qtrue;", alsa)
        self.assertLess(alsa.index("thread_started = qtrue;"), alsa.index("snd_inited = qtrue;"))
        self.assertIn("( period_time + 999 ) / 1000", alsa)
        self.assertIn("if ( tries++ >= 16 )", alsa)

    def test_openal_loader_rejects_partial_runtimes_and_keeps_searching(self) -> None:
        source = (ROOT / "code/client/audio/openal/AudioSystemOpenAL.inl").read_text(
            encoding="utf-8"
        )
        loader = source[source.index("bool OpenALLoader::Load()") :]
        loader = loader[: loader.index("void OpenALLoader::Unload()")]

        self.assertIn("missingSymbols", loader)
        self.assertIn("if ( missingSymbols.empty() )", loader)
        self.assertIn("Unload();", loader)
        self.assertIn("no usable OpenAL runtime was found", loader)


if __name__ == "__main__":
    unittest.main()
