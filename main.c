// Snake Cubos - clon de Snake hecho de cubos para Nintendo 3DS
// Pantalla superior: el tablero. Pantalla tactil: menu, ajustes y marcador.
// Los modos se pueden combinar (como un "blender").
//
// Controles: Cruceta / Circle Pad = mover | START = pausa (en el menu: salir)
//            A = jugar / continuar / reintentar | B = volver al menu

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240
#define MAXW  40
#define MAXH  24
#define MAXC  (MAXW * MAXH)
#define MAXAPPLES 8
#define NM    10

#define SAVE_PATH  "sdmc:/3ds/snake_cubos.sav"
#define SAVE_MAGIC 0x534E4B31u

#define RGB(r, g, b)     C2D_Color32((r), (g), (b), 0xFF)
#define RGBA(r, g, b, a) C2D_Color32((r), (g), (b), (a))

// ---- modos (se pueden activar varios a la vez) ----
enum { M_WALL, M_PORTAL, M_PEACE, M_BORDERLESS, M_TWIN,
       M_WINGED, M_POISON, M_LIGHT, M_MAGNET, M_STATUE };
static const char *modeName[NM] = {
    "Muros", "Portales", "Pacífico", "Sin bordes", "Gemelas",
    "Alas", "Veneno", "Luz", "Imán", "Estatuas"
};
#define HAS(m) ((modeMask >> (m)) & 1u)

// ---- ajustes ----
static const char *speedName[4] = { "Lenta", "Normal", "Rápida", "Turbo" };
static const int   speedFrames[4] = { 12, 9, 6, 4 };
static const char *sizeName[3]  = { "Pequeño", "Mediano", "Grande" };
static const int   gwTab[3]     = { 20, 25, 40 };      // celdas de ancho
static const int   applesTab[3] = { 1, 3, 5 };

typedef struct { const char *name; u8 r, g, b; } SnakeCol;
#define NCOL 10   // el ultimo (indice NCOL-1) es arcoiris
static const SnakeCol cols[NCOL] = {
    { "Azul",     0x4A, 0x75, 0xE8 }, { "Verde",    0x3C, 0xB0, 0x4C },
    { "Rojo",     0xE0, 0x40, 0x40 }, { "Naranja",  0xF5, 0x92, 0x1E },
    { "Morado",   0x9B, 0x59, 0xD0 }, { "Rosa",     0xF0, 0x6E, 0xB4 },
    { "Amarillo", 0xF2, 0xD0, 0x30 }, { "Celeste",  0x30, 0xC8, 0xE8 },
    { "Blanco",   0xF0, 0xF0, 0xF0 }, { "Arcoíris", 0, 0, 0 }
};

typedef struct { const char *name; u8 ar, ag, ab, br, bg, bb; } Theme;
#define NTHEME 4
static const Theme themes[NTHEME] = {
    { "Clásico", 0xAA, 0xD7, 0x51, 0xA2, 0xD1, 0x49 },
    { "Noche",   0x2B, 0x2D, 0x42, 0x25, 0x27, 0x3B },
    { "Océano",  0x7F, 0xC8, 0xF0, 0x74, 0xBE, 0xE8 },
    { "Arena",   0xE8, 0xD8, 0xA8, 0xE0, 0xCE, 0x9C }
};

// ---- tipos ----
typedef struct { short x, y; } Pt;
typedef struct { Pt b[MAXC]; int len; int dir; } Snake;
typedef struct { short x, y; unsigned char poison; bool on; } Apple;
typedef struct {
    u32 magic; int speed, size, apples, color, theme; u32 modes;
    int best[1 << NM][3];
} SaveData;

enum { ST_MENU, ST_PLAY, ST_PAUSE, ST_OVER };
enum { GR_EMPTY, GR_WALL, GR_STATUE };

static const int DX[4] = { 0, 1, 0, -1 };   // 0 arriba, 1 derecha, 2 abajo, 3 izquierda
static const int DY[4] = { -1, 0, 1, 0 };

// ---- estado ----
static int      state = ST_MENU, menuPage = 0;
static unsigned modeMask = 0;
static int      speedIdx = 1, sizeIdx = 1, applesIdx = 0, colorIdx = 0, themeIdx = 0;
static SaveData sd;
static bool     quitGame = false;
static unsigned frame = 0;

