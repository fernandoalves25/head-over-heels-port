// Modulo room_render: desenho da sala (piso, sombras, paredes, portas, objetos com oclusao,
// iluminacao de sala escura, brilhos) e camera. Dono: room_render.cpp.
#pragma once

#include "compat/allegro.h"
#include "game/engine_types.h"

namespace hoh {

// ------------------------------------------------------------------ Flare (brilho preso a objeto)
void *Flare_deleting_dtor(Flare *self, int flags);                      // 0x004016c0 vtable[0]
void Flare_dtor(Flare *self);                                           // 0x004016e0
Flare *Flare_ctor(Flare *self, Object *obj, int mode, int x, int y, int alpha, BITMAP *image);  // 0x004016f0 (Ghidra: AFX_DDPDATA)
void Flare_draw(Flare *self, BITMAP *dst, int origin_x, int origin_y);  // 0x00401730
int Flare_count_transparent(Flare *self);                               // 0x00401860 49 - pixels opacos 7x7 em (x,y)
void Flare_destroy(Flare *self);  // port: (***vtable)(1) = Flare_deleting_dtor(self, 1) (o original nunca chama)

// ------------------------------------------------------------------ Sprite: desenho
void Sprite_create_work_bitmap(Sprite *self);                           // 0x0040fdc0
void Sprite_update_screen_pos(Sprite *self);                            // 0x0040fe00
void Sprite_draw(Sprite *self, BITMAP *dst, int origin_x, int origin_y);              // 0x0040fe50
void Sprite_draw_translucent(Sprite *self, BITMAP *dst, int origin_x, int origin_y);  // 0x0040fea0
void Sprite_draw_received_shadow(Sprite *self, BITMAP *dst, int origin_x, int origin_y);  // 0x0040fee0
void Sprite_restore_work_bitmap(Sprite *self);                          // 0x0040ff10
void Sprite_draw_shadow(Sprite *self, BITMAP *dst, int origin_x, int origin_y);       // 0x0040ff40
void Sprite_clear_overlaps(Sprite *self);                               // 0x0040ff70
void Sprite_add_overlap(Sprite *self, Sprite *other);                   // 0x0040ff80
void Sprite_invalidate(Sprite *self);                                   // 0x0040ffa0 marca self e overlaps sujos

// ------------------------------------------------------------------ Room: desenho
// Ordem de Room_draw: Room_update_attached_lights, piso+sombras, paredes, Room_light_objects (se
// dark), portas, objetos, brilhos.
void Room_draw_floor_and_shadows(Room *self, BITMAP *dst, bool shadows_complex, BITMAP *shadow_buf);  // 0x00413020
// shadow_buf e shadows_complex nao sao lidos (o Ghidra mostrava so 2 argumentos; o asm tem 4).
void Room_draw_objects(Room *self, BITMAP *dst, bool shadows_on, BITMAP *shadow_buf,
                       bool shadows_complex);                           // 0x004130a0
void Room_update_occlusion(Room *self);                                 // 0x00413330
void object_cut_occluded(Object *obj, Object *front);                   // 0x00413530 apaga de obj->work_bmp o que front cobre
void Room_cast_shadows_on_objects(Room *self);                          // 0x00413690
void Room_invalidate_objects(Room *self);                               // 0x004138d0
void object_shadow_extend_sides(Object *obj);                           // 0x00413910
void Room_draw_floor_shadows_buffered(Room *self, BITMAP *dst, BITMAP *shadow_buf);  // 0x00413b30
void Room_draw_floor_shadow_buffered(Room *self, int index, BITMAP *dst, BITMAP *shadow_buf);  // 0x00413ba0
void Room_draw_floor(Room *self, BITMAP *dst, bool first_pass);         // 0x00413f60 1o passe: celulas com floor_height 0 (+ luz)
void Room_draw_walls(Room *self, BITMAP *dst);                          // 0x00414a60
void Room_draw_doors(Room *self, BITMAP *dst);                          // 0x00416b50
void Room_draw(Room *self, BITMAP *dst, bool shadows_on, BITMAP *shadow_buf,
               bool shadows_complex);                                   // 0x00416ca0
int Room_update_camera(Room *self, int view_x, int view_z);             // 0x004177c0 (g_view_ext_x/z); sempre 0

// ------------------------------------------------------------------ iluminacao (sala escura)
// No original e __thiscall com ecx = sala (nao usado); 8 args na pilha, o ultimo (-1) nao e lido.
// scale = 255.0f / raio.
int light_attenuation(int lx, int ly, int lz, int px, int py, int pz, float scale, int unused);  // 0x00415060
// Inlines da classe `fix` do Allegro (fix.h) que o MSVC nao expandiu.
fixed *fix_assign(fixed *self, const fixed *src);                       // 0x004150f0 fix::operator=
fixed *fix_from_int(fixed *self, int x);                                // 0x00415100 fix::fix(int)
fixed *fix_shl(fixed *out, fixed x, int shift);                         // 0x00415110 operator<<
fixed *fix_sqrt(fixed *out, fixed x);                                   // 0x004152a0 sqrt(fix) = fixsqrt
void Room_light_objects(Room *self);                                    // 0x00415130 obj->light = ambient + luzes
void Room_light_floor(Room *self, int rows, int cols, int tile);        // 0x004152c0 rows = W+B, cols = D+B
void Room_light_walls(Room *self, int tile);                            // 0x004154c0
void draw_sprite_h_flip_lit(BITMAP *dst, BITMAP *sprite, int x, int y, int light);  // 0x004159f0

// ------------------------------------------------------------------ brilhos
int Room_add_glow(Room *self, Object *obj, int mode, int x, int y, int alpha, BITMAP *image);  // 0x00415e50 devolve o indice
void Room_draw_glows(Room *self, BITMAP *dst);                          // 0x00415ef0

// ------------------------------------------------------------------ externas ja portadas (render_util.cpp)
void draw_sprite_lit(BITMAP *dst, BITMAP *spr, int x, int y, int light);  // 0x004158a0 (tambem em pending.h)

}  // namespace hoh
