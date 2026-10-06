/*
 * MARIO KART - PSP EDITION
 * PSP icin sade kontrol ve performans ayarlari
 * Kontroller:
 *   Analog / D-pad : direksiyon
 *   X              : gaz
 *   Kare           : fren
 *   L / R          : drift (birakinca mini turbo)
 *   Daire          : eldeki ozel gucu kullan
 *   Ucgen          : kamera acisi degistir
 *   START          : duraklat / devam et
 *   SELECT         : bitis ekraninda yeniden baslat
 *
 *   Ozel gucler: Mantar, Turbo Yildizi, Kabuk, Muz, Yildirim, Kalkan
 *
 *   Menu: YUKARI/ASAGI ile mod sec, X ile baslat
 *   Modlar: TEK KISILIK (5 bot) / TIME TRIAL (botsuz + ghost)
 *   Kayit: ms0:/PSP/SAVEDATA/MKPSP/  (en iyi tur, en iyi sure, galibiyet, ghost)

 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psppower.h>
#include <pspaudio.h>
#include <pspiofilemgr.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PSP_MODULE_INFO("MarioKartPSP", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

/* ---------- HOME tusu ---------- */
static int exit_callback(int a1, int a2, void *c) { (void)a1; (void)a2; (void)c; sceKernelExitGame(); return 0; }
static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cb = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int th = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}

/* ---------- Sabitler ---------- */
#define W 480
#define H 272
#define BUFW 512
#define PI 3.14159265f
#define DT (1.0f / 60.0f)
#define M 320            /* PSP icin yeterli pist ornek sayisi */
#define HW 11.0f         /* yol yarim genislik */
#define VMAX 52.0f
#define LAPS 3
#define NK 6             /* oyuncu + 5 bot */
#define MAXV 56000
#define NTRACKS 4

static unsigned int __attribute__((aligned(16))) list[262144];

#define RGB(r,g,b) (0xFF000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define RGBA(r,g,b,a) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))

static float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static float wrapA(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static unsigned scol(unsigned c, float f) {
    int r = (int)((c & 255) * f), g = (int)(((c >> 8) & 255) * f), b = (int)(((c >> 16) & 255) * f);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (c & 0xFF000000u) | ((unsigned)b << 16) | ((unsigned)g << 8) | (unsigned)r;
}
static unsigned mixc(unsigned a, unsigned b, float t) {
    t = clampf(t, 0, 1);
    int ar = a & 255, ag = (a >> 8) & 255, ab = (a >> 16) & 255;
    int br = b & 255, bg = (b >> 8) & 255, bb = (b >> 16) & 255;
    return RGB((int)(ar + (br - ar) * t), (int)(ag + (bg - ag) * t), (int)(ab + (bb - ab) * t));
}
static unsigned rngState = 12345;
static float rnd(void) { rngState = rngState * 1664525u + 1013904223u; return ((rngState >> 8) & 0xFFFF) / 65536.0f; }

/* ---------- 2D cizim (HUD) ---------- */
typedef struct { unsigned int c; short x, y, z; } V2;
#define VF2 (GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)

static short cs(float v) {
    if (v != v) v = 0;
    if (v > 30000) v = 30000;
    if (v < -30000) v = -30000;
    return (short)v;
}
static void v2set(V2 *v, float x, float y, unsigned c) { v->c = c; v->x = cs(x); v->y = cs(y); v->z = 0; }
static void rect(float x, float y, float w, float h, unsigned c) {
    V2 *v = (V2 *)sceGuGetMemory(2 * sizeof(V2));
    v2set(&v[0], x, y, c); v2set(&v[1], x + w, y + h, c);
    sceGuDrawArray(GU_SPRITES, VF2, 2, 0, v);
}
static void vgrad(float x, float y, float w, float h, unsigned top, unsigned bot) {
    V2 *v = (V2 *)sceGuGetMemory(4 * sizeof(V2));
    v2set(&v[0], x, y, top); v2set(&v[1], x + w, y, top);
    v2set(&v[2], x, y + h, bot); v2set(&v[3], x + w, y + h, bot);
    sceGuDrawArray(GU_TRIANGLE_STRIP, VF2, 4, 0, v);
}
static void trap(float xl1, float xr1, float y1, float xl2, float xr2, float y2, unsigned c) {
    V2 *v = (V2 *)sceGuGetMemory(4 * sizeof(V2));
    v2set(&v[0], xl1, y1, c); v2set(&v[1], xr1, y1, c);
    v2set(&v[2], xl2, y2, c); v2set(&v[3], xr2, y2, c);
    sceGuDrawArray(GU_TRIANGLE_STRIP, VF2, 4, 0, v);
}
static void tri2(float x1, float y1, float x2, float y2, float x3, float y3, unsigned c) {
    V2 *v = (V2 *)sceGuGetMemory(3 * sizeof(V2));
    v2set(&v[0], x1, y1, c); v2set(&v[1], x2, y2, c); v2set(&v[2], x3, y3, c);
    sceGuDrawArray(GU_TRIANGLES, VF2, 3, 0, v);
}

/* ---------- 5x7 piksel font (A-Z, 0-9, bazi isaretler) ---------- */
/* her harf 7 satir, her satir 5 bit (en soldaki bit = 0x10) */
typedef struct { char c; unsigned char r[7]; } Glyph5;
static const Glyph5 font5[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
};
#define FONT5_COUNT ((int)(sizeof(font5) / sizeof(font5[0])))

static const unsigned char *glyphFor(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);   /* kucuk harf -> buyuk harf */
    for (int i = 0; i < FONT5_COUNT; i++) if (font5[i].c == ch) return font5[i].r;
    return NULL;
}

/* v == NULL ise sadece dikdortgen sayar, doluysa vertex yazar */
static void emitR(V2 *v, int *n, int x, int y, int w, int h, unsigned c) {
    if (v) { v2set(&v[2 * (*n)], x, y, c); v2set(&v[2 * (*n) + 1], x + w, y + h, c); }
    (*n)++;
}
static void glyphEmit(V2 *v, int *n, const unsigned char *r, int x, int y, int w, int h, unsigned c) {
    for (int row = 0; row < 7; row++) {
        int y0 = y + row * h / 7, y1 = y + (row + 1) * h / 7;
        if (y1 <= y0) y1 = y0 + 1;
        int col = 0;
        while (col < 5) {
            if (r[row] & (0x10 >> col)) {
                int c0 = col;
                while (col < 5 && (r[row] & (0x10 >> col))) col++;
                int x0 = x + c0 * w / 5, x1 = x + col * w / 5;
                if (x1 <= x0) x1 = x0 + 1;
                emitR(v, n, x0, y0, x1 - x0, y1 - y0, c);
            } else col++;
        }
    }
}

/* Yazi tek seferde cizilir (tek GU cagrisi): hizli ve PSP dostu */
static void text(int x, int y, int w, int h, int t, const char *s, unsigned c) {
    unsigned sh = RGBA(0, 0, 0, 200);
    int total = 0;
    V2 *v = NULL;
    for (int pass = 0; pass < 2; pass++) {
        int n = 0, cx = x;
        if (pass == 1) {
            if (total == 0) return;
            v = (V2 *)sceGuGetMemory(2 * total * sizeof(V2));
        }
        for (const char *p = s; *p; p++) {
            char ch = *p;
            if (ch == ' ') { cx += w; continue; }
            if (ch == '.') {
                emitR(v, &n, cx + 1, y + h - t + 1, t, t, sh); emitR(v, &n, cx, y + h - t, t, t, c);
                cx += t + 3; continue;
            }
            if (ch == ':') {
                emitR(v, &n, cx + 1, y + h / 3 + 1, t, t, sh); emitR(v, &n, cx, y + h / 3, t, t, c);
                emitR(v, &n, cx + 1, y + 2 * h / 3 - t + 1, t, t, sh); emitR(v, &n, cx, y + 2 * h / 3 - t, t, t, c);
                cx += t + 3; continue;
            }
            const unsigned char *g = glyphFor(ch);
            if (g) {
                glyphEmit(v, &n, g, cx + 1, y + 1, w, h, sh);
                glyphEmit(v, &n, g, cx, y, w, h, c);
            }
            cx += (ch == '/') ? w : (w + t + 3);
        }
        if (pass == 0) total = n;
        else sceGuDrawArray(GU_SPRITES, VF2, 2 * total, 0, v);
    }
}
static int textWidth(const char *s, int w, int t) {
    int x = 0;
    for (; *s; s++) {
        if (*s == ' ' || *s == '/') x += w;
        else if (*s == '.' || *s == ':') x += t + 3;
        else x += w + t + 3;
    }
    return x;
}

/* ---------- 3D mesh deposu ---------- */
typedef struct { float u, v; unsigned c; float x, y, z; } Vtx;
#define VF3 (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
typedef struct { float x, y, z; } P3;

static Vtx __attribute__((aligned(16))) mesh[MAXV];
static int nv = 0;
static P3 mk(float x, float y, float z) { P3 p; p.x = x; p.y = y; p.z = z; return p; }
static unsigned pvAlpha = 255;   /* ghost kart icin yari saydam */
static void pv(P3 p, unsigned c) {
    if (pvAlpha != 255) c = (c & 0x00FFFFFFu) | (pvAlpha << 24);
    if (nv < MAXV) { mesh[nv].u = 0; mesh[nv].v = 0; mesh[nv].c = c; mesh[nv].x = p.x; mesh[nv].y = p.y; mesh[nv].z = p.z; nv++; }
}
static void tri3(P3 a, P3 b, P3 c, unsigned col) {
    if (nv + 3 > MAXV) return;
    pv(a, col); pv(b, col); pv(c, col);
}
static void quad3(P3 a, P3 b, P3 c, P3 d, unsigned col) { tri3(a, b, c, col); tri3(a, c, d, col); }
/* dokulu dortgen: a=(u0,v0) b=(u1,v0) c=(u1,v1) d=(u0,v1) */
static void tquad(P3 a, P3 b, P3 c, P3 d, unsigned col, float u0, float v0, float u1, float v1) {
    int s0 = nv;
    quad3(a, b, c, d, col);
    if (nv - s0 == 6) {
        mesh[s0 + 0].u = u0; mesh[s0 + 0].v = v0;
        mesh[s0 + 1].u = u1; mesh[s0 + 1].v = v0;
        mesh[s0 + 2].u = u1; mesh[s0 + 2].v = v1;
        mesh[s0 + 3].u = u0; mesh[s0 + 3].v = v0;
        mesh[s0 + 4].u = u1; mesh[s0 + 4].v = v1;
        mesh[s0 + 5].u = u0; mesh[s0 + 5].v = v1;
    }
}

/* yone gore donmus kutu (f = ileri yonu xz) */
static void boxO(float cx, float cy, float cz, float fx, float fz, float hx, float hy, float hz, unsigned c) {
    float rx = -fz, rz = fx;
#define BP(sx, sy, sz) mk(cx + fx * hx * (sx) + rx * hz * (sz), cy + hy * (sy), cz + fz * hx * (sx) + rz * hz * (sz))
    quad3(BP(-1, 1, -1), BP(1, 1, -1), BP(1, 1, 1), BP(-1, 1, 1), scol(c, 1.00f));
    quad3(BP(1, -1, -1), BP(1, 1, -1), BP(1, 1, 1), BP(1, -1, 1), scol(c, 0.82f));
    quad3(BP(-1, -1, -1), BP(-1, 1, -1), BP(-1, 1, 1), BP(-1, -1, 1), scol(c, 0.60f));
    quad3(BP(-1, -1, 1), BP(1, -1, 1), BP(1, 1, 1), BP(-1, 1, 1), scol(c, 0.90f));
    quad3(BP(-1, -1, -1), BP(1, -1, -1), BP(1, 1, -1), BP(-1, 1, -1), scol(c, 0.70f));
#undef BP
}
static void box3(float cx, float cy, float cz, float hx, float hy, float hz, unsigned c) {
    boxO(cx, cy, cz, 1, 0, hx, hy, hz, c);
}
static void cone3(float cx, float cy, float cz, float r, float h, int sides, float rot, unsigned c) {
    P3 apex = mk(cx, cy + h, cz);
    for (int i = 0; i < sides; i++) {
        float a0 = rot + i * 2 * PI / sides, a1 = rot + (i + 1) * 2 * PI / sides;
        float am = (a0 + a1) * 0.5f;
        float sh = 0.62f + 0.38f * (0.5f + 0.5f * cosf(am - 0.8f));
        tri3(mk(cx + cosf(a0) * r, cy, cz + sinf(a0) * r), mk(cx + cosf(a1) * r, cy, cz + sinf(a1) * r), apex, scol(c, sh));
    }
}

/* ---------- Karakterler, pistler, temalar ---------- */
static const unsigned kBody[NK] = { RGB(225, 35, 35), RGB(40, 90, 235), RGB(30, 175, 70), RGB(245, 205, 25), RGB(160, 70, 205), RGB(245, 135, 25) };
static const unsigned kTrim[NK] = { RGB(255, 255, 255), RGB(255, 255, 255), RGB(255, 255, 255), RGB(40, 40, 40), RGB(255, 255, 255), RGB(40, 40, 40) };
static const char *charNames[NK] = { "RED", "BLUE", "GREEN", "YELLOW", "PURPLE", "ORANGE" };
/* karakter ozellikleri: hiz / ivme / direksiyon carpani */
static const float cSpd[NK] = { 1.00f, 1.05f, 0.97f, 0.97f, 1.06f, 0.94f };
static const float cAcc[NK] = { 1.00f, 0.92f, 1.12f, 1.00f, 0.88f, 1.15f };
static const float cTrn[NK] = { 1.00f, 0.98f, 1.00f, 1.12f, 0.92f, 1.08f };