static int  gw = 25, gh = 15, cell = 16;
static unsigned char grid[MAXH][MAXW];
static unsigned char occ[MAXH][MAXW];
static Pt   portal[2];
static bool portalOn = false;
static Snake sn[2];
static int  nsn = 1;
static Apple apples[MAXAPPLES];
static int  napl = 0, statueCount = 0;
static int  score = 0, tickCount = 0, tickAcc = 0, queue[3], qn = 0;
static bool started = false, won = false;
static C2D_TextBuf textBuf;

static const u32 txtDark  = 0xFF402C30;   // ABGR
static const u32 txtWhite = 0xFFFFFFFF;

// ---- utilidades ----
static int  wrapi(int v, int n) { return ((v % n) + n) % n; }
static bool inRect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}
static bool inBounds(int x, int y) { return x >= 0 && x < gw && y >= 0 && y < gh; }

static void saveGame(void)
{
    sd.magic = SAVE_MAGIC;
    sd.speed = speedIdx; sd.size = sizeIdx; sd.apples = applesIdx;
    sd.color = colorIdx; sd.theme = themeIdx; sd.modes = modeMask;
    mkdir("sdmc:/3ds", 0777);
    FILE *f = fopen(SAVE_PATH, "wb");
    if (!f) return;
    fwrite(&sd, sizeof sd, 1, f);
    fclose(f);
}

static void loadGame(void)
{
    memset(&sd, 0, sizeof sd);
    FILE *f = fopen(SAVE_PATH, "rb");
    if (!f) return;
    static SaveData tmp;
    size_t n = fread(&tmp, sizeof tmp, 1, f);
    fclose(f);
    if (n != 1 || tmp.magic != SAVE_MAGIC) return;
    sd = tmp;
    speedIdx   = wrapi(sd.speed, 4);
    sizeIdx    = wrapi(sd.size, 3);
    applesIdx  = wrapi(sd.apples, 3);
    colorIdx   = wrapi(sd.color, NCOL);
    themeIdx   = wrapi(sd.theme, NTHEME);
    modeMask   = sd.modes & ((1u << NM) - 1u);
}

// ---- texto ----
static void drawText(const char *s, float x, float y, float scale, u32 clr, bool center)
{
    C2D_Text t;
    C2D_TextParse(&t, textBuf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | (center ? C2D_AlignCenter : 0),
                 x, y, 0.0f, scale, scale, clr);
}

// ---- reglas del juego ----
static bool isPortal(int x, int y)
{
    return portalOn && ((portal[0].x == x && portal[0].y == y) ||
                        (portal[1].x == x && portal[1].y == y));
}

static int appleAt(int x, int y)
{
    for (int i = 0; i < napl; i++)
        if (apples[i].on && apples[i].x == x && apples[i].y == y) return i;
    return -1;
}

static void rebuildOcc(void)
{
    memset(occ, 0, sizeof occ);
    for (int i = 0; i < nsn; i++)
        for (int k = 0; k < sn[i].len; k++)
            occ[sn[i].b[k].y][sn[i].b[k].x] = 1;
}

static bool cellFree(int x, int y)
{
    return inBounds(x, y) && grid[y][x] == GR_EMPTY && !occ[y][x] &&
           !isPortal(x, y) && appleAt(x, y) < 0;
}

static bool farFromHeads(int x, int y, int minDist)
{
    for (int i = 0; i < nsn; i++)
        if (abs(x - sn[i].b[0].x) + abs(y - sn[i].b[0].y) < minDist) return false;
    return true;
}

static bool randomFree(Pt *out, int minDist)
{
    for (int t = 0; t < 300; t++) {
        int x = rand() % gw, y = rand() % gh;
        if (cellFree(x, y) && farFromHeads(x, y, minDist)) { out->x = x; out->y = y; return true; }
    }
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            if (cellFree(x, y) && farFromHeads(x, y, minDist)) { out->x = x; out->y = y; return true; }
    return false;
}

static void placeApple(int idx, bool poison)
{
    Pt p;
    apples[idx].on = false;
    if (randomFree(&p, 0)) {
        apples[idx].x = p.x; apples[idx].y = p.y;
        apples[idx].poison = poison ? 1 : 0;
        apples[idx].on = true;
    }
}

static bool reservedRow(int y, int y0, int y1)
{
    return abs(y - y0) <= 1 || abs(y - y1) <= 1;
}

