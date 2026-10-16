// Modulo engine_data: classes de dados graficos do motor (Block, Behaviour, FloorSet = "Mobile",
// Named, World), auxiliares do carregador e a parte de CONSTRUCAO do load_game_data (0x402f30).
//
// Posse dos bitmaps no port: os lidos do HoHOriginal.dat pertencem ao GameData (g_data) e sao
// destruidos por ~GameData; os criados aqui (copias espelhadas, ladrilhos de piso, celulas de
// parede, mascaras) pertencem aos objetos. Os dtors abaixo sao os do original (que destruiam
// tambem os bitmaps lidos do arquivo, entao seus); engine_free_game_objects() solta dos objetos os
// bitmaps do GameData antes de chamar os dtors e libera o que o original deixava vazar.
//
// O campo vtbl dos objetos so preserva o layout de 32 bits: nao e gravado (o port nao o usa).
#include "game/engine_data.h"

#include "game/data_bridge.h"
#include "game/globals.h"
#include "game/gfx_util.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hoh {

namespace {

// _strdup do MSVC (devolve NULL para NULL); liberado com free() no Block_dtor.
char *dup_cstr(const char *s) {
    if (!s) return nullptr;
    size_t n = std::strlen(s) + 1;
    char *d = static_cast<char *>(std::malloc(n));
    if (d) std::memcpy(d, s, n);
    return d;
}

// _strcmpi do MSVC no locale "C" (so a igualdade importa aqui).
int strcmpi_c(const char *a, const char *b) {
    for (;; a++, b++) {
        int ca = static_cast<unsigned char>(*a), cb = static_cast<unsigned char>(*b);
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
}

// pack_fread de um int32 little-endian (o original lia direto num int do x86).
int32_t pack_read_i32(PACKFILE *f) {
    uint8_t b[4] = {0, 0, 0, 0};
    pack_fread(b, 4, f);
    return static_cast<int32_t>(uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) |
                                (uint32_t(b[3]) << 24));
}

bool fail(const char *what, int index) {
    std::snprintf(allegro_error, 256, "HoHOriginal.dat: %s (%d)", what, index);
    return false;
}

}  // namespace

// ================================================================== World (pecas de porta)

// 0x00401000 World_ctor
World *World_ctor(World *self) {
    return self;  // so grava a vtable 0x430238
}

// 0x00401010 World_deleting_dtor
void *World_deleting_dtor(World *self, int flags) {
    World_dtor(self);
    if (flags & 1) delete self;
    return self;
}

// 0x00401030 World_dtor
void World_dtor(World *self) {
    // original: percorre o array plano de 12 Block* a partir de +0x04 e o de +0x34 em paralelo
    for (int j = 0; j < 6; j++) {
        for (int k = 0; k < 2; k++) {
            if (self->door_part[j][k]) Block_destroy(self->door_part[j][k]);
            if (self->pillar_part[j][k]) Block_destroy(self->pillar_part[j][k]);
        }
    }
}

// 0x00401070 World_init_door_blocks
void World_init_door_blocks(World *self, int depth, BITMAP *const door_img[6],
                            BITMAP *const pillar_img[6], BITMAP *const door_shadow[6],
                            BITMAP *const pillar_shadow[6]) {
    self->depth = depth;
    for (int j = 0; j < 3; j++) {
        Block *b = Block_ctor_from_bitmap(new Block(), door_img[j], 0, depth);
        self->door_part[j][0] = b;
        b->shadow = door_shadow[j];
        b = Block_ctor_from_bitmap(new Block(), door_img[3 + j], 0, depth);
        self->door_part[3 + j][0] = b;
        b->shadow = door_shadow[3 + j];
        b = Block_ctor_from_bitmap(new Block(), pillar_img[j], j == 0, depth);
        self->pillar_part[j][0] = b;
        b->shadow = pillar_shadow[j];
        b = Block_ctor_from_bitmap(new Block(), pillar_img[3 + j], j == 0, depth);
        self->pillar_part[3 + j][0] = b;
        b->shadow = pillar_shadow[3 + j];
    }
    for (int j = 0; j < 6; j++) {
        self->door_part[j][1] = Block_ctor_mirrored(new Block(), self->door_part[j][0]);
        self->pillar_part[j][1] = Block_ctor_mirrored(new Block(), self->pillar_part[j][0]);
    }
}

// port: (***vtable)(1)
void World_destroy(World *self) { World_deleting_dtor(self, 1); }

// ================================================================== FloorSet (o "Mobile": piso)

// 0x00401910 Mobile_ctor
FloorSet *Mobile_ctor(FloorSet *self) {
    return self;  // so grava a vtable 0x430258
}

// 0x00401920 Mobile_deleting_dtor
void *Mobile_deleting_dtor(FloorSet *self, int flags) {
    Mobile_dtor(self);
    if (flags & 1) delete self;
    return self;
}

// 0x00401940 Mobile_dtor
void Mobile_dtor(FloorSet *self) {
    if (self->tile_mask) destroy_bitmap(self->tile_mask);
    if (self->frames_built == 0) self->frames_built = 1;  // original: sem efeito visivel
    for (int i = 0; i < self->nframes && i < kSetMaxFrames; i++) {
        if (self->src_frames[i]) destroy_bitmap(self->src_frames[i]);
        // original: laco vazio de n iteracoes aqui (os ladrilhos e seus Blocks nunca eram
        // liberados); no port engine_free_game_objects os libera.
    }
}

