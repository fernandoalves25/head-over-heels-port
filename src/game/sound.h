// Efeitos sonoros do Sound.dat (filtro por categoria = SOUND MODE, volumes, vozes exclusivas).
//
// O "id" de um efeito e o indice no datafile Sound.dat (g_sound_datafile[id].dat = SAMPLE*,
// ordem alfabetica dos nomes). A categoria de cada id vem da tabela g_sample_category
// (0x431e0c, 48 pares {id, categoria}, em game/globals.h): o efeito so toca se
// categoria >= g_sound_mode (SOUND MODE 0 NASTY, 1 SO-SO, 2 LATE).
#pragma once

#include "compat/allegro.h"

namespace hoh {

// 0x44b7d0: {voz, sample} x 4. Voz exclusiva para sons continuos; voice == -1 = livre. So vale
// enquanto voice_check(voice) == spl (senao a voz ja foi reaproveitada pelo Allegro).
struct VoiceSlot {
    int voice;
    const SAMPLE *spl;
};
extern VoiceSlot g_voice_slots[4];
extern VoiceSlot *g_walk_voice;     // 0x4323e8 -> &g_voice_slots[0] (passos)
extern VoiceSlot *g_voice_jump;     // 0x4323ec -> &g_voice_slots[1] (pulo)
extern VoiceSlot *g_voice_rain;     // 0x4323f0 -> &g_voice_slots[2] (chuva)
extern VoiceSlot *g_voice_ambient;  // 0x4323f4 -> &g_voice_slots[3] (trovao / som do mundo)

int get_sample_category(int id);                                   // 0x004022f0
int scale_sfx_volume(int vol);                                     // 0x00402310
void play_sfx_vol(int id, int vol);                                // 0x00402350
void play_sfx(int id);                                             // 0x004023b0
void play_sample_at_volume(int id, int vol);                       // 0x004023c0
void play_sfx_throttled_vol(int id, int *last_frame, int vol);     // 0x00402410
void play_sfx_throttled(int id, int *last_frame);                  // 0x00402480
bool play_sfx_on_voice_vol(int id, VoiceSlot *slot, int vol);      // 0x004024a0
void play_sfx_on_voice(int id, VoiceSlot *slot);                   // 0x004025a0
void stop_voice_slot(VoiceSlot *slot);                             // 0x004025c0
void release_voice_slots();                                        // 0x004022c0

}  // namespace hoh
