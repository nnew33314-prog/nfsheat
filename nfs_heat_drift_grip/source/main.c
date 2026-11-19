// Minimal NFS Heat plugin - TEST VERSION
// Only shows "loaded" notification, delays hooks by 10 seconds

#include <Common.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/Sysmodule.h>
#include "plugin_common.h"

#define PLUGIN_NAME "nfs_heat_drift_grip"
#define MOD_DIR     GOLDHEN_PATH "/nfs_heat"
#define CFG_PATH    MOD_DIR "/config.ini"

attr_public const char *g_pluginName = PLUGIN_NAME;
attr_public const char *g_pluginDesc = "NFS Heat Street/Drift/Race handling selector";
attr_public const char *g_pluginAuth = "nfsheat";
attr_public u32 g_pluginVersion = 0x00000100;

HOOK_INIT(sceKernelOpen);
HOOK_INIT(sceKernelStat);
HOOK_INIT(fopen);

enum { MODE_STREET = 0, MODE_DRIFT = 1, MODE_RACE = 2, MODE_COUNT = 3 };
static const char *k_mode_dir[MODE_COUNT]  = { "street", "drift", "race" };
static const char *k_mode_name[MODE_COUNT] = { "STREET", "DRIFT", "RACE" };

static char titleid[16];
static volatile int  g_mode = MODE_STREET;
static volatile bool g_running = false;
static volatile bool g_hooks_installed = false;

// ---------------------------------------------------------------- helpers
static inline bool is_app0(const char *p)
{
    return p && p[0] == '/' && p[1] == 'a' && p[2] == 'p' && p[3] == 'p' &&
           p[4] == '0' && p[5] == '/' && p[6] != '\0';
}

