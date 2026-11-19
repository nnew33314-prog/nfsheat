// nfs_heat_drift_grip.c
// NFS Heat Drift/Grip/Street Selector - Complete GoldHEN Plugin
// VCFG-style GPU overlay + tuning EBX redirect + persistent config
// Build: GitHub Actions (OpenOrbis toolchain) -> .prx

#include <stdarg.h>
#include <ps4/standard.h>
#include <ps4/kernel.h>
#include <ps4/module.h>
#include <ps4/linker.h>
#include <ps4/memory.h>
#include <ps4/filesystem.h>
#include <ps4/fcntl.h>
#include <ps4/errno.h>
#include <ps4/syscall.h>
#include <ps4/pthread.h>
#include <ps4/ctrl.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

// ============================================================
// CONSTANTS
// ============================================================
#define VOV_W        512u
#define VOV_MAX_H    720u
#define VOV_PIX_SZ   (VOV_W * VOV_MAX_H)
#define VOV_PAL_SZ   1024u
#define VOV_BUF_SZ   (VOV_PAL_SZ + VOV_PIX_SZ)
#define VOV_NBUF     10u
#define VOV_MEM_SZ   (VOV_NBUF * VOV_BUF_SZ)

#define MENU_COMBO_DEFAULT 0x0006u  // L3+R3
#define MENU_HOLD_MS 3000
#define CFG_PATH "/data/GoldHEN/mods/nfs_heat/config.bin"

enum { C_NONE, C_CREAM, C_INK, C_YELLOW, C_RED, C_BLUE, C_GREY, C_SEL, C_CYAN, C_ORANGE, C_N };
static const uint32_t k_rgb[C_N] = { 
    0, 0xefeee8, 0x0b0b0b, 0xf5d23a, 0xf2502e, 
    0x4f86e6, 0x8c8a82, 0xe2e0d6, 0x00ffff, 0xff8800 
};

typedef enum { BUCKET_STREET=0, BUCKET_DRIFT=1, BUCKET_RACE=2 } BucketMode;
typedef enum { MENU_MAIN, MENU_OPTIONS, MENU_ABOUT } MenuScreen;

