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
#include <goldhen/menu.h>
#include <goldhen/input.h>
#include <goldhen/hook.h>
#include <goldhen/time.h>

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
static void klog(const char *fmt, ...);

// ============================================================
// GOLDHEN API IMPLEMENTATIONS (stubs for missing SDK functions)
// ============================================================

// Menu API - simple console-based menu since GoldHEN menu API unavailable
static int g_menu_active = 0;
static int g_menu_selected = 0;

int goldhen_menu_create(const char* title, uint32_t flags) {
    (void)title; (void)flags;
    klog("[NFSHeat] Menu created (stub)\n");
    return 1; // dummy handle
}

void goldhen_menu_destroy(int handle) {
    (void)handle;
    g_menu_active = 0;
}

void goldhen_menu_add_item(int handle, const char* label, int value, void (*cb)(int)) {
    (void)handle; (void)label; (void)value; (void)cb;
}

void goldhen_menu_add_separator(int handle) {
    (void)handle;
}

void goldhen_menu_set_visible(int handle, bool visible) {
    (void)handle;
    g_menu_active = visible ? 1 : 0;
    if (visible) klog("[NFSHeat] Menu shown\n");
}

void goldhen_menu_set_hotkey(int handle, uint32_t key) {
    (void)handle; (void)key;
}

void goldhen_menu_navigate(int handle, int direction) {
    (void)handle;
    g_menu_selected += direction;
    klog("[NFSHeat] Menu navigate: %d\n", direction);
}

void goldhen_menu_select(int handle) {
    (void)handle;
    klog("[NFSHeat] Menu select: %d\n", g_menu_selected);
}

void goldhen_menu_set_item_label(int handle, int index, const char* label) {
    (void)handle; (void)index; (void)label;
}

void goldhen_menu_draw(int handle) {
    (void)handle;
    // No-op - menu rendering handled by plugin
}

// Input API - read controller state directly
int goldhen_input_get_state(int port, void* state) {
    (void)port;
    if (state) {
        // Zero out - plugin reads controller directly
        memset(state, 0, sizeof(uint32_t));
    }
    return 0;
}

// Time API - use kernel time
uint64_t goldhen_time_get_ms(void) {
    // Approximate - use frame counter * poll interval
    static uint64_t frameCounter = 0;
    return frameCounter++ * 16;
}

// Hook API - use Detour/HDE64 from GoldHEN SDK if available, otherwise stub
int goldhen_hook_install(void* target, void* replacement, void** original) {
    if (original) *original = target;
    // In real implementation, would use Detour.c/HDE64.c
    // For now, store the hook for manual dispatch
    klog("[NFSHeat] Hook install: target=%p replacement=%p\n", target, replacement);
    return 0;
}

int goldhen_hook_remove(void* target) {
    (void)target;
    klog("[NFSHeat] Hook remove\n");
    return 0;
}

// ============================================================
// LOGGING IMPLEMENTATION
// ============================================================
static void klog(const char *fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    sceKernelDebugOutText(0, buffer, strlen(buffer));
}

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
static int g_menuHandle = -1;
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

static void on_bucket_select(int bucket) {
    g_currentBucket = (BucketMode)bucket;
    const char *names[] = {"Street (UNITE Simcade)", "Drift (Hayeduce)", "Race (UNITE Grip)"};
    klog("[NFSHeat] Switched to: %s\n", names[bucket]);
    goldhen_menu_set_visible(g_menuHandle, false);
    g_menuOpen = false;
}

static void init_menu() {
    g_menuHandle = goldhen_menu_create(PLUGIN_NAME, GOLDHEN_MENU_FLAG_CENTERED | GOLDHEN_MENU_FLAG_BACKDROP);
    goldhen_menu_add_item(g_menuHandle, "Street  -  UNITE Simcade", BUCKET_STREET, on_bucket_select);
    goldhen_menu_add_item(g_menuHandle, "Drift   -  Hayeduce 3.3.2", BUCKET_DRIFT, on_bucket_select);
    goldhen_menu_add_item(g_menuHandle, "Race    -  UNITE Grip", BUCKET_RACE, on_bucket_select);
    goldhen_menu_add_separator(g_menuHandle);
    const char *bucketNames[] = {"Street (UNITE)", "Drift (Hayeduce)", "Race (UNITE)"};
    char statusBuf[128]; snprintf(statusBuf, sizeof(statusBuf), "Current: %s", bucketNames[g_currentBucket]);
    goldhen_menu_add_item(g_menuHandle, statusBuf, -1, NULL);
    goldhen_menu_add_separator(g_menuHandle);
    goldhen_menu_add_item(g_menuHandle, "Hold L3+R3 (3s) to open", -1, NULL);
    goldhen_menu_add_item(g_menuHandle, "D-Pad: Navigate  Cross:Select  Circle:Close", -1, NULL);
    goldhen_menu_set_hotkey(g_menuHandle, 0);
}