// 0x004019b0 Mobile_build_static
void Mobile_build_static(FloorSet *self) {
    int w = self->src_frames[0]->w;
    self->w = w;
    int h = self->src_frames[0]->h;
    self->h = h;
    int depth = h - (w >> 1);
    self->depth = depth;
    if (depth < 0) self->depth = 1;
    self->frames_built = 0;
    self->n = w / 16;
    Mobile_create_tile_mask(self);
    Mobile_cut_tiles(self, 0, self->flip);
    Mobile_create_tile_blocks(self);
}

// 0x00401a20 Mobile_build_animated
void Mobile_build_animated(FloorSet *self, int nframes) {
    int w = self->src_frames[0]->w;
    self->w = w;
    int h = self->src_frames[0]->h;
    self->h = h;
    int depth = h - (w >> 1);
    self->depth = depth;
    if (depth < 0) self->depth = 1;
    self->n = w / 16;
    Mobile_create_tile_mask(self);
    for (int f = 0; f < nframes; f++) {
        Mobile_cut_tiles(self, f, self->flip);
        if (f == 0) {
            Mobile_create_tile_blocks(self);
            Mobile_set_animated(self, self->speed);
        } else {
            Mobile_add_frame(self, f);
        }
    }
}

// 0x00401ab0 Mobile_create_tile_mask
// Losango 16 x (depth+8): 4 linhas de cima e 4 de baixo em degraus (2 px por linha), meio preto.
void Mobile_create_tile_mask(FloorSet *self) {
    int magenta = makecol(0xff, 0, 0xff);
    int black = makecol(0, 0, 0);
    BITMAP *mask = create_bitmap(0x10, self->depth + 8);
    self->tile_mask = mask;
    int y_bottom = self->depth + 7;
    int limit = 7;
    int y = 0;
    do {
        int xl = 0;
        int xr = 0xf;
        do {
            int color = magenta;
            if (limit <= xl) color = black;
            putpixel(mask, xl, y, color);
            putpixel(mask, xr, y, color);
            putpixel(mask, xl, y_bottom, color);
            putpixel(mask, xr, y_bottom, color);
            xl++;
            xr--;
        } while (7 < xr);
        limit -= 2;
        y++;
        y_bottom--;
    } while (y < 4);
    rectfill(mask, 0, 4, 0xf, self->depth + 3, black);
}

// 0x00401bd0 bitmap_apply_mask
// Pixel de bmp vira magenta onde mask e magenta (mask do mesmo tamanho, ou maior, que bmp).
void bitmap_apply_mask(BITMAP *bmp, BITMAP *mask) {
    const uint16_t magenta = static_cast<uint16_t>(makecol(0xff, 0, 0xff));
    int w = bmp->w;
    for (int y = 0; y < bmp->h; y++) {
        uint16_t *row = bmp->line[y];
        const uint16_t *mrow = mask->line[y];
        for (int x = 0; x < w; x++)
            if (mrow[x] == magenta) row[x] = magenta;
    }
}

// 0x00401c40 Mobile_cut_tiles
// Ladrilho [lin][col] = recorte 16 x (depth+8) em (w/2 - 8 - 8*lin + 8*col, 4*lin + 4*col).
void Mobile_cut_tiles(FloorSet *self, int frame, bool flip) {
    int x_row = self->w / 2;
    int y_row = 0;
    for (int row = 0; row < self->n; row++) {
        x_row -= 8;
        int x = x_row;
        int y = y_row;
        for (int col = 0; col < self->n; col++) {
            BITMAP *tile = create_bitmap(0x10, self->depth + 8);
            self->tile_bmp[row][col][frame] = tile;
            blit(self->src_frames[frame], tile, x, y, 0, 0, 0x10, self->depth + 8);
            bitmap_apply_mask(tile, self->tile_mask);
            // Ladrilhos internos recebem as faces laterais da borda (as da borda ja as tem).
            if (col < self->n - 1)
                Mobile_copy_right_face(self, tile, flip ? (self->n - row) - 1 : row, frame);
            if (row < self->n - 1)
                Mobile_copy_left_face(self, tile, flip ? (self->n - col) - 1 : col, frame);
            y += 4;
            x += 8;
        }
        y_row += 4;
    }
}

// 0x00401da0 Mobile_copy_left_face
// Copia a faixa da borda inferior-esquerda da imagem-fonte para x 0..7 do ladrilho.
void Mobile_copy_left_face(FloorSet *self, BITMAP *tile, int tile_index, int frame) {
    int phase = 1;
    int n = self->n;
    int y_start = 4;
    int y_neg = -4;
    int x = 0;
    do {
        int y = y_start;
        if (0 < self->depth) {
            do {
                BITMAP *src = self->src_frames[frame];
                int c = getpixel(src, x + tile_index * 8, (n << 3) / 2 + tile_index * 4 - 4 + y);
                putpixel(tile, x, y, c);
                y++;
            } while (y_neg + y < self->depth);
        }
        phase++;
        if (phase == 2) {
            y_start++;
            y_neg--;
            phase = 0;
        }
        x++;
    } while (x < 8);
}

// 0x00401e70 Mobile_copy_right_face
// Idem para a borda inferior-direita, gravando em x 8..15 do ladrilho.
void Mobile_copy_right_face(FloorSet *self, BITMAP *tile, int tile_index, int frame) {
    int phase = 1;
    int y_start = 8;
    int y_neg = -8;
    int w = self->w;
    int n = self->n;
    int x = 8;
    do {
        int y = y_start;
        if (0 < self->depth) {
            do {
                BITMAP *src = self->src_frames[frame];
                int c = getpixel(src, (w - (tile_index * 8 + 8)) - 8 + x,
                                 (n * 8 + 8) / 2 + tile_index * 4 - 8 + y);
                putpixel(tile, x, y, c);
                y++;
            } while (y_neg + y < self->depth);
        }
        phase++;
        if (phase == 2) {
            y_start--;
            y_neg++;
            phase = 0;
        }
        x++;
    } while (x - 8 < 8);  // original: x de 8 a 15
}

