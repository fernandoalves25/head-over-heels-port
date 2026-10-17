// Port: idiomas (ver lang.h). As traducoes usam so os sinais que as fontes do jogo tem (letras sem
// minusculas na fonte de 18 px, ! , . - : ? [ ] e os glifos extras de lang_glyphs.cpp): sem
// parenteses nem apostrofos. Linhas do letreiro cabem em 35 colunas, as do menu em 21.
#include "game/lang.h"

#include "compat/platform.h"
#include "game/gfx_util.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>

namespace hoh {

namespace {

struct Entry {
    const char *en, *pt, *es, *zh;  // zh nulo = fica em ingles (a fonte pequena nao tem chines)
};

// clang-format off
const Entry kTable[] = {
    // ---------------------------------------------------------------- menus
    {"PLAY THE GAME", "JOGAR", "JUGAR", "开始游戏"},
    {"CHANGE CONTROLS", "MUDAR CONTROLES", "CAMBIAR CONTROLES", "更改按键"},
    {"ADJUST THE SOUND", "AJUSTAR O SOM", "AJUSTAR SONIDO", "调整声音"},
    {"GAME OPTIONS", "OPÇÕES DO JOGO", "OPCIONES DE JUEGO", "游戏选项"},
    {"QUIT TO SYSTEM", "SAIR PARA O SISTEMA", "SALIR AL SISTEMA", "退出游戏"},
    {"NEW GAME", "NOVO JOGO", "NUEVA PARTIDA", "新游戏"},
    {"OLD GAME", "JOGO SALVO", "CARGAR PARTIDA", "读取存档"},
    {"BACK", "VOLTAR", "VOLVER", "返回"},
    {"YES - QUIT", "SIM - SAIR", "SÍ - SALIR", "是 - 退出"},
    {"RESUME GAME", "CONTINUAR", "CONTINUAR", "继续游戏"},
    {"QUIT GAME", "SAIR DO JOGO", "SALIR DEL JUEGO", "退出本局"},
    {"SAVE GAME", "SALVAR JOGO", "GUARDAR PARTIDA", "保存游戏"},
    // controles (rotulos de ate 12 colunas)
    {"UP", "CIMA", "ARRIBA", "上"},
    {"DOWN", "BAIXO", "ABAJO", "下"},
    {"LEFT", "ESQUERDA", "IZQUIERDA", "左"},
    {"RIGHT", "DIREITA", "DERECHA", "右"},
    {"JUMP", "PULAR", "SALTAR", "跳跃"},
    {"CARRY", "PEGAR", "COGER", "拿取"},
    {"JUMP-CARRY", "PULAR-PEGAR", "SALTAR-COGER", "跳跃拿取"},
    {"FIRE", "ATIRAR", "DISPARAR", "射击"},
    {"SWAP", "TROCAR", "CAMBIAR", "切换"},
    {"PAUSE", "PAUSA", "PAUSA", "暂停"},
    // opcoes: rotulos
    {"SOUND MODE", "MODO DO SOM", "MODO SONIDO", "音效模式"},
    {"MUSIC VOLUME", "VOLUME MÚSICA", "VOL. MÚSICA", "音乐音量"},
    {"SOUND VOLUME", "VOLUME EFEITOS", "VOL. EFECTOS", "音效音量"},
    {"SHADOWS", "SOMBRAS", "SOMBRAS", "阴影"},
    {"GAME SPEED", "VELOCIDADE", "VELOCIDAD", "游戏速度"},
    {"GFX MODE", "VÍDEO", "VÍDEO", "显示"},
    {"TRIPLE BUFFERING", "BUFFER TRIPLO", "TRIPLE BÚFER", "三重缓冲"},
    {"C.SENSITIVITY", "SENSIBILIDADE", "SENSIBILIDAD", "灵敏度"},
    {"FRAME RATE", "QUADROS", "FOTOGRAMAS", "帧率"},
    {"LANGUAGE", "IDIOMA", "IDIOMA", "语言"},
    // opcoes: valores
    {"NASTY", "RUIM", "MALO", "差"},
    {"SO-SO", "MÉDIO", "REGULAR", "一般"},
    {"LATE", "ATRASADO", "TARDÍO", "延迟"},
    {"SIMPLE", "SIMPLES", "SIMPLES", "简单"},
    {"MEDIUM", "MÉDIAS", "MEDIAS", "中等"},
    {"COMPLEX", "COMPLEXAS", "COMPLEJAS", "复杂"},
    {"SLOW", "LENTA", "LENTA", "慢"},
    {"ORIGINAL", "ORIGINAL", "ORIGINAL", "原版"},
    {"FAST", "RÁPIDA", "RÁPIDA", "快"},
    {"WINDOWED", "JANELA", "VENTANA", "窗口"},
    {"FULL SCREEN", "TELA CHEIA", "PANT. COMPLETA", "全屏"},
    {"SPECIAL-GDI", "ESPECIAL-GDI", "ESPECIAL-GDI", "特殊-GDI"},
    {"NO", "NÃO", "NO", "否"},
    {"YES", "SIM", "SÍ", "是"},
    {"N-A", "N-D", "N-D", "不可用"},
    {"HIGH", "ALTA", "ALTA", "高"},
    {"LOW", "BAIXA", "BAJA", "低"},
    {"AUTO", "AUTO", "AUTO", "自动"},
    // slots e saves
    {"- EMPTY -", "- VAZIO -", "- VACÍO -", "- 空 -"},
    {"%2i SAVES LEFT", "%2i SALVAMENTOS", "%2i GUARDADOS", "剩余存档:%2i"},
    {" 1 SAVE LEFT", " 1 SALVAMENTO", " 1 GUARDADO", "剩余存档: 1"},
    {"NO SAVES LEFT", "SEM SALVAMENTOS", "SIN GUARDADOS", "没有存档了"},
    // ---------------------------------------------------------------- abertura, mapa, final
    {"PRESENTS...", "APRESENTA...", "PRESENTA...", "出品..."},
    {"THE BLACKTOOTH EMPIRE", "O IMPÉRIO BLACKTOOTH", "EL IMPERIO BLACKTOOTH", "黑牙帝国"},
    {"EGYPTUS", "EGYPTUS", "EGYPTUS", "埃及"},
    {"PENITENTIARY", "PENITENCIÁRIA", "PENITENCIARÍA", "监狱"},
    {"SAFARI", "SAFÁRI", "SAFARI", "狩猎"},
    {"BOOK WORLD", "MUNDO DOS LIVROS", "MUNDO LIBRO", "书之世界"},
    {"BLACKTOOTH", "BLACKTOOTH", "BLACKTOOTH", "黑牙"},
    {"GAME OVER", "FIM DE JOGO", "FIN DEL JUEGO", "游戏结束"},
    {"PEOPLE SALUTE YOU!", "O POVO TE SAÚDA!", "¡EL PUEBLO TE SALUDA!", "人民向你致敬!"},
    {"%i%% ROOMS EXPLORED.", "%i%% EXPLORADO.", "%i%% EXPLORADO.", "探索了 %i%%."},
    {"LIBERATED %i PLANETS.", "%i PLANETAS LIBERTOS.", "%i PLANETAS LIBRES.", "解放了 %i 个星球."},
    {"RATING", "CLASSIFICAÇÃO", "CLASIFICACIÓN", "评级"},
    {"DUMMY", "PATETA", "TONTO", "笨蛋"},
    {"VERY POOR", "MUITO FRACO", "MUY MALO", "很差"},
    {"POOR", "FRACO", "MALO", "差"},
    {"NOVICE", "NOVATO", "NOVATO", "新手"},
    {"LEARNER", "INICIANTE", "PRINCIPIANTE", "初学者"},
    {"APPRENTICE", "APRENDIZ", "APRENDIZ", "学徒"},
    {"SPY", "ESPIÃO", "ESPÍA", "间谍"},
    {"MASTER SPY", "MESTRE ESPIÃO", "MAESTRO ESPÍA", "王牌间谍"},
    {"HERO", "HERÓI", "HÉROE", "英雄"},
    {"EMPEROR", "IMPERADOR", "EMPERADOR", "皇帝"},
    // fonte pequena (com minusculas, sem . nem ,): o chines fica em ingles
    {"Retrospec High Score Code: %s", "Código de recorde Retrospec: %s", "Código de récord Retrospec: %s", nullptr},
    {"Press `S` To Send The Code To The Server Now!", "Aperte `S` para enviar o código ao servidor!",
     "Pulsa `S` para enviar el código al servidor!", nullptr},
    {"Enter Your Name: %s", "Digite seu nome: %s", "Escribe tu nombre: %s", nullptr},
    {"Congratulations!", "Parabéns!", "¡Enhorabuena!", nullptr},
    {"High Score Position: %i", "Posição no recorde: %i", "Posición en el récord: %i", nullptr},
    {"Error!  Press `S` to resend!", "Erro!  Aperte `S` para reenviar!", "¡Error!  Pulsa `S` para reenviar!", nullptr},
    // ---------------------------------------------------------------- letreiro de creditos
    {"WELCOME TO HEAD OVER HEELS", "BEM-VINDO AO HEAD OVER HEELS", "BIENVENIDO A HEAD OVER HEELS", "欢迎来到 HEAD OVER HEELS"},
    {"A REMAKE OF THE BEST SELLING GAME", "UM REMAKE DO JOGO CAMPEÃO DE VENDAS", "UN REMAKE DEL ÉXITO DE VENTAS", "畅销经典游戏的重制版"},
    {"FROM THE GREAT EIGHTEES.", "DOS GRANDES ANOS OITENTA.", "DE LOS GRANDES AÑOS OCHENTA.", "来自伟大的八十年代."},
    {"CODING BY TOMAZ KAC", "PROGRAMAÇÃO: TOMAZ KAC", "PROGRAMACIÓN: TOMAZ KAC", "程序: TOMAZ KAC"},
    {"GRAPHICS BY GRAHAM GORING", "GRÁFICOS: GRAHAM GORING", "GRÁFICOS: GRAHAM GORING", "美术: GRAHAM GORING"},
    {"MUSIC AND SOUND FX BY DORIAN BLACK", "MÚSICA E EFEITOS: DORIAN BLACK", "MÚSICA Y EFECTOS: DORIAN BLACK", "音乐与音效: DORIAN BLACK"},
    {"TITLE AND PDF BY PETER JOVANOVIC", "TÍTULO E PDF: PETER JOVANOVIC", "TÍTULO Y PDF: PETER JOVANOVIC", "标题与PDF: PETER JOVANOVIC"},
    {"AND ADDITIONAL SFX BY IGNACIO GIL.", "E EFEITOS EXTRAS: IGNACIO GIL.", "Y EFECTOS EXTRA: IGNACIO GIL.", "附加音效: IGNACIO GIL."},
    {"ORIGINAL GAME DESIGN BY JON RITMAN", "JOGO ORIGINAL DE JON RITMAN", "JUEGO ORIGINAL DE JON RITMAN", "原版游戏设计: JON RITMAN"},
    {"AND GRAPHICS BY BERNIE DRUMMOND.", "E BERNIE DRUMMOND NOS GRÁFICOS.", "Y BERNIE DRUMMOND EN GRÁFICOS.", "原版美术: BERNIE DRUMMOND."},
    {"THANX TO EVERYONE AT RETROSPEC", "OBRIGADO A TODOS DA RETROSPEC", "GRACIAS A TODOS EN RETROSPEC", "感谢 RETROSPEC 的所有人"},
    {"ALL MY AMIGA FRIENDS,LUND,JELLY", "AMIGOS DO AMIGA,LUND,JELLY", "AMIGOS DEL AMIGA,LUND,JELLY", "所有AMIGA朋友,LUND,JELLY"},
    {"MIKEDX, REEFA FOR ASCII LOGOS AND", "MIKEDX, REEFA PELOS LOGOS ASCII E", "MIKEDX, REEFA POR LOS LOGOS ASCII Y", "MIKEDX, REEFA 的ASCII标志, 以及"},
    {"ALL BETA TESTERS WHO I FORGOT.", "TODOS OS TESTADORES ESQUECIDOS.", "TODOS LOS PROBADORES OLVIDADOS.", "所有被我遗忘的测试者."},
    {"SPECIAL THANX TO MY GIRLFRIEND", "OBRIGADO ESPECIAL À MINHA NAMORADA", "GRACIAS ESPECIALES A MI NOVIA", "特别感谢我的女朋友"},
    {"METI, FOR PUTTING UP WITH ME!", "METI, POR ME AGUENTAR!", "METI, ¡POR AGUANTARME!", "METI, 谢谢你包容我!"},
    {"WE HOPE WE DID THE GAME JUSTICE!", "ESPERAMOS TER HONRADO O JOGO!", "¡ESPERAMOS HABER HONRADO EL JUEGO!", "希望我们没有辜负这款游戏!"},
    {"SEE YOU IN THE SEQUEL!", "ATÉ A CONTINUAÇÃO!", "¡NOS VEMOS EN LA SECUELA!", "续作再见!"},
};
const char *const kLangNames[LANG_COUNT] = {"ENGLISH", "PORTUGUÊS", "ESPAÑOL", "中文"};
const char *const kLangCodes[LANG_COUNT] = {"en", "pt", "es", "zh"};
// clang-format on

Lang g_lang = LANG_EN;

const Entry *find(const char *en) {
    static const std::unordered_map<std::string, const Entry *> index = [] {
        std::unordered_map<std::string, const Entry *> m;
        for (const Entry &e : kTable) m.emplace(e.en, &e);
        return m;
    }();
    auto it = index.find(en);
    return it == index.end() ? nullptr : it->second;
}

const char *pick(const Entry *e, const char *fallback) {
    if (!e) return fallback;
    const char *t = g_lang == LANG_PT ? e->pt : g_lang == LANG_ES ? e->es : g_lang == LANG_ZH ? e->zh : e->en;
    return t ? t : fallback;
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string spaces(int n) { return std::string(static_cast<size_t>(std::max(0, n)), ' '); }

}  // namespace

void lang_init() {
    const char *code = platform_lang();
    for (int i = 0; i < LANG_COUNT; i++)
        if (code && std::strcmp(code, kLangCodes[i]) == 0) g_lang = static_cast<Lang>(i);
}

Lang lang_current() { return g_lang; }

void lang_set(Lang l) {
    if (l < 0 || l >= LANG_COUNT || l == g_lang) return;
    g_lang = l;
    platform_set_lang(kLangCodes[l]);
    rebuild_ticker_bitmaps();  // frases do letreiro sao pre-desenhadas
}

const char *lang_name(Lang l) { return (l >= 0 && l < LANG_COUNT) ? kLangNames[l] : kLangNames[0]; }

const char *tr(const char *english) {
    if (!english || g_lang == LANG_EN) return english;
    return pick(find(english), english);
}

const char *tr_menu(int menu, const char *line) {
    if (!line || g_lang == LANG_EN || !*line) return line;
    static std::string out[4];
    static int slot = 0;
    std::string &o = out[slot++ & 3];
    if (const Entry *e = find(line)) return pick(e, line);
    const std::string s = line;
    if (menu == 2) {  // controles: "ROTULO      TECLA " (12 colunas de rotulo); redefinindo: "ROTULO_"
        if (!s.empty() && s.back() == '_') {
            o = std::string(tr(trim(s.substr(0, s.size() - 1)).c_str())) + "_";
            return o.c_str();
        }
        if (s.size() > 12) {
            const std::string label = trim(s.substr(0, 12));
            const char *t = tr(label.c_str());
            o = std::string(t) + spaces(12 - utf8_cols(t)) + s.substr(12);
            return o.c_str();
        }
        return line;
    }
    if (menu == 6 && s.rfind("- EMPTY -", 0) == 0) {  // slot vazio: "- EMPTY -         -"
        const char *t = tr("- EMPTY -");
        o = std::string(t) + spaces(9 - utf8_cols(t)) + s.substr(9);
        return o.c_str();
    }
    const size_t colon = (menu == 3 || menu == 4) ? s.find(':') : std::string::npos;
    if (colon != std::string::npos) {  // "ROTULO:   VALOR", valor alinhado a direita
        const std::string label = trim(s.substr(0, colon)), value = trim(s.substr(colon + 1));
        const char *tl = tr(label.c_str());
        const char *tv = label == "LANGUAGE" ? lang_name(g_lang) : tr(value.c_str());
        const int used = utf8_cols(tl) + 1 + utf8_cols(tv);
        o = std::string(tl) + ":" + spaces(std::max(1, utf8_cols(line) - used)) + tv;
        return o.c_str();
    }
    return line;
}

uint32_t utf8_next(const char *&s) {
    const auto *p = reinterpret_cast<const unsigned char *>(s);
    uint32_t c = p[0];
    int n = 0;
    if (c < 0x80) n = 0;
    else if ((c & 0xe0) == 0xc0) { c &= 0x1f; n = 1; }
    else if ((c & 0xf0) == 0xe0) { c &= 0x0f; n = 2; }
    else if ((c & 0xf8) == 0xf0) { c &= 0x07; n = 3; }
    else { s++; return 0xfffd; }
    s++;
    for (int i = 0; i < n; i++) {
        if ((static_cast<unsigned char>(*s) & 0xc0) != 0x80) return 0xfffd;
        c = (c << 6) | (static_cast<unsigned char>(*s) & 0x3f);
        s++;
    }
    return c;
}

int utf8_cols(const char *s) {
    int n = 0;
    while (s && *s) {
        utf8_next(s);
        n++;
    }
    return n;
}

std::vector<uint32_t> lang_codepoints() {
    std::set<uint32_t> cps;
    auto add = [&](const char *s) {
        while (s && *s) {
            const uint32_t c = utf8_next(s);
            if (c >= 0x80) cps.insert(c);
        }
    };
    for (const Entry &e : kTable) {
        add(e.pt);
        add(e.es);
        add(e.zh);
    }
    for (const char *n : kLangNames) add(n);
    return std::vector<uint32_t>(cps.begin(), cps.end());
}

}  // namespace hoh