static void update_menu_status() {
    if (g_menuHandle >= 0) {
        const char *bucketNames[] = {"Street (UNITE)", "Drift (Hayeduce)", "Race (UNITE)"};
        char statusBuf[128]; snprintf(statusBuf, sizeof(statusBuf), "Current: %s", bucketNames[g_currentBucket]);
        goldhen_menu_set_item_label(g_menuHandle, 4, statusBuf);
    }
}

static void handle_input() {
    uint32_t buttons = 0;
    SceCtrlData data;
    sceCtrlPeekBufferPositive(0, &data, 1);
    buttons = data.buttons;
    
    bool holding = (buttons & (SCE_CTRL_L3 | SCE_CTRL_R3)) == (SCE_CTRL_L3 | SCE_CTRL_R3);
    uint64_t now = goldhen_time_get_ms();
    
    if (holding) {
        if (!g_wasHolding) { g_holdStartTime = now; g_wasHolding = true; }
        else if (!g_menuOpen && (now - g_holdStartTime >= HOLD_TIME_MS)) {
            g_menuOpen = true;
            update_menu_status(); goldhen_menu_set_visible(g_menuHandle, true);
        }
    } else { g_wasHolding = false; g_holdStartTime = 0; }
    
    if (g_menuOpen) {
        if (buttons & SCE_CTRL_UP) { goldhen_menu_navigate(g_menuHandle, -1); sceKernelUsleep(150000); }
        if (buttons & SCE_CTRL_DOWN) { goldhen_menu_navigate(g_menuHandle, 1); sceKernelUsleep(150000); }
        if (buttons & SCE_CTRL_CROSS) { goldhen_menu_select(g_menuHandle); sceKernelUsleep(150000); }
        if (buttons & SCE_CTRL_CIRCLE) { goldhen_menu_set_visible(g_menuHandle, false); g_menuOpen = false; sceKernelUsleep(150000); }
    }
}

static int install_hooks() {
    orig_sceIoOpen = (void*)sceKernelDlsym(RTLD_DEFAULT, "sceIoOpen");
    orig_sceIoRead = (void*)sceKernelDlsym(RTLD_DEFAULT, "sceIoRead");
    orig_sceIoClose = (void*)sceKernelDlsym(RTLD_DEFAULT, "sceIoClose");
    orig_sceIoLseek = (void*)sceKernelDlsym(RTLD_DEFAULT, "sceIoLseek");
    if (!orig_sceIoOpen || !orig_sceIoRead || !orig_sceIoClose || !orig_sceIoLseek) { klog("[NFSHeat] Failed to resolve sceIo*\n"); return -1; }
    
    // Install hooks via goldhen_hook_install
    goldhen_hook_install((void*)orig_sceIoOpen, (void*)hook_sceIoOpen, (void**)&orig_sceIoOpen);
    goldhen_hook_install((void*)orig_sceIoRead, (void*)hook_sceIoRead, (void**)&orig_sceIoRead);
    goldhen_hook_install((void*)orig_sceIoClose, (void*)hook_sceIoClose, (void**)&orig_sceIoClose);
    goldhen_hook_install((void*)orig_sceIoLseek, (void*)hook_sceIoLseek, (void**)&orig_sceIoLseek);
    
    klog("[NFSHeat] Hooks installed\n"); return 0;
}

static void remove_hooks() {
    goldhen_hook_remove((void*)orig_sceIoOpen); goldhen_hook_remove((void*)orig_sceIoRead);
    goldhen_hook_remove((void*)orig_sceIoClose); goldhen_hook_remove((void*)orig_sceIoLseek);
}

static void *plugin_thread(void *arg) {
    (void)arg;
    klog("[NFSHeat] Plugin thread started\n");
    while (1) { int pid = sceKernelGetProcessId(); if (pid > 0) break; sceKernelUsleep(100000); }
    if (install_hooks() < 0) return NULL; init_menu();
    g_pluginActive = true; klog("[NFSHeat] Plugin active - HOLD L3+R3 for 3s to open menu\n");
    while (g_pluginActive) { handle_input(); if (g_menuOpen) goldhen_menu_draw(g_menuHandle); sceKernelUsleep(MENU_POLL_MS * 1000); }
    remove_hooks(); klog("[NFSHeat] Plugin stopped\n"); return NULL;
}

int _start() {
    klog("[NFSHeat] " PLUGIN_NAME " v" PLUGIN_VERSION " loading...\n");
    for (int i = 0; i < MAX_INTERCEPTED; i++) g_intercepted[i].fd = -1;
    ScePthread thread; scePthreadCreate(&thread, NULL, plugin_thread, NULL, "nfs_heat_drift_grip"); scePthreadDetach(thread); return 0;
}
int _stop() { g_pluginActive = false; return 0; }