// 0x00401f50 Mobile_create_tile_blocks
void Mobile_create_tile_blocks(FloorSet *self) {
    for (int row = 0; row < self->n; row++)
        for (int col = 0; col < self->n; col++)
            self->tile_block[row][col] =
                Block_ctor_from_bitmap_auto(new Block(), self->tile_bmp[row][col][0], 2);
}

// 0x00402020 Mobile_set_animated
void Mobile_set_animated(FloorSet *self, int speed) {
    self->animated = true;
    self->frames_built = 1;
    self->speed = speed;
    for (int row = 0; row < self->n; row++)
        for (int col = 0; col < self->n; col++) Block_make_animated(self->tile_block[row][col], speed);
}

// 0x004020a0 Mobile_add_frame
void Mobile_add_frame(FloorSet *self, int frame) {
    for (int row = 0; row < self->n; row++) {
        for (int col = 0; col < self->n; col++) {
            // port: guarda de indice (o original leria a casa vizinha do array); a validacao de
            // engine_build_game_objects garante 0 <= frame < 20 com os dados do jogo.
            BITMAP *bmp = (frame >= 0 && frame < kSetMaxFrames) ? self->tile_bmp[row][col][frame] : nullptr;
            Block_append_frame(self->tile_block[row][col], 0, bmp, nullptr);
        }
    }
    self->frames_built++;
}

// port: (***vtable)(1)
void Mobile_destroy(FloorSet *self) { Mobile_deleting_dtor(self, 1); }

// ================================================================== carregador (load_game_data)

// 0x00402ce0 read_string
// original: le int32 len + len bytes, SEM terminador (os buffers de destino eram globais zerados).
void read_string(char *dst, PACKFILE *f) {
    int32_t len = pack_read_i32(f);
    pack_fread(dst, len, f);
}

// 0x00402d10 read_behaviour_table
void read_behaviour_table(Behaviour *dst, PACKFILE *f) {
    uint8_t a = 0, b = 0;
    pack_fread(&a, 1, f);
    pack_fread(&b, 1, f);
    dst->inherit = b != 0;
    if (b == 0 || a != 0) {
        int32_t n = pack_read_i32(f);
        dst->count = n;
        for (int i = 0; i < n; i++) {
            int32_t id = pack_read_i32(f);
            if (i < kBehaviourMaxAttrs) dst->attr_id[i] = id;
            for (int k = 0; k < kBehaviourParams; k++) {
                int32_t v = pack_read_i32(f);
                if (i < kBehaviourMaxAttrs) dst->params[i][k] = v;
            }
        }
        // port: o original nao limitava (transbordaria a tabela de 255)
        if (dst->count > kBehaviourMaxAttrs) dst->count = kBehaviourMaxAttrs;
    }
}

// 0x00402e40 map_attribute_names
// g_attr_map[i] = indice (na tabela do exe 0x431038) do nome do atributo i do arquivo.
void map_attribute_names() {
    const int slots = static_cast<int>(sizeof g_attr_names / 0xff);
    for (int i = 0; i < g_attr_count && i < slots && i < 255; i++) {
        const char *name = &g_attr_names[i * 0xff];
        for (int k = 0; k < 35; k++)
            if (strcmpi_c(name, g_attr_name_table[k]) == 0) g_attr_map[i] = k;  // sem break: vale o ultimo
    }
}

// 0x00402eb0 fix_bitmap_colors
void fix_bitmap_colors(BITMAP *bmp) {
    if (g_convert_colors != 0) {
        for (int x = 0; x < bmp->w; x++) {
            for (int y = 0; y < bmp->h; y++) {
                int c = getpixel(bmp, x, y);
                putpixel(bmp, x, y, makecol(getr(c), getg(c), getb(c)));
            }
        }
    }
}

// ================================================================== Block

// 0x0040f700 Block_ctor
Block *Block_ctor(Block *self) {
    self->image = nullptr;
    self->shadow = nullptr;
    self->name = nullptr;
    self->hidden = false;
    self->id = -1;
    return self;
}

// 0x0040f730 Block_deleting_dtor
void *Block_deleting_dtor(Block *self, int flags) {
    Block_dtor(self);
    if (flags & 1) delete self;
    return self;
}

// 0x0040f750 Block_dtor
void Block_dtor(Block *self) {
    if (self->name) std::free(self->name);
    if (!self->animated) {
        if (self->image) destroy_bitmap(self->image);
        if (self->shadow) destroy_bitmap(self->shadow);
    } else {
        for (int a = 0; a < self->anim_count && a < kBlockMaxAnims; a++) {
            for (int f = 0; f < self->owned_count[a] && f < kBlockMaxFrames; f++) {
                if (self->frames[a][f]) destroy_bitmap(self->frames[a][f]);
                if (self->shadows[a][f]) destroy_bitmap(self->shadows[a][f]);
            }
        }
    }
}