static int trackSel = 0, charSel = 0;
static int kChar[NK];            /* kart slotu -> karakter (0 = oyuncu) */
static int ghostBestChar = 0;    /* ghost kaydini yapan karakter */

typedef struct { const char *name; unsigned g1, g2, sky, haze, mtn, cap, leaf; float skip; } Theme;
static const Theme themes[NTRACKS] = {
    { "SUNNY HILLS", RGB(34, 150, 48),   RGB(28, 138, 42),   RGB(30, 100, 225), RGB(175, 218, 255), RGB(98, 108, 170),  RGB(240, 245, 255), RGB(20, 120, 45),  0.30f },
    { "DESERT OVAL", RGB(214, 184, 112), RGB(204, 172, 100), RGB(70, 140, 225), RGB(240, 214, 168), RGB(176, 120, 78),  RGB(235, 200, 150), RGB(110, 140, 60), 0.85f },
    { "SNOW FIELD",  RGB(236, 242, 250), RGB(224, 232, 246), RGB(100, 140, 205), RGB(206, 224, 244), RGB(130, 150, 190), RGB(250, 252, 255), RGB(40, 100, 80),  0.40f },
    { "TWILIGHT",    RGB(24, 86, 50),    RGB(18, 74, 42),    RGB(14, 10, 50),   RGB(70, 44, 100),   RGB(50, 44, 100),   RGB(150, 130, 200), RGB(16, 80, 60),   0.35f },
};
static unsigned HAZE = RGB(175, 218, 255);
static unsigned SKYTOP = RGB(30, 100, 225);

/* ---------- Pist ---------- */
static float tcx[M], tcz[M], tfx[M], tfz[M], tsd[M], tth[M];
static float TL = 0;
static float mapMinX, mapMaxX, mapMinZ, mapMaxZ;

static const float ctrl0[12][2] = {
    {0, 0}, {160, 0}, {300, 60}, {360, 180}, {300, 300}, {160, 330},
    {60, 260}, {-40, 330}, {-180, 300}, {-260, 180}, {-200, 60}, {-100, 40} };
static float ctrl[24][2];
static int nCtrl = 12;
static float trackSC = 1.3f;
typedef struct { float rx, rz, e, k, ph; } TrackShape;
static const TrackShape shapes[NTRACKS] = {
    { 0, 0, 0, 0, 0 },
    { 520, 300, 0.05f, 3, 0.0f },   /* oval */
    { 420, 420, 0.20f, 3, 0.5f },   /* uc yaprak */
    { 560, 380, 0.10f, 5, 0.5f },   /* dalgali */
};

static void catmull(float p0, float p1, float p2, float p3, float t, float *o) {
    float t2 = t * t, t3 = t2 * t;
    *o = 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}

#define DENSE_MAX 480
static void buildTrackPath(void) {
    static float dx[DENSE_MAX], dz[DENSE_MAX], cum[DENSE_MAX + 1];
    if (trackSel == 0) {
        nCtrl = 12; trackSC = 1.3f;
        for (int i = 0; i < 12; i++) { ctrl[i][0] = ctrl0[i][0]; ctrl[i][1] = ctrl0[i][1]; }
    } else {
        const TrackShape *sh = &shapes[trackSel];
        nCtrl = 24; trackSC = 1.0f;
        for (int i = 0; i < 24; i++) {
            float th = i * 2 * PI / 24.0f;
            float r = 1.0f + sh->e * cosf(sh->k * th + sh->ph);
            ctrl[i][0] = sh->rx * r * cosf(th);
            ctrl[i][1] = sh->rz * r * sinf(th);
        }
    }
    const int per = DENSE_MAX / nCtrl;
    const int KD = nCtrl * per;
    const float SC = trackSC;
    int k = 0;
    for (int i = 0; i < nCtrl; i++) {
        const float *p0 = ctrl[(i + nCtrl - 1) % nCtrl], *p1 = ctrl[i], *p2 = ctrl[(i + 1) % nCtrl], *p3 = ctrl[(i + 2) % nCtrl];
        for (int j = 0; j < per; j++) {
            float t = (float)j / per, x, z;
            catmull(p0[0], p1[0], p2[0], p3[0], t, &x);
            catmull(p0[1], p1[1], p2[1], p3[1], t, &z);
            dx[k] = x * SC; dz[k] = z * SC; k++;
        }
    }
    cum[0] = 0;
    for (int i = 0; i < KD; i++) {
        int j = (i + 1) % KD;
        cum[i + 1] = cum[i] + sqrtf((dx[j] - dx[i]) * (dx[j] - dx[i]) + (dz[j] - dz[i]) * (dz[j] - dz[i]));
    }
    TL = cum[KD];
    int ptr = 0;
    for (int m = 0; m < M; m++) {
        float target = TL * m / M;
        while (ptr < KD - 1 && cum[ptr + 1] < target) ptr++;
        int j = (ptr + 1) % KD;
        float seglen = cum[ptr + 1] - cum[ptr];
        float u = seglen > 0.0001f ? (target - cum[ptr]) / seglen : 0;
        tcx[m] = dx[ptr] + (dx[j] - dx[ptr]) * u;
        tcz[m] = dz[ptr] + (dz[j] - dz[ptr]) * u;
        tsd[m] = target;
    }
    mapMinX = mapMinZ = 1e9f; mapMaxX = mapMaxZ = -1e9f;
    for (int m = 0; m < M; m++) {
        int a = (m + M - 1) % M, b = (m + 1) % M;
        float fx = tcx[b] - tcx[a], fz = tcz[b] - tcz[a];
        float l = sqrtf(fx * fx + fz * fz);
        if (l < 0.0001f) l = 1;
        tfx[m] = fx / l; tfz[m] = fz / l; tth[m] = atan2f(tfz[m], tfx[m]);
        if (tcx[m] < mapMinX) mapMinX = tcx[m];
        if (tcx[m] > mapMaxX) mapMaxX = tcx[m];
        if (tcz[m] < mapMinZ) mapMinZ = tcz[m];
        if (tcz[m] > mapMaxZ) mapMaxZ = tcz[m];
    }
}

static int nFlat = 0, nScene = 0;
static int kartStart[NK], kartCount = 0;
static int boxStart, boxCount, coneStart, coneCount, flameStart, flameCount, shadowStart, shadowCount;
static int sparkStart, sparkCount;
static int groundEnd, roadStart, roadEnd;
static int shellGStart, shellGCount, shellRStart, shellRCount, banStart, banCount, ghostStart, ghostCount3;
static unsigned char padMark[M];

static void buildKartMesh(unsigned body, unsigned trim) {
    unsigned dark = RGB(30, 30, 34), grey = RGB(150, 150, 158), skin = RGB(255, 208, 165);
    box3(0, 0.55f, 0, 1.55f, 0.28f, 0.75f, body);
    box3(1.9f, 0.45f, 0, 0.5f, 0.2f, 0.5f, body);
    box3(2.35f, 0.3f, 0, 0.16f, 0.07f, 1.05f, trim);
    box3(-1.75f, 1.35f, 0, 0.25f, 0.07f, 1.0f, trim);
    box3(-1.6f, 1.0f, 0.55f, 0.06f, 0.32f, 0.06f, dark);
    box3(-1.6f, 1.0f, -0.55f, 0.06f, 0.32f, 0.06f, dark);
    box3(-1.3f, 0.95f, 0, 0.3f, 0.25f, 0.5f, grey);
    for (int i = 0; i < 2; i++) {
        float s = i ? 1.0f : -1.0f;
        box3(1.25f, 0.42f, s * 1.0f, 0.42f, 0.42f, 0.22f, dark);
        box3(1.25f, 0.42f, s * 1.23f, 0.2f, 0.2f, 0.02f, grey);
        box3(-1.2f, 0.5f, s * 1.05f, 0.5f, 0.5f, 0.28f, dark);
        box3(-1.2f, 0.5f, s * 1.34f, 0.24f, 0.24f, 0.02f, grey);
    }
    box3(-0.2f, 1.0f, 0, 0.3f, 0.38f, 0.38f, trim);
    box3(-0.2f, 1.62f, 0, 0.27f, 0.27f, 0.27f, skin);
    box3(-0.2f, 1.78f, 0, 0.31f, 0.2f, 0.31f, body);
    box3(0.1f, 1.62f, 0, 0.04f, 0.1f, 0.22f, dark);
    box3(0.15f, 1.0f, 0.42f, 0.25f, 0.07f, 0.07f, skin);
    box3(0.15f, 1.0f, -0.42f, 0.25f, 0.07f, 0.07f, skin);
    box3(0.5f, 1.05f, 0, 0.04f, 0.12f, 0.18f, dark);
}

static void buildSparkMesh(void) {
    sparkStart = nv;
    P3 a = mk(0, 0.9f, 0);
    P3 b = mk(-0.42f, -0.25f, 0);
    P3 c = mk(0.42f, -0.25f, 0);
    P3 d = mk(0, 0, 0.55f);
    P3 e = mk(0, 0, -0.55f);
    unsigned q = RGB(255, 220, 70);
    tri3(a, b, d, q); tri3(a, d, c, q);
    tri3(a, c, e, q); tri3(a, e, b, q);
    sparkCount = nv - sparkStart;
}

/* kabuk: ust yari renkli, alt yari krem, ortada beyaz bant */
static void sphereMesh(float r, float sy, int seg, int rings, unsigned top, unsigned bot, unsigned band) {
    for (int j = 0; j < rings; j++) {
        float p0 = -PI * 0.5f + PI * j / rings, p1 = -PI * 0.5f + PI * (j + 1) / rings;
        float pm = (p0 + p1) * 0.5f;
        unsigned base = pm > 0.25f ? top : (pm < -0.25f ? bot : band);
        for (int i = 0; i < seg; i++) {
            float a0 = i * 2 * PI / seg, a1 = (i + 1) * 2 * PI / seg, am = (a0 + a1) * 0.5f;
            float sh = (0.70f + 0.30f * (0.5f + 0.5f * cosf(am - 0.8f))) * (0.85f + 0.25f * sinf(pm));
            P3 q00 = mk(cosf(a0) * cosf(p0) * r, sinf(p0) * r * sy, sinf(a0) * cosf(p0) * r);
            P3 q10 = mk(cosf(a1) * cosf(p0) * r, sinf(p0) * r * sy, sinf(a1) * cosf(p0) * r);
            P3 q11 = mk(cosf(a1) * cosf(p1) * r, sinf(p1) * r * sy, sinf(a1) * cosf(p1) * r);
            P3 q01 = mk(cosf(a0) * cosf(p1) * r, sinf(p1) * r * sy, sinf(a0) * cosf(p1) * r);
            quad3(q00, q10, q11, q01, scol(base, sh));
        }
    }
}

/* muz: yay seklinde kivrilan, uclari kahverengi koni gibi daralan tup */
static void bananaMesh(void) {
    enum { N = 8, S = 6 };
    const float R = 1.5f;
    P3 ring[N + 1][S];
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N, a = -0.95f + 1.9f * t;
        float cx = sinf(a) * R, cy = (1.0f - cosf(a)) * R + 0.15f;
        float rad = 0.08f + 0.26f * sinf(PI * t);
        float nx = -sinf(a), ny = cosf(a);
        for (int q = 0; q < S; q++) {
            float ph = q * 2 * PI / S;
            ring[i][q] = mk(cx + nx * rad * cosf(ph), cy + ny * rad * cosf(ph), rad * sinf(ph));
        }
    }
    for (int i = 0; i < N; i++) for (int q = 0; q < S; q++) {
        int q2 = (q + 1) % S;
        float ph = (q + 0.5f) * 2 * PI / S;
        unsigned c = (i == 0 || i == N - 1) ? RGB(110, 76, 30) : scol(RGB(255, 214, 40), 0.72f + 0.28f * (0.5f + 0.5f * cosf(ph - 0.6f)));
        quad3(ring[i][q], ring[i + 1][q], ring[i + 1][q2], ring[i][q2], c);
    }
}