static void newGame(void)
{
    gw = gwTab[sizeIdx];
    cell = TOP_W / gw;
    gh = SCR_H / cell;
    memset(grid, 0, sizeof grid);
    memset(occ, 0, sizeof occ);
    portalOn = false;
    statueCount = 0;

    nsn = HAS(M_TWIN) ? 2 : 1;
    int y0 = HAS(M_TWIN) ? gh / 3 : gh / 2;
    int x0 = gw / 3;
    int y1 = gh - 1 - y0;

    sn[0].len = 3; sn[0].dir = 1;
    for (int k = 0; k < 3; k++) { sn[0].b[k].x = x0 - k; sn[0].b[k].y = y0; }
    if (nsn == 2) {
        sn[1].len = 3; sn[1].dir = 3;
        for (int k = 0; k < 3; k++) {
            sn[1].b[k].x = gw - 1 - sn[0].b[k].x;
            sn[1].b[k].y = gh - 1 - sn[0].b[k].y;
        }
    }

    if (HAS(M_WALL)) {
        int blocks = gw * gh / 45;
        for (int n = 0; n < blocks; n++) {
            int x = 1 + rand() % (gw - 2), y = 1 + rand() % (gh - 2);
            int len = 2 + rand() % 3;
            bool horiz = rand() & 1;
            for (int k = 0; k < len; k++) {
                int xx = x + (horiz ? k : 0), yy = y + (horiz ? 0 : k);
                if (inBounds(xx, yy) && !reservedRow(yy, y0, y1)) grid[yy][xx] = GR_WALL;
            }
        }
    }

    if (HAS(M_PORTAL)) {
        for (int t = 0; t < 300 && !portalOn; t++) {
            int ax = 1 + rand() % (gw - 2), ay = 1 + rand() % (gh - 2);
            int bx = 1 + rand() % (gw - 2), by = 1 + rand() % (gh - 2);
            if (abs(ax - bx) + abs(ay - by) < gw / 3) continue;
            if (reservedRow(ay, y0, y1) || reservedRow(by, y0, y1)) continue;
            if (grid[ay][ax] != GR_EMPTY || grid[by][bx] != GR_EMPTY) continue;
            portal[0].x = ax; portal[0].y = ay;
            portal[1].x = bx; portal[1].y = by;
            portalOn = true;
        }
    }

    rebuildOcc();
    napl = 0;
    for (int i = 0; i < applesTab[applesIdx]; i++) { placeApple(napl, false); napl++; }
    if (HAS(M_POISON))
        for (int i = 0; i < 2; i++) { placeApple(napl, true); napl++; }

    score = 0; tickCount = 0; tickAcc = 0; qn = 0;
    started = false; won = false;
    state = ST_PLAY;
}

static void gameOver(bool win)
{
    won = win;
    state = ST_OVER;
    if (score > sd.best[modeMask][sizeIdx]) sd.best[modeMask][sizeIdx] = score;
    saveGame();
}

static void addStatue(void)
{
    Pt p;
    if (statueCount < 30 && randomFree(&p, 4)) {
        grid[p.y][p.x] = GR_STATUE;
        statueCount++;
    }
}

static void moveApples(bool magnet)
{
    for (int i = 0; i < napl; i++) {
        if (!apples[i].on || apples[i].poison) continue;
        int ax = apples[i].x, ay = apples[i].y;
        if (magnet) {
            int best = 0, bd = 1 << 20;
            for (int s = 0; s < nsn; s++) {
                int d = abs(ax - sn[s].b[0].x) + abs(ay - sn[s].b[0].y);
                if (d < bd) { bd = d; best = s; }
            }
            if (bd > 8) continue;
            int hx = sn[best].b[0].x, hy = sn[best].b[0].y;
            int sx = (hx > ax) - (hx < ax), sy = (hy > ay) - (hy < ay);
            bool xFirst = abs(hx - ax) >= abs(hy - ay);
            for (int pass = 0; pass < 2; pass++) {
                bool useX = (pass == 0) ? xFirst : !xFirst;
                int nx = ax + (useX ? sx : 0), ny = ay + (useX ? 0 : sy);
                if ((nx != ax || ny != ay) && cellFree(nx, ny)) {
                    apples[i].x = nx; apples[i].y = ny;
                    break;
                }
            }
        } else {
            if (rand() & 1) continue;
            int d0 = rand() % 4;
            for (int k = 0; k < 4; k++) {
                int d = (d0 + k) % 4;
                int nx = ax + DX[d], ny = ay + DY[d];
                if (cellFree(nx, ny)) { apples[i].x = nx; apples[i].y = ny; break; }
            }
        }
    }
}

