// Vozes/samples do Allegro 4.0 + canal de musica Ogg (substitui o alogg), mixados num callback SDL2.
#include "compat/allegro.h"
#include "compat/platform.h"

#include <SDL.h>
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern void (*g_compat_sample_destroy_hook)(const SAMPLE *);

namespace {

constexpr int kMaxVoices = 64;

struct Voice {
    const SAMPLE *spl = nullptr;
    bool allocated = false;
    bool playing = false;
    bool autokill = false;  // vozes de play_sample se liberam sozinhas ao terminar
    double pos = 0.0;       // em quadros da amostra
    int dir = 1;
    int vol = 255, pan = 128, freq = 0;
    int playmode = PLAYMODE_PLAY;
    uint32_t serial = 0;    // para roubar a voz mais antiga
};

SDL_AudioDeviceID g_dev = 0;
int g_rate = 44100;
Voice g_voices[kMaxVoices];
int g_usable_voices = 8;
float g_voice_gain = 1.0f;
uint32_t g_serial = 0;
std::vector<MusicStream *> g_music;

struct Lock {
    Lock() { if (g_dev) SDL_LockAudioDevice(g_dev); }
    ~Lock() { if (g_dev) SDL_UnlockAudioDevice(g_dev); }
};

inline float sample_at(const SAMPLE *s, long frame, int channel) {
    long idx = frame * (s->stereo ? 2 : 1) + (s->stereo ? channel : 0);
    if (s->bits == 8) return (static_cast<const uint8_t *>(s->data)[idx] - 128) / 128.0f;
    return (static_cast<const uint16_t *>(s->data)[idx] - 32768) / 32768.0f;  // PCM sem sinal
}

void mix_voice(Voice &v, float *out, int frames) {
    const SAMPLE *s = v.spl;
    if (!s || s->len == 0) { v.playing = false; return; }
    double step = static_cast<double>(v.freq) / g_rate;
    float gain = (v.vol / 255.0f) * g_voice_gain;
    float lg = gain * std::min(1.0f, 2.0f * (255 - v.pan) / 255.0f);
    float rg = gain * std::min(1.0f, 2.0f * v.pan / 255.0f);
    double loop_start = static_cast<double>(s->loop_start);
    double loop_end = static_cast<double>(std::min<unsigned long>(s->loop_end ? s->loop_end : s->len, s->len));
    if (loop_end <= loop_start) loop_end = static_cast<double>(s->len);
    for (int i = 0; i < frames; i++) {
        long f0 = static_cast<long>(v.pos);
        double frac = v.pos - f0;
        long f1 = std::min<long>(f0 + 1, static_cast<long>(s->len) - 1);
        float l0 = sample_at(s, f0, 0), r0 = sample_at(s, f0, 1);
        float l = l0 + (sample_at(s, f1, 0) - l0) * static_cast<float>(frac);
        float r = r0 + (sample_at(s, f1, 1) - r0) * static_cast<float>(frac);
        out[i * 2] += l * lg;
        out[i * 2 + 1] += r * rg;
        v.pos += step * v.dir;
        if (v.dir > 0 && v.pos >= loop_end) {
            if (v.playmode & PLAYMODE_BIDIR) { v.dir = -1; v.pos = loop_end - (v.pos - loop_end) - 1; }
            else if (v.playmode & PLAYMODE_LOOP) v.pos = loop_start + (v.pos - loop_end);
            else { v.playing = false; break; }
        } else if (v.dir < 0 && v.pos < ((v.playmode & (PLAYMODE_LOOP | PLAYMODE_BIDIR)) ? loop_start : 0.0)) {
            if (v.playmode & PLAYMODE_BIDIR) { v.dir = 1; v.pos = loop_start; }
            else if (v.playmode & PLAYMODE_LOOP) v.pos = loop_end - 1;
            else { v.playing = false; break; }
        }
    }
    if (!v.playing && v.autokill) v = Voice{};
}

}  // namespace

// ------------------------------------------------------------------ musica (Ogg Vorbis)
struct MusicStream {
    OggVorbis_File vf{};
    bool open = false, playing = false, loop = false;
    int volume = 255;
    int channels = 2;
    long src_rate = 44100;
    std::vector<float> buf;  // quadros decodificados (intercalados), ainda nao consumidos
    size_t buf_pos = 0;      // em quadros
    double frac = 0.0;       // posicao fracionaria para reamostragem linear