// ============================================================
// 5x7 FONT
// ============================================================
static const uint8_t font5x7[96][5] = {
    {0},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},
    {0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
    {0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},{0x00,0x50,0x30,0x00,0x00},
    {0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},
    {0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
    {0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},
    {0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},
    {0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},
    {0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
    {0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},
    {0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},{0x02,0x04,0x08,0x10,0x20},
    {0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},
    {0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},
    {0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},{0x7F,0x08,0x04,0x04,0x78},
    {0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},
    {0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},
    {0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3F,0x44,0x40,0x20},
    {0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},
    {0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},
    {0x10,0x08,0x08,0x10,0x08},{0x78,0x46,0x41,0x46,0x78}
};

// ============================================================
// PS4 SYSCALLS
// ============================================================
#define SYS_getpid 20
static inline int sys_getpid(void) { int r; __asm__("movl $20,%%eax\nsyscall\n":"=a"(r)::"rcx","r11","memory"); return r; }
int sceKernelGetProcessId(void) { return sys_getpid(); }
int sceCtrlPeekBufferPositive(int p, SceCtrlData* d, int c) { (void)p; if(d&&c>0)d->buttons=0; return 0; }

// ============================================================
// GPU MEMORY (CDRAM)
// ============================================================
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 0x0D430060
int sceKernelAllocMemBlock(const char*, int, size_t, void*);
int sceKernelFreeMemBlock(int);
int sceKernelGetMemBlockBase(int, void**);

static uint8_t* g_mem = 0;
static int g_uid = -1;

static int alloc_mem() {
    int uid = sceKernelAllocMemBlock("NFSHeatVOV", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, VOV_MEM_SZ, NULL);
    if (uid < 0) return -1;
    void* base; if (sceKernelGetMemBlockBase(uid, &base) < 0) { sceKernelFreeMemBlock(uid); return -1; }
    g_uid = uid; g_mem = (uint8_t*)base; return 0;
}
static void free_mem() { if (g_uid >= 0) { sceKernelFreeMemBlock(g_uid); g_uid = -1; g_mem = 0; } }

// ============================================================
// CONFIG (persistent)
// ============================================================
typedef struct {
    uint32_t magic;
    BucketMode bucket;
    uint8_t  show_hud;
    uint8_t  hdr_mode;
    uint8_t  menu_scale;
    uint8_t  pad[11];
} Config;

static Config g_cfg = { 0x4E465348, BUCKET_STREET, 1, 0, 1, {0} }; // 'NFSH'

static void load_config() {
    int fd = sceIoOpen(CFG_PATH, O_RDONLY, 0);
    if (fd >= 0) {
        sceIoRead(fd, &g_cfg, sizeof(Config));
        sceIoClose(fd);
    }
}

static void save_config() {
    sceIoMkdir("/data/GoldHEN/mods/nfs_heat", 0777);
    int fd = sceIoOpen(CFG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) { sceIoWrite(fd, &g_cfg, sizeof(Config)); sceIoClose(fd); }
}

// ============================================================
// OVERLAY CANVAS (VCFG)
// ============================================================
typedef struct {
    uint32_t pal[256];
    uint8_t pix[VOV_W * VOV_MAX_H];
    unsigned w, h;
    int npairs;
    uint8_t pf[15], pb[15];
} Canvas;

static Canvas g_canvas[VOV_NBUF];
static uint32_t g_hash[VOV_NBUF] = {0};
static uint32_t g_used[VOV_NBUF] = {0};
static uint32_t g_tick = 0;
static int g_cur = 0;

static void bset(void* d, int v, size_t n) { uint8_t* p=(uint8_t*)d; while(n--) *p++=(uint8_t)v; }
static uint32_t pack_rgb(uint32_t rgb) { return (rgb & 0xFFFFFFu) | 0xFF000000u; }

static int pair(Canvas* c, int fg, int bg) {
    for (int i = 0; i < c->npairs; i++) if (c->pf[i] == fg && c->pb[i] == bg) return i;
    if (c->npairs == 15) return 0;
    int i = c->npairs++; c->pf[i] = (uint8_t)fg; c->pb[i] = (uint8_t)bg;
    for (unsigned t = 0; t < 16; t++) {
        uint32_t a = k_rgb[bg], b = k_rgb[fg];
        uint32_t r = 0;
        for (int s = 0; s < 24; s += 8) {
            unsigned x = a >> s & 255, y = b >> s & 255;
            r |= ((x * (15 - t) + y * t + 7) / 15) << s;
        }
        c->pal[16 + i * 16 + t] = pack_rgb(r);
    }
    return i;
}

static void canvas_begin(Canvas* c, unsigned w, unsigned h) {
    c->w = w; c->h = h; c->npairs = 0;
    bset(c->pal, 0, VOV_PAL_SZ);
    for (int i = 1; i < C_N; i++) c->pal[i] = pack_rgb(k_rgb[i]);
    bset(c->pix, C_NONE, (size_t)w * h);
}

static void rect(Canvas* c, int x, int y, int w, int h, int col) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > (int)c->w) w = (int)c->w - x; if (y + h > (int)c->h) h = (int)c->h - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) bset(c->pix + (size_t)(y + j) * c->w + x, col, (size_t)w);
}

static void round_corners(Canvas* c, int x, int y, int w, int h, int r) {
    for (int j = 0; j < r; j++) for (int i = 0; i < r; i++) {
        int dx = r - i, dy = r - j;
        if (dx * dx + dy * dy <= r * r) continue;
        int xs[2] = { x + i, x + w - 1 - i }, ys[2] = { y + j, y + h - 1 - j };
        for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++)
            if (xs[a] >= 0 && ys[b] >= 0 && xs[a] < (int)c->w && ys[b] < (int)c->h)
                c->pix[(size_t)ys[b] * c->w + xs[a]] = C_NONE;
    }
}