static void tick(void)
{
    tickCount++;
    if (qn > 0) {
        sn[0].dir = queue[0];
        queue[0] = queue[1]; queue[1] = queue[2];
        qn--;
    }
    rebuildOcc();

    Pt   nx[2];
    int  ai[2] = { -1, -1 };
    bool grow[2] = { false, false }, hit[2] = { false, false };

    for (int i = 0; i < nsn; i++) {
        if (i == 1) sn[1].dir = sn[0].dir ^ 2;      // gemela: simetria central
        int d = sn[i].dir;
        int x = sn[i].b[0].x + DX[d], y = sn[i].b[0].y + DY[d];
        bool out = false;
        if (!inBounds(x, y)) {
            if (HAS(M_BORDERLESS)) { x = wrapi(x, gw); y = wrapi(y, gh); }
            else out = true;
        }
        if (!out && portalOn) {
            if (x == portal[0].x && y == portal[0].y)      { x = portal[1].x; y = portal[1].y; }
            else if (x == portal[1].x && y == portal[1].y) { x = portal[0].x; y = portal[0].y; }
        }
        nx[i].x = x; nx[i].y = y;
        hit[i] = out;
        if (!out) {
            ai[i] = appleAt(x, y);
            grow[i] = (ai[i] >= 0 && !apples[ai[i]].poison);
        }
    }

    bool anyHit = false;
    for (int i = 0; i < nsn; i++) {
        if (hit[i]) { anyHit = true; continue; }
        int x = nx[i].x, y = nx[i].y;
        if (grid[y][x] != GR_EMPTY) hit[i] = true;
        else if (occ[y][x]) {
            bool tailFree = false;
            for (int j = 0; j < nsn; j++) {
                Pt t = sn[j].b[sn[j].len - 1];
                if (!grow[j] && t.x == x && t.y == y) tailFree = true;
            }
            if (!tailFree) hit[i] = true;
        }
        if (hit[i]) anyHit = true;
    }
    if (nsn == 2 && nx[0].x == nx[1].x && nx[0].y == nx[1].y) anyHit = true;

    if (anyHit) {
        if (!HAS(M_PEACE)) gameOver(false);
        return;                         // modo pacifico: se queda quieta
    }

    for (int i = 0; i < nsn; i++) {
        Snake *s = &sn[i];
        int newLen = s->len + ((grow[i] && s->len < MAXC) ? 1 : 0);
        memmove(&s->b[1], &s->b[0], (size_t)(newLen - 1) * sizeof(Pt));
        s->b[0] = nx[i];
        s->len = newLen;
    }

    bool respawn[MAXAPPLES] = { false };
    int  newStatues = 0;
    for (int i = 0; i < nsn; i++) {
        int a = ai[i];
        if (a < 0 || !apples[a].on) continue;
        if (apples[a].poison) {
            if (sn[i].len <= 4) { apples[a].on = false; gameOver(false); return; }   // el veneno es mortal
            sn[i].len -= 2;
        } else {
            score++;
            newStatues++;
        }
        apples[a].on = false;
        respawn[a] = true;
    }

    rebuildOcc();
    for (int a = 0; a < napl; a++)
        if (respawn[a]) placeApple(a, apples[a].poison != 0);
    if (HAS(M_STATUE))
        for (int k = 0; k < newStatues; k++) addStatue();

    if (HAS(M_MAGNET) && tickCount % 4 == 0)       moveApples(true);
    else if (HAS(M_WINGED) && tickCount % 3 == 0)  moveApples(false);

    int normalLeft = 0;
    for (int a = 0; a < napl; a++)
        if (apples[a].on && !apples[a].poison) normalLeft++;
    if (normalLeft == 0) gameOver(true);
}

static void pushDir(int d)
{
    started = true;
    int last = (qn > 0) ? queue[qn - 1] : sn[0].dir;
    if (d == last || d == (last ^ 2)) return;
    if (qn < 3) queue[qn++] = d;
}

static int cpadDir(const circlePosition *cp)
{
    int ax = abs(cp->dx), ay = abs(cp->dy);
    if (ax < 60 && ay < 60) return -1;
    if (ax > ay) return cp->dx > 0 ? 1 : 3;
    return cp->dy > 0 ? 0 : 2;
}

