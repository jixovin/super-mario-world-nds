/* Audio: SNES voices played on the DS's own sound hardware.
 *
 * The SNES sound chip (S-DSP) has 8 voices, each playing a BRR-compressed
 * sample at some pitch with a volume envelope. The DS has 16 hardware
 * channels that play PCM at any pitch and volume. So:
 *
 *   - the game's own sound driver (snesrev's C version of the SPC700
 *     program) runs every frame and writes S-DSP registers as usual;
 *   - the S-DSP's envelopes (ADSR/GAIN) are tracked exactly, but no samples
 *     are synthesised (see dsp_cycleEnvelopeOnly);
 *   - on key-on, the voice's BRR sample is decoded once into PCM (cached)
 *     and started on DS channel = voice number;
 *   - each frame, DS channel volume/pan/pitch follow the S-DSP state.
 *
 * Voices using the noise generator play on DS channel 14 (PSG noise).
 *
 * Echo: the DS's two sound capture units record the left/right mixer
 * output into ring buffers as long as the S-DSP echo delay, and DS
 * channels 1 and 3 play those buffers back (the classic DS reverb). What
 * they play is captured again, so each repeat is quieter by the echo
 * volume, like the S-DSP's echo feedback. Differences: every voice is
 * echoed (not just the ones with echo on) and there is no FIR filter.
 * Not yet: pitch modulation.
 */
#include "nds_audio.h"

#include <nds.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "smw_port.h"
#include "smw_spc_player.h"
#include "snes/dsp.h"

enum {
  kVoices = 8,
  kCacheEntries = 256,
  kArenaBytes = 640 * 1024,
  kMaxSampleBlocks = 4096,   /* 64K samples: guards against bad data */
};

/* DS channel for each S-DSP voice: 1 and 3 play the echo (capture units
 * 0 and 1), 14 plays noise. */
static const uint8_t kVoiceCh[kVoices] = {0, 2, 4, 5, 6, 7, 8, 9};
enum { kNoiseCh = 14, kEchoChL = 1, kEchoChR = 3, kEchoMaxSamples = 15 * 512 };

/* SNES pitch p plays at 32000 * p / 4096 Hz; DS timer = clock / rate. */
#define PITCH_TO_TIMER(p) (2144895u / (p))

typedef struct Sample {
  uint16_t start, loop;      /* SPC RAM addresses from the sample directory */
  uint32_t check;            /* cheap fingerprint of the BRR data */
  int16_t *pcm;
  uint32_t words, loop_words;
  bool loops;
} Sample;

typedef struct Voice {
  bool playing, noise;
  uint16_t timer, vol, pan;  /* last values sent to the hardware */
} Voice;

static Sample s_cache[kCacheEntries];
static int s_cache_used;
static uint8_t *s_arena;
static uint32_t s_arena_used;
static uint32_t s_upload_seen;
static Voice s_voice[kVoices];
static bool s_on;

static int16_t *s_echo_buf[2];     /* capture ring buffers, left and right */
static unsigned s_echo_len;        /* samples; 0 = echo not running */
static uint16_t s_echo_vol[2];

static void StopAll(void) {
  soundStop(0xffff & ~((1u << kEchoChL) | (1u << kEchoChR)));
  memset(s_voice, 0, sizeof(s_voice));
}

static void ClearCache(void) {
  StopAll();
  s_cache_used = 0;
  s_arena_used = 0;
}

void NdsAudioInit(void) {
  s_arena = malloc(kArenaBytes);
  if (!s_arena) return;
  for (int i = 0; i < 2; i++) {
    s_echo_buf[i] = aligned_alloc(32, kEchoMaxSamples * 2);
    if (!s_echo_buf[i]) return;
  }
  soundInit();
  soundPowerOn();
  soundSetMixerVolume(127);
  PortSetAudioMode(kPortAudio_Hardware);
  s_upload_seen = g_spc_upload_count;
  s_on = true;
}

static uint32_t Fingerprint(const uint8_t *ram, uint16_t addr) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 36; i++) h = (h ^ ram[(uint16_t)(addr + i)]) * 16777619u;
  return h;
}

