// Modulo room_render: desenho da sala (piso, sombras, paredes, portas, objetos com oclusao,
// iluminacao de sala escura, brilhos) e camera. Port de HeadOverHeels/re/decomp/game.c, conferido
// com a desmontagem do HoH.exe onde o pseudo-C era ambiguo.
//
// Notas gerais:
// * As chamadas `(**(code **)(bmp->vtable + 0x40))` / `+0x44` (draw_sprite / draw_256_sprite, o
//   inline draw_sprite do Allegro) viram draw_sprite: no jogo tudo e 16 bpp, o ramo de 8 bpp nunca
//   ocorre. `+0x4c` = draw_sprite_h_flip, `+0x9c` = clear_to_color, `+0x34` = line, `+0x20` =
//   getpixel (todas da GFX_VTABLE, ver C_structs.md).
// * fblend_sprite_a 0x42a510 = fblend_trans, fblend_sprite_b 0x429d10 = fblend_add.
// * Acessos `*(int*)(bmp + 0x40 + y*4)` (line[y]) viram bmp->line[y]; as contas com pixels RGB565
//   (luz, escurecimento da sombra) sao feitas em uint32_t como no exe (mul/shr sem sinal).
#include "game/room_render.h"

#include "game/engine_data.h"  // bitmap_apply_mask
#include "game/globals.h"      // rand() do MSVC (crt.h), g_vtbl_Flare
#include "game/room_load.h"    // Room_update_attached_lights

#include <cstdint>