// ---- dibujo ----
static u32 shade(u32 c, float f)
{
    int r = (int)((c & 0xFF) * f), g = (int)(((c >> 8) & 0xFF) * f), b = (int)(((c >> 16) & 0xFF) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

static u32 hsvColor(float h)
{
    float x = 1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f);
    float r = 0, g = 0, b = 0;
    switch (((int)(h / 60.0f)) % 6) {
    case 0: r = 1; g = x; break;
    case 1: r = x; g = 1; break;
    case 2: g = 1; b = x; break;
    case 3: g = x; b = 1; break;
    case 4: r = x; b = 1; break;
    default: r = 1; b = x; break;
    }
    return RGB((int)(255 * (0.25f + 0.75f * r)), (int)(255 * (0.25f + 0.75f * g)),
               (int)(255 * (0.25f + 0.75f * b)));
}

static u32 segColor(int si, int k)
{
    if (colorIdx == NCOL - 1)
        return hsvColor(fmodf(k * 14.0f + (float)frame * 2.0f + si * 180.0f, 360.0f));
    int ci = (si == 0) ? colorIdx : (colorIdx + 4) % (NCOL - 1);
    return RGB(cols[ci].r, cols[ci].g, cols[ci].b);
}

static void drawCube(float px, float py, float sz, u32 col)
{
    C2D_DrawRectSolid(px + 1, py + 1, 0, sz - 2, sz - 2, col);
    if (sz >= 14)
        C2D_DrawRectSolid(px + 3, py + 3, 0, sz - 8, 2, RGBA(255, 255, 255, 70));
}

static void drawApple(float px, float py, float sz, bool poison)
{
    float cx = px + sz * 0.5f, cy = py + sz * 0.55f, r = sz * 0.38f;
    u32 body = poison ? RGB(0x8E, 0x24, 0xAA) : RGB(0xE7, 0x47, 0x1D);
    C2D_DrawCircleSolid(cx, cy, 0, r, body);
    C2D_DrawRectSolid(cx - 0.5f, cy - r - sz * 0.15f, 0, sz >= 14 ? 2 : 1, sz * 0.2f, RGB(0x6B, 0x45, 0x20));
    C2D_DrawCircleSolid(cx + sz * 0.2f, cy - r - sz * 0.02f, 0, r * 0.38f, RGB(0x4C, 0xAF, 0x50));
    if (poison) {
        C2D_DrawCircleSolid(cx - r * 0.35f, cy - r * 0.1f, 0, r * 0.2f, RGB(255, 255, 255));
        C2D_DrawCircleSolid(cx + r * 0.35f, cy - r * 0.1f, 0, r * 0.2f, RGB(255, 255, 255));
    } else {
        C2D_DrawCircleSolid(cx - r * 0.4f, cy - r * 0.35f, 0, r * 0.2f, RGBA(255, 255, 255, 170));
    }
}

static void drawHead(float px, float py, float sz, int dir)
{
    float cx = px + sz * 0.5f, cy = py + sz * 0.5f;
    float fx = (float)DX[dir], fy = (float)DY[dir];
    float sx = -fy, sy = fx;
    float e = sz / 4.0f; if (e < 2) e = 2;
    for (int k = -1; k <= 1; k += 2) {
        float ex = cx + fx * sz * 0.2f + sx * k * sz * 0.22f;
        float ey = cy + fy * sz * 0.2f + sy * k * sz * 0.22f;
        C2D_DrawRectSolid(ex - e / 2, ey - e / 2, 0, e, e, RGB(255, 255, 255));
        float pe = e / 2 < 1 ? 1 : e / 2;
        C2D_DrawRectSolid(ex + fx * pe * 0.5f - pe / 2, ey + fy * pe * 0.5f - pe / 2, 0, pe, pe, RGB(0x20, 0x20, 0x30));
    }
}

static void drawBg(int cs)
{
    const Theme *th = &themes[themeIdx];
    C2D_DrawRectSolid(0, 0, 0, TOP_W, SCR_H, RGB(th->ar, th->ag, th->ab));
    u32 cb = RGB(th->br, th->bg, th->bb);
    for (int y = 0; y < SCR_H / cs; y++)
        for (int x = 0; x < TOP_W / cs; x++)
            if ((x + y) & 1) C2D_DrawRectSolid(x * cs, y * cs, 0, cs, cs, cb);
}

static void drawBoard(void)
{
    drawBg(cell);
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            if (grid[y][x] == GR_WALL)
                drawCube(x * cell, y * cell, cell, RGB(0x6A, 0x72, 0x84));
            else if (grid[y][x] == GR_STATUE)
                drawCube(x * cell, y * cell, cell, RGB(0xB8, 0xAE, 0x9A));
        }
    if (portalOn) {
        for (int i = 0; i < 2; i++) {
            float cx = portal[i].x * cell + cell * 0.5f, cy = portal[i].y * cell + cell * 0.5f;
            u32 c1 = i == 0 ? RGB(0xF5, 0x92, 0x1E) : RGB(0x30, 0x90, 0xF0);
            C2D_DrawCircleSolid(cx, cy, 0, cell * 0.46f, c1);
            C2D_DrawCircleSolid(cx, cy, 0, cell * 0.26f, shade(c1, 0.55f));
        }
    }
    for (int a = 0; a < napl; a++)
        if (apples[a].on) drawApple(apples[a].x * cell, apples[a].y * cell, cell, apples[a].poison);

    for (int i = 0; i < nsn; i++) {
        for (int k = sn[i].len - 1; k >= 0; k--) {
            u32 c = segColor(i, k);
            if (k == 0) c = shade(c, 0.85f);
            drawCube(sn[i].b[k].x * cell, sn[i].b[k].y * cell, cell, c);
        }
        drawHead(sn[i].b[0].x * cell, sn[i].b[0].y * cell, cell, sn[i].dir);
    }

    if (HAS(M_LIGHT)) {
        const float R = 88.0f;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++) {
                float cx = x * cell + cell * 0.5f, cy = y * cell + cell * 0.5f, dm = 1e9f;
                for (int i = 0; i < nsn; i++) {
                    float dx = cx - (sn[i].b[0].x * cell + cell * 0.5f);
                    float dy = cy - (sn[i].b[0].y * cell + cell * 0.5f);
                    float d = dx * dx + dy * dy;
                    if (d < dm) dm = d;
                }
                if (dm > R * R)
                    C2D_DrawRectSolid(x * cell, y * cell, 0, cell, cell, RGB(0x08, 0x08, 0x12));
                else if (dm > (R * 0.75f) * (R * 0.75f))
                    C2D_DrawRectSolid(x * cell, y * cell, 0, cell, cell, RGBA(0x08, 0x08, 0x12, 170));
            }
    }
}

