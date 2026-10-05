/* MarioKartPSP_Advanced.c
   Duzeltilmis ve calisir hale getirilmis PSP yaris oyunu iskeleti.
   Icerik: AI, tur sayimi, siralama, itemler, mermiler (kabuk/muz),
   drift + boost, waypoint pisti, carpisma, HUD (debug ekran).
   Cizim (Render*) fonksiyonlari ve Save/Load hala kanca (hook) olarak duruyor.
*/

#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <math.h>
#include <string.h>

PSP_MODULE_INFO("MarioKartPSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define NUM_KARTS 6
#define NUM_WAYPOINTS 32
#define NUM_ITEMS 8
#define NUM_PROJECTILES 16
#define MAX_LAPS 3

#define PI_F 3.1415927f
#define WAYPOINT_RADIUS 6.0f

/* ------------------------------------------------------------------ */
/* Veri yapilari                                                       */
/* ------------------------------------------------------------------ */
typedef struct { float x, y, z; } Vec3;
typedef enum { ITEM_NONE, ITEM_MUSHROOM, ITEM_BANANA, ITEM_SHELL } ItemType;

typedef struct {
    Vec3 pos;
    float rotY, speed;
    int lap, currentWaypoint, rank, isAI;
    int finished, finishOrder;
    float boostTimer, driftTimer, spinTimer, progress;
    ItemType item;
} Kart;

typedef struct { Vec3 pos; int active; float respawn; } ItemBox;

typedef struct {
    Vec3 pos, vel;
    int active, owner;
    float life;
    ItemType type;
} Projectile;

static Kart g_karts[NUM_KARTS];
static ItemBox g_boxes[NUM_ITEMS];
static Projectile g_projectiles[NUM_PROJECTILES];
static Vec3 g_waypoints[NUM_WAYPOINTS];

static SceCtrlData g_pad;
static int g_finishCount = 0;

static const char *g_itemNames[] = { "-", "Mantar", "Muz", "Kabuk" };

/* ------------------------------------------------------------------ */
/* Cikis callback'i (HOME tusu icin sart)                              */
/* ------------------------------------------------------------------ */
static int exitCallback(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    sceKernelExitGame();
    return 0;
}

static int callbackThread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exitCallback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setupCallbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callbackThread,
                                     0x11, 0xFA0, 0, NULL);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
}

/* ------------------------------------------------------------------ */
/* Yardimci fonksiyonlar                                               */
/* ------------------------------------------------------------------ */
static unsigned int g_seed = 12345;

static unsigned int Rand(void)
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 16;
}