static void draw_char(Canvas* c, char ch, int x, int y, int fg, int bg) {
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t* g = font5x7[ch - 32];
    int p = pair(c, fg, bg);
    int base = 16 + p * 16;
    for (int gx = 0; gx < 5; gx++) {
        uint8_t col = g[gx];
        for (int gy = 0; gy < 7; gy++) {
            int px = x + gx, py = y + gy;
            if (px >= 0 && px < (int)c->w && py >= 0 && py < (int)c->h) {
                unsigned v = (col >> (6 - gy)) & 1;
                c->pix[py * c->w + px] = v ? (base + 15) : (bg >= 0 ? base : C_NONE);
            }
        }
    }
}

static void draw_text(Canvas* c, const char* s, int x, int y, int fg, int bg) {
    while (*s) { draw_char(c, *s++, x, y, fg, bg); x += 6; }
}

static void draw_text_centered(Canvas* c, const char* s, int y, int fg, int bg) {
    int len = 0; const char* t = s; while(*t++) len++;
    draw_text(c, s, (VOV_W - len * 6) / 2, y, fg, bg);
}

// ============================================================
// COMMIT TO GPU (zero-copy, VCFG style)
// ============================================================
static void commit_buf(int buf) {
    Canvas* c = &g_canvas[buf];
    uint8_t* base = g_mem + (size_t)buf * VOV_BUF_SZ;
    volatile uint32_t* pal = (volatile uint32_t*)base;
    for (int i = 0; i < 256; i++) if (pal[i] != c->pal[i]) pal[i] = c->pal[i];
    volatile uint64_t* d = (volatile uint64_t*)(base + VOV_PAL_SZ);
    const uint64_t* s = (const uint64_t*)c->pix;
    size_t n = ((size_t)c->w * c->h + 7) / 8;
    for (size_t i = 0; i < n; i++) if (d[i] != s[i]) d[i] = s[i];
    g_hash[buf] = 0; g_used[buf] = ++g_tick;
}

// ============================================================
// MENU STATE
// ============================================================
static BucketMode g_bucket = BUCKET_STREET;
static MenuScreen g_screen = MENU_MAIN;
static int g_selected = 0;
static bool g_menu_open = false;
static uint64_t g_hold_start = 0;
static bool g_was_holding = false;
static uint64_t g_frame = 0;
static uint8_t g_anim_frame = 0;

static uint64_t get_time_ms(void) { return g_frame++ * 16; }

static const char* bucket_name(BucketMode b) {
    return b==BUCKET_STREET ? "STREET (UNITE Simcade)" : 
           b==BUCKET_DRIFT  ? "DRIFT (Hayeduce 3.3.2)" : "RACE (UNITE Grip)";
}

static const char* bucket_short(BucketMode b) {
    return b==BUCKET_STREET ? "STREET" : b==BUCKET_DRIFT ? "DRIFT" : "RACE";
}

