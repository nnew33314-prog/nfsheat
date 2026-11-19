// nfs_heat_drift_grip.c
// GoldHEN plugin for NFS Heat - Drift/Grip handling bucket selector
// VCFG-style GPU overlay: CPU draws palette bitmap, osd8 shader composites

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
// VCFG CONSTANTS (from vov.h)
// ============================================================
#define VOV_W        512u
#define VOV_MAX_H    720u
#define VOV_PIX_SZ   (VOV_W * VOV_MAX_H)
#define VOV_PAL_SZ   1024u
#define VOV_BUF_SZ   (VOV_PAL_SZ + VOV_PIX_SZ)
#define VOV_NBUF     10u
#define VOV_MEM_SZ   (VOV_NBUF * VOV_BUF_SZ)

// Palette indices
enum { C_NONE, C_CREAM, C_INK, C_YELLOW, C_RED, C_BLUE, C_GREY, C_SEL, C_N };
static const uint32_t k_rgb[C_N] = { 0, 0xefeee8, 0x0b0b0b, 0xf5d23a, 0xf2502e, 0x4f86e6, 0x8c8a82, 0xe2e0d6 };

// Menu combo (from menu_combo.h)
#define MENU_COMBO_DEFAULT 0x0006u
#define MENU_COMBO_ALLOWED 0x0F06u
#define MENU_HOLD_MS 3000

// ============================================================
// SIMPLE 5x7 FONT (embedded)
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
// VCFG OVERLAY STATE
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

static void draw_char2(Canvas* c, char ch, int x, int y, int fg, int bg) {
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
    while (*s) { draw_char2(c, *s++, x, y, fg, bg); x += 6; }
}

// ============================================================
// MENU STATE
// ============================================================
typedef enum { BUCKET_STREET=0, BUCKET_DRIFT=1, BUCKET_RACE=2 } BucketMode;
static BucketMode g_bucket = BUCKET_STREET;
static bool g_menu_open = false;
static int g_selected = 0;
static uint64_t g_hold_start = 0;
static bool g_was_holding = false;
static uint64_t g_frame = 0;
static uint64_t g_tick_counter = 0;

static uint64_t get_time_ms(void) { return g_frame++ * 16; }

static void handle_input() {
    SceCtrlData data;
    sceCtrlPeekBufferPositive(0, &data, 1);
    uint32_t btn = data.buttons;
    
    // Menu combo: L3(0x0002) + R3(0x0004) = 0x0006
    bool holding = (btn & MENU_COMBO_DEFAULT) == MENU_COMBO_DEFAULT;
    uint64_t now = get_time_ms();
    
    if (holding) {
        if (!g_was_holding) { g_hold_start = now; g_was_holding = true; }
        else if (!g_menu_open && (now - g_hold_start >= MENU_HOLD_MS)) { g_menu_open = true; g_selected = g_bucket; }
    } else { g_was_holding = false; g_hold_start = 0; }
    
    if (g_menu_open) {
        if (btn & 0x1000) { g_selected = (g_selected + 2) % 3; } // UP
        if (btn & 0x2000) { g_selected = (g_selected + 1) % 3; } // DOWN
        if (btn & 0x10000) { g_bucket = g_selected; g_menu_open = false; } // CROSS
        if (btn & 0x20000) { g_menu_open = false; } // CIRCLE
    }
}

// ============================================================
// DRAW MENU (VCFG style)
// ============================================================
static void draw_menu(Canvas* c) {
    if (!g_menu_open) {
        canvas_begin(c, VOV_W, 48);
        rect(c, 0, 0, VOV_W, 48, C_INK);
        round_corners(c, 0, 0, VOV_W, 48, 8);
        draw_text(c, "NFS HEAT: HOLD L3+R3 (3s) FOR MENU", 16, 18, C_CREAM, -1);
        c->w = VOV_W; c->h = 48;
        return;
    }
    
    unsigned h = 256;
    canvas_begin(c, VOV_W, h);
    
    // Panel background
    rect(c, 0, 0, VOV_W, h, C_INK);
    round_corners(c, 0, 0, VOV_W, h, 8);
    rect(c, 2, 2, VOV_W - 4, h - 4, C_CREAM);
    round_corners(c, 2, 2, VOV_W - 4, h - 4, 6);
    
    // Title
    draw_text(c, "NFS HEAT DRIFT/GRIP", 16, 12, C_INK, -1);
    draw_text(c, "v1.0", 420, 12, C_GREY, -1);
    
    // Current
    const char* buckets[] = {"STREET (UNITE Simcade)", "DRIFT (Hayeduce 3.3.2)", "RACE (UNITE Grip)"};
    draw_text(c, "Current:", 16, 36, C_INK, -1);
    draw_text(c, buckets[g_bucket], 16, 50, C_BLUE, -1);
    
    // Menu items
    draw_text(c, "SELECT HANDLING:", 16, 74, C_INK, -1);
    for (int i = 0; i < 3; i++) {
        int y = 94 + i * 22;
        int sel = (g_selected == i);
        rect(c, 12, y - 2, VOV_W - 24, 20, sel ? C_SEL : C_CREAM);
        draw_text(c, sel ? "> " : "  ", 16, y, sel ? C_INK : C_GREY, -1);
        draw_text(c, buckets[i], 32, y, sel ? C_INK : C_GREY, -1);
    }
    
    // Hints
    draw_text(c, "D-PAD: NAVIGATE  CROSS: SELECT  CIRCLE: CLOSE", 16, 180, C_GREY, -1);
    
    c->w = VOV_W; c->h = h;
}

// ============================================================
// COMMIT TO GPU MEMORY (VCFG vov_commit style)
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
    g_hash[buf] = 0;
    g_used[buf] = ++g_tick;
}

// ============================================================
// GNM HOOK (inject overlay draw via osd8 shader)
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
// OVERLAY THREAD
// ============================================================
static void* overlay_thread(void* arg) {
    (void)arg;
    while (1) { if (sceKernelGetProcessId() > 0) break; sceKernelUsleep(100000); }
    
    if (alloc_mem() < 0) return NULL;
    
    // Hook sceGnmSubmitCommandBuffers
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
// FILE HOOKS (tuning EBX redirect)
// ============================================================
#define MOD_BASE "/data/GoldHEN/mods/nfs_heat"
static int (*o_open)(const char*,int,mode_t)=0;
static ssize_t (*o_read)(int,void*,size_t)=0;
static int (*o_close)(int)=0;
static off_t (*o_lseek)(int,off_t,int)=0;

typedef struct { int fd, real_fd; char path[512]; } Intercepted;
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
            strcpy(g_int[g_inc].path, mp);
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
// PLUGIN ENTRY
// ============================================================
int plugin_load(void) {
    ScePthread t;
    scePthreadCreate(&t, NULL, overlay_thread, NULL, "nfs_heat_vov");
    scePthreadDetach(t);
    
    // File hooks in separate thread
    ScePthread t2;
    scePthreadCreate(&t2, NULL, [](void*){
        while(sceKernelGetProcessId()<=0) sceKernelUsleep(100000);
        install_file_hooks();
        return NULL;
    }, NULL, "nfs_heat_file");
    scePthreadDetach(t2);
    
    return 0;
}

int plugin_unload(void) {
    free_mem();
    return 0;
}