/* Decode BRR blocks starting at addr into dst (16 samples per block),
 * continuing the filter history in *old/*older. Stops after the block
 * with the end flag; returns the number of blocks. */
static int DecodeBrr(const uint8_t *ram, uint16_t addr, int16_t *dst, int max_blocks,
                     int *old_p, int *older_p, uint8_t *flags_out) {
  int old = *old_p, older = *older_p, blocks = 0;
  uint8_t header = 0;
  while (blocks < max_blocks) {
    header = ram[addr++];
    const int shift = header >> 4, filter = (header >> 2) & 3;
    for (int i = 0; i < 16; i++) {
      const uint8_t byte = ram[(uint16_t)(addr + (i >> 1))];
      int s = (i & 1) ? (byte & 15) : (byte >> 4);
      if (s > 7) s -= 16;
      s = shift <= 12 ? (s << shift) >> 1 : (s >> 3) << 12;
      switch (filter) {
        case 1: s += old + (-old >> 4); break;
        case 2: s += 2 * old + ((3 * -old) >> 5) - older + (older >> 4); break;
        case 3: s += 2 * old + ((13 * -old) >> 6) - older + ((3 * older) >> 4); break;
      }
      s = s < -0x8000 ? -0x8000 : (s > 0x7fff ? 0x7fff : s);
      s = ((int16_t)((s & 0x7fff) << 1)) >> 1;  /* 15-bit, as the S-DSP */
      older = old;
      old = s;
      *dst++ = (int16_t)(s << 1);                /* full 16-bit range */
    }
    addr += 8;
    blocks++;
    if (header & 1) break;
  }
  *old_p = old;
  *older_p = older;
  *flags_out = header & 3;
  return blocks;
}

static int CountBlocks(const uint8_t *ram, uint16_t addr) {
  for (int n = 1; n <= kMaxSampleBlocks; n++, addr += 9)
    if (ram[addr] & 1) return n;
  return kMaxSampleBlocks;
}

/* The PCM version of sample `srcn` (decoded on first use). */
static const Sample *GetSample(const Dsp *dsp, const uint8_t *ram, uint8_t srcn) {
  const uint16_t dir = dsp->dirPage + 4 * srcn;
  const uint16_t start = ram[dir] | ram[(uint16_t)(dir + 1)] << 8;
  const uint16_t loop = ram[(uint16_t)(dir + 2)] | ram[(uint16_t)(dir + 3)] << 8;
  const uint32_t check = Fingerprint(ram, start);
  for (int i = 0; i < s_cache_used; i++) {
    const Sample *c = &s_cache[i];
    if (c->start == start && c->loop == loop && c->check == check) return c;
  }

  /* Decode: the part from start to the end block, then (if it loops back
   * somewhere else) the loop part, once more, so the loop has the filter
   * history it would have on the S-DSP. */
  const int blocks1 = CountBlocks(ram, start);
  const bool loop_inside = loop >= start && (uint32_t)(loop - start) < (uint32_t)blocks1 * 9 &&
                           (loop - start) % 9 == 0;
  const int blocks2 = loop_inside ? 0 : CountBlocks(ram, loop);
  const uint32_t bytes = (uint32_t)(blocks1 + blocks2) * 32;
  if (s_cache_used == kCacheEntries || s_arena_used + bytes > kArenaBytes) ClearCache();
  if (bytes > kArenaBytes) return NULL;

  Sample *c = &s_cache[s_cache_used++];
  c->start = start;
  c->loop = loop;
  c->check = check;
  c->pcm = (int16_t *)(s_arena + s_arena_used);
  s_arena_used += bytes;
  int old = 0, older = 0;
  uint8_t flags = 0;
  DecodeBrr(ram, start, c->pcm, blocks1, &old, &older, &flags);
  c->loops = (flags & 2) != 0;
  if (loop_inside) {
    c->loop_words = (uint32_t)(loop - start) / 9 * 8;
  } else {
    c->loop_words = (uint32_t)blocks1 * 8;
    if (c->loops) DecodeBrr(ram, loop, c->pcm + blocks1 * 16, blocks2, &old, &older, &flags);
  }
  c->words = (uint32_t)(blocks1 + (loop_inside ? 0 : blocks2)) * 8;
  if (!c->loops) c->words = (uint32_t)blocks1 * 8;
  DC_FlushRange(c->pcm, bytes);
  return c;
}