// 0x0040f830 Block_ctor_mirrored
// Copia espelhada: troca size_x/size_z e espelha cada bitmap (todos com o tamanho do 1o quadro).
Block *Block_ctor_mirrored(Block *self, const Block *src) {
    int w = src->image->w;
    int h = src->image->h;
    int shadow_h = src->shadow ? src->shadow->h : 0;  // port: guarda (o original exigia sombra)
    self->name = dup_cstr(src->name);
    self->shadow_mode = src->shadow_mode;
    self->hidden = src->hidden;
    self->size_x = src->size_z;
    self->size_y = src->size_y;
    self->size_z = src->size_x;
    self->sprite_w = src->sprite_w;
    self->sprite_h = src->sprite_h;
    self->animated = src->animated;
    self->anim_count = src->anim_count;
    if (!self->animated) {
        self->image = create_bitmap(w, h);
        bitmap_make_h_flip(src->image, self->image);
        self->shadow = src->shadow ? create_bitmap(w, shadow_h) : nullptr;
        if (self->shadow) bitmap_make_h_flip(src->shadow, self->shadow);
    } else {
        for (int a = 0; a < self->anim_count && a < kBlockMaxAnims; a++) {
            self->anim_speed[a] = src->anim_speed[a];
            self->frame_count[a] = src->frame_count[a];
            self->owned_count[a] = src->owned_count[a];
            // original: copia TODOS os frame_count[a] quadros (inclusive os repetidos de hold e
            // ping-pong, cada um uma copia nova), mas o dtor so destroi owned_count[a].
            for (int f = 0; f < self->frame_count[a] && f < kBlockMaxFrames; f++) {
                self->frames[a][f] = create_bitmap(w, h);
                bitmap_make_h_flip(src->frames[a][f], self->frames[a][f]);
                if (src->shadows[a][f]) {  // port: guarda
                    self->shadows[a][f] = create_bitmap(w, shadow_h);
                    bitmap_make_h_flip(src->shadows[a][f], self->shadows[a][f]);
                }
            }
        }
        self->image = self->frames[0][0];
        self->shadow = self->shadows[0][0];
    }
    self->id = src->id;
    return self;
}

// 0x0040fa30 Block_ctor_from_bitmap
Block *Block_ctor_from_bitmap(Block *self, BITMAP *image, int shadow_mode, int size_x) {
    self->image = image;
    self->shadow = nullptr;
    self->name = dup_cstr(g_empty_name);
    Block_init_from_bitmap(self, shadow_mode);
    Block_calc_size(self, size_x);
    return self;
}

// 0x0040fa80 Block_ctor_from_bitmap_auto
Block *Block_ctor_from_bitmap_auto(Block *self, BITMAP *image, int shadow_mode) {
    self->image = image;
    self->shadow = nullptr;
    self->name = dup_cstr(g_empty_name);
    Block_init_from_bitmap(self, shadow_mode);
    Block_calc_size_auto(self);
    return self;
}

// 0x0040fad0 Block_calc_size_auto
void Block_calc_size_auto(Block *self) {
    int half = self->sprite_w >> 1;
    self->size_y = self->sprite_h - half;
    self->size_x = half;
    self->size_z = self->sprite_w - half;
}

// 0x0040faf0 Block_calc_size
void Block_calc_size(Block *self, int size_x) {
    self->size_x = size_x;
    self->size_z = self->sprite_w - size_x;
    self->size_y = self->sprite_h - (self->sprite_w >> 1);
}

// 0x0040fb10 Block_init_from_bitmap
void Block_init_from_bitmap(Block *self, int shadow_mode) {
    self->sprite_w = self->image->w;
    self->sprite_h = self->image->h;
    self->shadow_mode = shadow_mode;
    self->animated = false;
    self->anim_count = 0;
    self->hidden = false;
}

// 0x0040fb40 Block_make_animated
void Block_make_animated(Block *self, int speed) {
    self->animated = true;
    self->anim_count = 0;
    Block_add_anim(self, speed);
    Block_append_frame_from_block(self, 0, self);
}

// 0x0040fb70 Block_add_anim
int Block_add_anim(Block *self, int speed) {
    int a = self->anim_count;
    if (a >= 0 && a < kBlockMaxAnims) {  // port: guarda de indice
        self->frame_count[a] = 0;
        self->anim_speed[a] = speed;
    }
    self->anim_count = a + 1;
    return a;
}

// 0x0040fba0 Block_set_frame
void Block_set_frame(Block *self, int anim, int frame, BITMAP *image, BITMAP *shadow) {
    // port: guarda de indice; o original grava em +0x1c0 + (anim*20 + frame)*4, e um frame >= 20
    // cairia na animacao seguinte (nao ocorre com os dados do jogo: validado no carregamento).
    if (anim < 0 || anim >= kBlockMaxAnims || frame < 0 || frame >= kBlockMaxFrames) return;
    self->frames[anim][frame] = image;
    self->shadows[anim][frame] = shadow;
}

// 0x0040fbd0 Block_append_frame
void Block_append_frame(Block *self, int anim, BITMAP *image, BITMAP *shadow) {
    if (anim < 0 || anim >= kBlockMaxAnims) return;  // port: guarda de indice
    Block_set_frame(self, anim, self->frame_count[anim], image, shadow);
    self->frame_count[anim]++;
}

// 0x0040fc00 Block_append_frame_from_block
void Block_append_frame_from_block(Block *self, int anim, const Block *src) {
    if (anim < 0 || anim >= kBlockMaxAnims) return;  // port: guarda de indice
    Block_set_frame(self, anim, self->frame_count[anim], src->image, src->shadow);
    self->frame_count[anim]++;
}

// port: (***vtable)(1)
void Block_destroy(Block *self) { Block_deleting_dtor(self, 1); }

// ================================================================== Behaviour

// 0x00412df0 Behaviour_ctor
Behaviour *Behaviour_ctor(Behaviour *self) {
    return self;  // so grava a vtable 0x4302f0
}

// 0x00412e00 Behaviour_scalar_dtor
void *Behaviour_scalar_dtor(Behaviour *self, int flags) {
    Behaviour_dtor(self);
    if (flags & 1) delete self;
    return self;
}

// 0x00412e20 Behaviour_dtor
void Behaviour_dtor(Behaviour *self) {
    (void)self;  // vazio (so restaura a vtable)
}

// port: (***vtable)(1)
void Behaviour_destroy(Behaviour *self) { Behaviour_scalar_dtor(self, 1); }

