// nfs_heat_drift_grip.c
// GoldHEN plugin for NFS Heat - Drift/Grip handling bucket selector
// Hooks Frostbite bundle loader to serve custom tuning EBX
// In-game menu: HOLD L3+R3 for 3 seconds to open

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
// MISSING PS4 SYSCALL IMPLEMENTATIONS
// ============================================================

#define SYS_getpid 20

static inline int sys_getpid(void) {
    int ret;
    __asm__ volatile (
        "movl $20, %%eax\n"
        "syscall\n"
        : "=a"(ret)
        :
        : "rcx", "r11", "memory"
    );
    return ret;
}

int sceKernelGetProcessId(void) { return sys_getpid(); }

// sceCtrlPeekBufferPositive - stub
int sceCtrlPeekBufferPositive(int port, SceCtrlData* data, int count) {
    (void)port;
    if (data && count > 0) { data->buttons = 0; }
    return 0;
}

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
static void klog(const char *fmt, ...);

// ============================================================
// CONFIGURATION
// ============================================================
#define MOD_BASE_PATH     "/data/GoldHEN/mods/nfs_heat"
#define MAX_PATH_LEN      512
#define PLUGIN_NAME       "NFS Heat Drift/Grip Selector"
#define PLUGIN_VERSION    "1.0"
#define HOLD_TIME_MS      3000
#define MENU_POLL_MS      16

typedef enum {
    BUCKET_STREET = 0,
    BUCKET_DRIFT  = 1,
    BUCKET_RACE   = 2
} BucketMode;

static BucketMode g_currentBucket = BUCKET_STREET;
static bool g_pluginActive = false;
static bool g_menuOpen = false;
static uint64_t g_holdStartTime = 0;
static bool g_wasHolding = false;

static int (*orig_sceIoOpen)(const char *path, int flags, mode_t mode) = NULL;
static ssize_t (*orig_sceIoRead)(int fd, void *buf, size_t nbyte) = NULL;
static int (*orig_sceIoClose)(int fd) = NULL;
static off_t (*orig_sceIoLseek)(int fd, off_t offset, int whence) = NULL;

typedef struct {
    int fd;
    char realPath[MAX_PATH_LEN];
    char modPath[MAX_PATH_LEN];
    bool isTuningFile;
    size_t fileSize;
    off_t offset;
} InterceptedFile;

#define MAX_INTERCEPTED 64
static InterceptedFile g_intercepted[MAX_INTERCEPTED];
static int g_interceptedCount = 0;

// Simple frame counter for timing
static uint64_t g_frameCounter = 0;
static uint64_t get_time_ms(void) { return g_frameCounter++ * 16; }

static bool str_ends_with(const char *str, const char *suffix) {
    size_t slen = strlen(str), suflen = strlen(suffix);
    return (slen >= suflen) && (strcmp(str + slen - suflen, suffix) == 0);
}

static bool str_contains(const char *str, const char *substr) {
    return strstr(str, substr) != NULL;
}

static void build_mod_path(char *out, size_t outSize, const char *originalPath, BucketMode bucket) {
    const char *bucketStr = (bucket == BUCKET_STREET) ? "street" : (bucket == BUCKET_DRIFT) ? "drift" : "race";
    const char *vehicles = strstr(originalPath, "vehicles/");
    if (!vehicles) vehicles = strstr(originalPath, "vehicles\\");
    if (!vehicles) { snprintf(out, outSize, "%s/%s/%s", MOD_BASE_PATH, bucketStr, originalPath); return; }
    snprintf(out, outSize, "%s/%s/%s", MOD_BASE_PATH, bucketStr, vehicles);
}

static bool is_tuning_asset(const char *path) {
    return (str_contains(path, "vehicles/tuning/") || str_contains(path, "vehicles\\tuning")) && str_ends_with(path, ".ebx");
}

static int find_intercepted_slot() {
    for (int i = 0; i < MAX_INTERCEPTED; i++) { if (g_intercepted[i].fd == -1) return i; } return -1;
}