namespace hoh {

namespace {

inline uint16_t mask_color16() { return static_cast<uint16_t>(makecol(0xff, 0, 0xff)); }

// Multiplica um pixel RGB565 por f/32 (f ja em 32 avos), como em draw_sprite_lit:
// ((p & 0x7e0) << 16 | p & 0x7e0f81f) * f >> 5 & 0x7e0f81f, depois junta as duas metades.
inline uint16_t scale565(uint32_t p, uint32_t f) {
    p = ((((p & 0x7e0) << 16) | (p & 0x7e0f81f)) * f >> 5) & 0x7e0f81f;
    return static_cast<uint16_t>((p >> 16) | p);
}

}  // namespace

// ================================================================== Flare (brilho preso a objeto)

// 0x004016c0 Flare_deleting_dtor
void *Flare_deleting_dtor(Flare *self, int flags) {
    Flare_dtor(self);
    if ((flags & 1) != 0) delete self;
    return self;
}

// 0x004016e0 Flare_dtor (so restaura a vtable)
void Flare_dtor(Flare *self) {
    self->vtbl = g_vtbl_Flare;
}

// 0x004016f0 Flare_ctor (o Ghidra rotulou AFX_DDPDATA::AFX_DDPDATA)
Flare *Flare_ctor(Flare *self, Object *obj, int mode, int x, int y, int alpha, BITMAP *image) {
    self->obj = obj;
    self->mode = mode;
    self->x = x;
    self->y = y;
    self->vtbl = g_vtbl_Flare;
    self->alpha = alpha;
    self->image = image;
    return self;
}

// 0x00401730 Flare_draw: ancora = sprite do objeto + (x, y) + origem. O tamanho cai com o numero
// de pixels transparentes na janela 7x7 em volta da ancora (objeto encoberto => brilho menor):
// n = min(Flare_count_transparent, 30); se n != 30, len = (30 - n) / 30 * alpha (em double, _ftol).
// Modo 0: cruz de 4 linhas brancas de comprimento len; modo 1: imagem aditiva com fator len.
void Flare_draw(Flare *self, BITMAP *dst, int origin_x, int origin_y) {
    const int x = self->obj->scr_x + self->x + origin_x;
    const int y = (self->y - self->obj->scr_y) + origin_y;
    const int white = makecol(0xff, 0xff, 0xff);
    int n = Flare_count_transparent(self);
    if (30.0 < static_cast<double>(n)) n = 0x1e;
    if (static_cast<double>(n) == 30.0) return;
    // fild n; fsubr 30.0; fmul 1/30 (0x430248); fimul alpha; _ftol (trunca)
    const int len = static_cast<int>((30.0 - static_cast<double>(n)) * (1.0 / 30.0) *
                                     static_cast<double>(self->alpha));
    if (self->mode == 0) {
        // vtable +0x34 = line (_normal_line do Allegro): x1 == x2 -> vline, y1 == y2 -> hline.
        hline(dst, x, y, x + len, white);  // line(dst, x, y, x + len, y)
        vline(dst, x, y, y - len, white);  // line(dst, x, y, x, y - len)
        hline(dst, x, y, x - len, white);  // line(dst, x, y, x - len, y)
        vline(dst, x, y, len + y, white);  // line(dst, x, y, x, y + len)
    } else if (self->mode == 1) {
        BITMAP *img = self->image;
        fblend_add(img, dst, x - (img->w >> 1), y - (img->h >> 1), len);
    }
}

// 0x00401860 Flare_count_transparent: 49 - pixels nao-mascara do work_bmp do objeto na janela
// 7x7 [x-3, x+3] x [y-3, y+3] (cortada nas bordas do bitmap).
int Flare_count_transparent(Flare *self) {
    const int mask = makecol(0xff, 0, 0xff);
    int count = 0;
    int px = self->x - 3;
    int py0 = self->y - 3;
    if (px < 0) px = 0;
    if (py0 < 0) py0 = 0;
    for (; px < self->x + 4; px++) {
        if (self->obj->work_bmp->w <= px) break;
        for (int py = py0; py < self->y + 4; py++) {
            BITMAP *bmp = self->obj->work_bmp;
            if (bmp->h <= py) break;
            if (getpixel(bmp, px, py) != mask) count++;
        }
    }
    return 0x31 - count;
}

// port: (***vtable)(1) = Flare_deleting_dtor(self, 1); o original nunca chama.
void Flare_destroy(Flare *self) {
    Flare_deleting_dtor(self, 1);
}

// ================================================================== Sprite: desenho

// 0x0040fdc0 Sprite_create_work_bitmap: work_bmp = copia do quadro atual.
void Sprite_create_work_bitmap(Sprite *self) {
    const int h = self->cur_img->h;
    const int w = self->cur_img->w;
    BITMAP *work = create_bitmap(w, h);
    self->work_bmp = work;
    blit(self->cur_img, work, 0, 0, 0, 0, w, h);
}

// 0x0040fe00 Sprite_update_screen_pos: projecao isometrica do sprite (e da sombra, se visivel).
void Sprite_update_screen_pos(Sprite *self) {
    Block *block = self->block;
    const int x = self->x;
    const int z = self->z;
    self->scr_x = (x - block->size_z) - z;
    const int half = (z + x) >> 1;
    self->scr_y = block->sprite_h + self->y + half;
    if (!self->hidden) {
        self->shadow_scr_x = (x - block->size_z) - z;
        self->shadow_scr_y = block->shadow->h + half;  // original: block->shadow, nao cur_shadow
    }
}

// 0x0040fe50 Sprite_draw
void Sprite_draw(Sprite *self, BITMAP *dst, int origin_x, int origin_y) {
    // original: draw_256_sprite se o work_bmp fosse de 8 bpp (nunca no jogo).
    draw_sprite(dst, self->work_bmp, self->scr_x + origin_x, origin_y - self->scr_y);
}

// 0x0040fea0 Sprite_draw_translucent (OBJ-Transparent)
void Sprite_draw_translucent(Sprite *self, BITMAP *dst, int origin_x, int origin_y) {
    fblend_trans(self->work_bmp, dst, self->scr_x + origin_x, origin_y - self->scr_y,
                 0xff - self->alpha);
}

// 0x0040fee0 Sprite_draw_received_shadow: sombras de outros objetos projetadas sobre este.
void Sprite_draw_received_shadow(Sprite *self, BITMAP *dst, int origin_x, int origin_y) {
    fblend_trans(self->recv_shadow, dst, self->scr_x + origin_x, origin_y - self->scr_y, 100);
}

// 0x0040ff10 Sprite_restore_work_bitmap
void Sprite_restore_work_bitmap(Sprite *self) {
    BITMAP *src = self->cur_img;
    blit(src, self->work_bmp, 0, 0, 0, 0, src->w, src->h);
}

// 0x0040ff40 Sprite_draw_shadow: sombra propria no chao.
void Sprite_draw_shadow(Sprite *self, BITMAP *dst, int origin_x, int origin_y) {
    fblend_trans(self->cur_shadow, dst, self->shadow_scr_x + origin_x,
                 origin_y - self->shadow_scr_y, 100);
}

// 0x0040ff70 Sprite_clear_overlaps
void Sprite_clear_overlaps(Sprite *self) {
    self->overlap_count = 0;
}

// 0x0040ff80 Sprite_add_overlap
void Sprite_add_overlap(Sprite *self, Sprite *other) {
    self->overlaps[self->overlap_count] = other;
    self->overlap_count = self->overlap_count + 1;
}

// 0x0040ffa0 Sprite_invalidate: marca self e os sobrepostos como sujos; recalcula a projecao.
void Sprite_invalidate(Sprite *self) {
    self->dirty = true;
    for (int i = 0; i < self->overlap_count; i++) self->overlaps[i]->dirty = true;
    Sprite_update_screen_pos(self);  // jmp 0x40fe00
}

// ================================================================== Room: desenho

// 0x00413020 Room_draw_floor_and_shadows: piso plano (passe 1), sombras no chao, resto do piso.
void Room_draw_floor_and_shadows(Room *self, BITMAP *dst, bool shadows_complex, BITMAP *shadow_buf) {
    Room_draw_floor(self, dst, true);
    if (!shadows_complex) {
        // do ultimo objeto para o primeiro
        for (int i = self->n_objects - 1; i >= 0; i--) {
            Object *obj = self->objects[i];
            if (!obj->hidden) Sprite_draw_shadow(obj, dst, self->origin_x, self->origin_y);
        }
    } else {
        Room_draw_floor_shadows_buffered(self, dst, shadow_buf);
    }
    Room_draw_floor(self, dst, false);
}

// 0x004130a0 Room_draw_objects: oclusao, objetos opacos (com luz se escura), sombras recebidas
// (shadows_on), depois os transparentes. shadow_buf/shadows_complex nao sao lidos (ret 0x10).
void Room_draw_objects(Room *self, BITMAP *dst, bool shadows_on, BITMAP *shadow_buf,
                       bool shadows_complex) {
    Room_update_occlusion(self);
    if (shadows_on) {
        Room_cast_shadows_on_objects(self);
        for (int i = 0; i < self->n_objects; i++) {
            Object *obj = self->objects[i];
            if (!obj->hidden && !obj->transparent) {
                if (!self->dark) {
                    Sprite_draw(obj, dst, self->origin_x, self->origin_y);
                } else {
                    draw_sprite_lit(dst, obj->work_bmp, self->origin_x + obj->scr_x,
                                    self->origin_y - obj->scr_y, obj->light);
                }
            }
            // original: testado fora do if acima (vale tambem para ocultos/transparentes)
            if (obj->has_recv_shadow) {
                bitmap_apply_mask(obj->recv_shadow, obj->work_bmp);
                Sprite_draw_received_shadow(obj, dst, self->origin_x, self->origin_y);
            }
        }
    } else {
        for (int i = 0; i < self->n_objects; i++) {
            Object *obj = self->objects[i];
            if (!obj->hidden && !obj->transparent) {
                if (!self->dark) {
                    Sprite_draw(obj, dst, self->origin_x, self->origin_y);
                } else {
                    draw_sprite_lit(dst, obj->work_bmp, self->origin_x + obj->scr_x,
                                    self->origin_y - obj->scr_y, obj->light);
                }
            }
        }
    }
    // transparentes (igual nos dois ramos do original)
    for (int i = 0; i < self->n_objects; i++) {
        Object *obj = self->objects[i];
        if (!obj->hidden && obj->transparent) {
            if (self->dark) {
                // ilumina o proprio work_bmp e o marca sujo (sera restaurado no proximo quadro)
                draw_sprite_lit(obj->work_bmp, obj->work_bmp, 0, 0, obj->light);
                obj->dirty = true;
            }
            Sprite_draw_translucent(obj, dst, self->origin_x, self->origin_y);
        }
    }
}

// 0x00413330 Room_update_occlusion: para cada objeto sujo, restaura o work_bmp, refaz a lista de
// sobreposicao (retangulos de tela) e apaga dos de tras o que os da frente cobrem.
void Room_update_occlusion(Room *self) {
    for (int i = 0; i < self->n_objects; i++) {
        Object *obj = self->objects[i];
        if (!obj->hidden && obj->dirty) Sprite_restore_work_bitmap(obj);
    }
    for (int i = 0; i < self->n_objects; i++) {
        Object *a = self->objects[i];
        if (a->hidden || !a->dirty) continue;
        Sprite_clear_overlaps(a);
        const int ax = a->scr_x;
        a->dirty = false;
        const int ah = a->block->sprite_h;
        const int ay = a->scr_y - ah;
        const int aw = a->block->sprite_w;
        for (int j = 0; j < self->n_objects; j++) {
            Object *b = self->objects[j];
            if (j == i || b->hidden) continue;
            const int bh = b->block->sprite_h;
            const int by = b->scr_y - bh;
            if (b->scr_x <= aw + ax && by <= ah + ay && ax <= b->block->sprite_w + b->scr_x &&
                ay <= bh + by) {
                Sprite_add_overlap(a, b);
                Block *bb = b->block;
                if (a->z < bb->size_z + b->z && a->x < bb->size_x + b->x &&
                    (bb->size_y + b->y <= a->y || a->block->size_x + a->x <= b->x ||
                     a->block->size_z + a->z <= b->z)) {
                    // b esta atras de a: recorta b (se b nao for refeito depois)
                    if (b->dirty) continue;
                    if (!a->transparent) object_cut_occluded(b, a);
                } else {
                    // a esta atras de b
                    if (!b->transparent) object_cut_occluded(a, b);
                }
            }
        }
    }
}

// 0x00413530 object_cut_occluded (ret 8; o ecx do chamador e ignorado): pinta magenta no work_bmp
// de obj onde o quadro (cur_img) de front e opaco, deslocado pela diferenca das posicoes de tela.
void object_cut_occluded(Object *obj, Object *front) {
    const uint16_t mask = mask_color16();
    BITMAP *work = obj->work_bmp;
    BITMAP *fimg = front->cur_img;
    BITMAP *bimg = obj->cur_img;
    const int dx = front->scr_x - obj->scr_x;
    const int dy = front->scr_y - obj->scr_y;  // linha r de obj = linha r + dy de front
    int x0, x1, y0 = 0, y1;
    if (dx > 0) {
        x0 = dx;
        x1 = bimg->w;
    } else {
        x0 = 0;
        x1 = fimg->w + dx;
        if (x1 > bimg->w) x1 = bimg->w;
    }
    if (dy > 0) {
        y1 = fimg->h - dy;
        if (y1 > bimg->h) y1 = bimg->h;
    } else {
        y1 = bimg->h;
        y0 = -dy;
    }
    // original: estes dois testes nunca disparam (e o segundo usaria dy, nao -dy)
    if (x0 - dx < 0) x0 = dx;
    if (y0 + dy < 0) y0 = dy;
    if (x1 - dx > fimg->w) x1 = fimg->w + dx;
    if (y1 + dy > fimg->h) y1 = fimg->h - dy;
    for (int y = y0; y < y1; y++) {
        const uint16_t *f = fimg->line[y + dy];
        uint16_t *w = work->line[y];
        for (int x = x0; x < x1; x++) {
            if (f[x - dx] != mask) w[x] = mask;
        }
    }
}

// 0x00413690 Room_cast_shadows_on_objects: para cada par (A visivel, B que recebe sombra) com o
// topo de B abaixo de A e caixas cruzando em x/z, desenha a silhueta de A no recv_shadow de B.
void Room_cast_shadows_on_objects(Room *self) {
    const int mask = makecol(0xff, 0, 0xff);
    for (int i = 0; i < self->n_objects; i++) self->objects[i]->has_recv_shadow = false;
    for (int i = 0; i < self->n_objects; i++) {
        Object *a = self->objects[i];
        if (a->hidden) continue;
        const int ay = a->y;
        for (int j = 0; j < self->n_objects; j++) {
            Object *b = self->objects[j];
            if (i == j) continue;
            Block *bb = b->block;
            if (bb->shadow_mode == 0) continue;
            const int bsy = bb->size_y;
            if (bsy + b->y > ay) continue;
            const int ax = a->x;
            const int az = a->z;
            Block *ab = a->block;
            if (b->x < ab->size_x + ax && ax < bb->size_x + b->x && b->z < ab->size_z + az &&
                az < bb->size_z + b->z) {
                const int sx = (ax - ab->size_z) - az;
                const int sy = ((az + ax) >> 1) + ab->shadow->h + bsy + b->y;
                if (!b->has_recv_shadow) {
                    b->has_recv_shadow = true;
                    clear_to_color(b->recv_shadow, mask);
                }
                draw_sprite(b->recv_shadow, a->cur_shadow, sx - b->scr_x, b->scr_y - sy);
            }
        }
    }
    for (int i = 0; i < self->n_objects; i++) {
        Object *obj = self->objects[i];
        if (obj->has_recv_shadow && obj->block->shadow_mode == 1) object_shadow_extend_sides(obj);
    }
}

// 0x004138d0 Room_invalidate_objects
void Room_invalidate_objects(Room *self) {
    for (int i = 0; i < self->n_objects; i++) {
        Object *obj = self->objects[i];
        obj->dirty = true;
        obj->has_recv_shadow = false;
        obj->overlap_count = 0;
    }
}

// 0x00413910 object_shadow_extend_sides (ret 4; ecx ignorado): blocos com shadow_mode 1 so
// recebem sombra no topo. Percorre a borda inferior do losango do topo no recv_shadow (lado
// esquerdo: colunas 0..size_z-1 descendo 1 linha a cada 2 colunas; lado direito: colunas
// sprite_w-1..sprite_w-size_x) e pinta a coluna dali ate o fim do bitmap de preto se a borda tem
// sombra, ou de magenta se nao tem.
void object_shadow_extend_sides(Object *obj) {
    const uint16_t mask = mask_color16();
    const uint16_t black = static_cast<uint16_t>(makecol(0, 0, 0));
    Block *block = obj->block;
    BITMAP *s = obj->recv_shadow;
    const int sprite_w = block->sprite_w;
    const int size_x = block->size_x;
    const int size_z = block->size_z;
    const int sprite_h = block->sprite_h;
    // port: o original le s->line[row][col] sem testar limites; quando a borda chega ao fim do
    // bitmap (row == h, ou col == -1 na ultima coluna) a cor lida nunca e usada (nao sobra linha
    // nem coluna a pintar), entao fora dos limites devolve-se magenta sem ler fora da memoria.
    auto edge_is_mask = [&](int row, int col) {
        if (row < 0 || row >= s->h || col < 0 || col >= s->w) return true;
        return s->line[row][col] == mask;
    };

    int step = 0;
    int row = size_x >> 1;
    uint16_t color = edge_is_mask(row, 0) ? mask : black;
    for (int col = 0; col < size_z; col++) {
        for (int k = row; k < sprite_h; k++) s->line[k][col] = color;
        if (++step == 2) {
            row++;
            step = 0;
            color = edge_is_mask(row, col + 1) ? mask : black;
        }
    }

    step = 0;
    row = size_z >> 1;
    const int limit = sprite_w - size_x;
    color = edge_is_mask(row, sprite_w - 1) ? mask : black;
    for (int col = sprite_w - 1; col >= limit; col--) {
        for (int k = row; k < sprite_h; k++) s->line[k][col] = color;
        if (++step == 2) {
            row++;
            step = 0;
            color = edge_is_mask(row, col - 1) ? mask : black;
        }
    }
}

// 0x00413b30 Room_draw_floor_shadows_buffered (SHADOWS COMPLEX): shadow_buf marca os pixels ja
// escurecidos, para que sombras sobrepostas nao escurecam duas vezes.
void Room_draw_floor_shadows_buffered(Room *self, BITMAP *dst, BITMAP *shadow_buf) {
    clear_to_color(shadow_buf, makecol(0xff, 0, 0xff));
    for (int i = 0; i < self->n_objects; i++) {
        if (!self->objects[i]->hidden) Room_draw_floor_shadow_buffered(self, i, dst, shadow_buf);
    }
}

// 0x00413ba0 Room_draw_floor_shadow_buffered: silhueta cur_shadow do objeto index em
// (origin_x + shadow_scr_x, origin_y - shadow_scr_y); onde a silhueta e opaca e shadow_buf ainda e
// magenta, escurece o pixel de dst para 19/32 e marca shadow_buf de preto. Recorte como em
// draw_sprite_lit (a altura usa o y original).
void Room_draw_floor_shadow_buffered(Room *self, int index, BITMAP *dst, BITMAP *shadow_buf) {
    const uint16_t mask = mask_color16();
    const uint16_t black = static_cast<uint16_t>(makecol(0, 0, 0));
    int sy0 = 0;
    int sx0 = 0;
    Object *obj = self->objects[index];
    BITMAP *s = obj->cur_shadow;
    int x = obj->shadow_scr_x + self->origin_x;
    const int y_orig = self->origin_y - obj->shadow_scr_y;
    int w = s->w;
    int h = s->h;
    int y = y_orig;
    if (y_orig < 0) {
        y = 0;
        sy0 = -y_orig;
    }
    if (x < 0) {
        sx0 = -x;
        x = 0;
    }
    if (dst->h < h + y_orig) h = dst->h - y_orig;
    if (dst->w < w + x) w = dst->w - x;
    for (int j = sy0; j < h; j++) {
        const uint16_t *sp = s->line[j];
        uint16_t *d = dst->line[y + (j - sy0)];
        uint16_t *b = shadow_buf->line[y + (j - sy0)];  // mesma linha de dst no buffer
        for (int i = sx0; i < w; i++) {
            const int dx = x + (i - sx0);
            if (sp[i] != mask && b[dx] == mask) {
                d[dx] = scale565(d[dx], 0x13);
                b[dx] = black;
            }
        }
    }
}

// 0x00413f60 Room_draw_floor: projeta a grade de piso (rows = W + B linhas x cols = D + B
// colunas). first_pass: so celulas com floor_height 0 (e recalcula a luz se a sala e escura);
// senao so as com floor_height > 0.
void Room_draw_floor(Room *self, BITMAP *dst, bool first_pass) {
    const int cols = self->depth + self->border;
    const int tile = self->tile;
    const int rows = self->width + self->border;
    const int half = tile >> 1;
    int row_x = tile * rows + (self->origin_x - tile * cols) - 8;
    int row_y = self->origin_y - ((tile * cols + tile * rows) >> 1);
    if (self->dark && first_pass) Room_light_floor(self, rows, cols, tile);
    for (int r = 0; r < rows; r++) {
        int dx = 0;
        int dy = 0;
        for (int c = 0; c < cols; c++) {
            const int height = self->floor_height[r][c];
            Block *cell = self->floor_cell[r][c];
            if (cell != nullptr && (first_pass ? height == 0 : height > 0)) {
                BITMAP *img = cell->animated ? cell->frames[0][self->floor_anim_frame[r][c]]
                                             : cell->image;
                if (!self->dark) {
                    draw_sprite(dst, img, row_x + dx, (dy - height) + row_y);
                } else {
                    draw_sprite_lit(dst, img, dx + row_x, (dy - height) + row_y,
                                    self->floor_light[r][c]);
                }
            }
            dx += self->tile;
            dy += half;
        }
        row_y += half;
        row_x -= self->tile;
    }
}

// 0x00414a60 Room_draw_walls: celulas das duas paredes, sx = ox + x - z, sy = oy - (x+z)/2 - 8k.
// Parede 0 (ao longo de x, z = wall0_z): base sprite_h da celula; parede 1 (ao longo de z, x =
// wall1_x): base size_y (original: assimetrico, e x inicial da parede 0 usa W - 1 + B, o z inicial
// da parede 1 usa D + B).
void Room_draw_walls(Room *self, BITMAP *dst) {
    int px = (self->width - 1 + self->border) * self->tile;
    if (self->dark) Room_light_walls(self, self->tile);
    for (int p = 0; p < self->width + self->border; p++) {
        const int n = self->wall0_height[p];
        for (int k = 0; k < n; k++) {
            Block *cell = self->wall0_cell[p][k];
            if (cell == nullptr) continue;
            const int z = self->wall0_z[p][k];
            const int sx = (self->origin_x - z) + px;
            const int sy = (self->origin_y - ((px + z) >> 1)) - k * 8;
            BITMAP *img = cell->animated ? cell->frames[0][self->wall0_anim_frame[p][k]] : cell->image;
            if (!self->dark) {
                draw_sprite(dst, img, sx, sy - cell->sprite_h);
            } else {
                draw_sprite_lit(dst, img, sx, sy - cell->sprite_h, self->wall0_light[p][k]);
            }
        }
        px -= self->tile;
    }
    int pz = (self->depth + self->border) * self->tile;
    for (int p = 0; p < self->depth + self->border; p++) {
        const int n = self->wall1_height[p];
        for (int k = 0; k < n; k++) {
            Block *cell = self->wall1_cell[p][k];
            if (cell == nullptr) continue;
            const int x = self->wall1_x[p][k];
            const int sx = (self->origin_x - pz) + x;
            const int sy = (self->origin_y - ((pz + x) >> 1)) - k * 8;
            BITMAP *img = cell->animated ? cell->frames[0][self->wall1_anim_frame[p][k]] : cell->image;
            if (!self->dark) {
                draw_sprite(dst, img, sx, sy - cell->size_y);
            } else {
                draw_sprite_lit(dst, img, sx, sy - cell->size_y, self->wall1_light[p][k]);
            }
        }
        pz -= self->tile;
    }
}

// 0x00416b50 Room_draw_doors: imagem do mundo sobre as portas tipo 1 (normal) e 0 (espelhada); na
// sala escura usa a luz do 1o batente (post_obj).
void Room_draw_doors(Room *self, BITMAP *dst) {
    for (int i = 0; i < self->n_doors; i++) {
        Door *door = self->doors[i];
        if (door->type == 1) {
            if (!self->dark) {
                draw_sprite(dst, door->world->image, self->origin_x + door->img_sx,
                            self->origin_y - door->img_sy);
            } else {
                draw_sprite_lit(dst, door->world->image, self->origin_x + door->img_sx,
                                self->origin_y - door->img_sy, door->post_obj->light);
            }
        }
        if (door->type == 0) {
            if (!self->dark) {
                draw_sprite_h_flip(dst, door->world->image, self->origin_x + door->img_sx,
                                   self->origin_y - door->img_sy);
            } else {
                draw_sprite_h_flip_lit(dst, door->world->image, self->origin_x + door->img_sx,
                                       self->origin_y - door->img_sy, door->post_obj->light);
            }
        }
    }
}

// 0x00416ca0 Room_draw: desenho completo da sala.
void Room_draw(Room *self, BITMAP *dst, bool shadows_on, BITMAP *shadow_buf, bool shadows_complex) {
    Room_update_attached_lights(self);
    Room_draw_floor_and_shadows(self, dst, shadows_complex, shadow_buf);
    Room_draw_walls(self, dst);
    if (self->dark) Room_light_objects(self);
    Room_draw_doors(self, dst);
    Room_draw_objects(self, dst, shadows_on, shadow_buf, shadows_complex);
    Room_draw_glows(self, dst);
}

// 0x004177c0 Room_update_camera: origem de tela = base, deslocada para seguir o jogador ativo nas
// salas maiores que a tela (scroll_x/scroll_z; view_x/z = g_view_ext_x/z) e, com ROOM-Vertical
// Scroll, mantendo o jogador a >= 15 px do topo. A sala 0x2b0 (688) nunca rola. Sempre devolve 0.
int Room_update_camera(Room *self, int view_x, int view_z) {
    if (self->id == 0x2b0) return 0;
    int ox = self->base_origin_x;
    int oy = self->base_origin_y;
    if (self->scroll_x) {
        int m = self->width_px - view_x;
        const int px = self->active_player->x;
        const int c = (self->width_px - m) >> 1;
        if (c < px) {
            if (px <= c + m) m = px - c;
            ox -= m;
            oy += m >> 1;
        }
    }
    if (self->scroll_z) {
        int m = self->depth_px - view_z;
        const int pz = self->active_player->z;
        const int c = (self->depth_px - m) >> 1;
        if (c < pz) {
            if (pz <= c + m) m = pz - c;
            ox += m;
            oy += m >> 1;
        }
    }
    if (self->vertical_scroll) {
        const int sy = self->active_player->scr_y;
        if (oy - sy < 0xf) oy = sy + 0xf;
    }
    self->origin_y = oy;
    self->origin_x = ox;
    return 0;
}

// ================================================================== iluminacao (sala escura)

// 0x00415060 light_attenuation (no exe: __thiscall com ecx nao usado, ret 0x20): distancia em
// ponto fixo como a classe `fix` do Allegro: dist = fixtoi(sqrt(fix((d2) >> 6)) << 3), luz =
// max(0, 255 - _ftol(dist * scale)) com scale = 255 / raio.
int light_attenuation(int lx, int ly, int lz, int px, int py, int pz, float scale, int unused) {
    (void)unused;
    // imul/add de 32 bits (com o mesmo estouro do exe, sem UB)
    const uint32_t dy = static_cast<uint32_t>(ly - py);
    const uint32_t dx = static_cast<uint32_t>(lx - px);
    const uint32_t dz = static_cast<uint32_t>(lz - pz);
    const int32_t d2 = static_cast<int32_t>(dz * dz + dy * dy + dx * dx);
    fixed tmp;
    fix_from_int(&tmp, d2 >> 6);
    fixed root;
    fix_sqrt(&root, tmp);
    fix_assign(&tmp, &root);
    fixed shifted;
    const fixed v = *fix_shl(&shifted, tmp, 3);
    const int dist = (v >> 16) + ((v >> 15) & 1);  // fixtoi
    // fild dist; fmul dword scale; _ftol (trunca)
    const int k = static_cast<int>(static_cast<double>(dist) * static_cast<double>(scale));
    if (0xff - k < 0) return 0;
    return 0xff - k;
}

// 0x004150f0 fix_assign (fix::operator=)
fixed *fix_assign(fixed *self, const fixed *src) {
    *self = *src;
    return self;
}

// 0x00415100 fix_from_int (fix::fix(int) = itofix: x << 16)
fixed *fix_from_int(fixed *self, int x) {
    *self = static_cast<fixed>(static_cast<uint32_t>(x) << 16);
    return self;
}

// 0x00415110 fix_shl (operator<<(fix, int))
fixed *fix_shl(fixed *out, fixed x, int shift) {
    *out = static_cast<fixed>(static_cast<uint32_t>(x) << (shift & 0x1f));
    return out;
}

// 0x004152a0 fix_sqrt (sqrt(fix) = fixsqrt)
fixed *fix_sqrt(fixed *out, fixed x) {
    *out = fixsqrt(x);
    return out;
}

// Raio efetivo da luz i: com cintilacao soma o delta sorteado (Room_light_floor) e satura em 1.
// scale = 255.0 / raio em double (fild; fdivr 255.0) guardado como float (fstp dword).
static float light_scale(const Room *self, int i) {
    int radius = self->light_radius[i];
    if (self->light_flicker[i]) {
        radius += self->light_flicker_delta[i];
        if (radius < 1) radius = 1;
    }
    return static_cast<float>(255.0 / static_cast<double>(radius));
}

// 0x00415130 Room_light_objects: obj->light = ambiente + luzes ligadas no centro da caixa.
void Room_light_objects(Room *self) {
    for (int i = 0; i < self->n_objects; i++) self->objects[i]->light = self->ambient;
    for (int l = 0; l < self->n_lights; l++) {
        if (!self->light_on[l]) continue;
        const int lz = self->light_z[l];
        const int lx = self->light_x[l];
        const int ly = self->light_y[l];
        const float scale = light_scale(self, l);
        for (int i = 0; i < self->n_objects; i++) {
            Object *obj = self->objects[i];
            if (obj->hidden) continue;
            Block *b = obj->block;
            int light = light_attenuation(lx, ly, lz, (b->size_x >> 1) + obj->x,
                                          (b->size_y >> 1) + obj->y, (b->size_z >> 1) + obj->z,
                                          scale, -1);
            light = obj->light + light;
            if (0xff < light) light = 0xff;
            obj->light = light;
        }
    }
}

// 0x004152c0 Room_light_floor(rows = W + B, cols = D + B, tile): floor_light = ambiente +
// contribuicao de cada luz no centro da celula (x = (rows-1-r)*tile + size_x/2, y = floor_height,
// z = (cols-1-c)*tile + size_z/2). Sorteia aqui o delta de cintilacao do quadro (rand()%4 - 1),
// usado depois por Room_light_walls/Room_light_objects.
void Room_light_floor(Room *self, int rows, int cols, int tile) {
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) self->floor_light[r][c] = self->ambient;
    for (int l = 0; l < self->n_lights; l++) {
        if (!self->light_on[l]) continue;
        const int lx = self->light_x[l];
        const int ly = self->light_y[l];
        const int lz = self->light_z[l];
        int radius = self->light_radius[l];
        if (self->light_flicker[l]) {
            const int delta = rand() % 4 - 1;  // and 0x80000003 + ajuste de sinal = %4
            radius += delta;
            self->light_flicker_delta[l] = delta;
            if (radius < 1) radius = 1;
        }
        const float scale = static_cast<float>(255.0 / static_cast<double>(radius));
        int cx = (rows - 1) * tile;
        for (int r = 0; r < rows; r++) {
            int cz = (cols - 1) * tile;
            for (int c = 0; c < cols; c++) {
                Block *cell = self->floor_cell[r][c];
                if (cell != nullptr) {
                    int light = light_attenuation(lx, ly, lz, (cell->size_x >> 1) + cx,
                                                  self->floor_height[r][c],
                                                  (cell->size_z >> 1) + cz, scale, -1);
                    light = self->floor_light[r][c] + light;
                    if (0xff < light) light = 0xff;
                    self->floor_light[r][c] = light;
                }
                cz -= tile;
            }
            cx -= tile;
        }
    }
}

