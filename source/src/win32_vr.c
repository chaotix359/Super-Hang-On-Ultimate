/* Windows VR frontend part: OpenXR session on Direct3D 11, the game picture as quad layers, handlebar input.
 *
 * No OpenXR SDK is needed to build or run: the headers are in third_party/openxr, and at run time either
 * openxr_loader.dll (if one sits next to the program) or the active runtime itself is loaded - the runtime is
 * found the way the loader finds it (registry HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime -> its JSON manifest
 * -> library_path -> xrNegotiateLoaderRuntimeInterface).
 *
 * The picture: one swapchain holds left | right (each game pixel enlarged, so the headset's smoothing keeps the
 * pixels crisp; the 3D picture comes with SHO_3D_SUB columns per pixel already); two quad layers, one per eye, show its halves on the same rectangle in the
 * room. With 3D off one quad shows the same picture to both eyes.
 * The handlebars: the line from the left hand to the right hand is the bar. Tilting it (one hand lower) leans
 * the bike that way; triggers are throttle (right) and brake (left). */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include "third_party/openxr/openxr.h"
#include "third_party/openxr/openxr_platform.h"
#include "third_party/openxr/openxr_loader_negotiation.h"
#include "win32_vr.h"

#define VR_SCALE_X 4                       /* columns per game pixel in the headset picture (>= SHO_3D_SUB, a multiple of it) */
#define VR_SCALE_Y 3                       /* rows per game line */
#define TEX_W (2 * SHO_MAX_W * VR_SCALE_X)
#define TEX_H (SHO_H * VR_SCALE_Y)

/* ---- the OpenXR functions used, fetched through xrGetInstanceProcAddr ---- */
#define XR_FUNCS(X) \
    X(xrDestroyInstance) X(xrGetSystem) X(xrCreateSession) X(xrDestroySession) X(xrPollEvent) \
    X(xrBeginSession) X(xrEndSession) X(xrRequestExitSession) X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) \
    X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrLocateSpace) X(xrEnumerateSwapchainFormats) \
    X(xrCreateSwapchain) X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) X(xrAcquireSwapchainImage) \
    X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) X(xrStringToPath) X(xrCreateActionSet) X(xrCreateAction) \
    X(xrSuggestInteractionProfileBindings) X(xrAttachSessionActionSets) X(xrCreateActionSpace) X(xrSyncActions) \
    X(xrGetActionStateFloat) X(xrGetActionStateBoolean) X(xrGetActionStateVector2f) \
    X(xrEnumerateEnvironmentBlendModes) X(xrGetD3D11GraphicsRequirementsKHR) X(xrResultToString)
#define X(n) static PFN_##n n;
XR_FUNCS(X)
#undef X
static PFN_xrGetInstanceProcAddr gipa;
static PFN_xrCreateInstance xrCreateInstance;

void sho_diag(const char *fmt, ...);

static HMODULE xr_dll;
static XrInstance inst; static XrSystemId sysid; static XrSession sess; static XrSpace local_space, view_space;
static XrSwapchain chain; static ID3D11Texture2D *chain_tex[16]; static uint32_t chain_n; static int chain_rgba;
static ID3D11Device *dev; static ID3D11DeviceContext *ctx;
static XrEnvironmentBlendMode blend = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
static int running, frame_begun, should_render, want_recenter = 1, have_screen;
static XrTime disp_time; static XrDuration disp_period;
static XrPosef screen_pose = { { 0, 0, 0, 1 }, { 0, 0, -2 } };
static vr_settings cfg = { 2.0f, 3.2f, -0.10f, 30.0f, 0, 0, 0, 40.0f };
static XrQuaternionf twist_ref; static int twist_on;   /* the right hand's orientation when its grip button went down */

static XrActionSet aset;
static XrAction a_grip, a_trigger, a_squeeze, a_a, a_b, a_x, a_y, a_menu, a_stick, a_recenter, a_rstick;
static float anchor_x, anchor_y, anchor_z, anchor_fx, anchor_fz = -1;   /* the head when the screen was placed */
static int cfg_moved, cfg_idle;
static XrPath hand_path[2]; static XrSpace grip_space[2];
static int menu_edge, menu_prev, recenter_prev, y_edge, y_prev;
/* the headset thread and what it shares with the game's thread (under `lock`) */
static CRITICAL_SECTION lock; static int lock_made;
static HANDLE thread; static volatile LONG stop_thread;
static int quit_flag, menu_flag;                 /* the session ended; the game has a menu up (from vr_input) */
static sho_input pad_snap;                       /* the controllers as of the headset's last frame */
static vr_settings tc; static float t_afx, t_afz = -1, t_dheight; static int t_y_edge, t_menu_edge, t_recenter;   /* the headset thread's own (see gather) */
static double st_frames, st_ms; static LARGE_INTEGER st_freq;   /* statistics: headset frames made and the time spent making them */
static uint32_t pic[2 * SHO_MAX_W * SHO_3D_SUB * SHO_H]; static int pic_fw, pic_eye_w, pic_depth, pic_seq; static double pic_aspect;   /* the newest picture */

static void seterr(char *err, int n, const char *what, XrResult r) {
    char name[XR_MAX_RESULT_STRING_SIZE] = "";
    if (inst && xrResultToString) xrResultToString(inst, r, name);
    if (err && n) snprintf(err, (size_t)n, "%s (%s, %d)", what, name[0] ? name : "OpenXR error", (int)r);
    sho_diag("VR: %s failed: %s (%d)", what, name, (int)r);
}

