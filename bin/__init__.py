import threading
import wave
import io
import os
import tempfile
import re
import time
from synthDriverHandler import SynthDriver as BaseSynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking
from speech.commands import IndexCommand, CharacterModeCommand, LangChangeCommand, BreakCommand, PitchCommand, RateCommand, VolumeCommand
from autoSettingsUtils.driverSetting import BooleanDriverSetting, NumericDriverSetting
import nvwave
import config
from logHandler import log
from .sam import SAM, VOICE_PRESETS, text_to_phonemes
from .reciter import expand_numbers
def split_sentences(text):
    """Split text at sentence boundaries for streaming synthesis."""
    sentences = re.split(r'(?<=[.!?])\s+', text)
    return [s for s in sentences if s.strip()]
class SynthDriver(BaseSynthDriver):
    """SAM (Software Automatic Mouth) synthesizer driver for NVDA."""
    name = "sam"
    description = "SAM (Software Automatic Mouth)"
    supportedSettings = (
        BaseSynthDriver.VoiceSetting(),
        BaseSynthDriver.RateSetting(),
        BaseSynthDriver.PitchSetting(),
        BaseSynthDriver.InflectionSetting(),
        BaseSynthDriver.VolumeSetting(),
        NumericDriverSetting("mouth", "Mouth", availableInSettingsRing=True),
        NumericDriverSetting("throat", "Throat", availableInSettingsRing=True),
        BooleanDriverSetting("singmode", "Sing mode", defaultVal=False),
    )
    supportedCommands = {
        IndexCommand,
        CharacterModeCommand,
        PitchCommand,
        RateCommand,
        VolumeCommand,
        BreakCommand,
    }
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}
    @classmethod
    def check(cls):
        """Check if this synth is available."""
        return True
    def __init__(self):
        super().__init__()
        self._sam = SAM()
        self._voice = "sam"
        self._rate = 50  # 0-100 scale
        self._pitch = 50  # 0-100 scale
        self._inflection = 50  # 0-100 scale
        self._volume = 100  # 0-100 scale
        self._mouth = 50  # 0-100 scale, maps to 0-255
        self._throat = 50  # 0-100 scale, maps to 0-255
        self._singmode = False
        self._speaking = False
        self._cancel_flag = threading.Event()
        self._speech_thread = None
        self._index_callback = None
        try:
            outputDevice = config.conf['speech']['outputDevice']
        except:
            outputDevice = config.conf["audio"]["outputDevice"]
        self._player = nvwave.WavePlayer(
            channels=1,
            samplesPerSec=22050,
            bitsPerSample=8,
            outputDevice=outputDevice
        )
        self._update_sam_params()
    def terminate(self):
        """Clean up when synth is terminated."""
        self.cancel()
        if self._player:
            self._player.close()
            self._player = None
        self._sam = None
    def _update_sam_params(self):
        """Update SAM parameters based on current settings."""
        preset = VOICE_PRESETS.get(self._voice, VOICE_PRESETS['sam'])
        speed = int(40 + (150 - 40) * (100 - self._rate) / 100)
        self._sam.speed = max(20, min(255, speed))
        pitch = int(20 + (120 - 20) * (100 - self._pitch) / 100)
        self._sam.pitch = max(0, min(255, pitch))
        mouth = int(self._mouth * 255 / 100)
        self._sam.mouth = max(0, min(255, mouth))
        throat = int(self._throat * 255 / 100)
        self._sam.throat = max(0, min(255, throat))
        self._sam.singmode = self._singmode
        self._sam.inflection = self._inflection
    def _getAvailableVoices(self):
        """Return available voices."""
        voices = {}
        for name in VOICE_PRESETS.keys():
            display_name = name.replace('_', ' ').title()
            voices[name] = VoiceInfo(name, display_name, "en")
        return voices
    def _get_voice(self):
        return self._voice
    def _set_voice(self, value):
        if value in VOICE_PRESETS:
            self._voice = value
            preset = VOICE_PRESETS[value]
            self._mouth = int(preset['mouth'] * 100 / 255)
            self._throat = int(preset['throat'] * 100 / 255)
            self._update_sam_params()
    def _get_rate(self):
        return self._rate
    def _set_rate(self, value):
        self._rate = max(0, min(100, value))
        self._update_sam_params()
    def _get_pitch(self):
        return self._pitch
    def _set_pitch(self, value):
        self._pitch = max(0, min(100, value))
        self._update_sam_params()
    def _get_volume(self):
        return self._volume
    def _set_volume(self, value):
        self._volume = max(0, min(100, value))
    def _get_inflection(self):
        return self._inflection
    def _set_inflection(self, value):
        self._inflection = max(0, min(100, value))
        self._sam.inflection = self._inflection
    def _get_mouth(self):
        return self._mouth
    def _set_mouth(self, value):
        self._mouth = max(0, min(100, value))
        self._update_sam_params()
    def _get_throat(self):
        return self._throat
    def _set_throat(self, value):
        self._throat = max(0, min(100, value))
        self._update_sam_params()
    def _get_singmode(self):
        return self._singmode
    def _set_singmode(self, value):
        self._singmode = value
        self._update_sam_params()
    def speak(self, speechSequence):
        """
        Speak a sequence of text and commands.
        Args:
            speechSequence: List of text strings and speech commands
        """
        self.cancel()
        self._cancel_flag.clear()
        self._speech_thread = threading.Thread(target=self._speak_thread, args=(speechSequence,))
        self._speech_thread.daemon = True
        self._speech_thread.start()
    def _speak_thread(self, speechSequence):
        """Background thread for speech synthesis."""
        self._speaking = True
        text_buffer = []
        pending_index = None
        try:
            for item in speechSequence:
                if self._cancel_flag.is_set():
                    break
                if isinstance(item, str):
                    text_buffer.append(item)
                elif isinstance(item, IndexCommand):
                    if text_buffer:
                        self._speak_text(''.join(text_buffer))
                        text_buffer = []
                    if self._cancel_flag.is_set():
                        break
                    synthIndexReached.notify(synth=self, index=item.index)
                elif isinstance(item, CharacterModeCommand):
                    if text_buffer:
                        self._speak_text(''.join(text_buffer))
                        text_buffer = []
                elif isinstance(item, BreakCommand):
                    if text_buffer:
                        self._speak_text(''.join(text_buffer))
                        text_buffer = []
                    import time
                    time.sleep(item.time / 1000.0 if item.time else 0.1)
                elif isinstance(item, PitchCommand):
                    if item.offset:
                        self._sam.pitch = max(0, min(255, self._sam.pitch + item.offset))
                elif isinstance(item, RateCommand):
                    if item.offset:
                        speed = self._sam.speed - item.offset  # Inverted: higher rate = lower speed value
                        self._sam.speed = max(20, min(255, speed))
                elif isinstance(item, VolumeCommand):
                    if item.offset:
                        self._volume = max(0, min(100, self._volume + item.offset))
            if text_buffer and not self._cancel_flag.is_set():
                self._speak_text(''.join(text_buffer))
        except Exception as e:
            log.error(f"SAM speech error: {e}")
        finally:
            self._speaking = False
    def _speak_text(self, text):
        """Synthesize and play text with word-level streaming for low latency."""
        if not text.strip():
            return
        try:
            text = expand_numbers(text)
            words = text.split()
            for word in words:
                if self._cancel_flag.is_set():
                    return
                word = word.strip()
                if not word:
                    continue
                audio_data = self._sam.speak(word)
                if audio_data is None or len(audio_data) == 0:
                    continue
                if self._volume < 100:
                    audio_data = self._apply_volume(audio_data)
                audio_data = self._fade_audio(audio_data)
                self._player.feed(audio_data, len(audio_data))
            if not self._cancel_flag.is_set():
                self._player.idle()
        except Exception as e:
            log.error(f"SAM synthesis error: {e}")
    def _apply_volume(self, data):
        """Apply volume scaling to audio data."""
        if self._volume >= 100:
            return data
        scale = self._volume / 100.0
        result = bytearray(len(data))
        for i, sample in enumerate(data):
            signed = sample - 128
            scaled = int(signed * scale)
            result[i] = max(0, min(255, scaled + 128))
        return bytes(result)
    def _fade_audio(self, data, fade_ms=5):
        """Apply fade in/out to avoid clicks at word boundaries."""
        if len(data) < 20:
            return data
        samples = int(22050 * fade_ms / 1000)  # ~110 samples for 5ms
        samples = min(samples, len(data) // 4)  # Don't fade more than 1/4 of audio
        result = bytearray(data)
        for i in range(samples):
            scale = i / samples
            result[i] = int(128 + (result[i] - 128) * scale)
        for i in range(samples):
            idx = len(result) - 1 - i
            scale = i / samples
            result[idx] = int(128 + (result[idx] - 128) * scale)
        return bytes(result)
    def cancel(self):
        """Cancel current speech."""
        self._cancel_flag.set()
        self._speaking = False
        if self._player:
            self._player.stop()
        if self._speech_thread and self._speech_thread.is_alive():
            self._speech_thread.join(timeout=0.5)
    def pause(self, switch):
        """
        Pause or resume speech.
        Args:
            switch: True to pause, False to resume
        """
        if self._player:
            self._player.pause(switch)
    @property
    def isSpeaking(self):
        """Return whether the synth is currently speaking."""
        return self._speaking