static void buildModeLines(char *l1, char *l2)
{
    char all[160]; all[0] = 0;
    for (int i = 0; i < NM; i++)
        if (HAS(i)) {
            if (all[0]) strcat(all, ", ");
            strcat(all, modeName[i]);
        }
    if (!all[0]) strcpy(all, "Clásico");
    size_t L = strlen(all);
    l2[0] = 0;
    if (L <= 34) { strcpy(l1, all); return; }
    size_t cut = 34;
    while (cut > 0 && !(all[cut] == ' ' && all[cut - 1] == ',')) cut--;
    if (cut == 0) cut = 34;
    memcpy(l1, all, cut); l1[cut] = 0;
    strcpy(l2, all + cut + (all[cut] == ' ' ? 1 : 0));
}

static void drawButtonUI(int x, int y, int w, int h, const char *label, bool active)
{
    C2D_DrawRectSolid(x, y, 0, w, h, RGB(0x3A, 0x34, 0x52));
    C2D_DrawRectSolid(x + 3, y + 3, 0, w - 6, h - 6, active ? RGB(0xA8, 0xE0, 0x7A) : RGB(0xF4, 0xF1, 0xFA));
    drawText(label, x + w / 2.0f, y + h / 2.0f - 11, 0.62f, txtDark, true);
}

static void drawArrow(float cx, float cy, bool right)
{
    u32 c = RGB(0x3A, 0x34, 0x52);
    float s = right ? 1.0f : -1.0f;
    C2D_DrawTriangle(cx - 6 * s, cy - 9, c, cx - 6 * s, cy + 9, c, cx + 7 * s, cy, c, 0);
}

static void drawTop(void)
{
    if (state == ST_MENU) {
        drawBg(20);
        drawText("SNAKE", 200, 10, 1.3f, txtDark, true);
        drawText("Edición de cubos", 200, 62, 0.55f, txtDark, true);
        for (int k = 6; k >= 0; k--) {
            u32 c = segColor(0, k);
            if (k == 0) c = shade(c, 0.85f);
            drawCube(96 + (6 - k) * 24, 104, 24, c);
        }
        drawHead(96 + 6 * 24, 104, 24, 1);
        drawApple(96 + 8 * 24, 104, 24, false);
        char l1[64], l2[96], buf[64];
        buildModeLines(l1, l2);
        drawText("Modos:", 200, 146, 0.5f, txtDark, true);
        drawText(l1, 200, 164, 0.42f, txtDark, true);
        if (l2[0]) drawText(l2, 200, 180, 0.42f, txtDark, true);
        snprintf(buf, sizeof buf, "Récord: %d", sd.best[modeMask][sizeIdx]);
        drawText(buf, 200, 204, 0.55f, txtDark, true);
        return;
    }

    drawBoard();

    if (state == ST_PLAY && !started) {
        C2D_DrawRectSolid(0, 214, 0, TOP_W, 26, RGBA(0, 0, 0, 150));
        drawText("Pulsa una dirección para empezar", 200, 218, 0.5f, txtWhite, true);
    }
    if (state == ST_PAUSE || state == ST_OVER) {
        char buf[48];
        C2D_DrawRectSolid(0, 0, 0, TOP_W, SCR_H, RGBA(0, 0, 0, 110));
        C2D_DrawRectSolid(80, 78, 0, 240, 84, RGBA(0x20, 0x20, 0x30, 235));
        if (state == ST_PAUSE) {
            drawText("Pausa", 200, 100, 0.9f, txtWhite, true);
        } else {
            drawText(won ? "¡Ganaste!" : "¡Fin del juego!", 200, 86, 0.8f, txtWhite, true);
            snprintf(buf, sizeof buf, "Puntos: %d", score);
            drawText(buf, 200, 124, 0.7f, txtWhite, true);
        }
    }
}