static void buildMesh(void) {
    nv = 0;
    const Theme *th = &themes[trackSel];
    HAZE = th->haze; SKYTOP = th->sky;
    unsigned G1 = th->g1, G2 = th->g2;
    /* --- duz katmanlar (derinlik testsiz, sirayla) --- */
    /* zemin (dokulu) */
    const int GN = 24; const float GS = 125.0f;
    for (int i = 0; i < GN; i++) for (int j = 0; j < GN; j++) {
        float x0 = -1500 + i * GS, z0 = -1500 + j * GS;
        tquad(mk(x0, 0, z0), mk(x0 + GS, 0, z0), mk(x0 + GS, 0, z0 + GS), mk(x0, 0, z0 + GS), ((i + j) & 1) ? G1 : G2,
              x0 / 24.0f, z0 / 24.0f, (x0 + GS) / 24.0f, (z0 + GS) / 24.0f);
    }
    groundEnd = nv;
    /* cim seritleri (yolun yani) */
    for (int k = 0; k < M; k++) {
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        unsigned c = ((k / 4) & 1) ? scol(G1, 1.18f) : scol(G1, 1.08f);
        float a = HW + 1.8f, b = HW + 14.0f;
        quad3(mk(tcx[k] + rx0 * a, 0.0f, tcz[k] + rz0 * a), mk(tcx[k] + rx0 * b, 0.0f, tcz[k] + rz0 * b),
              mk(tcx[k2] + rx1 * b, 0.0f, tcz[k2] + rz1 * b), mk(tcx[k2] + rx1 * a, 0.0f, tcz[k2] + rz1 * a), c);
        quad3(mk(tcx[k] - rx0 * a, 0.0f, tcz[k] - rz0 * a), mk(tcx[k] - rx0 * b, 0.0f, tcz[k] - rz0 * b),
              mk(tcx[k2] - rx1 * b, 0.0f, tcz[k2] - rz1 * b), mk(tcx[k2] - rx1 * a, 0.0f, tcz[k2] - rz1 * a), c);
    }
    /* rumble */
    for (int k = 0; k < M; k++) {
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        unsigned c = ((k / 2) & 1) ? RGB(225, 40, 40) : RGB(245, 245, 245);
        float a = HW, b = HW + 1.8f;
        quad3(mk(tcx[k] + rx0 * a, 0, tcz[k] + rz0 * a), mk(tcx[k] + rx0 * b, 0, tcz[k] + rz0 * b),
              mk(tcx[k2] + rx1 * b, 0, tcz[k2] + rz1 * b), mk(tcx[k2] + rx1 * a, 0, tcz[k2] + rz1 * a), c);
        quad3(mk(tcx[k] - rx0 * a, 0, tcz[k] - rz0 * a), mk(tcx[k] - rx0 * b, 0, tcz[k] - rz0 * b),
              mk(tcx[k2] - rx1 * b, 0, tcz[k2] - rz1 * b), mk(tcx[k2] - rx1 * a, 0, tcz[k2] - rz1 * a), c);
    }
    /* yol (dokulu asfalt) */
    roadStart = nv;
    for (int k = 0; k < M; k++) {
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        unsigned c = ((k / 3) & 1) ? RGB(96, 96, 102) : RGB(106, 106, 112);
        float v0 = tsd[k] / 8.0f, v1 = (k2 == 0 ? TL : tsd[k2]) / 8.0f;
        tquad(mk(tcx[k] - rx0 * HW, 0, tcz[k] - rz0 * HW), mk(tcx[k] + rx0 * HW, 0, tcz[k] + rz0 * HW),
              mk(tcx[k2] + rx1 * HW, 0, tcz[k2] + rz1 * HW), mk(tcx[k2] - rx1 * HW, 0, tcz[k2] - rz1 * HW), c,
              0.0f, v0, 2.0f, v1);
    }
    roadEnd = nv;
    /* turbo seritleri */
    memset(padMark, 0, sizeof(padMark));
    for (int q = 1; q < 10; q++) {
        int b = M * q / 10 + 7;
        for (int j = 0; j < 7; j++) {
            int k = (b + j) % M, k2 = (k + 1) % M;
            padMark[k] = 1;
            float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
            float w = HW * 0.4f;
            unsigned c = (j & 1) ? RGB(255, 215, 30) : RGB(255, 120, 20);
            quad3(mk(tcx[k] - rx0 * w, 0, tcz[k] - rz0 * w), mk(tcx[k] + rx0 * w, 0, tcz[k] + rz0 * w),
                  mk(tcx[k2] + rx1 * w, 0, tcz[k2] + rz1 * w), mk(tcx[k2] - rx1 * w, 0, tcz[k2] - rz1 * w), c);
        }
    }
    /* seritler */
    for (int k = 0; k < M; k++) {
        if ((k % 4) > 1) continue;
        int k2 = (k + 1) % M;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        for (int ln = -1; ln <= 1; ln += 2) {
            float o = ln * HW / 3.0f, w = 0.22f;
            quad3(mk(tcx[k] + rx0 * (o - w), 0, tcz[k] + rz0 * (o - w)), mk(tcx[k] + rx0 * (o + w), 0, tcz[k] + rz0 * (o + w)),
                  mk(tcx[k2] + rx1 * (o + w), 0, tcz[k2] + rz1 * (o + w)), mk(tcx[k2] + rx1 * (o - w), 0, tcz[k2] + rz1 * (o - w)), RGB(240, 240, 240));
        }
    }
    /* baslangic cizgisi */
    for (int row = 0; row < 2; row++) {
        int k = row, k2 = row + 1;
        float rx0 = -tfz[k], rz0 = tfx[k], rx1 = -tfz[k2], rz1 = tfx[k2];
        for (int i = 0; i < 8; i++) {
            float a = -HW + i * (2 * HW / 8.0f), b = a + 2 * HW / 8.0f;
            quad3(mk(tcx[k] + rx0 * a, 0, tcz[k] + rz0 * a), mk(tcx[k] + rx0 * b, 0, tcz[k] + rz0 * b),
                  mk(tcx[k2] + rx1 * b, 0, tcz[k2] + rz1 * b), mk(tcx[k2] + rx1 * a, 0, tcz[k2] + rz1 * a),
                  ((i + row) & 1) ? RGB(250, 250, 250) : RGB(20, 20, 20));
        }
    }
    nFlat = nv;

    /* --- sahne objeleri --- */
    /* kapi */
    {
        float fx = tfx[0], fz = tfz[0], rx = -fz, rz = fx;
        float px0 = tcx[0], pz0 = tcz[0];
        float off = HW + 2.0f;
        boxO(px0 + rx * off, 4.0f, pz0 + rz * off, fx, fz, 0.6f, 4.0f, 0.6f, RGB(230, 230, 235));
        boxO(px0 - rx * off, 4.0f, pz0 - rz * off, fx, fz, 0.6f, 4.0f, 0.6f, RGB(230, 230, 235));
        boxO(px0, 8.4f, pz0, fx, fz, 0.9f, 0.9f, off + 0.6f, RGB(220, 40, 40));
        boxO(px0, 8.4f, pz0, fx, fz, 0.95f, 0.35f, off * 0.7f, RGB(250, 250, 250));
    }
    /* agaclar */
    for (int k = 0; k < M; k += 2) {
        for (int side = -1; side <= 1; side += 2) {
            if (rnd() < th->skip) continue;
            float lat = side * (HW + 15.0f + rnd() * 18.0f);
            float x = tcx[k] - tfz[k] * lat, z = tcz[k] + tfx[k] * lat;
            float s = 0.8f + rnd() * 0.9f;
            int pine = rnd() < 0.7f;
            box3(x, 1.2f * s, z, 0.35f * s, 1.2f * s, 0.35f * s, RGB(110, 72, 40));
            unsigned leaf = pine ? scol(th->leaf, 0.9f + rnd() * 0.35f) : scol(th->leaf, 1.25f + rnd() * 0.3f);
            cone3(x, 2.0f * s, z, 2.2f * s, 4.2f * s, 6, rnd() * 3, leaf);
            cone3(x, 3.8f * s, z, 1.6f * s, 3.6f * s, 6, rnd() * 3, scol(leaf, 1.1f));
        }
    }
    /* daglar */
    {
        float mx = (mapMinX + mapMaxX) * 0.5f, mz = (mapMinZ + mapMaxZ) * 0.5f;
        float ext = 0;
        for (int m = 0; m < M; m++) {
            float d = sqrtf((tcx[m] - mx) * (tcx[m] - mx) + (tcz[m] - mz) * (tcz[m] - mz));
            if (d > ext) ext = d;
        }
        for (int i = 0; i < 22; i++) {
            float a = i * 2 * PI / 22 + rnd() * 0.1f;
            float rad = ext + 470.0f + rnd() * 120.0f;
            float h = 160.0f + rnd() * 160.0f, r = h * (0.9f + rnd() * 0.5f);
            float x = mx + cosf(a) * rad, z = mz + sinf(a) * rad;
            cone3(x, 0, z, r, h, 5, rnd() * 3, th->mtn);
            cone3(x, h * 0.72f, z, r * 0.29f, h * 0.28f, 5, 0, th->cap);
        }
    }
    nScene = nv;

    /* --- arac meshleri --- */
    kartCount = 0;
    for (int i = 0; i < NK; i++) {
        kartStart[i] = nv;
        buildKartMesh(kBody[kChar[i]], kTrim[kChar[i]]);
        kartCount = nv - kartStart[i];
    }
    /* ghost araci: yari saydam */
    pvAlpha = 140;
    ghostStart = nv;
    buildKartMesh(kBody[ghostBestChar], kTrim[ghostBestChar]);
    ghostCount3 = nv - ghostStart;
    pvAlpha = 255;
    boxStart = nv;
    box3(0, 0, 0, 0.8f, 0.8f, 0.8f, RGB(255, 200, 40));
    box3(0, 0.82f, 0, 0.82f, 0.02f, 0.82f, RGB(255, 245, 160));
    box3(0, 0, 0.82f, 0.18f, 0.5f, 0.02f, RGB(255, 255, 255));
    box3(0, 0, -0.82f, 0.18f, 0.5f, 0.02f, RGB(255, 255, 255));
    box3(0.82f, 0, 0, 0.02f, 0.5f, 0.18f, RGB(255, 255, 255));
    box3(-0.82f, 0, 0, 0.02f, 0.5f, 0.18f, RGB(255, 255, 255));
    boxCount = nv - boxStart;
    coneStart = nv;
    cone3(0, 0, 0, 0.9f, 2.2f, 8, 0, RGB(250, 120, 20));
    cone3(0, 0.7f, 0, 0.64f, 1.0f, 8, 0, RGB(255, 255, 255));
    box3(0, 0.1f, 0, 1.0f, 0.1f, 1.0f, RGB(60, 60, 66));
    coneCount = nv - coneStart;
    flameStart = nv;
    box3(-0.6f, 0, 0.45f, 0.6f, 0.13f, 0.13f, RGB(255, 140, 20));
    box3(-0.6f, 0, -0.45f, 0.6f, 0.13f, 0.13f, RGB(255, 140, 20));
    box3(-0.45f, 0, 0.45f, 0.45f, 0.08f, 0.08f, RGB(255, 235, 120));
    box3(-0.45f, 0, -0.45f, 0.45f, 0.08f, 0.08f, RGB(255, 235, 120));
    flameCount = nv - flameStart;
    shadowStart = nv;
    quad3(mk(-2.0f, 0.0f, -1.3f), mk(2.4f, 0.0f, -1.3f), mk(2.4f, 0.0f, 1.3f), mk(-2.0f, 0.0f, 1.3f), RGBA(0, 0, 0, 110));
    shadowCount = nv - shadowStart;
    /* gercek yesil / kirmizi kabuk ve muz meshleri */
    shellGStart = nv;
    sphereMesh(1.0f, 0.62f, 10, 6, RGB(40, 200, 70), RGB(245, 235, 200), RGB(250, 250, 250));
    shellGCount = nv - shellGStart;
    shellRStart = nv;
    sphereMesh(1.0f, 0.62f, 10, 6, RGB(225, 45, 45), RGB(245, 235, 200), RGB(250, 250, 250));
    shellRCount = nv - shellRStart;
    banStart = nv;
    bananaMesh();
    banCount = nv - banStart;
    buildSparkMesh();
    sceKernelDcacheWritebackAll();
}

/* ---------- Oyun nesneleri ---------- */
typedef struct {
    float x, z, h, vh, spd;
    int idx, lapc;
    float prog, lat;
    float boostT, driftT, spinT, spinA;
    float slowT, starT;
    int shield, driftDir;
    int item;
    float skill, lane, steerVis;
    float bobA, lean, pitch, accelVis, fxTimer;
} Kart;
static Kart K[NK];

typedef struct { float x, z; int type; int active; float resp; } Thing;

/* ---------- Animasyon / parcacik efektleri ---------- */
typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life, maxLife, size;
    int active, kind;
} FxPart;
#define MAXFX 32
static FxPart fx[MAXFX];
static int fxHead = 0;

static void spawnFx(float x, float y, float z, float vx, float vy, float vz,
                    float life, float size, int kind) {
    int id = fxHead++ % MAXFX;
    fx[id].x = x; fx[id].y = y; fx[id].z = z;
    fx[id].vx = vx; fx[id].vy = vy; fx[id].vz = vz;
    fx[id].life = life; fx[id].maxLife = life; fx[id].size = size;
    fx[id].active = 1; fx[id].kind = kind;
}

static void spawnKartFx(int who, int kind, int count) {
    if (who < 0 || who >= NK) return;
    Kart *k = &K[who];
    float ca = cosf(k->h), sa = sinf(k->h);
    for (int n = 0; n < count; n++) {
        float a = k->h + PI + (rnd() - 0.5f) * 0.8f;
        float sp = 4.0f + rnd() * 9.0f + k->spd * 0.06f;
        float side = (rnd() - 0.5f) * 1.2f;
        float dist = 1.8f + rnd() * 0.9f;
        float px = k->x - ca * dist - sa * side;
        float pz = k->z - sa * dist + ca * side;
        float py = 0.18f + rnd() * 0.7f;
        spawnFx(px, py, pz,
                cosf(a) * sp + (rnd() - 0.5f) * 2.0f,
                1.8f + rnd() * 4.5f,
                sinf(a) * sp + (rnd() - 0.5f) * 2.0f,
                0.22f + rnd() * 0.30f,
                0.16f + rnd() * 0.16f,
                kind);
    }
}

static void updateFx(void) {
    for (int i = 0; i < MAXFX; i++) if (fx[i].active) {
        fx[i].x += fx[i].vx * DT;
        fx[i].y += fx[i].vy * DT;
        fx[i].z += fx[i].vz * DT;
        fx[i].vy -= 10.0f * DT;
        fx[i].life -= DT;
        if (fx[i].life <= 0 || fx[i].y < 0.03f) fx[i].active = 0;
    }
}


/* type: 0 = esya kutusu, 1 = trafik konisi, 2 = muz tuzagi */
#define TH_ITEM   0
#define TH_CONE   1
#define TH_BANANA 2
#define MAXTHING 64
static Thing things[MAXTHING];
static int nthings = 0;