// ================================================================== Named (paredes)

// 0x00417a80 Named_ctor
Named *Named_ctor(Named *self) {
    return self;  // so grava a vtable 0x4302fc
}

// 0x00417a90 Named_scalar_dtor
void *Named_scalar_dtor(Named *self, int flags) {
    Named_dtor(self);
    if (flags & 1) delete self;
    return self;
}

// 0x00417ab0 Named_dtor
void Named_dtor(Named *self) {
    if (self->cell_mask) destroy_bitmap(self->cell_mask);
    if (self->frames_built == 0) self->frames_built = 1;  // original: sem efeito visivel
    for (int i = 0; i < self->nframes && i < kSetMaxFrames; i++) {
        if (self->src_frames[i]) destroy_bitmap(self->src_frames[i]);
        for (int c = 0; c < self->cols && c < kSetMaxTiles; c++) {
            for (int r = 0; r < self->rows && r < kSetMaxTiles; r++) {
                if (self->cells_a[c][r][i]) destroy_bitmap(self->cells_a[c][r][i]);
                if (self->cells_b[c][r][i]) destroy_bitmap(self->cells_b[c][r][i]);
            }
        }
    }
}

// 0x00417ba0 Named_build
void Named_build(Named *self) {
    int w = self->src_frames[0]->w;
    self->w = w;
    self->h = self->src_frames[0]->h;
    self->animated = false;
    self->frames_built = 0;
    self->cols = w / 8;
    self->rows = (self->h - (w >> 1)) / 8;
    Named_create_cell_mask(self);
    Named_slice_frame(self, 0);
    Named_create_cell_blocks(self);
}

// 0x00417c10 Named_build_animated
void Named_build_animated(Named *self, int nframes) {
    int w = self->src_frames[0]->w;
    self->w = w;
    self->h = self->src_frames[0]->h;
    self->cols = w / 8;
    self->rows = (self->h - (w >> 1)) / 8;
    Named_create_cell_mask(self);
    for (int f = 0; f < nframes; f++) {
        Named_slice_frame(self, f);
        if (f == 0) {
            Named_create_cell_blocks(self);
            Named_set_animated(self, self->speed);
        } else {
            Named_add_frame(self, f);
        }
    }
}

// 0x00417ce0 Named_create_cell_mask
// 8x12 preto com triangulos magenta nos cantos superior-esquerdo e inferior-direito.
void Named_create_cell_mask(Named *self) {
    int magenta = makecol(0xff, 0, 0xff);
    int black = makecol(0, 0, 0);
    BITMAP *mask = create_bitmap(8, 0xc);
    self->cell_mask = mask;
    clear_to_color(mask, black);
    int limit = 7;
    int y = 0;
    int y_bottom = self->cell_mask->h;
    do {
        y_bottom--;
        int x = 0;
        if (0 < limit) {
            int xr = 7;
            do {
                putpixel(self->cell_mask, x, y, magenta);
                putpixel(self->cell_mask, xr, y_bottom, magenta);
                x++;
                xr--;
            } while (x < limit);
        }
        limit -= 2;
        y++;
    } while (y < 4);
}

// 0x00417db0 Named_slice_frame
// Celula (coluna col, linha row da imagem) = recorte 8x12 em (8*col, w/2 - 4 + 8*row - 4*col);
// guardada invertida: cells_a[cols-1-col][rows-1-row]; cells_b = espelho horizontal.
void Named_slice_frame(Named *self, int frame) {
    int y_row = (self->w >> 1) - 4;
    for (int row = 0; row < self->rows; row++) {
        int x = 0;
        int y = y_row;
        for (int col = 0; col < self->cols; col++) {
            BITMAP *a = create_bitmap(self->cell_mask->w, self->cell_mask->h);
            blit(self->src_frames[frame], a, x, y, 0, 0, self->cell_mask->w, self->cell_mask->h);
            bitmap_apply_mask(a, self->cell_mask);
            int c = (self->cols - col) - 1;
            int r = (self->rows - row) - 1;
            self->cells_a[c][r][frame] = a;
            BITMAP *b = create_bitmap(self->cell_mask->w, self->cell_mask->h);
            self->cells_b[c][r][frame] = b;
            bitmap_make_h_flip(a, self->cells_b[c][r][frame]);
            x += 8;
            y -= 4;
        }
        y_row += 8;
    }
}

// 0x00417f10 Named_create_cell_blocks
void Named_create_cell_blocks(Named *self) {
    for (int c = 0; c < self->cols; c++) {
        for (int r = 0; r < self->rows; r++) {
            self->blocks_a[c][r] = Block_ctor_from_bitmap(new Block(), self->cells_a[c][r][0], 2, 8);
            self->blocks_b[c][r] = Block_ctor_from_bitmap(new Block(), self->cells_b[c][r][0], 2, 0);
        }
    }
}

// 0x00418030 Named_set_animated
void Named_set_animated(Named *self, int speed) {
    self->animated = true;
    self->frames_built = 1;
    self->speed = speed;
    for (int c = 0; c < self->cols; c++) {
        for (int r = 0; r < self->rows; r++) {
            Block_make_animated(self->blocks_a[c][r], speed);
            Block_make_animated(self->blocks_b[c][r], speed);
        }
    }
}

// 0x004180c0 Named_add_frame
void Named_add_frame(Named *self, int frame) {
    for (int c = 0; c < self->cols; c++) {
        for (int r = 0; r < self->rows; r++) {
            // port: guarda de indice (ver Mobile_add_frame)
            bool ok = frame >= 0 && frame < kSetMaxFrames;
            Block_append_frame(self->blocks_a[c][r], 0, ok ? self->cells_a[c][r][frame] : nullptr, nullptr);
            Block_append_frame(self->blocks_b[c][r], 0, ok ? self->cells_b[c][r][frame] : nullptr, nullptr);
        }
    }
    self->frames_built++;
}