static void drawBottom(void)
{
    C2D_DrawRectSolid(0, 0, 0, BOT_W, SCR_H, RGB(0xEE, 0xEA, 0xF6));

    if (state == ST_MENU) {
        drawButtonUI(0, 0, 160, 32, "Modos", menuPage == 0);
        drawButtonUI(160, 0, 160, 32, "Ajustes", menuPage == 1);
        if (menuPage == 0) {
            for (int i = 0; i < NM; i++) {
                int x = 6 + (i % 2) * 156, y = 38 + (i / 2) * 32;
                C2D_DrawRectSolid(x, y, 0, 152, 30, RGB(0x3A, 0x34, 0x52));
                C2D_DrawRectSolid(x + 2, y + 2, 0, 148, 26, RGB(0xFF, 0xFF, 0xFF));
                C2D_DrawRectSolid(x + 7, y + 7, 0, 16, 16, RGB(0x3A, 0x34, 0x52));
                C2D_DrawRectSolid(x + 9, y + 9, 0, 12, 12, HAS(i) ? RGB(0x4C, 0xC0, 0x5A) : RGB(0xFF, 0xFF, 0xFF));
                drawText(modeName[i], x + 30, y + 5, 0.55f, txtDark, false);
            }
        } else {
            static const char *labels[5] = { "Velocidad", "Mapa", "Manzanas", "Serpiente", "Fondo" };
            char val[24];
            for (int r = 0; r < 5; r++) {
                int y = 44 + r * 30;
                drawText(labels[r], 10, y + 3, 0.5f, txtDark, false);
                drawArrow(124, y + 13, false);
                drawArrow(276, y + 13, true);
                const char *v = "";
                switch (r) {
                case 0: v = speedName[speedIdx]; break;
                case 1: v = sizeName[sizeIdx]; break;
                case 2: snprintf(val, sizeof val, "%d", applesTab[applesIdx]); v = val; break;
                case 3: v = cols[colorIdx].name; break;
                default: v = themes[themeIdx].name; break;
                }
                drawText(v, 200, y + 3, 0.55f, txtDark, true);
                if (r == 3) {
                    u32 c = (colorIdx == NCOL - 1) ? hsvColor(fmodf(frame * 3.0f, 360.0f))
                                                   : RGB(cols[colorIdx].r, cols[colorIdx].g, cols[colorIdx].b);
                    drawCube(150, y + 2, 22, c);
                }
            }
        }
        drawButtonUI(60, 198, 200, 38, "¡JUGAR!", true);
        return;
    }

    char buf[48], l1[64], l2[96];
    drawText("SNAKE", 160, 6, 0.8f, txtDark, true);
    snprintf(buf, sizeof buf, "%d", score);
    drawText("Puntos", 160, 38, 0.5f, txtDark, true);
    drawText(buf, 160, 54, 1.4f, txtDark, true);
    snprintf(buf, sizeof buf, "Récord: %d", sd.best[modeMask][sizeIdx] > score ? sd.best[modeMask][sizeIdx] : score);
    drawText(buf, 160, 104, 0.55f, txtDark, true);
    snprintf(buf, sizeof buf, "Largo: %d", sn[0].len);
    drawText(buf, 160, 126, 0.55f, txtDark, true);
    buildModeLines(l1, l2);
    drawText(l1, 160, 154, 0.42f, txtDark, true);
    if (l2[0]) drawText(l2, 160, 170, 0.42f, txtDark, true);

    if (state == ST_PLAY) {
        drawButtonUI(20, 196, 130, 36, "Pausa", false);
        drawButtonUI(170, 196, 130, 36, "Menú", false);
    } else if (state == ST_PAUSE) {
        drawButtonUI(20, 196, 130, 36, "Seguir", true);
        drawButtonUI(170, 196, 130, 36, "Menú", false);
    } else {
        drawButtonUI(20, 196, 130, 36, "Otra vez", true);
        drawButtonUI(170, 196, 130, 36, "Menú", false);
    }
}