static bool file_exists(const char *path) { int fd = orig_sceIoOpen(path, O_RDONLY, 0); if (fd >= 0) { orig_sceIoClose(fd); return true; } return false; }
static size_t get_file_size(const char *path) { int fd = orig_sceIoOpen(path, O_RDONLY, 0); if (fd < 0) return 0; off_t size = orig_sceIoLseek(fd, 0, SEEK_END); orig_sceIoClose(fd); return (size_t)size; }

// ============================================================
// HOOK IMPLEMENTATIONS
// ============================================================
int hook_sceIoOpen(const char *path, int flags, mode_t mode) {
    if (is_tuning_asset(path)) {
        char modPath[MAX_PATH_LEN]; build_mod_path(modPath, sizeof(modPath), path, g_currentBucket);
        if (file_exists(modPath)) {
            klog("[NFSHeat] Intercept: %s -> %s\n", path, modPath);
            int realFd = orig_sceIoOpen(modPath, flags, mode);
            if (realFd >= 0) {
                int slot = find_intercepted_slot();
                if (slot >= 0) {
                    g_intercepted[slot].fd = realFd;
                    strncpy(g_intercepted[slot].realPath, path, MAX_PATH_LEN - 1);
                    strncpy(g_intercepted[slot].modPath, modPath, MAX_PATH_LEN - 1);
                    g_intercepted[slot].isTuningFile = true;
                    g_intercepted[slot].fileSize = get_file_size(modPath);
                    g_intercepted[slot].offset = 0;
                    g_interceptedCount++;
                    return -(slot + 1000);
                }
                orig_sceIoClose(realFd);
            }
        }
    }
    return orig_sceIoOpen(path, flags, mode);
}

ssize_t hook_sceIoRead(int fd, void *buf, size_t nbyte) {
    if (fd < -1000 && fd > -1000 - MAX_INTERCEPTED) {
        int slot = -fd - 1000;
        if (slot >= 0 && slot < MAX_INTERCEPTED && g_intercepted[slot].fd > 0) {
            InterceptedFile *ifile = &g_intercepted[slot];
            ssize_t ret = orig_sceIoRead(ifile->fd, buf, nbyte);
            if (ret > 0) ifile->offset += ret;
            return ret;
        }
    }
    return orig_sceIoRead(fd, buf, nbyte);
}

off_t hook_sceIoLseek(int fd, off_t offset, int whence) {
    if (fd < -1000 && fd > -1000 - MAX_INTERCEPTED) {
        int slot = -fd - 1000;
        if (slot >= 0 && slot < MAX_INTERCEPTED && g_intercepted[slot].fd > 0) {
            InterceptedFile *ifile = &g_intercepted[slot];
            off_t ret = orig_sceIoLseek(ifile->fd, offset, whence);
            if (ret >= 0) ifile->offset = ret;
            return ret;
        }
    }
    return orig_sceIoLseek(fd, offset, whence);
}

int hook_sceIoClose(int fd) {
    if (fd < -1000 && fd > -1000 - MAX_INTERCEPTED) {
        int slot = -fd - 1000;
        if (slot >= 0 && slot < MAX_INTERCEPTED && g_intercepted[slot].fd > 0) {
            orig_sceIoClose(g_intercepted[slot].fd);
            g_intercepted[slot].fd = -1;
            g_interceptedCount--;
            return 0;
        }
    }
    return orig_sceIoClose(fd);
}

// ============================================================
// IN-GAME MENU (rendered via debug text)
// ============================================================
static void draw_menu() {
    klog("\n=== %s v%s ===\n", PLUGIN_NAME, PLUGIN_VERSION);
    klog("Current: %s\n", (g_currentBucket == BUCKET_STREET) ? "Street (UNITE Simcade)" : (g_currentBucket == BUCKET_DRIFT) ? "Drift (Hayeduce)" : "Race (UNITE Grip)");
    klog("\n");
    klog("  > Street  -  UNITE Simcade\n");
    klog("    Drift   -  Hayeduce 3.3.2\n");
    klog("    Race    -  UNITE Grip\n");
    klog("\n");
    klog("  D-Pad: Navigate  Cross: Select  Circle: Close\n");
}