static int state, lap, finalRank;
#define STATE_MENU  (-1)
#define STATE_CHAR  (-2)
#define STATE_TRACK (-3)
#define STATE_PAUSE (3)
static int cameraMode = 0;
static float cd, raceTime, finalTime, shake, lapFlash, tAnim, camH, fovCur;
static unsigned prevB = 0;

/* ---------- Oyun modu ---------- */
#define GAME_RACE       0
#define GAME_TIME_TRIAL 1
static int gameMode = GAME_RACE;
static int menuSel = 0;
static int activeKarts(void) { return gameMode == GAME_RACE ? NK : 1; }

/* ---------- Ses: motor + muzik (sentezlenir, dosya gerekmez) ---------- */
#define AUDIO_BLOCK 512
static volatile float aSpd = 0.0f;
static volatile int aEngineOn = 0, aMusicOn = 1, aBoost = 0, aTrackId = 0;

#define REST 99
static const signed char leadPat[NTRACKS][16] = {
    { 0, 4, 7, 12, 7, 4, 7, 9, 5, 9, 12, 9, 7, 4, 2, 4 },
    { 0, 3, 7, 10, 12, 10, 7, 3, 5, 8, 12, 8, 7, 10, 14, 10 },
    { 12, REST, 7, REST, 9, REST, 5, REST, 7, REST, 4, REST, 2, 4, 5, 7 },
    { 0, REST, 3, 5, 7, REST, 5, 3, 0, REST, -2, 0, 3, 5, 7, REST },
};
static const signed char barRoot[NTRACKS][4] = { { 0, 5, 7, 3 }, { 0, -4, -2, -5 }, { 0, -3, 5, 2 }, { 0, 3, -2, -4 } };
static const float baseFreq[NTRACKS] = { 261.63f, 220.0f, 293.66f, 196.0f };
static const float stepRate[NTRACKS] = { 9.0f, 8.0f, 10.0f, 7.5f };

static int audioThread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, AUDIO_BLOCK, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;
    static short buf[AUDIO_BLOCK * 2];
    float leadPh = 0, bassPh = 0, engPh = 0, eng2Ph = 0, leadF = 0, bassF = 0, stepT = 0, env = 0, engVol = 0;
    int step = 0;
    unsigned noise = 1;
    const float inv = 1.0f / 44100.0f;
    while (1) {
        int tr = aTrackId;
        if (tr < 0 || tr >= NTRACKS) tr = 0;
        float ef = 48.0f + aSpd * 3.6f + (aBoost ? 30.0f : 0.0f);
        float engTarget = aEngineOn ? 1.0f : 0.0f;
        int musicOn = aMusicOn;
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            stepT += stepRate[tr] * inv;
            if (stepT >= 1.0f) {
                stepT -= 1.0f;
                step = (step + 1) & 63;
                int ls = leadPat[tr][step & 15], root = barRoot[tr][(step >> 4) & 3];
                leadF = (ls == REST) ? 0.0f : baseFreq[tr] * powf(2.0f, (ls + root) / 12.0f);
                if ((step & 3) == 0) bassF = baseFreq[tr] * 0.5f * powf(2.0f, root / 12.0f);
                env = 1.0f;
            }
            env *= 0.9997f;
            leadPh += leadF * inv; if (leadPh >= 1.0f) leadPh -= 1.0f;
            bassPh += bassF * inv; if (bassPh >= 1.0f) bassPh -= 1.0f;
            float lead = (leadF > 0.0f) ? (leadPh < 0.25f ? 0.5f : -0.5f) * env : 0.0f;
            float bass = (bassPh < 0.5f ? 4.0f * bassPh - 1.0f : 3.0f - 4.0f * bassPh) * 0.5f;
            float music = musicOn ? (lead * 0.30f + bass * 0.32f) : 0.0f;

            engPh += ef * inv; if (engPh >= 1.0f) engPh -= 1.0f;
            eng2Ph += ef * 2.01f * inv; if (eng2Ph >= 1.0f) eng2Ph -= 1.0f;
            noise = noise * 1664525u + 1013904223u;
            float nz = ((noise >> 16) & 0xFF) / 128.0f - 1.0f;
            engVol += (engTarget - engVol) * 0.0005f;
            float eng = ((2.0f * engPh - 1.0f) * 0.5f + (eng2Ph < 0.5f ? 0.25f : -0.25f) + nz * 0.08f) * 0.30f * engVol;

            float o = music + eng;
            if (o > 1.0f) o = 1.0f;
            if (o < -1.0f) o = -1.0f;
            short sm = (short)(o * 14000.0f);
            buf[2 * i] = sm; buf[2 * i + 1] = sm;
        }
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
    }
    return 0;
}
static void startAudio(void) {
    int th = sceKernelCreateThread("audio_thread", audioThread, 0x12, 0x8000, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
}

/* ---------- Dokular (kodla uretilir, tekrarlanan 64x64) ---------- */
static unsigned int __attribute__((aligned(16))) texGrass[64 * 64];
static unsigned int __attribute__((aligned(16))) texRoad[64 * 64];
static void makeTextures(void) {
    unsigned seed = 777u;
    for (int pass = 0; pass < 2; pass++) {
        unsigned int *tex = pass == 0 ? texGrass : texRoad;
        float coarse[8][8];
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            seed = seed * 1664525u + 1013904223u;
            coarse[y][x] = ((seed >> 8) & 0xFF) / 255.0f;
        }
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            seed = seed * 1664525u + 1013904223u;
            float n = ((seed >> 8) & 0xFF) / 255.0f;
            seed = seed * 1664525u + 1013904223u;
            float n2 = ((seed >> 8) & 0xFF) / 255.0f;
            float c = coarse[y / 8][x / 8];
            float v;
            int r, g, b;
            if (pass == 0) {            /* cim: yaprak benekleri */
                v = 205.0f + 28.0f * c + 22.0f * n;
                if (n2 > 0.93f) v -= 38.0f;
                r = (int)(v * 0.96f); g = (int)v; b = (int)(v * 0.88f);
            } else {                    /* asfalt: ince cakil */
                v = 215.0f + 14.0f * c + 22.0f * n;
                if (n2 > 0.95f) v -= 55.0f;
                r = (int)v; g = (int)v; b = (int)(v * 1.02f);
            }
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            tex[y * 64 + x] = RGB(r, g, b);
        }
    }
    sceKernelDcacheWritebackAll();
}

/* ---------- Kayit sistemi (v2) ---------- */
#define SAVE_DIR   "ms0:/PSP/SAVEDATA/MKPSP"
#define SAVE_FILE  "ms0:/PSP/SAVEDATA/MKPSP/save2.dat"
#define SAVE_MAGIC 0x32504B4Du   /* 'MKP2' */

typedef struct {
    unsigned magic;
    float bestLap[NTRACKS];     /* pist basina en iyi tur */
    float bestRace[NTRACKS];    /* pist basina en iyi yaris */
    float bestTT[NTRACKS];      /* pist basina time trial rekoru */
    int totalWins;
    int lastChar, lastTrack;
} SaveData;

static SaveData saveData;

/* ---------- Ghost (pist basina ayri dosya, 30 Hz kayit, en fazla 300 sn, checksum) ---------- */
#define GHOST_MAX   9000
#define GHOST_MAGIC 0x31485347u  /* 'GSH1' */
typedef struct { float x, z, h; } GhostPoint;
typedef struct { unsigned magic; int version, track, charId, count; float time; unsigned checksum; } GhostHeader;

static GhostPoint ghostRec[GHOST_MAX];    /* su an kaydedilen tur */
static GhostPoint ghostBest[GHOST_MAX];   /* en iyi tur (oynatilan) */
static int ghostCount = 0, ghostBestCount = 0, ghostMode = 0, ghostTick = 0;
static float ghostBestTime = 0;

static float lapTimer = 0, raceBestLap = 0;
static int newRecord = 0;

static unsigned ghostSum(const GhostPoint *g, int n) {
    const unsigned char *q = (const unsigned char *)g;
    unsigned h = 2166136261u;
    for (size_t i = 0; i < (size_t)n * sizeof(GhostPoint); i++) { h ^= q[i]; h *= 16777619u; }
    return h;
}
static void ghostPath(char *out, int n, int track) { snprintf(out, n, SAVE_DIR "/ghost_t%d.dat", track); }

static void saveGhost(int track, float time, int charId) {
    char path[96];
    ghostPath(path, sizeof(path), track);
    GhostHeader hd;
    hd.magic = GHOST_MAGIC; hd.version = 1; hd.track = track; hd.charId = charId;
    hd.count = ghostBestCount; hd.time = time; hd.checksum = ghostSum(ghostBest, ghostBestCount);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &hd, sizeof(hd));
        sceIoWrite(fd, ghostBest, ghostBestCount * sizeof(GhostPoint));
        sceIoClose(fd);
    }
}

static void loadGhost(int track) {
    ghostBestCount = 0; ghostBestChar = 0; ghostBestTime = 0;
    char path[96];
    ghostPath(path, sizeof(path), track);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0) return;
    GhostHeader hd;
    if (sceIoRead(fd, &hd, sizeof(hd)) == (int)sizeof(hd) &&
        hd.magic == GHOST_MAGIC && hd.version == 1 && hd.track == track &&
        hd.count > 0 && hd.count <= GHOST_MAX && hd.charId >= 0 && hd.charId < NK) {
        int bytes = hd.count * (int)sizeof(GhostPoint);
        if (sceIoRead(fd, ghostBest, bytes) == bytes && ghostSum(ghostBest, hd.count) == hd.checksum) {
            ghostBestCount = hd.count; ghostBestChar = hd.charId; ghostBestTime = hd.time;
        }
    }
    sceIoClose(fd);
}

static void saveGame(void) {
    sceIoMkdir("ms0:/PSP/SAVEDATA", 0777);
    sceIoMkdir(SAVE_DIR, 0777);
    saveData.magic = SAVE_MAGIC;
    SceUID fd = sceIoOpen(SAVE_FILE, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &saveData, sizeof(saveData));
        sceIoClose(fd);
    }
}

static void loadSave(void) {
    memset(&saveData, 0, sizeof(saveData));
    SceUID fd = sceIoOpen(SAVE_FILE, PSP_O_RDONLY, 0777);
    if (fd >= 0) {
        sceIoRead(fd, &saveData, sizeof(saveData));
        sceIoClose(fd);
    }
    if (saveData.magic != SAVE_MAGIC) memset(&saveData, 0, sizeof(saveData));
    saveData.magic = SAVE_MAGIC;
    for (int i = 0; i < NTRACKS; i++) {
        if (!(saveData.bestLap[i] >= 0)) saveData.bestLap[i] = 0;    /* bozuk veri korumasi */
        if (!(saveData.bestRace[i] >= 0)) saveData.bestRace[i] = 0;
        if (!(saveData.bestTT[i] >= 0)) saveData.bestTT[i] = 0;
    }
    if (saveData.totalWins < 0) saveData.totalWins = 0;
    if (saveData.lastChar < 0 || saveData.lastChar >= NK) saveData.lastChar = 0;
    if (saveData.lastTrack < 0 || saveData.lastTrack >= NTRACKS) saveData.lastTrack = 0;
}

/* tur bitince en iyi tur kontrolu */
static void checkBestLap(void) {
    if (lapTimer <= 0) return;
    float *bl = &saveData.bestLap[trackSel];
    if (*bl == 0 || lapTimer < *bl) *bl = lapTimer;
    if (raceBestLap == 0 || lapTimer < raceBestLap) raceBestLap = lapTimer;
}

/* yaris bitince: istatistik + kayit */
static void finishRace(float raceT, int rank) {
    checkBestLap();
    newRecord = 0;
    int t = trackSel;
    if (gameMode == GAME_RACE) {
        if (rank == 1) saveData.totalWins++;
        if (saveData.bestRace[t] == 0 || raceT < saveData.bestRace[t]) saveData.bestRace[t] = raceT;
    } else {
        if (saveData.bestTT[t] == 0 || raceT < saveData.bestTT[t]) {
            saveData.bestTT[t] = raceT;
            newRecord = 1;
            if (ghostCount > 0) {
                memcpy(ghostBest, ghostRec, ghostCount * sizeof(GhostPoint));
                ghostBestCount = ghostCount;
                ghostBestChar = charSel;
                ghostBestTime = raceT;
                saveGhost(t, raceT, charSel);
            }
        }
    }
    saveData.lastChar = charSel; saveData.lastTrack = trackSel;
    saveGame();
}

/* ---------- Ozel gucler ---------- */
#define ITEM_NONE        0
#define ITEM_MUSH        1
#define ITEM_GREENSHELL  2
#define ITEM_REDSHELL    3
#define ITEM_BANANA      4
#define ITEM_LIGHT       5
#define ITEM_SHIELD      6
#define ITEM_STAR        7

#define SHELL_GREEN 0
#define SHELL_RED   1

typedef struct {
    float x, z;
    float vx, vz;
    float speed;
    float life;
    int active;
    int target;
    int type;
    int owner;
} ItemShot;

static ItemShot shellShots[NK];
static int itemCntG = 0;

static const char *itemName(int item) {
    switch (item) {
    case ITEM_MUSH:   return "MUSH";
    case ITEM_GREENSHELL: return "GREEN";
    case ITEM_REDSHELL:   return "RED";
    case ITEM_BANANA: return "BANANA";
    case ITEM_LIGHT:  return "LIGHT";
    case ITEM_SHIELD: return "SHIELD";
    case ITEM_STAR:   return "STAR";
    }
    return "NONE";
}