/* ---- finding OpenXR ---- */
static int load_runtime_direct(char *err, int errlen) {
    char manifest[1024] = ""; DWORD sz = sizeof manifest, type = 0; HKEY k;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Khronos\\OpenXR\\1", 0, KEY_READ, &k) != ERROR_SUCCESS ||
        (RegQueryValueExA(k, "ActiveRuntime", NULL, &type, (BYTE *)manifest, &sz), RegCloseKey(k), !manifest[0])) {
        snprintf(err, (size_t)errlen, "No OpenXR runtime is set up on this PC. Start Virtual Desktop Streamer, the Meta Quest Link app or SteamVR first (and make it the active OpenXR runtime).");
        return 0;
    }
    manifest[sizeof manifest - 1] = 0;
    FILE *f = fopen(manifest, "rb");
    if (!f) { snprintf(err, (size_t)errlen, "The OpenXR runtime's description file can't be read:\n%s", manifest); return 0; }
    static char js[16384]; size_t n = fread(js, 1, sizeof js - 1, f); fclose(f); js[n] = 0;
    char *p = strstr(js, "\"library_path\"");
    if (p) p = strchr(p + 14, ':');
    if (p) p = strchr(p, '"');
    if (!p) { snprintf(err, (size_t)errlen, "The OpenXR runtime's description file has no library_path:\n%s", manifest); return 0; }
    char lib[1024]; int o = 0;
    for (p++; *p && *p != '"' && o < (int)sizeof lib - 1; p++) {
        if (*p == '\\' && p[1]) p++;                 /* JSON escapes: \\ and \/ */
        lib[o++] = *p == '/' ? '\\' : *p;
    }
    lib[o] = 0;
    char full[2048];
    if ((lib[0] && lib[1] == ':') || (lib[0] == '\\' && lib[1] == '\\')) snprintf(full, sizeof full, "%s", lib);
    else {   /* relative to the manifest's folder */
        char dir[1024]; snprintf(dir, sizeof dir, "%s", manifest);
        char *s1 = strrchr(dir, '\\'), *s2 = strrchr(dir, '/'); char *s = s1 > s2 ? s1 : s2; if (s) *s = 0; else dir[0] = 0;
        snprintf(full, sizeof full, "%s\\%s", dir, lib);
    }
    sho_diag("VR: OpenXR runtime %s", full);
    xr_dll = LoadLibraryExA(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!xr_dll) { snprintf(err, (size_t)errlen, "The OpenXR runtime could not be loaded:\n%s", full); return 0; }
    PFN_xrNegotiateLoaderRuntimeInterface nego = (PFN_xrNegotiateLoaderRuntimeInterface)(void *)GetProcAddress(xr_dll, "xrNegotiateLoaderRuntimeInterface");
    if (!nego) { snprintf(err, (size_t)errlen, "Not an OpenXR runtime:\n%s", full); return 0; }
    XrNegotiateLoaderInfo li; memset(&li, 0, sizeof li);
    li.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO; li.structVersion = XR_LOADER_INFO_STRUCT_VERSION; li.structSize = sizeof li;
    li.minInterfaceVersion = 1; li.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    li.minApiVersion = XR_MAKE_VERSION(1, 0, 0); li.maxApiVersion = XR_MAKE_VERSION(1, 0x3ff, 0xfff);
    XrNegotiateRuntimeRequest rr; memset(&rr, 0, sizeof rr);
    rr.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST; rr.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION; rr.structSize = sizeof rr;
    XrResult r = nego(&li, &rr);
    if (XR_FAILED(r) || !rr.getInstanceProcAddr) { snprintf(err, (size_t)errlen, "The OpenXR runtime refused the connection (%d).", (int)r); return 0; }
    gipa = rr.getInstanceProcAddr;
    return 1;
}
static int load_openxr(char *err, int errlen) {
    xr_dll = LoadLibraryA("openxr_loader.dll");       /* the standard loader, if the player put one next to the game */
    if (xr_dll) {
        gipa = (PFN_xrGetInstanceProcAddr)(void *)GetProcAddress(xr_dll, "xrGetInstanceProcAddr");
        if (gipa) { sho_diag("VR: using openxr_loader.dll"); return 1; }
        FreeLibrary(xr_dll); xr_dll = NULL;
    }
    return load_runtime_direct(err, errlen);
}

/* ---- Direct3D device on the headset's adapter ---- */
static int make_device(LUID luid, D3D_FEATURE_LEVEL minlevel) {
    IDXGIFactory1 *fac = NULL; IDXGIAdapter1 *ad = NULL, *found = NULL;
    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&fac))) return 0;
    for (UINT i = 0; IDXGIFactory1_EnumAdapters1(fac, i, &ad) == S_OK; i++) {
        DXGI_ADAPTER_DESC1 d; IDXGIAdapter1_GetDesc1(ad, &d);
        if (d.AdapterLuid.LowPart == luid.LowPart && d.AdapterLuid.HighPart == luid.HighPart) { found = ad; break; }
        IDXGIAdapter1_Release(ad);
    }
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    int first = 0; while (first < 3 && levels[first] > D3D_FEATURE_LEVEL_11_1) first++;
    int count = 4; while (count > 1 && levels[count - 1] < minlevel) count--;
    HRESULT hr = D3D11CreateDevice((IDXGIAdapter *)found, found ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, (UINT)count, D3D11_SDK_VERSION, &dev, NULL, &ctx);
    if (FAILED(hr))   /* (no 11.1 on this system: ask again without it) */
        hr = D3D11CreateDevice((IDXGIAdapter *)found, found ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels + 1, (UINT)(count > 1 ? count - 1 : 1), D3D11_SDK_VERSION, &dev, NULL, &ctx);
    if (found) IDXGIAdapter1_Release(found);
    IDXGIFactory1_Release(fac);
    return SUCCEEDED(hr);
}