    bool refill() {
        // mantem o ultimo quadro para interpolar na fronteira
        if (buf_pos > 0) {
            size_t keep = buf.size() / channels - buf_pos;
            buf.erase(buf.begin(), buf.begin() + static_cast<long>(buf_pos * channels));
            buf.resize(keep * channels);
            buf_pos = 0;
        }
        char pcm[8192];
        int section = 0;
        long got = ov_read(&vf, pcm, sizeof pcm, 0, 2, 1, &section);
        if (got == 0 && loop) {
            ov_pcm_seek(&vf, 0);
            got = ov_read(&vf, pcm, sizeof pcm, 0, 2, 1, &section);
        }
        if (got <= 0) return false;
        const int16_t *s = reinterpret_cast<const int16_t *>(pcm);
        for (long i = 0; i < got / 2; i++) buf.push_back(s[i] / 32768.0f);
        return true;
    }

    void mix(float *out, int frames) {
        double step = static_cast<double>(src_rate) / g_rate;
        float gain = volume / 255.0f;
        for (int i = 0; i < frames; i++) {
            while (buf_pos + 1 >= buf.size() / channels) {
                if (!refill()) { playing = false; return; }
            }
            const float *a = &buf[buf_pos * channels];
            const float *b = &buf[(buf_pos + 1) * channels];
            float t = static_cast<float>(frac);
            float l = a[0] + (b[0] - a[0]) * t;
            float r = channels > 1 ? a[1] + (b[1] - a[1]) * t : l;
            out[i * 2] += l * gain;
            out[i * 2 + 1] += r * gain;
            frac += step;
            size_t adv = static_cast<size_t>(frac);
            buf_pos += adv;
            frac -= static_cast<double>(adv);
        }
    }
};

namespace {

void SDLCALL audio_callback(void *, Uint8 *stream, int len) {
    int frames = len / static_cast<int>(sizeof(int16_t) * 2);
    static std::vector<float> acc;
    acc.assign(static_cast<size_t>(frames) * 2, 0.0f);
    for (Voice &v : g_voices)
        if (v.playing) mix_voice(v, acc.data(), frames);
    for (MusicStream *m : g_music)
        if (m->playing) m->mix(acc.data(), frames);
    int16_t *out = reinterpret_cast<int16_t *>(stream);
    for (int i = 0; i < frames * 2; i++) out[i] = static_cast<int16_t>(std::clamp(acc[i], -1.0f, 1.0f) * 32767.0f);
}

void on_destroy_sample(const SAMPLE *s) {
    Lock lock;
    for (Voice &v : g_voices)
        if (v.spl == s) v = Voice{};
}

Voice *voice_ptr(int voice) {
    if (voice < 0 || voice >= kMaxVoices || !g_voices[voice].allocated) return nullptr;
    return &g_voices[voice];
}

int find_free_voice() {
    for (int i = 0; i < g_usable_voices; i++)
        if (!g_voices[i].allocated) return i;
    // Sem voz livre: rouba a voz automatica mais antiga, como o Allegro.
    int best = -1;
    for (int i = 0; i < g_usable_voices; i++)
        if (g_voices[i].autokill && (best < 0 || g_voices[i].serial < g_voices[best].serial)) best = i;
    return best;
}

}  // namespace

int install_sound(int digi, int, const char *) {
    if (g_dev || digi == DIGI_NONE) return 0;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::snprintf(allegro_error, 256, "%s", SDL_GetError());
        return -1;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    const char *env = std::getenv("HOH_AUDIO_BUFFER");
    want.samples = static_cast<Uint16>(env ? std::atoi(env) : 1024);
    want.callback = audio_callback;
    g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!g_dev) {
        std::snprintf(allegro_error, 256, "%s", SDL_GetError());
        return -1;
    }
    g_rate = have.freq;
    g_compat_sample_destroy_hook = on_destroy_sample;
    SDL_PauseAudioDevice(g_dev, 0);
    return 0;
}

void remove_sound(void) {
    if (!g_dev) return;
    SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
    for (Voice &v : g_voices) v = Voice{};
    for (MusicStream *m : g_music) {
        if (m->open) ov_clear(&m->vf);
        delete m;
    }
    g_music.clear();
    g_compat_sample_destroy_hook = nullptr;
}

void reserve_voices(int digi_voices, int) {
    Lock lock;
    if (digi_voices > 0) g_usable_voices = std::min(digi_voices, kMaxVoices);
}

void set_volume_per_voice(int scale) {
    Lock lock;
    // Allegro: 0 = cada voz em volume cheio; cada unidade a mais divide por 2.
    g_voice_gain = scale <= 0 ? 1.0f : 1.0f / static_cast<float>(1 << std::min(scale, 10));
}