// 0x004154c0 Room_light_walls(tile): wall0_light / wall1_light = ambiente (as 255 celulas de cada
// posicao) + luzes no centro de cada celula (parede 0: x = (W-p+B)*tile - size_x/2, z = wall0_z;
// parede 1: x = wall1_x, z = (D-p+B)*tile - size_z/2; y = size_y/2 + 8k).
void Room_light_walls(Room *self, int tile) {
    for (int p = 0; p < self->width + self->border; p++)
        for (int k = 0; k < kRoomCells; k++) self->wall0_light[p][k] = self->ambient;
    for (int p = 0; p < self->depth + self->border; p++)
        for (int k = 0; k < kRoomCells; k++) self->wall1_light[p][k] = self->ambient;
    for (int l = 0; l < self->n_lights; l++) {
        if (!self->light_on[l]) continue;
        const int lx = self->light_x[l];
        const int ly = self->light_y[l];
        const int lz = self->light_z[l];
        const float scale = light_scale(self, l);
        for (int p = 0; p < self->width + self->border; p++) {
            const int n = self->wall0_height[p];
            for (int k = 0; k < n; k++) {
                Block *cell = self->wall0_cell[p][k];
                if (cell == nullptr) continue;
                int light = light_attenuation(
                    lx, ly, lz, ((self->width - p) + self->border) * tile - (cell->size_x >> 1),
                    (cell->size_y >> 1) + k * 8, self->wall0_z[p][k], scale, -1);
                light = light + self->wall0_light[p][k];
                if (0xff < light) light = 0xff;
                self->wall0_light[p][k] = light;
            }
        }
        for (int p = 0; p < self->depth + self->border; p++) {
            const int n = self->wall1_height[p];
            for (int k = 0; k < n; k++) {
                Block *cell = self->wall1_cell[p][k];
                if (cell == nullptr) continue;
                int light = light_attenuation(
                    lx, ly, lz, self->wall1_x[p][k], (cell->size_y >> 1) + k * 8,
                    ((self->depth - p) + self->border) * tile - (cell->size_z >> 1), scale, -1);
                light = light + self->wall1_light[p][k];
                if (0xff < light) light = 0xff;
                self->wall1_light[p][k] = light;
            }
        }
    }
}