/* ---- input actions ---- */
static XrPath path(const char *s) { XrPath p = XR_NULL_PATH; xrStringToPath(inst, s, &p); return p; }
static XrAction make_action(const char *name, const char *nice, XrActionType type, int hands) {
    XrActionCreateInfo ci; memset(&ci, 0, sizeof ci); ci.type = XR_TYPE_ACTION_CREATE_INFO; ci.actionType = type;
    snprintf(ci.actionName, sizeof ci.actionName, "%s", name); snprintf(ci.localizedActionName, sizeof ci.localizedActionName, "%s", nice);
    if (hands) { ci.countSubactionPaths = 2; ci.subactionPaths = hand_path; }
    XrAction a = XR_NULL_HANDLE; xrCreateAction(aset, &ci, &a); return a;
}
static void suggest(const char *profile, const XrActionSuggestedBinding *b, uint32_t n) {
    XrInteractionProfileSuggestedBinding s; memset(&s, 0, sizeof s); s.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    s.interactionProfile = path(profile); s.suggestedBindings = b; s.countSuggestedBindings = n;
    XrResult r = xrSuggestInteractionProfileBindings(inst, &s);
    if (XR_FAILED(r)) sho_diag("VR: bindings for %s not taken (%d)", profile, (int)r);
}
static int make_actions(void) {
    hand_path[0] = path("/user/hand/left"); hand_path[1] = path("/user/hand/right");
    XrActionSetCreateInfo si; memset(&si, 0, sizeof si); si.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    snprintf(si.actionSetName, sizeof si.actionSetName, "ride"); snprintf(si.localizedActionSetName, sizeof si.localizedActionSetName, "Riding");
    if (XR_FAILED(xrCreateActionSet(inst, &si, &aset))) return 0;
    a_grip = make_action("handlebar", "Handlebar grip", XR_ACTION_TYPE_POSE_INPUT, 1);
    a_trigger = make_action("pedals", "Throttle (right) / brake (left)", XR_ACTION_TYPE_FLOAT_INPUT, 1);
    a_squeeze = make_action("turbo", "Turbo", XR_ACTION_TYPE_FLOAT_INPUT, 1);
    a_a = make_action("select", "Select / accelerate", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    a_b = make_action("back", "Back", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    a_x = make_action("coin", "Insert coin", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    a_y = make_action("settings", "3D / screen settings menu", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    a_menu = make_action("pause", "Start / pause", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    a_stick = make_action("menu_move", "Menu directions", XR_ACTION_TYPE_VECTOR2F_INPUT, 0);
    a_recenter = make_action("recenter", "Recentre the screen", XR_ACTION_TYPE_BOOLEAN_INPUT, 0);
    a_rstick = make_action("screen_move", "Move the screen up / down", XR_ACTION_TYPE_VECTOR2F_INPUT, 0);

    XrActionSuggestedBinding touch[] = {
        { a_grip, path("/user/hand/left/input/grip/pose") }, { a_grip, path("/user/hand/right/input/grip/pose") },
        { a_trigger, path("/user/hand/left/input/trigger/value") }, { a_trigger, path("/user/hand/right/input/trigger/value") },
        { a_squeeze, path("/user/hand/left/input/squeeze/value") }, { a_squeeze, path("/user/hand/right/input/squeeze/value") },
        { a_a, path("/user/hand/right/input/a/click") }, { a_b, path("/user/hand/right/input/b/click") },
        { a_x, path("/user/hand/left/input/x/click") }, { a_y, path("/user/hand/left/input/y/click") },
        { a_menu, path("/user/hand/left/input/menu/click") },
        { a_stick, path("/user/hand/left/input/thumbstick") }, { a_recenter, path("/user/hand/right/input/thumbstick/click") },
        { a_rstick, path("/user/hand/right/input/thumbstick") },
    };
    suggest("/interaction_profiles/oculus/touch_controller", touch, sizeof touch / sizeof touch[0]);
    XrActionSuggestedBinding index[] = {
        { a_grip, path("/user/hand/left/input/grip/pose") }, { a_grip, path("/user/hand/right/input/grip/pose") },
        { a_trigger, path("/user/hand/left/input/trigger/value") }, { a_trigger, path("/user/hand/right/input/trigger/value") },
        { a_squeeze, path("/user/hand/left/input/squeeze/value") }, { a_squeeze, path("/user/hand/right/input/squeeze/value") },
        { a_a, path("/user/hand/right/input/a/click") }, { a_b, path("/user/hand/right/input/b/click") },
        { a_x, path("/user/hand/left/input/a/click") }, { a_y, path("/user/hand/left/input/b/click") },
        { a_stick, path("/user/hand/left/input/thumbstick") }, { a_recenter, path("/user/hand/right/input/thumbstick/click") },
        { a_rstick, path("/user/hand/right/input/thumbstick") },
    };
    suggest("/interaction_profiles/valve/index_controller", index, sizeof index / sizeof index[0]);
    XrActionSuggestedBinding wmr[] = {
        { a_grip, path("/user/hand/left/input/grip/pose") }, { a_grip, path("/user/hand/right/input/grip/pose") },
        { a_trigger, path("/user/hand/left/input/trigger/value") }, { a_trigger, path("/user/hand/right/input/trigger/value") },
        { a_squeeze, path("/user/hand/left/input/squeeze/click") }, { a_squeeze, path("/user/hand/right/input/squeeze/click") },
        { a_a, path("/user/hand/right/input/trackpad/click") }, { a_menu, path("/user/hand/left/input/menu/click") },
        { a_b, path("/user/hand/right/input/menu/click") },
        { a_stick, path("/user/hand/left/input/thumbstick") }, { a_recenter, path("/user/hand/right/input/thumbstick/click") },
        { a_rstick, path("/user/hand/right/input/thumbstick") },
    };
    suggest("/interaction_profiles/microsoft/motion_controller", wmr, sizeof wmr / sizeof wmr[0]);
    XrActionSuggestedBinding simple[] = {
        { a_grip, path("/user/hand/left/input/grip/pose") }, { a_grip, path("/user/hand/right/input/grip/pose") },
        { a_trigger, path("/user/hand/left/input/select/click") }, { a_trigger, path("/user/hand/right/input/select/click") },
        { a_menu, path("/user/hand/left/input/menu/click") }, { a_recenter, path("/user/hand/right/input/menu/click") },
    };
    suggest("/interaction_profiles/khr/simple_controller", simple, sizeof simple / sizeof simple[0]);

    XrSessionActionSetsAttachInfo at; memset(&at, 0, sizeof at); at.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    at.countActionSets = 1; at.actionSets = &aset;
    if (XR_FAILED(xrAttachSessionActionSets(sess, &at))) return 0;
    for (int h = 0; h < 2; h++) {
        XrActionSpaceCreateInfo sp; memset(&sp, 0, sizeof sp); sp.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
        sp.action = a_grip; sp.subactionPath = hand_path[h]; sp.poseInActionSpace.orientation.w = 1;
        xrCreateActionSpace(sess, &sp, &grip_space[h]);
    }
    return 1;
}

/* ---- start / stop ---- */
/* the screen from the head position it was placed for and the settings now (so a setting can change without
   the screen turning to wherever the player happens to look) */
static void pose_from_anchor(void) {
    float yaw = atan2f(-anchor_fx, -anchor_fz);
    screen_pose.orientation.x = 0; screen_pose.orientation.y = sinf(yaw * 0.5f); screen_pose.orientation.z = 0; screen_pose.orientation.w = cosf(yaw * 0.5f);
    screen_pose.position.x = anchor_x + anchor_fx * cfg.screen_distance;
    screen_pose.position.y = anchor_y + cfg.screen_height_off;
    screen_pose.position.z = anchor_z + anchor_fz * cfg.screen_distance;
}
void vr_configure(const vr_settings *s) {
    if (lock_made) EnterCriticalSection(&lock);
    cfg = *s;
    if (cfg.screen_distance < 0.5f) cfg.screen_distance = 0.5f; if (cfg.screen_distance > 20) cfg.screen_distance = 20;
    if (cfg.screen_width < 0.5f) cfg.screen_width = 0.5f; if (cfg.screen_width > 40) cfg.screen_width = 40;
    if (cfg.screen_height_off < -3) cfg.screen_height_off = -3; if (cfg.screen_height_off > 3) cfg.screen_height_off = 3;
    if (cfg.steer_angle < 5) cfg.steer_angle = 5; if (cfg.steer_angle > 80) cfg.steer_angle = 80;
    if (cfg.twist_angle < 10) cfg.twist_angle = 10; if (cfg.twist_angle > 90) cfg.twist_angle = 90;
    if (have_screen) pose_from_anchor(); else want_recenter = 1;
    if (lock_made) LeaveCriticalSection(&lock);
}
int vr_settings_changed(vr_settings *s) {
    int r = 0;
    if (!lock_made) return 0;
    EnterCriticalSection(&lock);
    if (cfg_moved && cfg_idle >= 30) { cfg_moved = 0; if (s) *s = cfg; r = 1; }      /* a moment after the stick was let go */
    LeaveCriticalSection(&lock);
    return r;
}
static DWORD WINAPI vr_thread(void *arg);
int vr_active(void) { return sess != XR_NULL_HANDLE; }

int vr_init(const vr_settings *s, char *err, int errlen) {
    XrResult r;
    if (err && errlen) err[0] = 0;
    if (s) vr_configure(s);
    if (!load_openxr(err, errlen)) return 0;
    if (XR_FAILED(gipa(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&xrCreateInstance)) || !xrCreateInstance) {
        snprintf(err, (size_t)errlen, "OpenXR has no xrCreateInstance."); return 0; }
    const char *exts[] = { "XR_KHR_D3D11_enable" };
    XrInstanceCreateInfo ici; memset(&ici, 0, sizeof ici); ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
    snprintf(ici.applicationInfo.applicationName, sizeof ici.applicationInfo.applicationName, "Super Hang-On Ultimate");
    snprintf(ici.applicationInfo.engineName, sizeof ici.applicationInfo.engineName, "sho");
    ici.applicationInfo.applicationVersion = 1; ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ici.enabledExtensionCount = 1; ici.enabledExtensionNames = exts;
    if (XR_FAILED(r = xrCreateInstance(&ici, &inst))) { inst = XR_NULL_HANDLE; seterr(err, errlen, "Starting OpenXR", r); return 0; }
#define X(n) gipa(inst, #n, (PFN_xrVoidFunction *)&n); if (!n) { snprintf(err, (size_t)errlen, "The OpenXR runtime lacks %s.", #n); vr_shutdown(); return 0; }
    XR_FUNCS(X)
#undef X
    XrSystemGetInfo sgi; memset(&sgi, 0, sizeof sgi); sgi.type = XR_TYPE_SYSTEM_GET_INFO; sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (XR_FAILED(r = xrGetSystem(inst, &sgi, &sysid))) {
        seterr(err, errlen, "Finding the headset", r);
        if (r == XR_ERROR_FORM_FACTOR_UNAVAILABLE) snprintf(err, (size_t)errlen, "No headset was found. Connect the headset (Virtual Desktop, Quest Link or Steam Link running) and start the game again.");
        vr_shutdown(); return 0;
    }
    XrGraphicsRequirementsD3D11KHR req; memset(&req, 0, sizeof req); req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR;
    if (XR_FAILED(r = xrGetD3D11GraphicsRequirementsKHR(inst, sysid, &req))) { seterr(err, errlen, "Asking for the headset's graphics card", r); vr_shutdown(); return 0; }
    if (!make_device(req.adapterLuid, req.minFeatureLevel)) { snprintf(err, (size_t)errlen, "Direct3D 11 could not be started on the headset's graphics card."); vr_shutdown(); return 0; }

    XrGraphicsBindingD3D11KHR gb; memset(&gb, 0, sizeof gb); gb.type = XR_TYPE_GRAPHICS_BINDING_D3D11_KHR; gb.device = dev;
    XrSessionCreateInfo sci; memset(&sci, 0, sizeof sci); sci.type = XR_TYPE_SESSION_CREATE_INFO; sci.next = &gb; sci.systemId = sysid;
    if (XR_FAILED(r = xrCreateSession(inst, &sci, &sess))) { sess = XR_NULL_HANDLE; seterr(err, errlen, "Starting the headset session", r); vr_shutdown(); return 0; }

    { uint32_t n = 0; XrEnvironmentBlendMode modes[8];
      if (XR_SUCCEEDED(xrEnumerateEnvironmentBlendModes(inst, sysid, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &n, NULL)) && n && n <= 8 &&
          XR_SUCCEEDED(xrEnumerateEnvironmentBlendModes(inst, sysid, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, n, &n, modes)) && n) blend = modes[0]; }

    XrReferenceSpaceCreateInfo rci; memset(&rci, 0, sizeof rci); rci.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    rci.poseInReferenceSpace.orientation.w = 1;
    rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (XR_FAILED(r = xrCreateReferenceSpace(sess, &rci, &local_space))) { seterr(err, errlen, "Making the play space", r); vr_shutdown(); return 0; }
    rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    xrCreateReferenceSpace(sess, &rci, &view_space);

    /* the picture's swapchain: 8-bit sRGB, BGRA if the runtime has it (the game's own byte order) */
    int64_t fmts[512]; uint32_t nf = 0; int64_t fmt = 0;     /* (asked twice: a list that does not fit comes back empty) */
    if (XR_FAILED(xrEnumerateSwapchainFormats(sess, 0, &nf, NULL)) || nf > 512 || XR_FAILED(xrEnumerateSwapchainFormats(sess, nf, &nf, fmts))) nf = 0;
    static const int64_t want[4] = { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM };
    for (int w = 0; w < 4 && !fmt; w++) for (uint32_t i = 0; i < nf; i++) if (fmts[i] == want[w]) { fmt = want[w]; break; }
    if (!fmt) { snprintf(err, (size_t)errlen, "The headset offers no 8-bit colour picture format."); vr_shutdown(); return 0; }
    chain_rgba = fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || fmt == DXGI_FORMAT_R8G8B8A8_UNORM;
    XrSwapchainCreateInfo sc; memset(&sc, 0, sizeof sc); sc.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    sc.format = fmt; sc.sampleCount = 1; sc.width = TEX_W; sc.height = TEX_H; sc.faceCount = 1; sc.arraySize = 1; sc.mipCount = 1;
    if (XR_FAILED(r = xrCreateSwapchain(sess, &sc, &chain))) { chain = XR_NULL_HANDLE; seterr(err, errlen, "Making the headset picture", r); vr_shutdown(); return 0; }
    XrSwapchainImageD3D11KHR imgs[16]; memset(imgs, 0, sizeof imgs);
    for (int i = 0; i < 16; i++) imgs[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
    if (XR_FAILED(r = xrEnumerateSwapchainImages(chain, 0, &chain_n, NULL)) || chain_n > 16 ||
        XR_FAILED(r = xrEnumerateSwapchainImages(chain, chain_n, &chain_n, (XrSwapchainImageBaseHeader *)imgs))) { seterr(err, errlen, "Getting the headset picture", r); vr_shutdown(); return 0; }
    for (uint32_t i = 0; i < chain_n; i++) chain_tex[i] = imgs[i].texture;

    if (!make_actions()) { snprintf(err, (size_t)errlen, "The hand controllers could not be set up."); vr_shutdown(); return 0; }
    sho_diag("VR: session made (picture format %d, %u images, %s)", (int)fmt, (unsigned)chain_n, chain_rgba ? "RGBA" : "BGRA");
    if (!lock_made) { InitializeCriticalSection(&lock); lock_made = 1; }
    stop_thread = 0; quit_flag = 0;
    thread = CreateThread(NULL, 0, vr_thread, NULL, 0, NULL);
    if (!thread) { snprintf(err, (size_t)errlen, "The headset thread could not be started."); vr_shutdown(); return 0; }
    return 1;
}

void vr_shutdown(void) {
    if (thread) {      /* the headset thread first: it owns the frame loop */
        InterlockedExchange(&stop_thread, 1);
        if (WaitForSingleObject(thread, 3000) == WAIT_TIMEOUT) TerminateThread(thread, 0);   /* (stuck inside the runtime) */
        CloseHandle(thread); thread = NULL;
    }
    if (sess && running && xrEndSession) xrEndSession(sess);
    running = 0; frame_begun = 0;
    if (chain && xrDestroySwapchain) xrDestroySwapchain(chain);
    chain = XR_NULL_HANDLE;
    if (sess && xrDestroySession) xrDestroySession(sess);
    sess = XR_NULL_HANDLE;
    if (inst && xrDestroyInstance) xrDestroyInstance(inst);
    inst = XR_NULL_HANDLE;
    if (ctx) { ID3D11DeviceContext_Release(ctx); ctx = NULL; }
    if (dev) { ID3D11Device_Release(dev); dev = NULL; }
    /* (the runtime's DLL stays loaded: some runtimes do not survive being unloaded) */
}

/* ---- frames ---- */
static void poll_events(int *quit) {
    XrEventDataBuffer ev;
    for (;;) {
        memset(&ev, 0, sizeof ev); ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        if (xrPollEvent(inst, &ev) != XR_SUCCESS) break;
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *s = (const XrEventDataSessionStateChanged *)&ev;
            if (s->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi; memset(&bi, 0, sizeof bi); bi.type = XR_TYPE_SESSION_BEGIN_INFO;
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(xrBeginSession(sess, &bi))) { running = 1; want_recenter = 1; }
            } else if (s->state == XR_SESSION_STATE_STOPPING) { xrEndSession(sess); running = 0; }
            else if (s->state == XR_SESSION_STATE_EXITING || s->state == XR_SESSION_STATE_LOSS_PENDING) { running = 0; if (quit) *quit = 1; }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) { running = 0; if (quit) *quit = 1; }
    }
}

static void gather(sho_input *in, int menu);
/* one headset frame's start: 1 = begun (frame_end must follow) */
static int frame_begin(void) {
    XrFrameState fs; memset(&fs, 0, sizeof fs); fs.type = XR_TYPE_FRAME_STATE;
    if (XR_FAILED(xrWaitFrame(sess, NULL, &fs))) return 0;
    if (XR_FAILED(xrBeginFrame(sess, NULL))) return 0;
    frame_begun = 1; should_render = fs.shouldRender != 0; disp_time = fs.predictedDisplayTime; disp_period = fs.predictedDisplayPeriod;
    XrActiveActionSet as; memset(&as, 0, sizeof as); as.actionSet = aset;
    XrActionsSyncInfo si; memset(&si, 0, sizeof si); si.type = XR_TYPE_ACTIONS_SYNC_INFO; si.countActiveActionSets = 1; si.activeActionSets = &as;
    xrSyncActions(sess, &si);
    return 1;
}

/* the screen goes in front of where the head points now (level, upright), at the head's height */
static void place_screen(const XrSpaceLocation *hl) {      /* hl: where the head is */
    XrSpaceLocation loc = *hl;
    if (!(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) || !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) return;
    XrQuaternionf q = loc.pose.orientation;
    float fx = -2.0f * (q.x * q.z + q.w * q.y), fz = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));   /* the head's forward (-Z) */
    float len = sqrtf(fx * fx + fz * fz); if (len < 0.01f) { fx = 0; fz = -1; len = 1; }
    fx /= len; fz /= len;
    anchor_x = loc.pose.position.x; anchor_y = loc.pose.position.y; anchor_z = loc.pose.position.z; anchor_fx = fx; anchor_fz = fz;
    pose_from_anchor();
    want_recenter = 0; have_screen = 1;
}
void vr_recenter(void) { want_recenter = 1; }      /* (taken up by the headset thread at its next frame) */

/* the frame's end: the newest picture onto the screen in the room */
static void frame_end(void) {
    static uint32_t tpic[2 * SHO_MAX_W * SHO_3D_SUB * SHO_H]; static int fw, eye_w, depth, seq = -1, big_seq = -2; static double aspect;
    const uint32_t *fb = tpic;
    if (!frame_begun) return;
    frame_begun = 0;
    EnterCriticalSection(&lock); int rc = want_recenter; LeaveCriticalSection(&lock);
    XrSpaceLocation hloc; memset(&hloc, 0, sizeof hloc); hloc.type = XR_TYPE_SPACE_LOCATION;
    if (rc && view_space && XR_FAILED(xrLocateSpace(view_space, local_space, disp_time, &hloc))) hloc.locationFlags = 0;   /* (the runtime call outside the lock) */
    EnterCriticalSection(&lock);
    if (rc) place_screen(&hloc);
    if (pic_seq != seq && pic_fw > 0) { memcpy(tpic, pic, (size_t)pic_fw * SHO_H * 4); fw = pic_fw; eye_w = pic_eye_w; depth = pic_depth; aspect = pic_aspect; seq = pic_seq; }
    XrPosef pose = screen_pose; float scr_w = cfg.screen_width, afx = anchor_fx, afz = anchor_fz;
    LeaveCriticalSection(&lock);
    if (seq < 0) fb = NULL;
    XrFrameEndInfo fe; memset(&fe, 0, sizeof fe); fe.type = XR_TYPE_FRAME_END_INFO; fe.displayTime = disp_time; fe.environmentBlendMode = blend;
    XrCompositionLayerQuad quad[2]; const XrCompositionLayerBaseHeader *layers[2];
    int stereo = fw >= 2 * eye_w, nl = 0;
    int ec = stereo ? fw / 2 : fw;                       /* columns of one eye's picture as it comes */
    int tw = eye_w * VR_SCALE_X, hx = ec > 0 ? tw / ec : 0;   /* ... and in the headset picture; each column hx times */
    if (should_render && have_screen && fb && eye_w > 0 && eye_w <= SHO_MAX_W && fw <= 2 * SHO_MAX_W * SHO_3D_SUB && hx >= 1 && hx * ec == tw) {
        uint32_t idx = 0; XrSwapchainImageWaitInfo wi; memset(&wi, 0, sizeof wi); wi.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO; wi.timeout = XR_INFINITE_DURATION;
        if (XR_SUCCEEDED(xrAcquireSwapchainImage(chain, NULL, &idx)) && XR_SUCCEEDED(xrWaitSwapchainImage(chain, &wi)) && idx < chain_n) {
            /* every game pixel VR_SCALE_X x VR_SCALE_Y (the 3D picture already has its columns), opaque, in the swapchain's byte order */
            static uint32_t big[TEX_W * TEX_H];
            int bw = fw * hx, bh = SHO_H * VR_SCALE_Y;
            if (big_seq != seq) for (int y = 0; y < SHO_H; y++) {      /* (the same picture as last time is already made) */
                const uint32_t *s = fb + y * fw; uint32_t *d = big + (size_t)y * VR_SCALE_Y * bw;
                for (int x = 0; x < fw; x++) {
                    uint32_t c = s[x] | 0xff000000u;
                    if (chain_rgba) c = (c & 0xff00ff00u) | ((c >> 16) & 0xff) | ((c & 0xff) << 16);
                    for (int k = 0; k < hx; k++) *d++ = c;
                }
                for (int k = 1; k < VR_SCALE_Y; k++) memcpy(big + ((size_t)y * VR_SCALE_Y + k) * bw, big + (size_t)y * VR_SCALE_Y * bw, (size_t)bw * 4);
            }
            big_seq = seq;
            D3D11_BOX box = { 0, 0, 0, (UINT)bw, (UINT)bh, 1 };
            static int img_seq[16] = { -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3, -3 };
            if (img_seq[idx] != seq) {      /* (this image may hold this very picture already: the headset is faster than the game) */
                ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)chain_tex[idx], 0, &box, big, (UINT)bw * 4, 0);
                img_seq[idx] = seq;
            }
            xrReleaseSwapchainImage(chain, NULL);
            float w = scr_w, h = aspect > 0 ? (float)(scr_w / aspect) : scr_w * 9.0f / 16.0f;
            for (int e = 0; e < (stereo ? 2 : 1); e++) {
                XrCompositionLayerQuad *q = &quad[e]; memset(q, 0, sizeof *q); q->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
                q->space = local_space; q->eyeVisibility = stereo ? (e ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT) : XR_EYE_VISIBILITY_BOTH;
                q->subImage.swapchain = chain; q->subImage.imageArrayIndex = 0;
                q->subImage.imageRect.offset.x = e * tw; q->subImage.imageRect.offset.y = 0;
                q->subImage.imageRect.extent.width = tw; q->subImage.imageRect.extent.height = bh;
                q->pose = pose; q->size.width = w; q->size.height = h;
                if (stereo) {
                    /* The backdrop's two pictures are `depth` game pixels apart. The eyes are about 63 mm apart: at
                       that separation a point is infinitely far, and wider would turn the eyes outwards. So when
                       the depth asks for more, each eye's screen slides inwards by half the excess: the backdrop
                       lands at infinity and everything else comes that much nearer. */
                    float excess = (float)depth * w / (float)eye_w - 0.063f;
                    if (excess > 0) {
                        float sh = (e ? -0.5f : 0.5f) * excess;      /* left eye's screen to the right, right eye's to the left */
                        q->pose.position.x += -afz * sh; q->pose.position.z += afx * sh;   /* along the screen's width */
                    }
                }
                layers[nl++] = (const XrCompositionLayerBaseHeader *)q;
            }
        }
    }
    fe.layerCount = (uint32_t)nl; fe.layers = nl ? layers : NULL;
    xrEndFrame(sess, &fe);
}