static unsigned itemColor(int item) {
    switch (item) {
    case ITEM_MUSH:   return RGB(235, 50, 50);
    case ITEM_GREENSHELL: return RGB(50, 220, 80);
    case ITEM_REDSHELL:   return RGB(240, 60, 60);
    case ITEM_BANANA: return RGB(255, 210, 40);
    case ITEM_LIGHT:  return RGB(255, 245, 70);
    case ITEM_SHIELD: return RGB(80, 230, 255);
    case ITEM_STAR:   return RGB(255, 150, 40);
    }
    return RGB(180, 180, 180);
}

static int calcRank(void);
static int kartRank(int who) {
    int r = 1;
    for (int i = 0; i < NK; i++) if (i != who && K[i].prog > K[who].prog) r++;
    return r;
}
static void initRace(void);
static int addBanana(float x, float z);

/* kabuk cikarsa yesil veya kirmizi */
static int randShell(void) { return rnd() < 0.5f ? ITEM_GREENSHELL : ITEM_REDSHELL; }

static int randomItem(void) {
    int rank = calcRank();
    int r = (int)(rnd() * 100.0f);
    if (rank >= 4) {
        if (r < 25) return ITEM_STAR;
        if (r < 45) return ITEM_LIGHT;
        if (r < 65) return randShell();
        if (r < 80) return ITEM_MUSH;
        if (r < 90) return ITEM_SHIELD;
        return ITEM_BANANA;
    }
    if (r < 30) return ITEM_MUSH;
    if (r < 50) return ITEM_BANANA;
    if (r < 70) return randShell();
    if (r < 82) return ITEM_SHIELD;
    if (r < 93) return ITEM_LIGHT;
    return ITEM_STAR;
}

static void giveItem(Kart *p) {
    p->item = randomItem();
    if (p == &K[0]) itemCntG = 1;
}

static int botRandomItem(int who) {
    int rank = kartRank(who);
    int r = (int)(rnd() * 100.0f);
    if (rank >= 4) {
        if (r < 24) return ITEM_STAR;
        if (r < 44) return ITEM_LIGHT;
        if (r < 68) return randShell();
        if (r < 82) return ITEM_MUSH;
        if (r < 92) return ITEM_BANANA;
        return ITEM_SHIELD;
    }
    if (r < 28) return ITEM_MUSH;
    if (r < 50) return ITEM_BANANA;
    if (r < 73) return randShell();
    if (r < 86) return ITEM_SHIELD;
    if (r < 95) return ITEM_LIGHT;
    return ITEM_STAR;
}

static void botGiveItem(int who) {
    if (who <= 0 || K[who].item != ITEM_NONE) return;
    K[who].item = botRandomItem(who);
}

static int botTargetAhead(int who) {
    int best = -1; float bestD = 1e30f;
    for (int i = 0; i < NK; i++) if (i != who) {
        float d = K[i].prog - K[who].prog;
        if (d > 0 && d < bestD) { bestD = d; best = i; }
    }
    if (best < 0) {
        for (int i = 0; i < NK; i++) if (i != who) {
            float d = fabsf(K[i].prog - K[who].prog);
            if (d < bestD) { bestD = d; best = i; }
        }
    }
    return best;
}

static int botTargetBehind(int who) {
    int best = -1; float bestD = 1e30f;
    for (int i = 0; i < NK; i++) if (i != who) {
        float d = K[who].prog - K[i].prog;
        if (d > 0 && d < bestD) { bestD = d; best = i; }
    }
    return best;
}

static void fireGreenShell(int owner, float x, float z, float ang) {
    for (int i = 0; i < NK; i++) {
        if (!shellShots[i].active) {
            ItemShot *sh = &shellShots[i];
            sh->active = 1;
            sh->type = SHELL_GREEN;
            sh->owner = owner;
            sh->target = -1;
            sh->x = x;
            sh->z = z;
            sh->vx = cosf(ang) * 80.0f;
            sh->vz = sinf(ang) * 80.0f;
            sh->speed = 80.0f;
            sh->life = 8.0f;
            break;
        }
    }
}

static void fireRedShell(int owner, int target) {
    if (target < 0 || target >= NK || owner == target) return;
    for (int i = 0; i < NK; i++) {
        if (!shellShots[i].active) {
            ItemShot *sh = &shellShots[i];
            sh->active = 1;
            sh->type = SHELL_RED;
            sh->owner = owner;
            sh->target = target;
            sh->x = K[owner].x + cosf(K[owner].h) * 2.5f;
            sh->z = K[owner].z + sinf(K[owner].h) * 2.5f;
            sh->vx = sh->vz = 0;
            sh->speed = 72.0f;
            sh->life = 10.0f;
            break;
        }
    }
}

static void botUseItem(int who) {
    if (who <= 0 || state != 1) return;
    Kart *b = &K[who];
    int item = b->item;
    if (item == ITEM_NONE) return;
    int ahead = botTargetAhead(who);
    int behind = botTargetBehind(who);

    if (item == ITEM_MUSH) {
        if (b->spd < VMAX * 0.82f || b->prog < K[0].prog - 40.0f) {
            b->boostT = 2.0f; b->spd += VMAX * 0.16f; b->item = ITEM_NONE;
        }
    } else if (item == ITEM_GREENSHELL) {
        if (ahead >= 0 && fabsf(K[ahead].prog - b->prog) < 420.0f) {
            fireGreenShell(who, b->x + cosf(b->h) * 2.5f, b->z + sinf(b->h) * 2.5f, b->h);
            b->item = ITEM_NONE;
        }
    } else if (item == ITEM_REDSHELL) {
        if (ahead >= 0) {
            fireRedShell(who, ahead);
            b->item = ITEM_NONE;
        }
    } else if (item == ITEM_BANANA) {
        if (behind >= 0 && K[who].prog - K[behind].prog < 120.0f) {
            addBanana(b->x - cosf(b->h) * 2.0f, b->z - sinf(b->h) * 2.0f); b->item = ITEM_NONE;
        }
    } else if (item == ITEM_LIGHT) {
        if (ahead >= 0 || behind >= 0) {
            for (int i = 0; i < NK; i++) if (i != who && K[i].starT <= 0) {
                if (K[i].shield) K[i].shield = 0;
                else { K[i].spd *= 0.45f; K[i].slowT = 2.8f; K[i].spinT = 0.45f; }
            }
            b->item = ITEM_NONE; shake = 0.45f;
        }
    } else if (item == ITEM_SHIELD) {
        if (behind >= 0 || b->prog < K[0].prog + 100.0f) { b->shield = 1; b->item = ITEM_NONE; }
    } else if (item == ITEM_STAR) {
        if (b->prog < K[0].prog + 180.0f || b->spd < VMAX * 0.7f) {
            b->starT = 5.0f; b->boostT = 5.0f; b->spd += VMAX * 0.18f; b->item = ITEM_NONE;
        }
    }
}

static void hitKart(Kart *k, float slow, float spin) {
    int who = (int)(k - K);
    if (k->starT > 0 || k->shield) {
        if (k->shield) {
            k->shield = 0;
            spawnKartFx(who, 2, 8);
        }
        return;
    }
    spawnKartFx(who, 2, 10);
    k->spd *= slow;
    k->boostT = 0;
    k->spinT = spin;
    k->driftDir = 0;
    k->slowT = 0.0f;
}

static int addBanana(float x, float z) {
    for (int i = 0; i < MAXTHING; i++) {
        if (things[i].active == 0 && things[i].type == TH_BANANA) {
            things[i].x = x; things[i].z = z;
            things[i].type = TH_BANANA; things[i].active = 1; things[i].resp = 0.6f;
            if (i >= nthings) nthings = i + 1;
            return 1;
        }
    }
    if (nthings >= MAXTHING) return 0;
    things[nthings].x = x; things[nthings].z = z;
    things[nthings].type = TH_BANANA; things[nthings].active = 1; things[nthings].resp = 0.6f;
    nthings++;
    return 1;
}

static void useItem(void) {
    Kart *p = &K[0];
    int item = p->item;
    if (item == ITEM_NONE || itemCntG <= 0 || state != 1) return;

    itemCntG = 0;
    p->item = ITEM_NONE;

    if (item == ITEM_MUSH) {
        p->boostT = 2.0f;
        p->spd += VMAX * 0.18f;
    } else if (item == ITEM_GREENSHELL) {
        fireGreenShell(0, p->x + cosf(p->h) * 2.5f, p->z + sinf(p->h) * 2.5f, p->h);
    } else if (item == ITEM_REDSHELL) {
        int best = botTargetAhead(0);
        if (best >= 0) fireRedShell(0, best);
    } else if (item == ITEM_BANANA) {
        addBanana(p->x - cosf(p->h) * 2.0f, p->z - sinf(p->h) * 2.0f);
    } else if (item == ITEM_LIGHT) {
        for (int i = 1; i < NK; i++) {
            if (K[i].starT <= 0 && K[i].shield) {
                K[i].shield = 0;
            } else if (K[i].starT <= 0) {
                K[i].spd *= 0.45f;
                K[i].slowT = 2.8f;
                K[i].spinT = 0.45f;
            }
        }
        shake = 0.65f;
    } else if (item == ITEM_SHIELD) {
        p->shield = 1;
    } else if (item == ITEM_STAR) {
        p->starT = 5.0f;
        p->boostT = 5.0f;
        p->spd += VMAX * 0.22f;
    }
}

static void updateShell(void) {
    for (int n = 0; n < NK; n++) {
        ItemShot *shot = &shellShots[n];
        if (!shot->active) continue;
        shot->life -= DT;
        if (shot->life <= 0) { shot->active = 0; continue; }

        if (shot->type == SHELL_GREEN) {
            shot->x += shot->vx * DT;
            shot->z += shot->vz * DT;
            if (shot->x < mapMinX || shot->x > mapMaxX) {
                shot->vx = -shot->vx;
                shot->x = clampf(shot->x, mapMinX, mapMaxX);
            }
            if (shot->z < mapMinZ || shot->z > mapMaxZ) {
                shot->vz = -shot->vz;
                shot->z = clampf(shot->z, mapMinZ, mapMaxZ);
            }
            for (int i = 0; i < activeKarts(); i++) {
                if (i == shot->owner && shot->life > 7.5f) continue;   /* atar atmaz kendine carpmasin */
                float dx = K[i].x - shot->x, dz = K[i].z - shot->z;
                if (dx * dx + dz * dz < 2.2f * 2.2f) {
                    hitKart(&K[i], 0.35f, 1.2f);
                    if (i == 0) shake = 0.35f;
                    shot->active = 0;
                    break;
                }
            }
        } else {
            if (shot->target < 0 || shot->target >= NK || shot->target == shot->owner) { shot->active = 0; continue; }
            Kart *t = &K[shot->target];
            float dx = t->x - shot->x, dz = t->z - shot->z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d < 2.2f) {
                hitKart(t, 0.35f, 1.2f);
                shot->active = 0;
                if (shot->target == 0) shake = 0.35f;
                continue;
            }
            if (d < 0.001f) d = 0.001f;
            shot->x += dx / d * shot->speed * DT;
            shot->z += dz / d * shot->speed * DT;
        }
    }
}

static void updateItemTimers(void) {
    for (int i = 0; i < NK; i++) {
        if (K[i].starT > 0) K[i].starT -= DT;
        if (K[i].slowT > 0) K[i].slowT -= DT;
    }
}

static void buildThings(void) {
    nthings = 0;
    for (int q = 0; q < 9; q++) {
        int s = (M * q / 9 + 10) % M;
        for (int j = -1; j <= 1; j++) {
            float lat = j * HW * 0.42f;
            things[nthings].x = tcx[s] - tfz[s] * lat; things[nthings].z = tcz[s] + tfx[s] * lat;
            things[nthings].type = TH_ITEM; things[nthings].active = 1; things[nthings].resp = 0; nthings++;
        }
    }
    for (int i = 0; i < 20; i++) {
        int s = (M * i / 20 + 17) % M;
        float lat = (((i * 37) % 9) - 4) * 0.2f * HW * 0.75f;
        things[nthings].x = tcx[s] - tfz[s] * lat; things[nthings].z = tcz[s] + tfx[s] * lat;
        things[nthings].type = TH_CONE; things[nthings].active = 1; things[nthings].resp = 0; nthings++;
    }
}