/* S-DSP voice/master volumes and envelope -> DS 11-bit volume + pan. */
static void VolumePan(const Dsp *dsp, const DspChannel *ch, uint16_t *vol, uint16_t *pan) {
  if (dsp->mute) {
    *vol = 0;
    *pan = 64;
    return;
  }
  const int vl = abs(ch->volumeL) * abs(dsp->masterVolumeL);  /* 0..16129 */
  const int vr = abs(ch->volumeR) * abs(dsp->masterVolumeR);
  /* Each side: sample * env/2048 * vol/64 * master/128; the DS pans a
   * single volume linearly, so send L+R and the right-hand share.
   * Scaled by 1/2 for headroom across 8 voices. */
  const uint32_t sum = (uint32_t)(vl + vr);
  uint32_t v = (sum * ch->gain) >> 12;          /* env 0..2047 */
  v = v * 2047 / (64 * 128);
  *vol = (uint16_t)(v > 2047 ? 2047 : v);
  *pan = (uint16_t)(sum ? (uint32_t)vr * 127 / sum : 64);
}

static unsigned NoiseTimer(const Dsp *dsp) {
  /* noiseRate = S-DSP samples per noise step; DS PSG noise steps at the
   * channel timer rate. */
  const unsigned steps_per_sec = dsp->noiseRate ? 32000u / dsp->noiseRate : 16000u;
  return soundTimerFromHz(steps_per_sec ? steps_per_sec : 1);
}

static void StopEcho(void) {
  if (!s_echo_len) return;
  soundStop((1u << kEchoChL) | (1u << kEchoChR) | (3u << 16));
  s_echo_len = 0;
}

/* DS echo channel volume for S-DSP echo volume `evol` and feedback `efb`.
 * On the DS the echo channel's volume is both the output level and the
 * feedback (its output is captured again), so pick the single gain g whose
 * repeats g, g^2, ... carry the same total energy as the S-DSP's
 * evol, evol*efb, evol*efb^2, ...:  g^2/(1-g^2) = evol^2/(1-efb^2).
 * Capped at 0.6 so the feedback always dies away. */
static uint16_t EchoVol(int8_t evol, int8_t efb) {
  static int last_key = -1;
  static uint16_t last;
  const int key = (uint8_t)evol << 8 | (uint8_t)efb;
  if (key == last_key) return last;
  last_key = key;
  const float v = abs(evol) / 128.0f, f = abs(efb) / 128.0f;
  const float e = v * v / (1.0f - (f < 0.95f ? f * f : 0.9025f));
  float g = sqrtf(e / (1.0f + e));
  if (g > 0.6f) g = 0.6f;
  return last = (uint16_t)(g * 2047.0f);
}