// port: (***vtable)(1)
void Named_destroy(Named *self) { Named_scalar_dtor(self, 1); }

// ================================================================== port: construcao/liberacao

namespace {

// Quadros que o carregador anexa alem dos lidos: hold (repeat x o ultimo) e ping-pong (n-2..1).
int extra_frames(int nframes, int repeat, bool pingpong) {
    int k = repeat > 0 ? repeat : 0;
    if (pingpong && nframes > 2) k += nframes - 2;
    return k;
}

// Limites dos arrays inline (o original nao conferia: transbordaria). Tudo conferido antes de
// construir qualquer objeto, para nao deixar estado parcial.
bool validate(const GameData &d) {
    if (g_attr_count < 0 || g_attr_count > static_cast<int>(sizeof g_attr_names / 0xff))
        return fail("atributos demais", g_attr_count);
    if (d.blocks.size() > 255) return fail("blocos demais", static_cast<int>(d.blocks.size()));
    for (size_t i = 0; i < d.blocks.size(); i++) {
        const BlockDef &b = d.blocks[i];
        int anims = b.multi ? b.ndirs_raw : 1;
        if (anims < 1 || anims > kBlockMaxAnims || static_cast<int>(b.dirs.size()) != anims)
            return fail("bloco com n de animacoes invalido", static_cast<int>(i));
        for (const BlockDir &dir : b.dirs) {
            if (dir.nframes < 1 || static_cast<int>(dir.frames.size()) != dir.nframes ||
                dir.nframes + extra_frames(dir.nframes, dir.repeat, dir.pingpong != 0) > kBlockMaxFrames)
                return fail("bloco com n de quadros invalido", static_cast<int>(i));
        }
        if (b.has_behaviour && b.behaviour.size() > static_cast<size_t>(kBehaviourMaxAttrs))
            return fail("bloco com atributos demais", static_cast<int>(i));
    }
    if (d.mobiles.size() > 255) return fail("pisos demais", static_cast<int>(d.mobiles.size()));
    for (size_t i = 0; i < d.mobiles.size(); i++) {
        const SpriteDef &m = d.mobiles[i];
        int n = static_cast<int>(m.frames.size());
        if (n < 1 || n > kSetMaxFrames || m.frames[0]->w / 16 > kSetMaxTiles)
            return fail("piso invalido", static_cast<int>(i));
        // Piso estatico: o original anexaria o quadro -1 (hold) ou quadros nao recortados.
        int built = m.directional ? n : 0;
        if (!m.directional && extra_frames(n, m.repeat, m.pingpong != 0) > 0)
            return fail("piso estatico com quadros extras", static_cast<int>(i));
        if (built + extra_frames(n, m.repeat, m.pingpong != 0) > kBlockMaxFrames)
            return fail("piso com quadros demais", static_cast<int>(i));
    }
    if (d.named.size() > 255) return fail("paredes demais", static_cast<int>(d.named.size()));
    for (size_t i = 0; i < d.named.size(); i++) {
        const SpriteDef &s = d.named[i];
        int n = static_cast<int>(s.frames.size());
        if (n < 1 || n > kSetMaxFrames) return fail("parede invalida", static_cast<int>(i));
        int w = s.frames[0]->w, h = s.frames[0]->h;
        if (w / 8 > kSetMaxTiles || (h - (w >> 1)) / 8 > kSetMaxTiles)
            return fail("parede grande demais", static_cast<int>(i));
        int built = s.directional ? n : 0;
        if (!s.directional && extra_frames(n, s.repeat, s.pingpong != 0) > 0)
            return fail("parede estatica com quadros extras", static_cast<int>(i));
        if (built + extra_frames(n, s.repeat, s.pingpong != 0) > kBlockMaxFrames)
            return fail("parede com quadros demais", static_cast<int>(i));
    }
    if (d.worlds.size() > 48) return fail("mundos demais", static_cast<int>(d.worlds.size()));
    return true;
}

// Equivalente a read_behaviour_table (0x402d10) sobre a tabela ja lida pelo GameData.
void behaviour_from_def(Behaviour *dst, const BlockDef &def) {
    dst->inherit = def.beh_b != 0;
    if (def.has_behaviour) {  // beh_b == 0 || beh_a != 0
        dst->count = static_cast<int>(def.behaviour.size());
        for (int i = 0; i < dst->count; i++) {
            dst->attr_id[i] = def.behaviour[i].attr;
            for (int k = 0; k < kBehaviourParams; k++) dst->params[i][k] = def.behaviour[i].param[k];
        }
    }
}

// Solta (sem destruir) os bitmaps referenciados pelo Block: pertencem ao GameData ou ao
// FloorSet/Named dono da celula. Depois disso Block_dtor so libera o nome.
void block_detach_bitmaps(Block *b) {
    b->image = nullptr;
    b->shadow = nullptr;
    for (int a = 0; a < kBlockMaxAnims; a++) {
        b->owned_count[a] = 0;
        for (int f = 0; f < kBlockMaxFrames; f++) {
            b->frames[a][f] = nullptr;
            b->shadows[a][f] = nullptr;
        }
    }
}

// Copias espelhadas dos quadros de hold/ping-pong (f >= owned_count[a]): o Block_dtor do
// original nao as destroi (vazamento); no port sao liberadas aqui.
void block_free_extra_mirrored_frames(Block *b) {
    if (!b->animated) return;
    for (int a = 0; a < b->anim_count && a < kBlockMaxAnims; a++) {
        for (int f = b->owned_count[a]; f < b->frame_count[a] && f < kBlockMaxFrames; f++) {
            if (f < 0) continue;
            if (b->frames[a][f]) destroy_bitmap(b->frames[a][f]);
            if (b->shadows[a][f]) destroy_bitmap(b->shadows[a][f]);
            b->frames[a][f] = nullptr;
            b->shadows[a][f] = nullptr;
        }
    }
}

void destroy_detached_block(Block *&b) {
    if (!b) return;
    block_detach_bitmaps(b);
    Block_destroy(b);
    b = nullptr;
}

}  // namespace