static void trackUpdate(Kart *k) {
    int best = k->idx; float bd = 1e18f;
    for (int j = -8; j <= 12; j++) {
        int i = (k->idx + j + M * 4) % M;
        float dx = k->x - tcx[i], dz = k->z - tcz[i];
        float d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    int old = k->idx;
    if (best < old - M / 2) k->lapc++;
    else if (best > old + M / 2) k->lapc--;
    k->idx = best;
    float dx = k->x - tcx[best], dz = k->z - tcz[best];
    k->lat = dx * (-tfz[best]) + dz * tfx[best];
    float along = dx * tfx[best] + dz * tfz[best];
    k->prog = k->lapc * TL + tsd[best] + along;
    /* gorunmez duvar */
    float lim = HW + 12.0f;
    if (fabsf(k->lat) > lim) {
        float sgn = k->lat > 0 ? 1.0f : -1.0f, over = fabsf(k->lat) - lim;
        k->x -= (-tfz[best]) * over * sgn; k->z -= tfx[best] * over * sgn;
        k->lat = sgn * lim; k->spd *= 0.96f;
    }
}

static void stepKart(Kart *k, float steer, int gas, int brake, int driftBtn) {
    float oldSpd = k->spd;
    const int ki = (int)(k - K);
    const float spdM = cSpd[kChar[ki]], accM = cAcc[kChar[ki]], trnM = cTrn[kChar[ki]];
    if (k->spinT > 0) { k->spinT -= DT; k->spinA += 14.0f * DT; steer = 0; gas = 0; brake = 0; driftBtn = 0; if (k->spinT <= 0) k->spinA = 0; }
    int boosting = k->boostT > 0;
    if (boosting) k->boostT -= DT;
    int off = fabsf(k->lat) > HW + 1.8f;
    float slowMul = (k->slowT > 0 ? 0.48f : 1.0f) * spdM;
    float lim = (boosting ? VMAX * (off ? 0.8f : 1.35f) : (off ? VMAX * 0.45f : VMAX)) * slowMul;
    if (boosting) k->spd += 90.0f * DT;
    else if (brake) k->spd -= 60.0f * DT;
    else if (gas) k->spd += 30.0f * accM * (1.0f - 0.55f * k->spd / VMAX) * DT;
    else k->spd -= 10.0f * DT;
    if (k->spd > lim) k->spd -= (off ? 80.0f : 40.0f) * DT;
    if (k->spd < 0) k->spd = 0;

    if (driftBtn && !k->driftDir && k->spd > VMAX * 0.45f && fabsf(steer) > 0.3f) { k->driftDir = steer > 0 ? 1 : -1; k->driftT = 0; }
    if (k->driftDir) {
        if (!driftBtn || k->spd < VMAX * 0.3f) {
            if (k->driftT > 1.8f) k->boostT = 1.5f;
            else if (k->driftT > 0.8f) k->boostT = 0.9f;
            k->driftDir = 0; k->driftT = 0;
        } else k->driftT += DT;
    }
    float sp = k->spd;
    float rate = 2.0f * trnM * clampf(sp / 14.0f, 0, 1) * (1.0f - 0.25f * sp / VMAX);
    float turn = k->driftDir ? (k->driftDir * 0.85f + steer * 0.75f) * rate * 1.15f : steer * rate;
    k->h = wrapA(k->h + turn * DT);
    float grip = k->driftDir ? 2.4f : 14.0f;
    k->vh = wrapA(k->vh + wrapA(k->h - k->vh) * clampf(grip * DT, 0, 1));
    k->x += cosf(k->vh) * k->spd * DT;
    k->z += sinf(k->vh) * k->spd * DT;
    k->steerVis += (steer - k->steerVis) * 0.2f;

    /* Hareket animasyonlari: suspansiyon, govde yatmasi, ivmelenme */
    k->bobA += DT * (3.0f + k->spd * 0.12f);
    k->accelVis += ((k->spd - oldSpd) - k->accelVis) * 0.22f;
    {
        float targetLean = -k->steerVis * 0.15f - (k->driftDir ? k->driftDir * 0.08f : 0.0f);
        float targetPitch = clampf(-k->accelVis * 0.018f, -0.10f, 0.10f);
        k->lean += (targetLean - k->lean) * 0.20f;
        k->pitch += (targetPitch - k->pitch) * 0.20f;
    }

    /* Boost ve drift sirasinda hareketli parcacik efekti */
    k->fxTimer -= DT;
    if (k->boostT > 0 && k->fxTimer <= 0) {
        spawnKartFx((int)(k - K), 0, 2);
        k->fxTimer = 0.06f;
    } else if (k->driftDir && k->spd > VMAX * 0.45f && k->fxTimer <= 0) {
        spawnKartFx((int)(k - K), 1, 1);
        k->fxTimer = 0.10f;
    }
}

static void aiDrive(Kart *k, int n) {
    int la = (k->idx + 6 + (int)(k->spd / 8.0f)) % M;
    float lane = (k->lane + 0.35f * sinf(k->prog * 0.01f + n)) * HW * 0.6f;
    float tx = tcx[la] - tfz[la] * lane, tz = tcz[la] + tfx[la] * lane;
    float want = atan2f(tz - k->z, tx - k->x);
    float steer = clampf(wrapA(want - k->h) * 2.2f, -1, 1);
    int j = (k->idx + 14) % M;
    float turn = fabsf(wrapA(tth[j] - tth[k->idx]));
    float target = VMAX * k->skill * (1.0f - clampf(turn * 0.8f, 0, 0.35f));
    float pd = k->prog - K[0].prog;
    if (n != 0) { if (pd > 250) target *= 0.93f; else if (pd < -250) target *= 1.07f; }

    /* Botlar da esya kutularini toplar. */
    for (int i = 0; i < nthings; i++) {
        Thing *t = &things[i];
        if (!t->active || t->type != TH_ITEM || k->item != ITEM_NONE) continue;
        float dx = t->x - k->x, dz = t->z - k->z;
        if (dx * dx + dz * dz < 2.3f * 2.3f) {
            t->active = 0; t->resp = 8.0f;
            botGiveItem(n);
            break;
        }
    }

    /* Zorluk ve pozisyona gore akilli esya kullanimi. */
    if (k->item != ITEM_NONE && (rnd() < 0.10f || k->item == ITEM_LIGHT || k->item == ITEM_GREENSHELL || k->item == ITEM_REDSHELL))
        botUseItem(n);

    /* Daha iyi botlar virajlarda hafif fren ve ara sira drift yapar. */
    int drift = (fabsf(turn) > 0.32f && k->spd > VMAX * 0.55f && ((int)(tAnim * 10.0f) + n) % 3 == 0);
    stepKart(k, steer, k->spd < target, k->spd > target + 6.0f, drift);
}

static int calcRank(void) {
    int r = 1;
    for (int i = 1; i < NK; i++) if (K[i].prog > K[0].prog) r++;
    return r;
}

static void initRace(void) {
    state = 0; lap = 1; cd = 4.0f; raceTime = 0; finalRank = 0; finalTime = 0; shake = 0; lapFlash = 0;
    lapTimer = 0; raceBestLap = 0; newRecord = 0; ghostCount = 0; ghostTick = 0;
    ghostMode = (gameMode == GAME_TIME_TRIAL && ghostBestCount > 0);
    itemCntG = 0; fxHead = 0; memset(fx, 0, sizeof(fx));
    for (int i = 0; i < NK; i++) shellShots[i].active = 0;
    static const float skills[NK] = { 0.85f, 0.95f, 0.92f, 0.90f, 0.88f, 0.86f };
    for (int i = 0; i < NK; i++) {
        int ii; float lat;
        if (i == 0) { ii = M - 9; lat = 0; }
        else { int r = (i - 1) / 2; ii = M - 3 - r * 2; lat = ((i - 1) % 2 ? 0.45f : -0.45f) * HW; }
        Kart *k = &K[i];
        memset(k, 0, sizeof(Kart));
        k->x = tcx[ii] - tfz[ii] * lat; k->z = tcz[ii] + tfx[ii] * lat;
        k->h = tth[ii]; k->vh = k->h; k->idx = ii; k->lapc = -1;
        k->skill = skills[i]; k->lane = -0.8f + (i - 1) * 0.4f;
        trackUpdate(k);
    }
    /* Time trial: botlar hareketsiz ve gizli */
    if (gameMode == GAME_TIME_TRIAL) for (int i = 1; i < NK; i++) K[i].spd = 0;
    for (int i = 0; i < nthings; i++) { things[i].active = 1; things[i].resp = 0; }
    camH = K[0].h; fovCur = 62.0f;
    cameraMode = 0;
}

static void assignChars(void) {
    kChar[0] = charSel;
    int n = 1;
    for (int c = 0; c < NK; c++) if (c != charSel) kChar[n++] = c;
}

/* karakter / pist degisince: yolu, meshi ve ghost'u yeniden kur */
static void rebuildAll(void) {
    assignChars();
    loadGhost(trackSel);
    rngState = 12345u + (unsigned)trackSel * 7777u;
    buildTrackPath();
    buildMesh();
    buildThings();
    initRace();
}

static float readSteer(const SceCtrlData *pad) {
    /* PSP analog stick: dead-zone + normalized response. */
    int lx = (int)pad->Lx - 128;
    float steer = 0.0f;
    const int dead = 18;
    const int full = 110;

    if (pad->Buttons & PSP_CTRL_LEFT) steer -= 1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) steer += 1.0f;

    if (abs(lx) > dead) {
        float a = (float)(abs(lx) - dead) / (float)(full - dead);
        if (a > 1.0f) a = 1.0f;
        float analog = (lx < 0) ? -a : a;
        /* Analog is the fine steering source; D-pad remains a digital fallback. */
        if (steer == 0.0f) steer = analog;
        else steer = clampf(steer + analog * 0.25f, -1.0f, 1.0f);
    }
    return clampf(steer, -1.0f, 1.0f);
}

static void update(SceCtrlData *pad) {
    tAnim += DT;
    updateFx();

    unsigned pressed = pad->Buttons & ~prevB;

    if (state == STATE_MENU) {
        prevB = pad->Buttons;
        if (pressed & PSP_CTRL_UP) menuSel = GAME_RACE;
        if (pressed & PSP_CTRL_DOWN) menuSel = GAME_TIME_TRIAL;
        if (pressed & PSP_CTRL_CROSS) { gameMode = menuSel; state = STATE_CHAR; }
        return;
    }
    if (state == STATE_CHAR) {
        prevB = pad->Buttons;
        int old = charSel;
        if (pressed & PSP_CTRL_LEFT) charSel = (charSel + NK - 1) % NK;
        if (pressed & PSP_CTRL_RIGHT) charSel = (charSel + 1) % NK;
        if (charSel != old) { rebuildAll(); state = STATE_CHAR; }
        if (pressed & PSP_CTRL_CIRCLE) state = STATE_MENU;
        if (pressed & PSP_CTRL_CROSS) { rebuildAll(); state = STATE_TRACK; }
        return;
    }
    if (state == STATE_TRACK) {
        prevB = pad->Buttons;
        int old = trackSel;
        if (pressed & PSP_CTRL_LEFT) trackSel = (trackSel + NTRACKS - 1) % NTRACKS;
        if (pressed & PSP_CTRL_RIGHT) trackSel = (trackSel + 1) % NTRACKS;
        if (trackSel != old) { rebuildAll(); state = STATE_TRACK; }
        if (pressed & PSP_CTRL_CIRCLE) state = STATE_CHAR;
        if (pressed & PSP_CTRL_CROSS) rebuildAll();     /* geri sayim baslar */
        return;
    }

    /* START is the pause button during a race. */
    if (state == 1 && (pressed & PSP_CTRL_START)) {
        state = STATE_PAUSE;
        prevB = pad->Buttons;
        return;
    }

    if (state == STATE_PAUSE) {
        if (pressed & PSP_CTRL_START) state = 1;
        else if (pressed & PSP_CTRL_SELECT) rebuildAll();
        if (pressed & PSP_CTRL_TRIANGLE) aMusicOn = !aMusicOn;
        prevB = pad->Buttons;
        return;
    }

    /* Triangle cycles three lightweight chase-camera presets. */
    if (state == 1 && (pressed & PSP_CTRL_TRIANGLE))
        cameraMode = (cameraMode + 1) % 3;

    /* SELECT provides a quick restart after the finish. */
    if (state == 2 && (pressed & PSP_CTRL_SELECT)) { rebuildAll(); return; }
    if (state == 2 && (pressed & PSP_CTRL_START)) { rebuildAll(); return; }

    prevB = pad->Buttons;

    float steer = readSteer(pad);
    int gas = (pad->Buttons & PSP_CTRL_CROSS) != 0;
    int brake = (pad->Buttons & PSP_CTRL_SQUARE) != 0;
    int drift = (pad->Buttons & (PSP_CTRL_RTRIGGER | PSP_CTRL_LTRIGGER)) != 0;

    if (shake > 0) shake -= DT;
    if (lapFlash > 0) lapFlash -= DT;

    if (state == 0) {
        cd -= DT;
        if (cd <= 1.0f) state = 1;
    } else if (state == 1) {
        raceTime += DT;
        lapTimer += DT;
        if (gameMode == GAME_TIME_TRIAL && (ghostTick++ & 1) == 0 && ghostCount < GHOST_MAX) {
            ghostRec[ghostCount].x = K[0].x;
            ghostRec[ghostCount].z = K[0].z;
            ghostRec[ghostCount].h = K[0].h;
            ghostCount++;
        }
        stepKart(&K[0], steer, gas, brake, drift);
        if (gameMode == GAME_RACE) for (int i = 1; i < NK; i++) aiDrive(&K[i], i);

        /* kart-kart carpisma */
        const int nk = activeKarts();
        for (int i = 0; i < nk; i++) for (int j = i + 1; j < nk; j++) {
            float dx = K[j].x - K[i].x, dz = K[j].z - K[i].z;
            float d2 = dx * dx + dz * dz;
            if (d2 < 2.7f * 2.7f && d2 > 0.0001f) {
                float d = sqrtf(d2), ov = (2.7f - d) * 0.5f;
                float nx = dx / d, nz = dz / d;
                K[i].x -= nx * ov; K[i].z -= nz * ov; K[j].x += nx * ov; K[j].z += nz * ov;
                K[i].spd *= 0.985f; K[j].spd *= 0.985f;
                if (i == 0 || j == 0) shake = 0.15f;
            }
        }
        for (int i = 0; i < NK; i++) trackUpdate(&K[i]);

        Kart *p = &K[0];
        updateItemTimers();
        updateShell();

        if (padMark[p->idx] && fabsf(p->lat) < HW * 0.45f && p->boostT < 0.8f)
            p->boostT = 1.2f;

        /* Player item boxes. Bots are handled in aiDrive(). */
        for (int i = 0; i < nthings; i++) {
            Thing *t = &things[i];
            if (!t->active) {
                if ((t->type == TH_ITEM || t->type == TH_BANANA) && t->resp > 0) {
                    t->resp -= DT;
                    if (t->resp <= 0 && t->type == TH_ITEM) t->active = 1;
                }
                continue;
            }

            float dx = t->x - p->x, dz = t->z - p->z;
            if (t->type == TH_ITEM) {
                if (dx * dx + dz * dz < 2.3f * 2.3f) {
                    t->active = 0;
                    t->resp = 8.0f;
                    if (itemCntG == 0 && gameMode == GAME_RACE) giveItem(p);
                }
            } else if (t->type == TH_CONE) {
                if (dx * dx + dz * dz < 1.8f * 1.8f) {
                    t->active = 0;
                    hitKart(p, 0.35f, 0.9f);
                    shake = 0.5f;
                }
            }
        }

        /* Banana traps can hit player or any bot. */
        for (int ti = 0; ti < nthings; ti++) {
            Thing *t = &things[ti];
            if (!t->active || t->type != TH_BANANA) continue;
            for (int ki = 0; ki < NK; ki++) {
                float dx = t->x - K[ki].x, dz = t->z - K[ki].z;
                if (dx * dx + dz * dz < 2.0f * 2.0f) {
                    hitKart(&K[ki], 0.32f, 1.0f);
                    t->active = 0;
                    t->resp = 0;
                    if (ki == 0) shake = 0.45f;
                    break;
                }
            }
        }

        if (pressed & PSP_CTRL_CIRCLE) useItem();

        /* tur */
        {
            int lp = (int)floorf(p->prog / TL) + 1;
            if (lp > lap && lp <= LAPS) {
                checkBestLap(); lapTimer = 0;
                lap = lp; lapFlash = 2.0f;
                for (int i = 0; i < nthings; i++) if (things[i].type == TH_CONE) things[i].active = 1;
            }
            if (p->prog >= LAPS * TL) {
                state = 2; finalRank = (gameMode == GAME_RACE) ? calcRank() : 1; finalTime = raceTime;
                finishRace(finalTime, finalRank);
            }
        }
    }
}
/* ---------- Cizim ---------- */
static void modelAt(float x, float y, float z, float yaw) {
    ScePspFVector3 t; t.x = x; t.y = y; t.z = z;
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumTranslate(&t);
    sceGumRotateY(-yaw);
}