static void notify(const char *fmt, ...)
{
    char msg[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    NotifyStatic(TEX_ICON_SYSTEM, msg);
    final_printf("[nfs_heat] %s\n", msg);
}

static void mode_path(char *out, size_t n, const char *rel)
{
    snprintf(out, n, MOD_DIR "/%s/%s", k_mode_dir[g_mode], rel);
}

static void afr_path(char *out, size_t n, const char *rel)
{
    snprintf(out, n, GOLDHEN_PATH "/AFR/%s/%s", titleid, rel);
}

// ---------------------------------------------------------------- config
static void save_config(void)
{
    FILE *f = fopen(CFG_PATH, "w");
    if (!f) return;
    fprintf(f, "mode=%d\n", (int)g_mode);
    fclose(f);
}

static void load_config(void)
{
    FILE *f = fopen(CFG_PATH, "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if (sscanf(line, "mode=%d", &v) == 1 && v >= 0 && v < MODE_COUNT)
            g_mode = v;
    }
    fclose(f);
}

static void make_dirs(void)
{
    mkdir(GOLDHEN_PATH, 0777);
    mkdir(MOD_DIR, 0777);
    for (int i = 0; i < MODE_COUNT; i++) {
        char d[128];
        snprintf(d, sizeof(d), MOD_DIR "/%s", k_mode_dir[i]);
        mkdir(d, 0777);
    }
}

// ---------------------------------------------------------------- hooks (only after delay)
s32 sceKernelOpen_hook(const char *path, s32 flags, OrbisKernelMode mode)
{
    if (!g_hooks_installed) return HOOK_CONTINUE(sceKernelOpen, s32 (*)(const char *, s32, OrbisKernelMode), path, flags, mode);
    
    if (is_app0(path) && (flags & 3) == 0 && !(flags & 0x200)) {
        const char *rel = path + 6;
        char p[MAX_PATH_];
        mode_path(p, sizeof(p), rel);
        s32 fd = HOOK_CONTINUE(sceKernelOpen, s32 (*)(const char *, s32, OrbisKernelMode), p, flags, mode);
        if (fd >= 0) return fd;
        afr_path(p, sizeof(p), rel);
        fd = HOOK_CONTINUE(sceKernelOpen, s32 (*)(const char *, s32, OrbisKernelMode), p, flags, mode);
        if (fd >= 0) return fd;
    }
    return HOOK_CONTINUE(sceKernelOpen, s32 (*)(const char *, s32, OrbisKernelMode), path, flags, mode);
}

s32 sceKernelStat_hook(const char *path, struct stat *st)
{
    if (!g_hooks_installed) return HOOK_CONTINUE(sceKernelStat, s32 (*)(const char *, struct stat *), path, st);
    if (is_app0(path)) {
        const char *rel = path + 6;
        char p[MAX_PATH_];
        mode_path(p, sizeof(p), rel);
        if (HOOK_CONTINUE(sceKernelStat, s32 (*)(const char *, struct stat *), p, st) == 0) return 0;
        afr_path(p, sizeof(p), rel);
        if (HOOK_CONTINUE(sceKernelStat, s32 (*)(const char *, struct stat *), p, st) == 0) return 0;
    }
    return HOOK_CONTINUE(sceKernelStat, s32 (*)(const char *, struct stat *), path, st);
}

FILE *fopen_hook(const char *path, const char *mode)
{
    if (!g_hooks_installed) return HOOK_CONTINUE(fopen, FILE *(*)(const char *, const char *), path, mode);
    if (is_app0(path) && mode && mode[0] == 'r' && !strchr(mode, '+')) {
        const char *rel = path + 6;
        char p[MAX_PATH_];
        FILE *fp;
        mode_path(p, sizeof(p), rel);
        fp = HOOK_CONTINUE(fopen, FILE *(*)(const char *, const char *), p, mode);
        if (fp) return fp;
        afr_path(p, sizeof(p), rel);
        fp = HOOK_CONTINUE(fopen, FILE *(*)(const char *, const char *), p, mode);
        if (fp) return fp;
    }
    return HOOK_CONTINUE(fopen, FILE *(*)(const char *, const char *), path, mode);
}

// ---------------------------------------------------------------- input thread (with 10s delay before hooks)
static int32_t get_pad_handle(void)
{
    int32_t user = 0;
    OrbisUserServiceInitializeParams param;
    param.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    sceUserServiceInitialize(&param);
    sceUserServiceGetInitialUser(&user);
    int32_t h = scePadOpen(user, 0, 0, 0);
    if (h == ORBIS_PAD_ERROR_ALREADY_OPENED)
        h = scePadGetHandle(user, 0, 0);
    return h > 0 ? h : 0;
}

static void *input_thread(void *arg)
{
    (void)arg;
    
    // CRITICAL: Wait 10 seconds for game to fully initialize
    sleep(10);
    
    // NOW install hooks - after game is stable
    HOOK32(sceKernelOpen);
    HOOK32(sceKernelStat);
    HOOK32(fopen);
    g_hooks_installed = true;
    final_printf("[nfs_heat] hooks installed after delay\n");
    
    if (scePadInit() < 0) {
        notify("NFS Heat plugin: pad init failed");
        scePthreadExit(NULL);
        return NULL;
    }

    notify("NFS Heat plugin LOADED - %s (L3+R3+Left/Right)", k_mode_name[g_mode]);

    const uint32_t HOLD = ORBIS_PAD_BUTTON_L3 | ORBIS_PAD_BUTTON_R3;
    uint32_t prev = 0;
    OrbisPadData pd;

    while (g_running) {
        int32_t h = get_pad_handle();
        if (h && scePadReadState(h, &pd) == 0 && pd.connected) {
            uint32_t b = pd.buttons;
            uint32_t pressed = b & ~prev;
            if ((b & HOLD) == HOLD) {
                if (pressed & ORBIS_PAD_BUTTON_RIGHT) {
                    g_mode = (g_mode + 1) % MODE_COUNT;
                    save_config();
                    notify("NFS Heat handling: %s", k_mode_name[g_mode]);
                } else if (pressed & ORBIS_PAD_BUTTON_LEFT) {
                    g_mode = (g_mode + MODE_COUNT - 1) % MODE_COUNT;
                    save_config();
                    notify("NFS Heat handling: %s", k_mode_name[g_mode]);
                } else if (pressed & ORBIS_PAD_BUTTON_UP) {
                    notify("NFS Heat: %s", k_mode_name[g_mode]);
                }
            }
            prev = b;
        }
        usleep(50 * 1000);
    }
    scePthreadExit(NULL);
    return NULL;
}

// ---------------------------------------------------------------- entry points
s32 attr_public plugin_load(s32 argc, const char *argv[])
{
    final_printf("[GoldHEN] <%s\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    boot_ver();

    struct proc_info procInfo;
    if (!sys_sdk_proc_info(&procInfo)) {
        memcpy(titleid, procInfo.titleid, sizeof(titleid));
        print_proc_info();
    }

    make_dirs();
    load_config();

    // DO NOT install hooks here - wait for input thread delay
    g_running = true;
    OrbisPthread thread;
    scePthreadCreate(&thread, NULL, input_thread, NULL, "nfs_heat_input");
    return 0;
}

s32 attr_public plugin_unload(s32 argc, const char *argv[])
{
    final_printf("[GoldHEN] <%s\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    g_running = false;
    if (g_hooks_installed) {
        UNHOOK(sceKernelOpen);
        UNHOOK(sceKernelStat);
        UNHOOK(fopen);
    }
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void *args)
{
    return 0;
}

s32 attr_module_hidden module_stop(s64 argc, const void *args)
{
    return 0;
}