static float DistXZ(Vec3 a, Vec3 b)
{
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

static float NormalizeAngle(float a)
{
    while (a > PI_F)  a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

/* Ortak hareket: boost, donme (spin), hiz siniri, konum guncelleme */
static void MoveKart(Kart *k, float dt)
{
    float maxSpeed = 24.0f;

    if (k->boostTimer > 0.0f) {
        k->boostTimer -= dt;
        k->speed += 30.0f * dt;
        maxSpeed = 35.0f;
    }

    if (k->spinTimer > 0.0f) {
        k->spinTimer -= dt;
        k->rotY += 10.0f * dt;
        k->speed -= k->speed * 3.0f * dt;
    }

    if (k->speed > maxSpeed) k->speed = maxSpeed;
    if (k->speed < -5.0f)    k->speed = -5.0f;

    k->pos.x += sinf(k->rotY) * k->speed * dt;
    k->pos.z += cosf(k->rotY) * k->speed * dt;
}

static void SpawnProjectile(int owner, ItemType type, Vec3 pos, Vec3 vel, float life)
{
    for (int i = 0; i < NUM_PROJECTILES; i++) {
        if (!g_projectiles[i].active) {
            g_projectiles[i].active = 1;
            g_projectiles[i].owner = owner;
            g_projectiles[i].type = type;
            g_projectiles[i].pos = pos;
            g_projectiles[i].vel = vel;
            g_projectiles[i].life = life;
            return;
        }
    }
}

void UseItem(int idx)
{
    Kart *k = &g_karts[idx];
    Vec3 p = k->pos;
    Vec3 v = { 0.0f, 0.0f, 0.0f };

    switch (k->item) {
    case ITEM_MUSHROOM:
        k->boostTimer = 2.0f;
        break;
    case ITEM_BANANA:
        p.x -= sinf(k->rotY) * 2.5f;
        p.z -= cosf(k->rotY) * 2.5f;
        SpawnProjectile(idx, ITEM_BANANA, p, v, 20.0f);
        break;
    case ITEM_SHELL:
        p.x += sinf(k->rotY) * 2.5f;
        p.z += cosf(k->rotY) * 2.5f;
        v.x = sinf(k->rotY) * 40.0f;
        v.z = cosf(k->rotY) * 40.0f;
        SpawnProjectile(idx, ITEM_SHELL, p, v, 6.0f);
        break;
    default:
        return;
    }
    k->item = ITEM_NONE;
}

/* ------------------------------------------------------------------ */
/* Oyun mantigi                                                        */
/* ------------------------------------------------------------------ */
void CreateTrack(void)
{
    for (int i = 0; i < NUM_WAYPOINTS; i++) {
        float t = ((float)i / NUM_WAYPOINTS) * 6.28318f;
        g_waypoints[i].x = cosf(t) * 40.0f;
        g_waypoints[i].y = 0.0f;
        g_waypoints[i].z = sinf(t) * 40.0f;
    }
}

void InitRace(void)
{
    memset(g_karts, 0, sizeof(g_karts));
    memset(g_boxes, 0, sizeof(g_boxes));
    memset(g_projectiles, 0, sizeof(g_projectiles));
    g_finishCount = 0;

    for (int i = 0; i < NUM_KARTS; i++) {
        Kart *k = &g_karts[i];
        k->pos.x = g_waypoints[0].x + ((i & 1) ? 2.5f : -2.5f);
        k->pos.y = 0.0f;
        k->pos.z = -(float)(i / 2) * 4.0f;
        k->rotY = 0.0f;               /* +Z yonu = pist teget yonu */
        k->lap = 1;
        k->currentWaypoint = 1;
        k->rank = i + 1;
        k->isAI = (i == 0) ? 0 : 1;
        k->item = ITEM_NONE;
    }

    for (int i = 0; i < NUM_ITEMS; i++) {
        int wp = i * (NUM_WAYPOINTS / NUM_ITEMS) + 2;
        g_boxes[i].pos = g_waypoints[wp % NUM_WAYPOINTS];
        g_boxes[i].active = 1;
    }
}

void UpdatePlayer(float dt)
{
    static unsigned int prevButtons = 0;
    unsigned int pressed = g_pad.Buttons & ~prevButtons;
    prevButtons = g_pad.Buttons;

    Kart *k = &g_karts[0];

    if (k->finished) {
        k->speed -= k->speed * 1.5f * dt;
        MoveKart(k, dt);
        return;
    }

    if (k->spinTimer <= 0.0f) {
        if (g_pad.Buttons & PSP_CTRL_CROSS)  k->speed += 18.0f * dt;
        if (g_pad.Buttons & PSP_CTRL_SQUARE) k->speed -= 20.0f * dt;

        float steer = ((float)g_pad.Lx - 128.0f) / 128.0f;
        if (fabsf(steer) < 0.2f) steer = 0.0f;

        float grip = fabsf(k->speed) / 8.0f;
        if (grip > 1.0f) grip = 1.0f;
        k->rotY -= steer * 2.0f * grip * dt;

        /* Drift: R tusu + direksiyon. Birakinca yeterli sure olduysa boost. */
        if ((g_pad.Buttons & PSP_CTRL_RTRIGGER) && steer != 0.0f && k->speed > 10.0f) {
            k->driftTimer += dt;
            k->rotY -= steer * 0.8f * dt;
        } else {
            if (k->driftTimer > 1.0f) k->boostTimer = 1.5f;
            k->driftTimer = 0.0f;
        }

        if (pressed & PSP_CTRL_CIRCLE)
            UseItem(0);
    }

    k->speed -= k->speed * 0.8f * dt;   /* surtunme */
    MoveKart(k, dt);
}

void UpdateAI(int idx, float dt)
{
    Kart *k = &g_karts[idx];

    if (k->finished) {
        k->speed -= k->speed * 1.5f * dt;
        MoveKart(k, dt);
        return;
    }

    if (k->spinTimer <= 0.0f) {
        Vec3 t = g_waypoints[k->currentWaypoint];
        float dx = t.x - k->pos.x;
        float dz = t.z - k->pos.z;
        float diff = NormalizeAngle(atan2f(dx, dz) - k->rotY);

        k->rotY += diff * 4.0f * dt;

        float target = 14.0f + (float)idx * 0.6f;
        k->speed += (target - k->speed) * 2.0f * dt;

        if (k->item != ITEM_NONE && (Rand() % 240) == 0)
            UseItem(idx);
    }

    MoveKart(k, dt);
}

void UpdateItems(float dt)
{
    for (int b = 0; b < NUM_ITEMS; b++) {
        ItemBox *box = &g_boxes[b];

        if (!box->active) {
            box->respawn -= dt;
            if (box->respawn <= 0.0f) box->active = 1;
            continue;
        }

        for (int i = 0; i < NUM_KARTS; i++) {
            Kart *k = &g_karts[i];
            if (k->item == ITEM_NONE && DistXZ(k->pos, box->pos) < 2.5f) {
                k->item = (ItemType)(1 + (Rand() % 3));
                box->active = 0;
                box->respawn = 5.0f;
                break;
            }
        }
    }
}

void UpdateProjectiles(float dt)
{
    for (int p = 0; p < NUM_PROJECTILES; p++) {
        Projectile *pr = &g_projectiles[p];
        if (!pr->active) continue;

        pr->pos.x += pr->vel.x * dt;
        pr->pos.z += pr->vel.z * dt;
        pr->life -= dt;
        if (pr->life <= 0.0f) { pr->active = 0; continue; }

        for (int i = 0; i < NUM_KARTS; i++) {
            Kart *k = &g_karts[i];
            if (i == pr->owner || k->spinTimer > 0.0f) continue;
            if (DistXZ(k->pos, pr->pos) < 1.5f) {
                k->spinTimer = 1.0f;
                k->speed *= 0.3f;
                pr->active = 0;
                break;
            }
        }
    }
}

void UpdateLaps(void)
{
    for (int i = 0; i < NUM_KARTS; i++) {
        Kart *k = &g_karts[i];
        if (k->finished) continue;

        int w = k->currentWaypoint;
        if (DistXZ(k->pos, g_waypoints[w]) < WAYPOINT_RADIUS) {
            if (w == 0) {                       /* start cizgisi */
                k->lap++;
                if (k->lap > MAX_LAPS) {
                    k->lap = MAX_LAPS;
                    k->finished = 1;
                    k->finishOrder = ++g_finishCount;
                }
            }
            k->currentWaypoint = (w + 1) % NUM_WAYPOINTS;
        }
    }
}

void UpdateRanks(void)
{
    for (int i = 0; i < NUM_KARTS; i++) {
        Kart *k = &g_karts[i];
        if (k->finished) {
            k->progress = 1000000.0f - (float)k->finishOrder;
        } else {
            int w = (k->currentWaypoint == 0) ? NUM_WAYPOINTS : k->currentWaypoint;
            float d = DistXZ(k->pos, g_waypoints[k->currentWaypoint]);
            k->progress = (float)((k->lap - 1) * NUM_WAYPOINTS + w) - d / 10.0f;
        }
    }

    for (int i = 0; i < NUM_KARTS; i++) {
        int rank = 1;
        for (int j = 0; j < NUM_KARTS; j++) {
            if (g_karts[j].progress > g_karts[i].progress ||
                (g_karts[j].progress == g_karts[i].progress && j < i))
                rank++;
        }
        g_karts[i].rank = rank;
    }
}

/* ------------------------------------------------------------------ */
/* Cizim kancalari (GU ile doldurulabilir)                             */
/* ------------------------------------------------------------------ */
void RenderSkybox(void) {}
void RenderTrack(void) {}
void RenderKarts(void) {}

void RenderHUD(void)
{
    Kart *p = &g_karts[0];

    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("Tur:%d/%d  Sira:%d/%d  Hiz:%3d  Item:%-6s  Boost:%s   \n",
                         p->lap, MAX_LAPS, p->rank, NUM_KARTS,
                         (int)p->speed, g_itemNames[p->item],
                         p->boostTimer > 0.0f ? "ON " : "off");
    pspDebugScreenPrintf("X:gaz  []:fren  O:item  R+analog:drift  START:cikis\n\n");

    for (int r = 1; r <= NUM_KARTS; r++) {
        for (int i = 0; i < NUM_KARTS; i++) {
            if (g_karts[i].rank != r) continue;
            if (i == 0)
                pspDebugScreenPrintf("%d. SEN     Tur %d %s\n", r, g_karts[i].lap,
                                     g_karts[i].finished ? "BITTI " : "      ");
            else
                pspDebugScreenPrintf("%d. AI %d    Tur %d %s\n", r, i, g_karts[i].lap,
                                     g_karts[i].finished ? "BITTI " : "      ");
        }
    }

    if (p->finished)
        pspDebugScreenPrintf("\nYARIS BITTI! Yerin: %d          \n", p->finishOrder);
}

/* ------------------------------------------------------------------ */
/* Kayit kancalari                                                     */
/* ------------------------------------------------------------------ */
void SaveGame(void) {}
void LoadGame(void) {}

/* ------------------------------------------------------------------ */
/* Ana dongu                                                           */
/* ------------------------------------------------------------------ */
int main(void)
{
    setupCallbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    CreateTrack();
    InitRace();
    LoadGame();

    u64 last = sceKernelGetSystemTimeWide();

    while (1) {
        sceCtrlReadBufferPositive(&g_pad, 1);
        if (g_pad.Buttons & PSP_CTRL_START) break;

        u64 now = sceKernelGetSystemTimeWide();
        float dt = (float)(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        UpdatePlayer(dt);
        for (int i = 1; i < NUM_KARTS; i++)
            UpdateAI(i, dt);
        UpdateItems(dt);
        UpdateProjectiles(dt);
        UpdateLaps();
        UpdateRanks();

        RenderSkybox();
        RenderTrack();
        RenderKarts();
        RenderHUD();

        sceDisplayWaitVblankStart();
    }

    SaveGame();
    sceKernelExitGame();
    return 0;
}