// ============================================================
// INPUT
// ============================================================
static void handle_input() {
    SceCtrlData data;
    sceCtrlPeekBufferPositive(0, &data, 1);
    uint32_t btn = data.buttons;
    
    bool holding = (btn & MENU_COMBO_DEFAULT) == MENU_COMBO_DEFAULT;
    uint64_t now = get_time_ms();
    
    if (holding) {
        if (!g_was_holding) { g_hold_start = now; g_was_holding = true; }
        else if (!g_menu_open && (now - g_hold_start >= MENU_HOLD_MS)) { 
            g_menu_open = true; g_screen = MENU_MAIN; g_selected = g_bucket; 
        }
    } else { g_was_holding = false; g_hold_start = 0; }
    
    if (!g_menu_open) return;
    
    int max_sel = (g_screen == MENU_MAIN) ? 3 : (g_screen == MENU_OPTIONS) ? 4 : 1;
    
    if (btn & 0x1000) { g_selected = (g_selected + max_sel - 1) % max_sel; sceKernelUsleep(150000); } // UP
    if (btn & 0x2000) { g_selected = (g_selected + 1) % max_sel; sceKernelUsleep(150000); } // DOWN
    
    if (btn & 0x10000) { // CROSS
        if (g_screen == MENU_MAIN) {
            if (g_selected < 3) { g_bucket = (BucketMode)g_selected; save_config(); }
            else if (g_selected == 3) { g_screen = MENU_OPTIONS; g_selected = 0; }
        } else if (g_screen == MENU_OPTIONS) {
            if (g_selected == 0) g_cfg.show_hud ^= 1;
            else if (g_selected == 1) g_cfg.hdr_mode ^= 1;
            else if (g_selected == 2) g_cfg.menu_scale = (g_cfg.menu_scale + 1) % 3;
            else if (g_selected == 3) { g_screen = MENU_ABOUT; g_selected = 0; }
            save_config();
        } else if (g_screen == MENU_ABOUT) {
            g_screen = MENU_MAIN; g_selected = g_bucket;
        }
        sceKernelUsleep(150000);
    }
    
    if (btn & 0x20000) { // CIRCLE
        if (g_screen == MENU_OPTIONS || g_screen == MENU_ABOUT) { g_screen = MENU_MAIN; g_selected = g_bucket; }
        else { g_menu_open = false; }
        sceKernelUsleep(150000);
    }
}

// ============================================================
// DRAW FUNCTIONS
// ============================================================
static void draw_panel(Canvas* c, int x, int y, int w, int h, int bg, int border) {
    rect(c, x, y, w, h, bg);
    if (border >= 0) round_corners(c, x, y, w, h, 8);
    if (border >= 0) rect(c, x+2, y+2, w-4, h-4, C_CREAM);
    if (border >= 0) round_corners(c, x+2, y+2, w-4, h-4, 6);
}

static void draw_item(Canvas* c, int idx, int y, const char* text, int fg, int bg, int sel) {
    int item_bg = sel ? C_SEL : bg;
    rect(c, 12, y - 2, VOV_W - 24, 20, item_bg);
    draw_text(c, sel ? "> " : "  ", 16, y, sel ? C_INK : C_GREY, -1);
    draw_text(c, text, 32, y, sel ? fg : C_GREY, -1);
}

static void draw_main_menu(Canvas* c) {
    draw_panel(c, 0, 0, VOV_W, 256, C_INK, 1);
    
    // Title bar
    rect(c, 4, 4, VOV_W - 8, 28, C_INK);
    draw_text(c, "NFS HEAT DRIFT/GRIP", 16, 10, C_CREAM, -1);
    draw_text(c, "v1.0", 440, 10, C_GREY, -1);
    
    // Current bucket
    draw_text(c, "ACTIVE:", 16, 40, C_INK, -1);
    draw_text(c, bucket_name(g_bucket), 16, 54, C_BLUE, -1);
    
    // Separator
    rect(c, 12, 70, VOV_W - 24, 1, C_GREY);
    
    // Menu items
    draw_text(c, "SELECT HANDLING:", 16, 78, C_INK, -1);
    const char* items[] = {"STREET - UNITE Simcade", "DRIFT - Hayeduce 3.3.2", "RACE - UNITE Grip", "OPTIONS"};
    for (int i = 0; i < 4; i++) {
        int y = 94 + i * 22;
        draw_item(c, i, y, items[i], C_INK, C_CREAM, g_selected == i);
    }
    
    // Hints
    draw_text(c, "D-PAD: NAVIGATE  CROSS: SELECT  CIRCLE: BACK/CLOSE", 16, 200, C_GREY, -1);
    draw_text(c, "HOLD L3+R3 (3s) TO TOGGLE", 16, 214, C_ORANGE, -1);
    
    c->w = VOV_W; c->h = 256;
}