// 0x004159f0 draw_sprite_h_flip_lit: draw_sprite_lit espelhado na horizontal (mesmo recorte: so
// contra o tamanho do destino, a altura com o y original), luz em 32 avos (light >> 3).
void draw_sprite_h_flip_lit(BITMAP *dst, BITMAP *sprite, int x, int y, int light) {
    const uint32_t mask = static_cast<uint32_t>(makecol(0xff, 0, 0xff));
    const int y_orig = y;
    const int f = light >> 3;
    const int last = sprite->w - 1;
    int w = sprite->w, h = sprite->h, sx0 = 0, sy0 = 0;
    if (y < 0) {
        sy0 = -y;
        y = 0;
    }
    if (x < 0) {
        sx0 = -x;
        x = 0;
    }
    if (dst->w < x + sprite->w) w = dst->w - x;
    if (dst->h < sprite->h + y_orig) h = dst->h - y_orig;
    for (int j = sy0; j < h; j++) {
        if (sx0 >= w) continue;
        const uint16_t *s = sprite->line[j] + (last - sx0);
        uint16_t *d = dst->line[y + (j - sy0)] + x;
        for (int i = sx0; i < w; i++, s--, d++) {
            const uint32_t p = *s;
            if (p == mask) continue;
            *d = scale565(p, static_cast<uint32_t>(f));
        }
    }
}

// ================================================================== brilhos

// 0x00415e50 Room_add_glow: new Flare em glows[n_glows], ligado; devolve o indice.
int Room_add_glow(Room *self, Object *obj, int mode, int x, int y, int alpha, BITMAP *image) {
    Flare *glow = Flare_ctor(new Flare(), obj, mode, x, y, alpha, image);
    self->glows[self->n_glows] = glow;
    self->glow_on[self->n_glows] = true;
    const int index = self->n_glows;
    self->n_glows = index + 1;
    return index;
}

// 0x00415ef0 Room_draw_glows
void Room_draw_glows(Room *self, BITMAP *dst) {
    for (int i = 0; i < self->n_glows; i++) {
        if (self->glow_on[i]) Flare_draw(self->glows[i], dst, self->origin_x, self->origin_y);
    }
}

}  // namespace hoh