static void render3D(void) {
    Kart *p = &K[0];
    float tgt = p->vh + wrapA(p->h - p->vh) * 0.5f;
    camH = wrapA(camH + wrapA(tgt - camH) * clampf(5.0f * DT, 0, 1));
    float boostF = (p->boostT > 0) ? 1.0f : 0.0f;

    /* Three PSP-friendly chase cameras: normal, close, wide. */
    float baseFov = 62.0f, back = 9.5f, eyeY = 4.3f, targetY = 1.4f, targetD = 7.0f;
    if (cameraMode == 1) {
        baseFov = 58.0f; back = 7.2f; eyeY = 3.5f; targetD = 7.8f;
    } else if (cameraMode == 2) {
        baseFov = 68.0f; back = 13.5f; eyeY = 5.8f; targetD = 8.5f;
    }
    back += 1.2f * boostF;
    fovCur += ((baseFov + 12.0f * boostF) - fovCur) * 0.10f;

    float sh = (shake > 0) ? sinf(tAnim * 90.0f) * 0.25f : 0.0f;
    ScePspFVector3 eye, ctr, up;
    eye.x = p->x - cosf(camH) * back; eye.y = eyeY + sh; eye.z = p->z - sinf(camH) * back;
    ctr.x = p->x + cosf(camH) * targetD; ctr.y = targetY; ctr.z = p->z + sinf(camH) * targetD;
    up.x = 0; up.y = 1; up.z = 0;

    /* gokyuzu */
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    vgrad(0, 0, W, 150, SKYTOP, HAZE);
    rect(0, 150, W, H - 150, HAZE);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(fovCur, (float)W / (float)H, 1.5f, 1800.0f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumLookAt(&eye, &ctr, &up);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    sceGuFog(220.0f, 1250.0f, HAZE);
    sceGuEnable(GU_FOG);
    /* doku ayarlari */
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    /* zemin (cim dokusu) */
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexImage(0, 64, 64, 64, texGrass);
    sceGuTexFlush();
    sceGumDrawArray(GU_TRIANGLES, VF3, groundEnd, 0, mesh);
    sceGuDisable(GU_TEXTURE_2D);
    /* cim seritleri + rumble (duz renk) */
    sceGumDrawArray(GU_TRIANGLES, VF3, roadStart - groundEnd, 0, &mesh[groundEnd]);
    /* yol (asfalt dokusu) */
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexImage(0, 64, 64, 64, texRoad);
    sceGuTexFlush();
    sceGumDrawArray(GU_TRIANGLES, VF3, roadEnd - roadStart, 0, &mesh[roadStart]);
    sceGuDisable(GU_TEXTURE_2D);
    /* turbo seritleri, cizgiler, baslangic (duz renk) */
    sceGumDrawArray(GU_TRIANGLES, VF3, nFlat - roadEnd, 0, &mesh[roadEnd]);
    /* golgeler */
    for (int i = 0; i < activeKarts(); i++) {
        modelAt(K[i].x, 0.08f, K[i].z, K[i].h);
        sceGumDrawArray(GU_TRIANGLES, VF3, shadowCount, 0, &mesh[shadowStart]);
    }
    /* nesneler (derinlik testli) */
    sceGuEnable(GU_DEPTH_TEST);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumDrawArray(GU_TRIANGLES, VF3, nScene - nFlat, 0, &mesh[nFlat]);

    for (int i = 0; i < activeKarts(); i++) {
        Kart *k = &K[i];
        float bob = 0.05f * sinf(k->bobA) * clampf(k->spd / VMAX, 0, 1);
        if (k->spinT > 0) bob += 0.05f * sinf(tAnim * 36.0f);
        float yaw = k->h + k->driftDir * 0.3f + k->spinA;
        modelAt(k->x, bob, k->z, yaw);
        sceGumRotateX(k->pitch);
        sceGumRotateZ(k->lean);
        sceGumDrawArray(GU_TRIANGLES, VF3, kartCount, 0, &mesh[kartStart[i]]);
        if (k->boostT > 0) {
            ScePspFVector3 t, sc;
            t.x = -1.9f; t.y = 0.7f + 0.05f * sinf(tAnim * 32.0f + i); t.z = 0;
            sc.x = 0.75f + 0.55f * (0.5f + 0.5f * sinf(tAnim * 46.0f + i)); sc.y = 1; sc.z = 1;
            sceGumTranslate(&t);
            sceGumScale(&sc);
            sceGumDrawArray(GU_TRIANGLES, VF3, flameCount, 0, &mesh[flameStart]);
        }
        if (k->starT > 0) {
            for (int q = 0; q < 4; q++) {
                float a = tAnim * 7.0f + q * (PI * 0.5f);
                ScePspFVector3 t;
                t.x = cosf(a) * 2.2f; t.y = 1.1f + 0.55f * sinf(a * 1.7f); t.z = sinf(a) * 2.2f;
                sceGumTranslate(&t);
                sceGumDrawArray(GU_TRIANGLES, VF3, coneCount, 0, &mesh[coneStart]);
            }
        }
    }
    /* ghost (en iyi time trial turu, 30 Hz kayit, aralari interpolasyon) */
    if (ghostMode && ghostBestCount > 0) {
        float ft = raceTime * 30.0f;
        int gi = (int)ft;
        float fr = ft - (float)gi;
        if (gi >= 0 && gi < ghostBestCount) {
            int g2 = (gi + 1 < ghostBestCount) ? gi + 1 : gi;
            float gx = ghostBest[gi].x + (ghostBest[g2].x - ghostBest[gi].x) * fr;
            float gz = ghostBest[gi].z + (ghostBest[g2].z - ghostBest[gi].z) * fr;
            float gh = ghostBest[gi].h + wrapA(ghostBest[g2].h - ghostBest[gi].h) * fr;
            modelAt(gx, 0.1f, gz, gh);
            sceGumDrawArray(GU_TRIANGLES, VF3, ghostCount3, 0, &mesh[ghostStart]);
        }
    }
    for (int i = 0; i < nthings; i++) {
        Thing *t = &things[i];
        if (!t->active) continue;
        if (t->type == TH_ITEM) {
            modelAt(t->x, 1.5f + 0.2f * sinf(tAnim * 3.0f + i), t->z, tAnim * 2.0f);
            sceGumDrawArray(GU_TRIANGLES, VF3, boxCount, 0, &mesh[boxStart]);
        } else if (t->type == TH_CONE) {
            modelAt(t->x, 0, t->z, 0);
            sceGumDrawArray(GU_TRIANGLES, VF3, coneCount, 0, &mesh[coneStart]);
        } else if (t->type == TH_BANANA) {
            modelAt(t->x, 0.35f, t->z, tAnim * 2.5f);
            sceGumDrawArray(GU_TRIANGLES, VF3, banCount, 0, &mesh[banStart]);
        }
    }

    for (int owner = 0; owner < NK; owner++) if (shellShots[owner].active) {
        modelAt(shellShots[owner].x, 1.0f + 0.18f * sinf(tAnim * 16.0f + owner), shellShots[owner].z, tAnim * 8.0f);
        if (shellShots[owner].type == SHELL_RED)
            sceGumDrawArray(GU_TRIANGLES, VF3, shellRCount, 0, &mesh[shellRStart]);
        else
            sceGumDrawArray(GU_TRIANGLES, VF3, shellGCount, 0, &mesh[shellGStart]);
    }
    /* Parcaciklari 3D sahnede canli sekilde goster */
    for (int i = 0; i < MAXFX; i++) if (fx[i].active) {
        float fade = clampf(fx[i].life / fx[i].maxLife, 0, 1);
        float s = fx[i].size * (0.55f + 0.85f * fade);
        modelAt(fx[i].x, fx[i].y, fx[i].z, tAnim * (fx[i].kind == 1 ? 12.0f : 18.0f));
        {
            ScePspFVector3 sc; sc.x = s; sc.y = s; sc.z = s;
            sceGumScale(&sc);
        }
        sceGumDrawArray(GU_TRIANGLES, VF3, sparkCount, 0, &mesh[sparkStart]);
    }
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
}

static void drawMush(float x, float y, float s) {
    tri2(x, y + s * 1.1f, x + s * 2, y + s * 1.1f, x + s, y - s * 0.2f, RGB(235, 40, 40));
    rect(x + s * 0.55f, y + s * 1.1f, s * 0.9f, s * 0.9f, RGB(250, 240, 215));
    rect(x + s * 0.8f, y + s * 0.5f, s * 0.4f, s * 0.35f, RGB(255, 255, 255));
}

static void drawHUD(void) {
    char buf[40];
    Kart *p = &K[0];

    /* Karakter secimi */
    if (state == STATE_CHAR) {
        rect(0, 0, W, H, RGBA(0, 0, 0, 150));
        text(W / 2 - textWidth("KARAKTER SEC", 14, 4) / 2, 12, 14, 24, 4, "KARAKTER SEC", RGB(255, 220, 50));
        for (int i = 0; i < NK; i++) {
            int bx = 30 + i * 72, by = 52;
            if (i == charSel) rect(bx - 4, by - 4, 64, 72, RGB(255, 220, 50));
            rect(bx, by, 56, 64, RGBA(20, 30, 60, 235));
            rect(bx + 6, by + 14, 44, 18, kBody[i]);
            rect(bx + 12, by + 8, 16, 8, kTrim[i]);
            rect(bx + 4, by + 30, 12, 12, RGB(30, 30, 34));
            rect(bx + 40, by + 30, 12, 12, RGB(30, 30, 34));
            rect(bx + 6, by + 46, 44, 4, scol(kBody[i], 0.6f));
        }
        text(W / 2 - textWidth(charNames[charSel], 18, 5) / 2, 132, 18, 30, 5, charNames[charSel], RGB(255, 255, 255));
        {
            static const char *lbl[3] = { "SPEED", "ACCEL", "TURN" };
            float vals[3] = { cSpd[charSel], cAcc[charSel], cTrn[charSel] };
            for (int j = 0; j < 3; j++) {
                int by2 = 176 + j * 22;
                text(110, by2, 8, 14, 2, lbl[j], RGB(200, 220, 240));
                rect(200, by2 + 2, 160, 10, RGBA(255, 255, 255, 50));
                rect(200, by2 + 2, 160.0f * clampf((vals[j] - 0.82f) / 0.34f, 0.05f, 1.0f), 10, RGB(120, 240, 255));
            }
        }
        text(W / 2 - textWidth("SOL SAG: SEC   X: TAMAM   O: GERI", 7, 2) / 2, 250, 7, 12, 2, "SOL SAG: SEC   X: TAMAM   O: GERI", RGB(205, 220, 235));
        return;
    }
    /* Pist secimi (arkada pist onizlemesi) */
    if (state == STATE_TRACK) {
        rect(0, 0, W, 66, RGBA(0, 0, 0, 170));
        rect(0, H - 76, W, 76, RGBA(0, 0, 0, 170));
        text(W / 2 - textWidth("PIST SEC", 14, 4) / 2, 8, 14, 24, 4, "PIST SEC", RGB(255, 220, 50));
        snprintf(buf, sizeof(buf), "%d/%d  %s", trackSel + 1, NTRACKS, themes[trackSel].name);
        text(W / 2 - textWidth(buf, 10, 3) / 2, 40, 10, 16, 3, buf, RGB(255, 255, 255));
        {
            float bt = (gameMode == GAME_RACE) ? saveData.bestRace[trackSel] : saveData.bestTT[trackSel];
            if (bt > 0) {
                int bm = (int)(bt / 60), bs = (int)bt % 60, bd = (int)(bt * 10) % 10;
                snprintf(buf, sizeof(buf), "BEST %d:%02d.%d", bm, bs, bd);
            } else snprintf(buf, sizeof(buf), "BEST --");
            text(W / 2 - textWidth(buf, 10, 3) / 2, H - 70, 10, 16, 3, buf, RGB(120, 240, 255));
            if (gameMode == GAME_TIME_TRIAL && ghostBestCount > 0)
                text(W / 2 - textWidth("GHOST VAR", 8, 2) / 2, H - 48, 8, 14, 2, "GHOST VAR", RGB(255, 230, 120));
        }
        text(W / 2 - textWidth("SOL SAG: PIST   X: BASLA   O: GERI", 7, 2) / 2, H - 22, 7, 12, 2, "SOL SAG: PIST   X: BASLA   O: GERI", RGB(205, 220, 235));
        return;
    }
    /* Ana menu: tek kisilik yaris, diger araclar BOT */
    if (state == STATE_MENU) {
        rect(0, 0, W, H, RGBA(0, 0, 0, 135));
        float mp = 1.0f + 0.05f * sinf(tAnim * 3.0f);
        int mw = (int)(30 * mp), mh = (int)(50 * mp);
        text(W / 2 - textWidth("MARIO KART PSP", mw, 8) / 2, 38 - (mh - 50) / 2, mw, mh, 8, "MARIO KART PSP", RGB(255, 220, 50));
        {
            static const char *names[2] = { "TEK KISILIK", "TIME TRIAL" };
            for (int i = 0; i < 2; i++) {
                int by = 96 + i * 44;
                int sel = (menuSel == i);
                if (sel) rect(W / 2 - 134, by - 4, 268, 40, RGB(255, 220, 50));
                rect(W / 2 - 130, by, 260, 32, sel ? RGBA(20, 70, 150, 240) : RGBA(20, 50, 100, 160));
                text(W / 2 - textWidth(names[i], 14, 4) / 2, by + 5, 14, 22, 4, names[i],
                     sel ? RGB(255, 255, 255) : RGB(170, 185, 205));
            }
        }
        text(W / 2 - textWidth("X: DEVAM", 11, 3) / 2, 188, 11, 18, 3, "X: DEVAM", RGB(120, 240, 255));
        if (menuSel == GAME_RACE)
            text(W / 2 - textWidth("5 BOT YARISMACI", 10, 3) / 2, 212, 10, 16, 3, "5 BOT YARISMACI", RGB(230, 230, 230));
        else
            text(W / 2 - textWidth("BOT YOK  GHOST", 10, 3) / 2, 212, 10, 16, 3, "BOT YOK  GHOST", RGB(230, 230, 230));
        text(W / 2 - textWidth("PSP kontrol ve ozel guc sistemi", 8, 2) / 2, 240, 8, 14, 2, "PSP kontrol ve ozel guc sistemi", RGB(180, 200, 220));
        return;
    }
    if (gameMode == GAME_RACE) {
        int rank = (state == 2) ? finalRank : calcRank();
        snprintf(buf, sizeof(buf), "POS %d/%d", rank, NK);
        text(8, 8, 11, 18, 3, buf, rank == 1 ? RGB(255, 215, 40) : RGB(255, 255, 255));
        text(8, 28, 8, 14, 2, "5 BOT", RGB(120, 220, 255));
    } else {
        text(8, 8, 8, 14, 2, "TIME TRIAL", RGB(255, 215, 40));
        if (saveData.bestTT[trackSel] > 0) {
            float btt = saveData.bestTT[trackSel];
            int bm = (int)(btt / 60), bs = (int)btt % 60, bd = (int)(btt * 10) % 10;
            snprintf(buf, sizeof(buf), "REC %d:%02d.%d", bm, bs, bd);
            text(8, 28, 8, 14, 2, buf, RGB(120, 220, 255));
        }
    }
    if (saveData.bestLap[trackSel] > 0) snprintf(buf, sizeof(buf), "BEST %.1f", saveData.bestLap[trackSel]);
    else snprintf(buf, sizeof(buf), "BEST --");
    text(8, 50, 8, 14, 3, buf, RGB(255, 255, 255));
    snprintf(buf, sizeof(buf), "LAP %d/%d", lap, LAPS);
    text(W - 8 - textWidth(buf, 11, 3), 8, 11, 18, 3, buf, RGB(255, 255, 255));

    float t = (state == 2) ? finalTime : raceTime;
    int m = (int)(t / 60), s = (int)t % 60, d = (int)(t * 10) % 10;
    snprintf(buf, sizeof(buf), "%d:%02d.%d", m, s, d);
    text(W / 2 - textWidth(buf, 9, 3) / 2, 10, 9, 15, 3, buf, RGB(255, 255, 255));

    /* harita */
    {
        float sx = 92.0f / (mapMaxX - mapMinX), sz = 66.0f / (mapMaxZ - mapMinZ);
        float sc = sx < sz ? sx : sz;
        float ox = W - 104 + 4, oy = 36 + 3;
        rect(W - 104, 34, 98, 74, RGBA(0, 0, 0, 120));
        for (int i = 0; i < M; i += 4) rect(ox + (tcx[i] - mapMinX) * sc, oy + (tcz[i] - mapMinZ) * sc, 3, 3, RGB(210, 210, 215));
        rect(ox + (tcx[0] - mapMinX) * sc - 1, oy + (tcz[0] - mapMinZ) * sc - 1, 5, 5, RGB(255, 255, 255));
        for (int i = activeKarts() - 1; i >= 0; i--) {
            float mx = ox + (K[i].x - mapMinX) * sc, mz = oy + (K[i].z - mapMinZ) * sc;
            if (i == 0) rect(mx - 3, mz - 3, 7, 7, RGB(255, 255, 255));
            rect(mx - 2, mz - 2, 5, 5, kBody[kChar[i]]);
        }
    }

    if (itemCntG > 0 && p->item != ITEM_NONE) {
        rect(8, H - 58, 82, 24, RGBA(0, 0, 0, 170));
        float ip = 0.85f + 0.15f * (0.5f + 0.5f * sinf(tAnim * 8.0f));
        rect(10, H - 56, 18, 20, scol(itemColor(p->item), ip));
        snprintf(buf, sizeof(buf), "%s", itemName(p->item));
        text(34, H - 54, 8, 14, 3, buf, RGB(255, 255, 255));
        text(12, H - 34, 7, 12, 2, "O:USE", RGB(255, 230, 70));
    }
    if (p->shield) text(108, H - 35, 8, 14, 3, "SHIELD", RGB(80, 230, 255));
    if (p->starT > 0) text(108, H - 55, 8, 14, 3, "STAR", RGB(255, 180, 40));

    /* PSP kontrol ipucu: kalabalik HUD yerine alt satirda kisa tut. */
    if (state == 1 && (((int)(tAnim * 1.5f)) & 3) == 0)
        text(8, H - 20, 7, 12, 2, "O:ITEM  T:CAM  L/R:DRIFT", RGB(205, 220, 235));

    /* hiz */
    float lim = VMAX * 1.35f;
    for (int i = 0; i < 20; i++) {
        int on = (p->spd / lim * 20.0f) > i;
        unsigned c = on ? mixc(RGB(60, 220, 60), RGB(240, 50, 40), i / 19.0f) : RGBA(0, 0, 0, 130);
        float hh = 5 + i * 0.6f;
        rect(W - 130 + i * 6, H - 12 - hh, 4, hh, c);
    }
    snprintf(buf, sizeof(buf), "%d", (int)(p->spd / VMAX * 180.0f));
    text(W - 130, H - 42, 11, 18, 3, buf, p->boostT > 0 ? RGB(255, 190, 40) : RGB(255, 255, 255));

    /* drift gostergesi */
    if (p->driftDir) {
        float f = clampf(p->driftT / 1.8f, 0, 1);
        unsigned c = p->driftT > 1.8f ? RGB(255, 150, 20) : (p->driftT > 0.8f ? RGB(60, 150, 255) : RGB(230, 230, 230));
        rect(W / 2 - 42, H - 26, 84, 10, RGBA(0, 0, 0, 160));
        rect(W / 2 - 40, H - 24, 80 * f, 6, c);
    }

    /* turbo cizgileri */
    if (p->boostT > 0 && state != 0) {
        for (int i = 0; i < 14; i++) {
            int side = i & 1;
            int y = (i * 47 + (int)(tAnim * 700)) % H;
            rect(side ? W - 70 : 0, y, 70, 2, RGBA(255, 255, 255, 90));
        }
    }

    /* geri sayim */
    if (state == 0) {
        int lit = (cd < 3.0f) ? ((cd < 2.0f) ? 3 : 2) : 1;
        for (int i = 0; i < 3; i++) {
            rect(W / 2 - 55 + i * 38, 50, 28, 28, RGB(20, 20, 20));
            rect(W / 2 - 52 + i * 38, 53, 22, 22, (i < lit) ? RGB(240, 40, 40) : RGB(80, 20, 20));
        }
        int dg = (int)ceilf(cd) - 1;
        if (dg >= 1 && dg <= 3) {
            snprintf(buf, sizeof(buf), "%d", dg);
            float pulse = 1.0f + 0.08f * sinf(tAnim * 14.0f);
            int nw = (int)(48 * pulse), nh = (int)(80 * pulse);
            text(W / 2 - nw / 2, 96 - (nh - 80) / 2, nw, nh, 12, buf, RGB(255, 230, 60));
        }
    } else if (state == 1 && raceTime < 1.2f) {
        for (int i = 0; i < 3; i++) {
            rect(W / 2 - 55 + i * 38, 50, 28, 28, RGB(20, 20, 20));
            rect(W / 2 - 52 + i * 38, 53, 22, 22, RGB(50, 235, 70));
        }
        text(W / 2 - textWidth("GO", 44, 11) / 2, 96, 44, 76, 11, "GO", RGB(60, 240, 80));
    }
    if (lapFlash > 0 && state == 1) {
        char b2[20];
        if (lap == LAPS) snprintf(b2, sizeof(b2), "FINAL LAP"); else snprintf(b2, sizeof(b2), "LAP %d", lap);
        text(W / 2 - textWidth(b2, 18, 5) / 2, 70, 18, 30, 5, b2, RGB(255, 230, 60));
    }

    /* duraklatma ekrani */
    if (state == STATE_PAUSE) {
        rect(0, 72, W, 142, RGBA(0, 0, 0, 175));
        text(W / 2 - textWidth("PAUSE", 30, 8) / 2, 84, 30, 50, 8, "PAUSE", RGB(255, 230, 70));
        text(W / 2 - textWidth("START:DEVAM", 10, 3) / 2, 148, 10, 16, 3, "START:DEVAM", RGB(255, 255, 255));
        text(W / 2 - textWidth("SELECT:YENI", 10, 3) / 2, 172, 10, 16, 3, "SELECT:YENI", RGB(180, 220, 255));
        text(W / 2 - textWidth(aMusicOn ? "T:MUZIK ACIK" : "T:MUZIK KAPALI", 10, 3) / 2, 194, 10, 16, 3, aMusicOn ? "T:MUZIK ACIK" : "T:MUZIK KAPALI", RGB(255, 230, 120));
    }

    /* bitis ekrani */
    if (state == 2) {
        rect(0, 70, W, 150, RGBA(0, 0, 0, 150));
        text(W / 2 - textWidth("FINISH", 30, 8) / 2, 80, 30, 50, 8, "FINISH", RGB(255, 255, 255));
        unsigned medal = finalRank == 1 ? RGB(255, 215, 40) : (finalRank == 2 ? RGB(210, 215, 225) : (finalRank == 3 ? RGB(215, 140, 70) : RGB(255, 255, 255)));
        snprintf(buf, sizeof(buf), "PLACE %d", finalRank);
        text(W / 2 - textWidth(buf, 24, 6) / 2, 142, 24, 40, 6, buf, medal);
        if (newRecord && (((int)(tAnim * 3)) & 1))
            text(W / 2 - textWidth("RECORD", 11, 3) / 2, 46, 11, 18, 3, "RECORD", RGB(255, 220, 50));
        if (((int)(tAnim * 2)) & 1)
            text(W / 2 - textWidth("PRESS START", 11, 3) / 2, 194, 11, 18, 3, "PRESS START", RGB(255, 255, 255));
    }
}

static void render(void) {
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(0xff000000);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    render3D();
    drawHUD();
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void initGraphics(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUFW);
    sceGuDispBuffer(W, H, (void *)0x88000, BUFW);
    sceGuDepthBuffer((void *)0x110000, BUFW);
    sceGuOffset(2048 - (W / 2), 2048 - (H / 2));
    sceGuViewport(2048, 2048, W, H);
    sceGuDepthRange(0xc350, 0x2710);
    sceGuScissor(0, 0, W, H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFog(220.0f, 1250.0f, HAZE);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

int main(void) {
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);
    loadSave();
    charSel = saveData.lastChar;
    trackSel = saveData.lastTrack;
    makeTextures();
    startAudio();
    initGraphics();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    rebuildAll();
    state = STATE_MENU;
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    while (1) {
        sceCtrlPeekBufferPositive(&pad, 1);
        update(&pad);
        /* ses durumunu ses thread'ine bildir */
        aSpd = K[0].spd;
        aBoost = K[0].boostT > 0;
        aEngineOn = (state == 0 || state == 1);
        aTrackId = trackSel;
        render();
    }
    return 0;
}