/* The headset's thread: frames at the headset's pace for as long as it wants them, each with the newest picture
   and a fresh reading of the controllers. When the headset is not showing the game (taken off, the runtime's
   own menu up) the runtime slows or stops these frames - which is why they are not the game's clock. */
static DWORD WINAPI vr_thread(void *arg) {
    (void)arg;
    while (!stop_thread) {
        int quit = 0;
        poll_events(&quit);
        if (quit) { EnterCriticalSection(&lock); quit_flag = 1; LeaveCriticalSection(&lock); }
        if (!running) { Sleep(20); continue; }
        if (!frame_begin()) { Sleep(5); continue; }
        LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
        sho_input in; memset(&in, 0, sizeof in);
        EnterCriticalSection(&lock);
        tc = cfg; t_afx = anchor_fx; t_afz = anchor_fz; int menu = menu_flag;
        LeaveCriticalSection(&lock);
        t_dheight = 0; t_y_edge = t_menu_edge = t_recenter = 0;
        gather(&in, menu);
        EnterCriticalSection(&lock);
        pad_snap = in;
        if (t_y_edge) y_edge = 1; if (t_menu_edge) menu_edge = 1; if (t_recenter) want_recenter = 1;
        if (t_dheight != 0 && have_screen) {      /* the right stick moved the screen */
            cfg.screen_height_off += t_dheight;
            if (cfg.screen_height_off < -3) cfg.screen_height_off = -3; if (cfg.screen_height_off > 3) cfg.screen_height_off = 3;
            pose_from_anchor(); cfg_moved = 1; cfg_idle = 0;
        } else if (cfg_idle < 1000) cfg_idle++;
        LeaveCriticalSection(&lock);
        frame_end();
        QueryPerformanceCounter(&t1);
        if (!st_freq.QuadPart) QueryPerformanceFrequency(&st_freq);
        EnterCriticalSection(&lock); st_frames += 1; st_ms += (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)st_freq.QuadPart; LeaveCriticalSection(&lock);
    }
    return 0;
}
/* since the last call: headset frames a second made, and the milliseconds each took to make (without the wait) */
void vr_stats(double seconds, double *fps, double *ms) {
    if (!lock_made) { *fps = *ms = 0; return; }
    EnterCriticalSection(&lock);
    *fps = seconds > 0 ? st_frames / seconds : 0; *ms = st_frames > 0 ? st_ms / st_frames : 0; st_frames = st_ms = 0;
    LeaveCriticalSection(&lock);
}
int vr_poll(void) {
    if (!lock_made) return 0;
    EnterCriticalSection(&lock); int q = quit_flag; LeaveCriticalSection(&lock);
    return q;
}
void vr_present(const uint32_t *fb, int fw, int eye_w, double aspect, int depth) {
    if (!thread || !fb || fw <= 0 || fw > 2 * SHO_MAX_W * SHO_3D_SUB) return;
    EnterCriticalSection(&lock);
    memcpy(pic, fb, (size_t)fw * SHO_H * 4);
    pic_fw = fw; pic_eye_w = eye_w; pic_aspect = aspect; pic_depth = depth; pic_seq = (pic_seq + 1) & 0x3fffffff;
    LeaveCriticalSection(&lock);
}