// ---- entrada ----
static void menuTouch(int tx, int ty)
{
    if (ty < 32) { menuPage = (tx < 160) ? 0 : 1; return; }
    if (inRect(tx, ty, 60, 198, 200, 38)) { newGame(); saveGame(); return; }
    if (menuPage == 0) {
        for (int i = 0; i < NM; i++) {
            int x = 6 + (i % 2) * 156, y = 38 + (i / 2) * 32;
            if (inRect(tx, ty, x, y, 152, 30)) { modeMask ^= (1u << i); saveGame(); }
        }
    } else {
        for (int r = 0; r < 5; r++) {
            int y = 44 + r * 30;
            if (ty < y || ty >= y + 28) continue;
            int delta = 0;
            if (tx >= 100 && tx < 150) delta = -1;
            else if (tx >= 250 && tx < 300) delta = 1;
            if (!delta) continue;
            switch (r) {
            case 0: speedIdx  = wrapi(speedIdx + delta, 4); break;
            case 1: sizeIdx   = wrapi(sizeIdx + delta, 3); break;
            case 2: applesIdx = wrapi(applesIdx + delta, 3); break;
            case 3: colorIdx  = wrapi(colorIdx + delta, NCOL); break;
            default: themeIdx = wrapi(themeIdx + delta, NTHEME); break;
            }
            saveGame();
        }
    }
}

static void handleInput(u32 kDown, int tx, int ty, const circlePosition *cp)
{
    static int lastCp = -1;
    bool touched = (kDown & KEY_TOUCH) != 0;

    if (state == ST_MENU) {
        if (kDown & KEY_START) quitGame = true;
        else if (kDown & KEY_A) { newGame(); saveGame(); }
        else if (touched) menuTouch(tx, ty);
        lastCp = -1;
        return;
    }

    if (state == ST_PLAY) {
        if (kDown & KEY_DUP)    pushDir(0);
        if (kDown & KEY_DRIGHT) pushDir(1);
        if (kDown & KEY_DDOWN)  pushDir(2);
        if (kDown & KEY_DLEFT)  pushDir(3);
        int cd = cpadDir(cp);
        if (cd >= 0 && cd != lastCp) pushDir(cd);
        lastCp = cd;
        if (kDown & KEY_START) state = ST_PAUSE;
        if (kDown & KEY_B) state = ST_MENU;
        if (touched) {
            if (inRect(tx, ty, 20, 196, 130, 36)) state = ST_PAUSE;
            else if (inRect(tx, ty, 170, 196, 130, 36)) state = ST_MENU;
        }
        return;
    }

    lastCp = -1;
    bool left  = (kDown & KEY_A) || (touched && inRect(tx, ty, 20, 196, 130, 36));
    bool right = (kDown & KEY_B) || (touched && inRect(tx, ty, 170, 196, 130, 36));
    if (state == ST_PAUSE) {
        if ((kDown & KEY_START) || left) state = ST_PLAY;
        else if (right) state = ST_MENU;
    } else {   // ST_OVER
        if (left) newGame();
        else if (right) state = ST_MENU;
    }
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
    C2D_Prepare();

    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    textBuf = C2D_TextBufNew(4096);

    srand((unsigned int)time(NULL));
    loadGame();
    newGame();           // deja el tablero listo para el fondo del menu
    state = ST_MENU;

    while (aptMainLoop() && !quitGame) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        touchPosition tp;
        hidTouchRead(&tp);
        circlePosition cp;
        hidCircleRead(&cp);
        frame++;

        handleInput(kDown, tp.px, tp.py, &cp);

        if (state == ST_PLAY && started) {
            if (++tickAcc >= speedFrames[speedIdx]) {
                tickAcc = 0;
                tick();
            }
        }

        C2D_TextBufClear(textBuf);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, RGB(0, 0, 0));
        C2D_SceneBegin(top);
        drawTop();
        C2D_TargetClear(bot, RGB(0, 0, 0));
        C2D_SceneBegin(bot);
        drawBottom();
        C3D_FrameEnd(0);
    }

    saveGame();
    C2D_TextBufDelete(textBuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