// Parte de construcao do load_game_data (0x402f30), na ordem do original, sobre o GameData ja
// lido por data_bridge (contagens, g_attr_names, g_named_names e g_images ja preenchidos).
bool engine_build_game_objects() {
    if (!g_data) return fail("dados nao carregados", 0);
    const GameData &d = *g_data;
    g_attr_count = static_cast<int32_t>(d.attr_names.size());
    if (!validate(d)) return false;
    engine_free_game_objects();  // se ja havia objetos construidos

    // atributos: os nomes ja estao em g_attr_names (data_bridge)
    map_attribute_names();

    // blocos (Block 0x21d0 + Behaviour 0x2be0 + copia espelhada)
    g_block_count = static_cast<int32_t>(d.blocks.size());
    for (int i = 0; i < g_block_count; i++) {
        const BlockDef &def = d.blocks[i];
        Block *b = Block_ctor(new Block());
        g_blocks[i] = b;
        b->id = i;
        b->shadow_mode = def.v24;
        int size_x = def.anim_ref;  // o "anim_ref" do hohdata e o size_x da caixa (-1 = automatico)
        b->animated = def.multi != 0;
        b->anim_count = def.ndirs_raw;
        if (!b->animated) b->anim_count = 1;
        int w = 0, h = 0;  // original: w/h1 da ULTIMA animacao lida
        for (int a = 0; a < b->anim_count; a++) {
            const BlockDir &dir = def.dirs[a];
            b->frame_count[a] = dir.nframes;
            b->owned_count[a] = dir.nframes;
            b->anim_speed[a] = dir.vc8;
            w = dir.w;
            h = dir.h1;
            for (int f = 0; f < b->frame_count[a]; f++) {
                BITMAP *img = dir.frames[f].a;
                BITMAP *shd = dir.frames[f].b;
                fix_bitmap_colors(img);
                fix_bitmap_colors(shd);
                b->frames[a][f] = img;
                b->shadows[a][f] = shd;
            }
            // hold: repete o ultimo quadro lido (indice calculado uma vez, antes do laco)
            int last = b->frame_count[a] - 1;
            for (int k = 0; k < dir.repeat; k++)
                Block_append_frame(b, a, b->frames[a][last], b->shadows[a][last]);
            // ping-pong: quadros n-2 .. 1 de volta
            if (dir.pingpong != 0 && 2 < b->owned_count[a]) {
                for (int f = b->owned_count[a] - 2; f != 0; f--)
                    Block_append_frame(b, a, b->frames[a][f], b->shadows[a][f]);
            }
        }
        b->image = b->frames[0][0];
        b->shadow = b->shadows[0][0];
        b->hidden = false;
        b->sprite_w = w;
        b->sprite_h = h;
        if (size_x == -1)
            Block_calc_size_auto(b);
        else
            Block_calc_size(b, size_x);
        Behaviour *beh = Behaviour_ctor(new Behaviour());
        g_block_behaviours[i] = beh;
        behaviour_from_def(beh, def);
        g_blocks_alt[i] = Block_ctor_mirrored(new Block(), g_blocks[i]);
    }

    // pisos (FloorSet 0x127c8)
    g_mobile_count = static_cast<int32_t>(d.mobiles.size());
    for (int i = 0; i < g_mobile_count; i++) {
        const SpriteDef &def = d.mobiles[i];
        FloorSet *m = Mobile_ctor(new FloorSet());
        g_mobiles[i] = m;
        m->flip = def.f0 != 0;
        m->animated = def.directional != 0;
        int nframes = static_cast<int>(def.frames.size());
        m->speed = def.v_c0;
        bool pingpong = def.pingpong != 0;
        int repeat = def.repeat;
        m->nframes = nframes;
        for (int f = 0; f < nframes; f++) {
            BITMAP *bmp = def.frames[f];
            fix_bitmap_colors(bmp);
            m->src_frames[f] = bmp;
        }
        if (!m->animated)
            Mobile_build_static(m);
        else
            Mobile_build_animated(m, nframes);
        int built = m->frames_built;
        for (int k = 0; k < repeat; k++) Mobile_add_frame(m, built - 1);
        if (pingpong && 2 < m->nframes)
            for (int f = m->nframes - 2; 0 < f; f--) Mobile_add_frame(m, f);
    }

    // paredes (Named 0x24f18); o nome ja esta em g_named_names (data_bridge)
    g_named_count = static_cast<int32_t>(d.named.size());
    for (int i = 0; i < g_named_count; i++) {
        const SpriteDef &def = d.named[i];
        Named *s = Named_ctor(new Named());
        g_named[i] = s;
        s->animated = def.directional != 0;
        int nframes = static_cast<int>(def.frames.size());
        s->speed = def.v_c0;
        bool pingpong = def.pingpong != 0;
        int repeat = def.repeat;
        s->nframes = nframes;
        for (int f = 0; f < nframes; f++) {
            BITMAP *bmp = def.frames[f];
            fix_bitmap_colors(bmp);
            s->src_frames[f] = bmp;
        }
        if (!s->animated)
            Named_build(s);
        else
            Named_build_animated(s, nframes);
        int built = s->frames_built;
        for (int k = 0; k < repeat; k++) Named_add_frame(s, built - 1);
        if (pingpong && 2 < s->nframes)
            for (int f = s->nframes - 2; 0 < f; f--) Named_add_frame(s, f);
    }

    // mundos (World 0x70): pecas P (portal) e Q (pedestal), arquivo intercala P_k, Q_k
    g_world_count = static_cast<int32_t>(d.worlds.size());
    for (int i = 0; i < g_world_count; i++) {
        const WorldDef &def = d.worlds[i];
        World *w = World_ctor(new World());
        g_worlds[i] = w;
        fix_bitmap_colors(def.main_image);
        w->image = def.main_image;
        // original: tambem registrava a imagem em g_bitmap_registry; no port pertence ao GameData.
        BITMAP *door_img[6], *door_shadow[6], *pillar_img[6], *pillar_shadow[6];
        for (int k = 0; k < 6; k++) {
            fix_bitmap_colors(def.parts[k][0].a);
            fix_bitmap_colors(def.parts[k][0].b);
            door_img[k] = def.parts[k][0].a;
            door_shadow[k] = def.parts[k][0].b;
            fix_bitmap_colors(def.parts[k][1].a);
            fix_bitmap_colors(def.parts[k][1].b);
            pillar_img[k] = def.parts[k][1].a;
            pillar_shadow[k] = def.parts[k][1].b;
        }
        World_init_door_blocks(w, def.v0, door_img, pillar_img, door_shadow, pillar_shadow);
    }

    // imagens: g_images ja aponta para elas (data_bridge); o original as registrava em
    // g_bitmap_registry (no port pertencem ao GameData).
    g_image_count = static_cast<int32_t>(d.images.size());
    for (BITMAP *img : d.images) fix_bitmap_colors(img);

    // salas. Original: g_room_blob = buffer de 900000 bytes; g_room_table = blob + len - 6400 e
    // g_start_room = *(int *)(blob + len - 6404) (tabela de 1600 offsets no fim do blob). O
    // GameData ja decodificou a mesma tabela (alinhada): g_room_table aponta para ela.
    g_room_count = d.room_count;
    g_room_blob = const_cast<uint8_t *>(d.room_blob.data());
    g_room_table = const_cast<int32_t *>(d.room_table.data());
    g_start_room = d.start_room;
    return true;
}