static void UpdateEcho(const Dsp *dsp) {
  bool any = false;
  for (int v = 0; v < kVoices; v++) any |= dsp->channel[v].echoEnable;
#ifdef SMW_NDS_NO_ECHO
  any = false;  /* test builds: compare with/without echo */
#endif
  const bool on = any && dsp->echoWrites && !dsp->mute && dsp->echoDelay >= 512 &&
                  (dsp->echoVolumeL || dsp->echoVolumeR);
  if (!on) {
    StopEcho();
    return;
  }
  const unsigned len = dsp->echoDelay > kEchoMaxSamples ? kEchoMaxSamples : dsp->echoDelay;
  const uint16_t vl = EchoVol(dsp->echoVolumeL, dsp->feedbackVolume),
                 vr = EchoVol(dsp->echoVolumeR, dsp->feedbackVolume);
  if (len != s_echo_len) {
    StopEcho();
    const unsigned timer = soundTimerFromHz(32000);
    for (int i = 0; i < 2; i++) {
      memset(s_echo_buf[i], 0, len * 2);
      DC_FlushRange(s_echo_buf[i], len * 2);
      soundPrepareCap(i, SoundCapDst_ChBNormal, SoundCapSrc_Mixer, true, SoundCapFmt_Pcm16,
                      s_echo_buf[i], len / 2);
    }
    soundPreparePcm(kEchoChL, vl, 0, timer, SoundMode_Repeat, SoundFmt_Pcm16, s_echo_buf[0], 0, len / 2);
    soundPreparePcm(kEchoChR, vr, 127, timer, SoundMode_Repeat, SoundFmt_Pcm16, s_echo_buf[1], 0, len / 2);
    soundStart((1u << kEchoChL) | (1u << kEchoChR) | (3u << 16));
    s_echo_len = len;
    s_echo_vol[0] = vl;
    s_echo_vol[1] = vr;
    return;
  }
  if (vl != s_echo_vol[0]) soundChSetVolume(kEchoChL, s_echo_vol[0] = vl);
  if (vr != s_echo_vol[1]) soundChSetVolume(kEchoChR, s_echo_vol[1] = vr);
}

void NdsAudioFrame(void) {
  if (!s_on) return;
  const Dsp *dsp = PortDsp();
  const uint8_t *ram = PortSpcRam();
  UpdateEcho(dsp);

  if (g_spc_upload_count != s_upload_seen) {  /* new music/sample data */
    s_upload_seen = g_spc_upload_count;
    ClearCache();
  }

  const uint8_t kon = g_dsp_kon_events;
  g_dsp_kon_events = 0;

  for (int v = 0; v < kVoices; v++) {
    const DspChannel *ch = &dsp->channel[v];
    Voice *voice = &s_voice[v];
    uint16_t vol, pan;
    VolumePan(dsp, ch, &vol, &pan);
    const uint16_t pitch = ch->pitch ? ch->pitch : 1;
    uint32_t timer = PITCH_TO_TIMER(pitch);
    if (timer > 0xffff) timer = 0xffff;
    if (timer < 16) timer = 16;

    if (kon & (1 << v)) {
      if (voice->playing) soundStop(1u << (voice->noise ? kNoiseCh : kVoiceCh[v]));
      voice->playing = false;
      voice->noise = ch->useNoise;
      if (ch->useNoise) {
        soundPreparePsg(kNoiseCh | SOUND_START, vol, pan, NoiseTimer(dsp), SoundDuty_50);
        voice->playing = true;
      } else {
        const Sample *smp = GetSample(dsp, ram, ch->srcn);
        if (smp && smp->words) {
          if (smp->loops)
            soundPreparePcm(kVoiceCh[v] | SOUND_START, vol, pan, timer, SoundMode_Repeat, SoundFmt_Pcm16,
                            smp->pcm, smp->loop_words, smp->words - smp->loop_words);
          else
            soundPreparePcm(kVoiceCh[v] | SOUND_START, vol, pan, timer, SoundMode_OneShot, SoundFmt_Pcm16,
                            smp->pcm, 0, smp->words);
          voice->playing = true;
        }
      }
      voice->timer = (uint16_t)timer;
      voice->vol = vol;
      voice->pan = pan;
      continue;
    }

    if (!voice->playing) continue;
    const unsigned hw = voice->noise ? kNoiseCh : kVoiceCh[v];
    if (ch->adsrState == 4 && ch->gain == 0) {  /* released and silent */
      soundStop(1u << hw);
      voice->playing = false;
      continue;
    }
    if (vol != voice->vol) {
      soundChSetVolume(hw, vol);
      voice->vol = vol;
    }
    if (pan != voice->pan) {
      soundChSetPan(hw, pan);
      voice->pan = pan;
    }
    if (!voice->noise && timer != voice->timer) {
      soundChSetTimer(hw, timer);
      voice->timer = (uint16_t)timer;
    }
  }
}

/* Before sleep mode: silence everything. Voices and echo start again
 * from the game's sound state on the following frames. */
void NdsAudioSuspend(void) {
  if (!s_on) return;
  StopAll();
  StopEcho();
}