/* ---- the handlebars ---- */
static float get_float(XrAction a, int hand) {
    XrActionStateGetInfo gi; memset(&gi, 0, sizeof gi); gi.type = XR_TYPE_ACTION_STATE_GET_INFO; gi.action = a; gi.subactionPath = hand_path[hand];
    XrActionStateFloat st; memset(&st, 0, sizeof st); st.type = XR_TYPE_ACTION_STATE_FLOAT;
    return XR_SUCCEEDED(xrGetActionStateFloat(sess, &gi, &st)) && st.isActive ? st.currentState : 0.0f;
}
static int get_bool(XrAction a) {
    XrActionStateGetInfo gi; memset(&gi, 0, sizeof gi); gi.type = XR_TYPE_ACTION_STATE_GET_INFO; gi.action = a;
    XrActionStateBoolean st; memset(&st, 0, sizeof st); st.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    return XR_SUCCEEDED(xrGetActionStateBoolean(sess, &gi, &st)) && st.isActive && st.currentState;
}
int vr_menu_pressed(void) { if (!lock_made) return 0; EnterCriticalSection(&lock); int e = menu_edge; menu_edge = 0; LeaveCriticalSection(&lock); return e; }
int vr_y_pressed(void) { if (!lock_made) return 0; EnterCriticalSection(&lock); int e = y_edge; y_edge = 0; LeaveCriticalSection(&lock); return e; }