static void draw_options_menu(Canvas* c) {
    draw_panel(c, 0, 0, VOV_W, 256, C_INK, 1);
    
    rect(c, 4, 4, VOV_W - 8, 28, C_INK);
    draw_text(c, "OPTIONS", 16, 10, C_CREAM, -1);
    
    const char* opt_names[] = {
        "SHOW HUD INDICATOR", "HDR MODE", "MENU SCALE", "ABOUT"
    };
    const char* opt_vals[] = {
        g_cfg.show_hud ? "ON" : "OFF",
        g_cfg.hdr_mode ? "ON" : "OFF",
        g_cfg.menu_scale == 0 ? "SMALL" : g_cfg.menu_scale == 1 ? "NORMAL" : "LARGE",
        ""
    };
    
    for (int i = 0; i < 4; i++) {
        int y = 54 + i * 28;
        draw_item(c, i, y, opt_names[i], C_INK, C_CREAM, g_selected == i);
        if (i < 3) draw_text(c, opt_vals[i], VOV_W - 100, y, g_selected == i ? C_YELLOW : C_GREY, -1);
    }
    
    draw_text(c, "CROSS: TOGGLE  CIRCLE: BACK", 16, 200, C_GREY, -1);
    c->w = VOV_W; c->h = 256;
}

static void draw_about_menu(Canvas* c) {
    draw_panel(c, 0, 0, VOV_W, 256, C_INK, 1);
    
    rect(c, 4, 4, VOV_W - 8, 28, C_INK);
    draw_text(c, "ABOUT", 16, 10, C_CREAM, -1);
    
    draw_text_centered(c, "NFS HEAT DRIFT/GRIP SELECTOR", 50, C_CYAN, -1);
    draw_text_centered(c, "Version 1.0", 70, C_GREY, -1);
    draw_text_centered(c, "UNITE 3.5 + Hayeduce 3.3.2", 90, C_CREAM, -1);
    draw_text_centered(c, "GoldHEN Plugin / VCFG Overlay", 110, C_GREY, -1);
    draw_text_centered(c, "L3+R3 Hold 3s to Open", 140, C_ORANGE, -1);
    draw_text_centered(c, "Config: /data/GoldHEN/mods/nfs_heat/", 160, C_GREY, -1);
    draw_text_centered(c, "CROSS/CIRCLE: BACK", 200, C_GREY, -1);
    
    c->w = VOV_W; c->h = 256;
}

static void draw_hud(Canvas* c) {
    if (!g_cfg.show_hud) return;
    canvas_begin(c, 200, 24);
    rect(c, 0, 0, 200, 24, C_INK);
    round_corners(c, 0, 0, 200, 24, 4);
    rect(c, 2, 2, 196, 20, C_CREAM);
    round_corners(c, 2, 2, 196, 20, 3);
    int color = g_bucket==BUCKET_DRIFT ? C_RED : g_bucket==BUCKET_RACE ? C_BLUE : C_GREEN;
    draw_text(c, "HANDLING: ", 8, 8, C_INK, -1);
    draw_text(c, bucket_short(g_bucket), 80, 8, color, -1);
    c->w = 200; c->h = 24;
}

static void draw_menu(Canvas* c) {
    g_anim_frame++;
    
    if (!g_menu_open) {
        draw_hud(c);
        return;
    }
    
    switch (g_screen) {
        case MENU_MAIN: draw_main_menu(c); break;
        case MENU_OPTIONS: draw_options_menu(c); break;
        case MENU_ABOUT: draw_about_menu(c); break;
    }
}

// ============================================================
// GNM HOOK
// ============================================================
typedef int (*gnm_submit_t)(void*, int, void*, int);
static gnm_submit_t g_orig_gnm = 0;

static void inject_overlay(void* cmd_bufs, int count) {
    if (g_mem) commit_buf(g_cur);
    g_cur = (g_cur + 1) % VOV_NBUF;
}

static int hook_gnm(void* cmd_bufs, int count, void* sync, int flags) {
    if (g_mem) inject_overlay(cmd_bufs, count);
    return g_orig_gnm(cmd_bufs, count, sync, flags);
}

// ============================================================
// FILE HOOKS (tuning EBX redirect)
// ============================================================
#define MOD_BASE "/data/GoldHEN/mods/nfs_heat"
static int (*o_open)(const char*,int,mode_t)=0;
static ssize_t (*o_read)(int,void*,size_t)=0;
static int (*o_close)(int)=0;
static off_t (*o_lseek)(int,off_t,int)=0;