// Destroi o que engine_build_game_objects criou, na ordem do free_game_resources (0x40e6b0):
// por i g_blocks[i] e g_blocks_alt[i]; depois pisos, paredes e mundos. Antes de cada dtor solta
// os bitmaps do GameData; libera tambem o que o original deixava vazar (Behaviours dos blocos,
// ladrilhos e Blocks de piso, Blocks de celula de parede, copias espelhadas de hold/ping-pong).
void engine_free_game_objects() {
    for (int i = 0; i < 255; i++) {
        if (g_blocks[i]) {
            block_detach_bitmaps(g_blocks[i]);  // quadros do GameData
            Block_destroy(g_blocks[i]);
            g_blocks[i] = nullptr;
        }
        if (g_blocks_alt[i]) {
            block_free_extra_mirrored_frames(g_blocks_alt[i]);
            Block_destroy(g_blocks_alt[i]);  // destroi as copias espelhadas
            g_blocks_alt[i] = nullptr;
        }
        if (g_block_behaviours[i]) {  // original: nunca liberadas
            Behaviour_destroy(g_block_behaviours[i]);
            g_block_behaviours[i] = nullptr;
        }
    }
    for (int i = 0; i < 255; i++) {
        FloorSet *m = g_mobiles[i];
        if (!m) continue;
        for (int r = 0; r < kSetMaxTiles; r++) {
            for (int c = 0; c < kSetMaxTiles; c++) {
                destroy_detached_block(m->tile_block[r][c]);
                for (int f = 0; f < kSetMaxFrames; f++) {
                    if (m->tile_bmp[r][c][f]) destroy_bitmap(m->tile_bmp[r][c][f]);
                    m->tile_bmp[r][c][f] = nullptr;
                }
            }
        }
        for (int f = 0; f < kSetMaxFrames; f++) m->src_frames[f] = nullptr;  // do GameData
        Mobile_destroy(m);  // destroi a mascara
        g_mobiles[i] = nullptr;
    }
    for (int i = 0; i < 255; i++) {
        Named *s = g_named[i];
        if (!s) continue;
        for (int c = 0; c < kSetMaxTiles; c++) {
            for (int r = 0; r < kSetMaxTiles; r++) {
                destroy_detached_block(s->blocks_a[c][r]);  // celulas: destruidas pelo Named_dtor
                destroy_detached_block(s->blocks_b[c][r]);
            }
        }
        for (int f = 0; f < kSetMaxFrames; f++) s->src_frames[f] = nullptr;  // do GameData
        Named_destroy(s);  // destroi a mascara e as celulas A/B
        g_named[i] = nullptr;
    }
    for (int i = 0; i < 48; i++) {
        World *w = g_worlds[i];
        if (!w) continue;
        for (int j = 0; j < 6; j++) {  // pecas [j][0]: bitmaps do GameData; [j][1]: copias proprias
            if (w->door_part[j][0]) block_detach_bitmaps(w->door_part[j][0]);
            if (w->pillar_part[j][0]) block_detach_bitmaps(w->pillar_part[j][0]);
        }
        w->image = nullptr;  // do GameData (o World_dtor nao a destroi)
        World_destroy(w);
        g_worlds[i] = nullptr;
    }
    // apontavam para dentro do g_data
    g_room_blob = nullptr;
    g_room_table = nullptr;
}

}  // namespace hoh
