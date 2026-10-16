// Efeitos sonoros do Sound.dat: filtro por SOUND MODE, escala de volume e vozes exclusivas.
#include "game/sound.h"

#include "game/globals.h"

namespace hoh {

// .bss no original (voz 0, sample nulo); init_ui_graphics (0x40e9c0) poe voice = -1 nos 4.
VoiceSlot g_voice_slots[4];
// .data no original: ponteiros fixos para os slots.
VoiceSlot *g_walk_voice = &g_voice_slots[0];     // 0x4323e8
VoiceSlot *g_voice_jump = &g_voice_slots[1];     // 0x4323ec
VoiceSlot *g_voice_rain = &g_voice_slots[2];     // 0x4323f0
VoiceSlot *g_voice_ambient = &g_voice_slots[3];  // 0x4323f4

// Sample do id no Sound.dat (original: *(SAMPLE **)(g_sound_datafile + id*0x10)).
static const SAMPLE *sound_sample(int id) {
    return static_cast<const SAMPLE *>(g_sound_datafile[id].dat);
}

// 0x004022f0 get_sample_category
// original: percorre os 48 pares sem parar no primeiro achado (vale o ultimo par com o id);
// id ausente da tabela = categoria 0.
int get_sample_category(int id) {
    int category = 0;
    for (int i = 0; i < 48; i++) {
        if (g_sample_category[i][0] == id) category = g_sample_category[i][1];
    }
    return category;
}

// 0x00402310 scale_sfx_volume: vol (0..15) * (1/15) * g_sfx_volume, truncado (_ftol). Um som
// com vol > 0 nao some de todo enquanto SOUND VOLUME > 0 (minimo 1).
int scale_sfx_volume(int vol) {
    // constante double em 0x430260 = 0x3fb1111111111111 (1/15); x87 com precisao de 53 bits.
    int scaled = static_cast<int>(static_cast<double>(vol) * (1.0 / 15.0) *
                                  static_cast<double>(g_sfx_volume));
    if (vol > 0 && scaled == 0 && g_sfx_volume > 0) scaled = 1;
    return scaled;
}

// 0x00402350 play_sfx_vol
void play_sfx_vol(int id, int vol) {
    if (g_sound_mode <= get_sample_category(id)) {
        int scaled = scale_sfx_volume(vol);
        play_sample(sound_sample(id), scaled * 0x11, 0x80, 1000, 0);
    }
}

// 0x004023b0 play_sfx
void play_sfx(int id) { play_sfx_vol(id, 0xf); }

// 0x004023c0 play_sample_at_volume: como play_sfx_vol, mas sem passar por g_sfx_volume (usado
// com g_music_volume para os jingles).
void play_sample_at_volume(int id, int vol) {
    if (g_sound_mode <= get_sample_category(id)) {
        play_sample(sound_sample(id), vol * 0x11, 0x80, 1000, 0);
    }
}

// 0x00402410 play_sfx_throttled_vol: so toca se o ultimo disparo foi ha mais de 2 quadros; o
// quadro e gravado mesmo quando nao toca (desde que a categoria passe no filtro).
void play_sfx_throttled_vol(int id, int *last_frame, int vol) {
    if (g_sound_mode <= get_sample_category(id)) {
        if (*last_frame < g_frame_counter - 2) {
            int scaled = scale_sfx_volume(vol);
            play_sample(sound_sample(id), scaled * 0x11, 0x80, 1000, 0);
        }
        *last_frame = g_frame_counter;
    }
}

// 0x00402480 play_sfx_throttled
void play_sfx_throttled(int id, int *last_frame) { play_sfx_throttled_vol(id, last_frame, 0xf); }

// 0x004024a0 play_sfx_on_voice_vol: toca o efeito numa voz exclusiva do slot, a menos que a voz
// do slot ainda esteja tocando. Retorna true se iniciou o som.
bool play_sfx_on_voice_vol(int id, VoiceSlot *slot, int vol) {
    if (get_sample_category(id) < g_sound_mode) return false;
    // voz reaproveitada pelo Allegro para outro sample: o slot a perdeu
    if (slot->voice != -1 && voice_check(slot->voice) != slot->spl) slot->voice = -1;
    // ainda tocando
    if (slot->voice != -1 && voice_get_position(slot->voice) != -1) return false;
    const SAMPLE *spl = sound_sample(id);
    slot->spl = spl;
    if (slot->voice != -1) release_voice(slot->voice);
    int voice = allocate_voice(spl);
    if (voice < 0) {
        slot->voice = -1;
        return false;
    }
    voice_set_volume(voice, scale_sfx_volume(vol) * 0x11);
    voice_set_pan(voice, 0x80);
    voice_set_frequency(voice, spl->freq);
    voice_set_playmode(voice, PLAYMODE_PLAY);
    voice_start(voice);
    slot->voice = voice;
    return true;
}

// 0x004025a0 play_sfx_on_voice
void play_sfx_on_voice(int id, VoiceSlot *slot) { play_sfx_on_voice_vol(id, slot, 0xf); }

// 0x004025c0 stop_voice_slot: para a voz so se ela ainda for do slot (sem libera-la).
void stop_voice_slot(VoiceSlot *slot) {
    if (slot->voice != -1) {
        if (voice_check(slot->voice) == slot->spl) voice_stop(slot->voice);
    }
}

// 0x004022c0 release_voice_slots (chamada a cada load_room)
// original: libera as vozes mas nao poe voice = -1; o proximo play_sfx_on_voice_vol descobre
// pelo voice_check que a voz nao e mais do slot.
void release_voice_slots() {
    for (VoiceSlot &slot : g_voice_slots) {
        if (slot.voice != -1) release_voice(slot.voice);
    }
}

}  // namespace hoh