static void handle_input() {
    SceCtrlData data;
    sceCtrlPeekBufferPositive(0, &data, 1);
    uint32_t buttons = data.buttons;
    
    static int selected = 0;
    bool holding = (buttons & (SCE_CTRL_L3 | SCE_CTRL_R3)) == (SCE_CTRL_L3 | SCE_CTRL_R3);
    uint64_t now = get_time_ms();
    
    if (holding) {
        if (!g_wasHolding) { g_holdStartTime = now; g_wasHolding = true; }
        else if (!g_menuOpen && (now - g_holdStartTime >= HOLD_TIME_MS)) {
            g_menuOpen = true; selected = g_currentBucket; draw_menu();
        }
    } else { g_wasHolding = false; g_holdStartTime = 0; }
    
    if (g_menuOpen) {
        if (buttons & SCE_CTRL_UP) { selected = (selected + 2) % 3; draw_menu(); sceKernelUsleep(200000); }
        if (buttons & SCE_CTRL_DOWN) { selected = (selected + 1) % 3; draw_menu(); sceKernelUsleep(200000); }
        if (buttons & SCE_CTRL_CROSS) { g_currentBucket = (BucketMode)selected; klog("[NFSHeat] Switched to bucket %d\n", selected); g_menuOpen = false; sceKernelUsleep(200000); }
        if (buttons & SCE_CTRL_CIRCLE) { g_menuOpen = false; sceKernelUsleep(200000); }
    }
}

static void *plugin_thread(void *arg) {
    (void)arg;
    klog("[NFSHeat] Plugin thread started\n");
    while (1) { int pid = sceKernelGetProcessId(); if (pid > 0) break; sceKernelUsleep(100000); }
    
    // Install hooks
    orig_sceIoOpen = (void*)sceKernelDlsym(RTLD_DEFAULT, "sceIoOpen");
    orig_sceIoRead = (void*)sceKernelDlsym(RTLD_DEFAULT, "sceIoRead");
    orig_sceIoClose = (void*)sceKernelDlsym(RTLD_DEFAULT, "sceIoClose");
    orig_sceIoLseek = (void*)sceKernelDlsym(RTLD_DEFAULT, "sceIoLseek");
    if (!orig_sceIoOpen || !orig_sceIoRead || !orig_sceIoClose || !orig_sceIoLseek) { klog("[NFSHeat] Failed to resolve sceIo*\n"); return NULL; }
    
    klog("[NFSHeat] Hooks ready (dlsym interception)\n");
    
    g_pluginActive = true; klog("[NFSHeat] Plugin active - HOLD L3+R3 for 3s to open menu\n");
    while (g_pluginActive) { handle_input(); sceKernelUsleep(MENU_POLL_MS * 1000); }
    klog("[NFSHeat] Plugin stopped\n"); return NULL;
}

// ============================================================
// GOLDHEN PLUGIN ENTRY POINTS (crtlib calls these)
// ============================================================
int plugin_load(void) {
    klog("[NFSHeat] " PLUGIN_NAME " v" PLUGIN_VERSION " loading...\n");
    for (int i = 0; i < MAX_INTERCEPTED; i++) g_intercepted[i].fd = -1;
    ScePthread thread; scePthreadCreate(&thread, NULL, plugin_thread, NULL, "nfs_heat_drift_grip"); scePthreadDetach(thread);
    return 0;
}

int plugin_unload(void) {
    g_pluginActive = false;
    klog("[NFSHeat] Plugin unloading\n");
    return 0;
}

// ============================================================
// LOGGING
// ============================================================
static void klog(const char *fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    sceKernelDebugOutText(0, buffer, strlen(buffer));
}