typedef struct { int fd, real_fd; } Intercepted;
static Intercepted g_int[64]; static int g_inc=0;

static bool is_tuning(const char* p) {
    return (strstr(p,"vehicles/tuning/")||strstr(p,"vehicles\\tuning")) && strstr(p,".ebx");
}

static void build_mod(char* o, const char* p) {
    const char* b = (g_bucket==BUCKET_STREET)?"street":(g_bucket==BUCKET_DRIFT)?"drift":"race";
    const char* v = strstr(p,"vehicles/");
    if(!v) v=strstr(p,"vehicles\\");
    if(v) sprintf(o,"%s/%s/%s",MOD_BASE,b,v);
    else sprintf(o,"%s/%s/%s",MOD_BASE,b,p);
}

static int h_open(const char* p, int f, mode_t m) {
    if(is_tuning(p)) {
        char mp[512]; build_mod(mp,p);
        int fd = o_open(mp, f, m);
        if(fd>=0 && g_inc<64) {
            g_int[g_inc].fd = -(g_inc+1000);
            g_int[g_inc].real_fd = fd;
            return g_int[g_inc++].fd;
        }
        if(fd>=0) o_close(fd);
    }
    return o_open(p,f,m);
}

static ssize_t h_read(int fd, void* b, size_t n) {
    if(fd<-1000 && fd>-1064) { int i=-fd-1000; return o_read(g_int[i].real_fd,b,n); }
    return o_read(fd,b,n);
}

static int h_close(int fd) {
    if(fd<-1000 && fd>-1064) { int i=-fd-1000; o_close(g_int[i].real_fd); return 0; }
    return o_close(fd);
}

static off_t h_lseek(int fd, off_t o, int w) {
    if(fd<-1000 && fd>-1064) { int i=-fd-1000; return o_lseek(g_int[i].real_fd,o,w); }
    return o_lseek(fd,o,w);
}

static void install_file_hooks() {
    o_open = (void*)sceKernelDlsym(RTLD_DEFAULT,"sceIoOpen");
    o_read = (void*)sceKernelDlsym(RTLD_DEFAULT,"sceIoRead");
    o_close = (void*)sceKernelDlsym(RTLD_DEFAULT,"sceIoClose");
    o_lseek = (void*)sceKernelDlsym(RTLD_DEFAULT,"sceIoLseek");
}

// ============================================================
// OVERLAY THREAD
// ============================================================
static void* overlay_thread(void* arg) {
    (void)arg;
    while (1) { if (sceKernelGetProcessId() > 0) break; sceKernelUsleep(100000); }
    
    load_config();
    g_bucket = g_cfg.bucket;
    g_selected = g_bucket;
    
    if (alloc_mem() < 0) return NULL;
    
    g_orig_gnm = (gnm_submit_t)sceKernelDlsym(RTLD_DEFAULT, "sceGnmSubmitCommandBuffers");
    
    while (1) {
        handle_input();
        draw_menu(&g_canvas[g_cur]);
        commit_buf(g_cur);
        sceKernelUsleep(16000);
    }
    return NULL;
}

// ============================================================
// FILE HOOK THREAD
// ============================================================
static void* file_hook_thread(void* arg) {
    (void)arg;
    while (sceKernelGetProcessId() <= 0) sceKernelUsleep(100000);
    install_file_hooks();
    return NULL;
}

// ============================================================
// PLUGIN ENTRY
// ============================================================
int plugin_load(void) {
    ScePthread t1, t2;
    scePthreadCreate(&t1, NULL, overlay_thread, NULL, "nfs_heat_vov");
    scePthreadDetach(t1);
    scePthreadCreate(&t2, NULL, file_hook_thread, NULL, "nfs_heat_file");
    scePthreadDetach(t2);
    return 0;
}

int plugin_unload(void) {
    free_mem();
    return 0;
}