int play_sample(const SAMPLE *spl, int vol, int pan, int freq, int loop) {
    if (!g_dev || !spl) return -1;
    Lock lock;
    int i = find_free_voice();
    if (i < 0) return -1;
    Voice &v = g_voices[i];
    v = Voice{};
    v.spl = spl;
    v.allocated = true;
    v.autokill = true;
    v.vol = std::clamp(vol, 0, 255);
    v.pan = std::clamp(pan, 0, 255);
    v.freq = spl->freq * freq / 1000;  // freq 1000 = velocidade original
    v.playmode = loop ? PLAYMODE_LOOP : PLAYMODE_PLAY;
    v.serial = ++g_serial;
    v.playing = true;
    return i;
}

void stop_sample(const SAMPLE *spl) {
    Lock lock;
    for (Voice &v : g_voices)
        if (v.spl == spl && v.autokill) v = Voice{};
}

int allocate_voice(const SAMPLE *spl) {
    if (!g_dev || !spl) return -1;
    Lock lock;
    int i = find_free_voice();
    if (i < 0) return -1;
    Voice &v = g_voices[i];
    v = Voice{};
    v.spl = spl;
    v.allocated = true;
    v.freq = spl->freq;
    v.serial = ++g_serial;
    return i;
}

void deallocate_voice(int voice) {
    Lock lock;
    if (Voice *v = voice_ptr(voice)) *v = Voice{};
}

void release_voice(int voice) {
    Lock lock;
    if (Voice *v = voice_ptr(voice)) {
        if (v->playing) v->autokill = true;  // continua ate o fim e se libera
        else *v = Voice{};
    }
}

void voice_start(int voice) {
    Lock lock;
    if (Voice *v = voice_ptr(voice)) v->playing = true;
}

void voice_stop(int voice) {
    Lock lock;
    if (Voice *v = voice_ptr(voice)) v->playing = false;
}

const SAMPLE *voice_check(int voice) {
    Lock lock;
    Voice *v = voice_ptr(voice);
    return v ? v->spl : nullptr;
}

int voice_get_position(int voice) {
    Lock lock;
    Voice *v = voice_ptr(voice);
    if (!v || !v->playing) return -1;
    return static_cast<int>(v->pos);
}

void voice_set_volume(int voice, int volume) {
    Lock lock;
    if (Voice *v = voice_ptr(voice)) v->vol = std::clamp(volume, 0, 255);
}

void voice_set_pan(int voice, int pan) {
    Lock lock;
    if (Voice *v = voice_ptr(voice)) v->pan = std::clamp(pan, 0, 255);
}

void voice_set_frequency(int voice, int frequency) {
    Lock lock;
    if (Voice *v = voice_ptr(voice)) v->freq = std::max(1, frequency);
}

void voice_set_playmode(int voice, int playmode) {
    Lock lock;
    if (Voice *v = voice_ptr(voice)) {
        v->playmode = playmode;
        v->dir = (playmode & PLAYMODE_BACKWARD) ? -1 : 1;
        if (v->dir < 0 && v->pos == 0.0 && v->spl) v->pos = static_cast<double>(v->spl->len) - 1;
    }
}

// ------------------------------------------------------------------ API de musica
MusicStream *music_open(const char *path) {
    MusicStream *m = new MusicStream();
    if (ov_fopen(path, &m->vf) != 0) {
        delete m;
        return nullptr;
    }
    m->open = true;
    vorbis_info *vi = ov_info(&m->vf, -1);
    m->channels = vi ? std::clamp(vi->channels, 1, 2) : 2;
    m->src_rate = vi ? vi->rate : 44100;
    Lock lock;
    g_music.push_back(m);
    return m;
}

void music_play(MusicStream *m, int volume, bool loop) {
    if (!m) return;
    Lock lock;
    m->volume = std::clamp(volume, 0, 255);
    m->loop = loop;
    ov_pcm_seek(&m->vf, 0);
    m->buf.clear();
    m->buf_pos = 0;
    m->frac = 0.0;
    m->playing = true;
}

void music_stop(MusicStream *m) {
    if (!m) return;
    Lock lock;
    m->playing = false;
}

void music_set_volume(MusicStream *m, int volume) {
    if (!m) return;
    Lock lock;
    m->volume = std::clamp(volume, 0, 255);
}

bool music_is_playing(MusicStream *m) {
    if (!m) return false;
    Lock lock;
    return m->playing;
}

void music_close(MusicStream *m) {
    if (!m) return;
    {
        Lock lock;
        g_music.erase(std::remove(g_music.begin(), g_music.end(), m), g_music.end());
    }
    if (m->open) ov_clear(&m->vf);
    delete m;
}