/* the controllers (as the headset thread last read them) added to the game's input */
void vr_input(sho_input *in, int menu) {
    if (!thread || !running) return;
    EnterCriticalSection(&lock);
    sho_input p = pad_snap; menu_flag = menu;
    LeaveCriticalSection(&lock);
    if (fabsf(p.steer) > fabsf(in->steer)) in->steer = p.steer;   /* (a pad or the keyboard still steers when it is pushed further) */
    if (p.throttle > in->throttle) in->throttle = p.throttle;
    if (p.brake > in->brake) in->brake = p.brake;
    in->turbo |= p.turbo; in->pad_a |= p.pad_a; in->pad_b |= p.pad_b; in->coin |= p.coin; in->start |= p.start;
    in->nav_up |= p.nav_up; in->nav_down |= p.nav_down; in->nav_left |= p.nav_left; in->nav_right |= p.nav_right;
}
/* reads the controllers into *in (zeroed by the caller), on the headset thread. No lock is held: these are calls
   into the VR runtime and can take a while. It works from the thread's own copy of the settings (tc, t_afx/z)
   and leaves what it found for the others in t_*; vr_thread hands those over. */
static void gather(sho_input *in, int menu) {
    /* the bar between the hands: its tilt is the lean */
    XrSpaceLocation l[2]; int ok = 1;
    for (int h = 0; h < 2; h++) {
        memset(&l[h], 0, sizeof l[h]); l[h].type = XR_TYPE_SPACE_LOCATION;
        if (!grip_space[h] || XR_FAILED(xrLocateSpace(grip_space[h], local_space, disp_time, &l[h])) ||
            !(l[h].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) ok = 0;
    }
    if (ok && !tc.steer_stick) {
        float dx = l[1].pose.position.x - l[0].pose.position.x, dy = l[1].pose.position.y - l[0].pose.position.y, dz = l[1].pose.position.z - l[0].pose.position.z;
        float flat = sqrtf(dx * dx + dz * dz);
        if (flat > 0.08f) {                                  /* hands apart, as on a bar (not one above the other) */
            float deg = atan2f(-dy, flat) * 57.29578f;       /* right hand lower = lean right */
            float dead = 1.5f, s = deg > dead ? (deg - dead) : deg < -dead ? (deg + dead) : 0.0f;
            s /= (tc.steer_angle - dead);
            if (s > 1) s = 1; if (s < -1) s = -1;
            if (fabsf(s) > fabsf(in->steer)) in->steer = s;   /* (a pad or the keyboard still steers when it is pushed further) */
        }
    }
    float thr = get_float(a_trigger, tc.swap_triggers ? 0 : 1), brk = get_float(a_trigger, tc.swap_triggers ? 1 : 0);
    float sel = thr;                                         /* (the trigger that selects on the menus) */
    if (tc.twist_throttle) {
        /* Twist grip: while the right grip button is held, that hand is on the throttle. How far the wrist has
           rolled back since the button went down - the turn about the handlebar's own line, left hand to right
           hand - is the throttle; letting go of the button shuts it. Either trigger is the brake lever. */
        float tl = get_float(a_trigger, 0), tr = get_float(a_trigger, 1);
        brk = tl > tr ? tl : tr; sel = tr; thr = 0;
        int held = get_float(a_squeeze, 1) > 0.5f && (l[1].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) && !menu;
        if (!held) twist_on = 0;
        else {
            XrQuaternionf q = l[1].pose.orientation;
            if (!twist_on) { twist_ref = q; twist_on = 1; }
            /* the turn from then to now: q x conj(ref) */
            XrQuaternionf r = twist_ref, d;
            d.w = q.w * r.w + q.x * r.x + q.y * r.y + q.z * r.z;
            d.x = -q.w * r.x + q.x * r.w - q.y * r.z + q.z * r.y;
            d.y = -q.w * r.y + q.x * r.z + q.y * r.w - q.z * r.x;
            d.z = -q.w * r.z - q.x * r.y + q.y * r.x + q.z * r.w;
            /* the bar's line: towards the player's right (from the hands when both are seen, else the screen's width) */
            float ax = -t_afz, az = t_afx;
            if (ok) { float bx = l[1].pose.position.x - l[0].pose.position.x, bz = l[1].pose.position.z - l[0].pose.position.z, n = sqrtf(bx * bx + bz * bz);
                      if (n > 0.08f) { ax = bx / n; az = bz / n; } }
            /* the part of the turn that is about that line; rolling the top of the hand back towards the player is positive */
            float deg = 2.0f * atan2f(d.x * ax + d.z * az, d.w) * 57.29578f;
            if (deg > 180) deg -= 360; if (deg < -180) deg += 360;
            float dead = 3.0f; thr = deg > dead ? (deg - dead) / (tc.twist_angle - dead) : 0.0f;
            if (thr > 1) thr = 1;
        }
    }
    if (thr > in->throttle) in->throttle = thr;
    if (brk > in->brake) in->brake = brk;
    if (get_float(a_squeeze, 0) > 0.6f || (!tc.twist_throttle && get_float(a_squeeze, 1) > 0.6f)) in->turbo = 1;   /* (twist: the right grip is the throttle hand, turbo on the left) */
    if (get_bool(a_a)) in->pad_a = 1;
    if (get_bool(a_b)) in->pad_b = 1;
    if (get_bool(a_x)) in->coin = 1;
    int m = get_bool(a_menu);
    int yb = get_bool(a_y);                 /* Y: the settings menu (the frontend opens it) */
    if (yb && !y_prev) t_y_edge = 1;
    y_prev = yb;
    if (m) in->start = 1;
    if (m && !menu_prev) t_menu_edge = 1;
    menu_prev = m;
    {   XrActionStateGetInfo gi; memset(&gi, 0, sizeof gi); gi.type = XR_TYPE_ACTION_STATE_GET_INFO; gi.action = a_stick;
        XrActionStateVector2f st; memset(&st, 0, sizeof st); st.type = XR_TYPE_ACTION_STATE_VECTOR2F;
        if (XR_SUCCEEDED(xrGetActionStateVector2f(sess, &gi, &st)) && st.isActive) {
            if (st.currentState.y > 0.6f) in->nav_up = 1; if (st.currentState.y < -0.6f) in->nav_down = 1;
            if (st.currentState.x < -0.6f) in->nav_left = 1; if (st.currentState.x > 0.6f) in->nav_right = 1;
            if (tc.steer_stick && !menu) {      /* thumbstick steering */
                float x = st.currentState.x, dead = 0.12f, s = x > dead ? (x - dead) / (1 - dead) : x < -dead ? (x + dead) / (1 - dead) : 0.0f;
                if (fabsf(s) > fabsf(in->steer)) in->steer = s;
            }
        }
    }
    {   /* right stick up / down: the screen moves up / down (30 cm a second), kept for next time */
        XrActionStateGetInfo gi; memset(&gi, 0, sizeof gi); gi.type = XR_TYPE_ACTION_STATE_GET_INFO; gi.action = a_rstick;
        XrActionStateVector2f st; memset(&st, 0, sizeof st); st.type = XR_TYPE_ACTION_STATE_VECTOR2F;
        float y = XR_SUCCEEDED(xrGetActionStateVector2f(sess, &gi, &st)) && st.isActive ? st.currentState.y : 0.0f;
        if (fabsf(y) > 0.5f && fabsf(st.currentState.x) < 0.5f)
            t_dheight = (y > 0 ? 0.30f : -0.30f) * (disp_period > 0 ? (float)disp_period * 1e-9f : 0.011f);
    }
    if (menu && sel > 0.6f) in->pad_a = 1;                   /* the trigger also selects on the menus */
    int rc = get_bool(a_recenter);
    if (rc && !recenter_prev) t_recenter = 1;
    recenter_prev = rc;
}
