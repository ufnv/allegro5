/*
 *    ICBM extension, included by src/win/d3d_disp.cpp: HDR output.
 *
 *    Direct3D 9 cannot present HDR, so with HDR on the display renders into
 *    FP16 (D3DFMT_A16B16G16R16F) render targets shared with Direct3D 11, and
 *    a D3D11 flip-model swap chain on the same window presents them in scRGB
 *    or HDR10. The game draws exactly as before: everything it draws is sRGB
 *    as today, and whatever additive blending or colours push above 1.0 is
 *    extra light on top of SDR white.
 *
 *       0..1    sRGB, shown at the Windows "SDR content brightness"
 *       1 + x   SDR white + x times SDR white of extra light
 *
 *    so a value of max_nits / sdr_white_nits reaches the display's peak.
 *
 *    - al_set_new_display_hdr() before al_create_display() requests it.
 *      ALLEGRO_HDR_AUTO means scRGB if Windows HDR is on for any monitor
 *      (the window may start on an SDR monitor and be moved to the HDR one;
 *      the output is SDR there), otherwise nothing changes (plain Direct3D 9).
 *      Sharing surfaces needs a Direct3D 9Ex device: the IDirect3D9 object is
 *      replaced by an IDirect3D9Ex one if no display exists yet. D3D9Ex has
 *      no D3DPOOL_MANAGED, so Allegro's managed buffers/textures use
 *      _al_d3d_icbm_managed_pool() (D3DPOOL_DEFAULT on 9Ex; 9Ex keeps
 *      default-pool resources over Reset).
 *    - Only windowed / ALLEGRO_FULLSCREEN_WINDOW displays without
 *      multisampling. If the setup fails (no 9Ex, no D3D11) the display stays
 *      on the normal backbuffer and D3D9 Present for good.
 *    - Once set up, the HDR path stays for the display's life: after a
 *      flip-model swap chain has presented to a window, D3D9 Present no
 *      longer shows there. When Windows HDR is off for the monitor showing
 *      the window (switched off, window moved; WM_DISPLAYCHANGE and window
 *      moves trigger a check, a slow poll catches the rest) or
 *      al_set_display_hdr_enabled(false), the output is SDR instead: white
 *      is scRGB 1.0 and nothing goes above it, so the levels report no
 *      headroom. The next flip switches.
 *    - Three render targets rotate, so that neither device waits for the
 *      other: D3D9 draws frame N while D3D11 presents frame N-1. This adds one
 *      frame of latency; if no flip follows within 50 ms, the display thread
 *      presents the waiting frame (icbm_hdr_idle), so a screen drawn once
 *      before a long load still appears.
 *    - Locking or drawing from the backbuffer bitmap is not supported while
 *      HDR is active (the surface is FP16, the bitmap says otherwise).
 */

/* With the DirectX SDK (June 2010) on the include path its d3d11.h is found
 * first; it redefines a few error codes winerror.h already has (same values).
 */
#ifdef _MSC_VER
   #pragma warning(push)
   #pragma warning(disable: 4005)
#endif
#include <dxgi1_6.h>
#include <d3d11.h>
#ifdef _MSC_VER
   #pragma warning(pop)
#endif

#define ICBM_HDR_BUFFERS 3
#define ICBM_HDR_IDLE_PRESENT 0.05  /* seconds without a flip */
#define ICBM_HDR_RECHECK      1.0   /* check again this long after a change notification */
#define ICBM_HDR_CHECK_PERIOD 5.0   /* safety check without notification (SDR brightness slider) */

typedef HRESULT (WINAPI *ICBM_D3D11_CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE,
   HMODULE, UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
   D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *ICBM_D3D_COMPILE)(LPCVOID, SIZE_T, LPCSTR,
   const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT,
   ID3DBlob **, ID3DBlob **);
typedef HRESULT (WINAPI *ICBM_CREATE_DXGI_FACTORY1)(REFIID, void **);
typedef HRESULT (WINAPI *ICBM_DIRECT3D_CREATE9EX)(UINT, IDirect3D9Ex **);

typedef struct ICBM_HDR_CONSTANTS
{
   float sdr_white;  /* nits */
   float max_nits;
   float knee;       /* nits where the soft roll-off to max_nits starts */
   float hdr10;      /* > 0: PQ / Rec.2020 output */
} ICBM_HDR_CONSTANTS;

/* What the monitor showing the window can do (icbm_hdr_query_monitor). */
typedef struct ICBM_HDR_MONITOR
{
   bool hdr;         /* Windows HDR is on for it */
   float sdr_white;  /* nits */
   float max_nits;
} ICBM_HDR_MONITOR;

typedef struct ICBM_HDR
{
   int mode;          /* ALLEGRO_HDR_SCRGB or ALLEGRO_HDR_HDR10 */
   bool failed;       /* setup or output failed: SDR for good */
   bool ready;        /* D3D11 device, shaders etc. exist */
   bool active;       /* rendering into our targets, presenting through D3D11 */
   bool shown;        /* the output is HDR now (else SDR: white = scRGB 1.0, clipped) */
   volatile bool enabled;  /* al_set_display_hdr_enabled() */
   UINT sc_flags;     /* DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING if supported */

   /* Monitor showing the window, written by the display thread. */
   volatile bool output_hdr;
   ICBM_HDR_MONITOR output;
   double last_check;
   double recheck_at;           /* one more check after a change notification, 0 = none */
   volatile bool check_requested;  /* WM_DISPLAYCHANGE, window moved etc. */

   /* al_set_display_hdr_levels(), 0 = from Windows */
   float user_sdr_white, user_max_nits;

   /* D3D9 side */
   IDirect3DSurface9 *real_bb;   /* Allegro's backbuffer while ours is in use */
   IDirect3DSurface9 *rt[ICBM_HDR_BUFFERS];
   HANDLE share[ICBM_HDR_BUFFERS];
   IDirect3DQuery9 *q9[ICBM_HDR_BUFFERS];
   int w, h;
   int write_idx;     /* target D3D9 draws into */
   int pending_idx;   /* finished frame not yet presented, -1 if none */
   double last_flip;  /* al_get_time() of the last flip */
   const char *present_error; /* set by a failed idle present, handled on the next flip */

   /* D3D11 side */
   HMODULE d3d11_module, compiler_module;
   ID3D11Device *dev;
   ID3D11DeviceContext *ctx;
   IDXGISwapChain1 *sc;
   ID3D11RenderTargetView *rtv;
   int sc_w, sc_h;
   ID3D11Texture2D *tex[ICBM_HDR_BUFFERS];
   ID3D11ShaderResourceView *srv[ICBM_HDR_BUFFERS];
   HANDLE opened[ICBM_HDR_BUFFERS];
   ID3D11Query *q11[ICBM_HDR_BUFFERS];
   bool q11_pending[ICBM_HDR_BUFFERS];
   ID3D11VertexShader *vs;
   ID3D11PixelShader *ps;
   ID3D11Buffer *cb;

   ICBM_HDR_CONSTANTS levels;
   bool levels_dirty;
} ICBM_HDR;

static int icbm_hdr_new_mode = ALLEGRO_HDR_OFF;   /* al_set_new_display_hdr() */
static int icbm_hdr_create_mode = ALLEGRO_HDR_OFF; /* for the display being created */
static bool icbm_d3d9ex = false;   /* _al_d3d is an IDirect3D9Ex */

#define ICBM_RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)


/* ---- monitors --------------------------------------------------------- */

/* Windows' "SDR content brightness" for a monitor (GDI name). */
static float icbm_hdr_sdr_white(const WCHAR *gdi_name)
{
   UINT32 npaths = 0, nmodes = 0;
   float white = 80.0f * 2.5f;   /* Windows' default: 200 nits */

   if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &npaths, &nmodes) != ERROR_SUCCESS)
      return white;
   DISPLAYCONFIG_PATH_INFO *paths = (DISPLAYCONFIG_PATH_INFO *)al_calloc(npaths, sizeof *paths);
   DISPLAYCONFIG_MODE_INFO *modes = (DISPLAYCONFIG_MODE_INFO *)al_calloc(nmodes, sizeof *modes);
   if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &npaths, paths, &nmodes, modes, NULL) == ERROR_SUCCESS) {
      for (UINT32 i = 0; i < npaths; i++) {
         DISPLAYCONFIG_SOURCE_DEVICE_NAME source;
         source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
         source.header.size = sizeof source;
         source.header.adapterId = paths[i].sourceInfo.adapterId;
         source.header.id = paths[i].sourceInfo.id;
         if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
             wcscmp(source.viewGdiDeviceName, gdi_name) != 0)
            continue;

         DISPLAYCONFIG_SDR_WHITE_LEVEL level;
         level.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
         level.header.size = sizeof level;
         level.header.adapterId = paths[i].targetInfo.adapterId;
         level.header.id = paths[i].targetInfo.id;
         if (DisplayConfigGetDeviceInfo(&level.header) == ERROR_SUCCESS)
            white = level.SDRWhiteLevel / 1000.0f * 80.0f;
         break;
      }
   }
   al_free(paths);
   al_free(modes);
   return white;
}

static IDXGIFactory1 *icbm_hdr_create_factory(void)
{
   static HMODULE dxgi = NULL;
   ICBM_CREATE_DXGI_FACTORY1 create_factory;
   IDXGIFactory1 *factory = NULL;

   if (!dxgi)
      dxgi = LoadLibraryA("dxgi.dll");
   create_factory = dxgi ? (ICBM_CREATE_DXGI_FACTORY1)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;
   if (!create_factory || FAILED(create_factory(__uuidof(IDXGIFactory1), (void **)&factory)))
      return NULL;
   return factory;
}

/* Is Windows HDR on for this monitor, and its levels. A new DXGI factory
 * each time: outputs of an older one keep their old colour space.
 */
static bool icbm_hdr_query_monitor(HMONITOR monitor, ICBM_HDR_MONITOR *out)
{
   IDXGIFactory1 *factory = icbm_hdr_create_factory();
   IDXGIAdapter1 *adapter = NULL;
   bool found = false;

   out->hdr = false;
   out->sdr_white = 0.0f;
   out->max_nits = 0.0f;
   if (!factory || !monitor) {
      ICBM_RELEASE(factory);
      return false;
   }

   for (UINT a = 0; !found && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; a++) {
      IDXGIOutput *output = NULL;
      for (UINT o = 0; !found && adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; o++) {
         IDXGIOutput6 *output6 = NULL;
         DXGI_OUTPUT_DESC1 desc;
         if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), (void **)&output6)) &&
             SUCCEEDED(output6->GetDesc1(&desc)) && desc.Monitor == monitor) {
            found = true;
            out->hdr = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            out->sdr_white = icbm_hdr_sdr_white(desc.DeviceName);
            /* Monitors often report nonsense here (seen: 6095 and 128000
             * nits on the same one); take 1000 nits unless it is plausible.
             * The game can override it with al_set_display_hdr_levels().
             */
            out->max_nits = desc.MaxLuminance;
            if (out->max_nits <= out->sdr_white || out->max_nits > 4000.0f)
               out->max_nits = 1000.0f;
         }
         ICBM_RELEASE(output6);
         ICBM_RELEASE(output);
      }
      ICBM_RELEASE(adapter);
   }
   factory->Release();
   return found;
}

/* Is Windows HDR on for any monitor of the desktop. */
static bool icbm_hdr_any_monitor_hdr(void)
{
   IDXGIFactory1 *factory = icbm_hdr_create_factory();
   IDXGIAdapter1 *adapter = NULL;
   bool found = false;

   if (!factory)
      return false;

   for (UINT a = 0; !found && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; a++) {
      IDXGIOutput *output = NULL;
      for (UINT o = 0; !found && adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; o++) {
         IDXGIOutput6 *output6 = NULL;
         DXGI_OUTPUT_DESC1 desc;
         if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), (void **)&output6)) &&
             SUCCEEDED(output6->GetDesc1(&desc)) && desc.AttachedToDesktop &&
             desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
            found = true;
         ICBM_RELEASE(output6);
         ICBM_RELEASE(output);
      }
      ICBM_RELEASE(adapter);
   }
   factory->Release();
   return found;
}


/* ---- D3D9Ex and the managed pool ---------------------------------- */

extern "C" D3DPOOL _al_d3d_icbm_managed_pool(void)
{
   return icbm_d3d9ex ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
}

/* Extra usage for textures that used to be managed: default-pool textures
 * are only lockable when dynamic.
 */
extern "C" DWORD _al_d3d_icbm_managed_texture_usage(void)
{
   return icbm_d3d9ex ? D3DUSAGE_DYNAMIC : 0;
}

static bool d3d_init_display();

/* Called before a display is created: decide the HDR mode (ALLEGRO_HDR_AUTO:
 * only if Windows HDR is on for any monitor) and switch to
 * IDirect3D9Ex for it if nothing uses the plain IDirect3D9 object yet.
 */
static void icbm_hdr_prepare_d3d(void)
{
   ALLEGRO_SYSTEM *system = al_get_system_driver();
   ICBM_DIRECT3D_CREATE9EX create_ex;
   IDirect3D9Ex *ex = NULL;

   icbm_hdr_create_mode = icbm_hdr_new_mode;
   if (icbm_hdr_create_mode == ALLEGRO_HDR_OFF)
      return;
   if (!_al_d3d && !d3d_init_display()) {
      icbm_hdr_create_mode = ALLEGRO_HDR_OFF;
      return;
   }

   if (icbm_hdr_create_mode == ALLEGRO_HDR_AUTO) {
      bool any = icbm_hdr_any_monitor_hdr();
      icbm_hdr_create_mode = any ? ALLEGRO_HDR_SCRGB : ALLEGRO_HDR_OFF;
      ALLEGRO_INFO("HDR auto: Windows HDR is %s for any monitor\n", any ? "on" : "off");
      if (icbm_hdr_create_mode == ALLEGRO_HDR_OFF)
         return;
   }

   if (icbm_d3d9ex)
      return;
   if (system->displays._size > 0) {
      ALLEGRO_WARN("HDR: a display already exists, cannot switch to Direct3D 9Ex\n");
      return;
   }

   create_ex = (ICBM_DIRECT3D_CREATE9EX)GetProcAddress(_al_d3d_module, "Direct3DCreate9Ex");
   if (!create_ex || create_ex(D3D_SDK_VERSION, &ex) != D3D_OK || !ex) {
      ALLEGRO_WARN("HDR: Direct3DCreate9Ex failed\n");
      return;
   }

   _al_d3d->Release();
   _al_d3d = ex;
   icbm_d3d9ex = true;
   ALLEGRO_INFO("HDR: using Direct3D 9Ex\n");
}


/* ---- D3D9 render targets, suspend / resume ----------------------------- */

static ICBM_HDR *icbm_hdr(ALLEGRO_DISPLAY_D3D *d)
{
   return (ICBM_HDR *)d->icbm_hdr;
}

/* From the display's window procedure (display thread, hook in wwindow.c):
 * Windows sends WM_DISPLAYCHANGE when HDR is switched on or off; a moved
 * window may now be on another monitor. icbm_hdr_idle() then checks.
 */
extern "C" void _al_d3d_icbm_window_message(ALLEGRO_DISPLAY *display, UINT message)
{
   ICBM_HDR *hdr;

   if (!display || !(display->flags & ALLEGRO_DIRECT3D_INTERNAL))
      return;
   hdr = icbm_hdr((ALLEGRO_DISPLAY_D3D *)display);
   if (!hdr)
      return;
   switch (message) {
      case WM_DISPLAYCHANGE:
      case WM_SETTINGCHANGE:
      case WM_EXITSIZEMOVE:
      case WM_WINDOWPOSCHANGED:
      case WM_DPICHANGED:
      case WM_ACTIVATEAPP:
         hdr->check_requested = true;
         break;
   }
}

static void icbm_hdr_release_d3d11_textures(ICBM_HDR *hdr)
{
   for (int i = 0; i < ICBM_HDR_BUFFERS; i++) {
      ICBM_RELEASE(hdr->srv[i]);
      ICBM_RELEASE(hdr->tex[i]);
      hdr->opened[i] = NULL;
      hdr->q11_pending[i] = false;
   }
}

static void icbm_hdr_release_targets(ICBM_HDR *hdr)
{
   if (hdr->ctx)
      hdr->ctx->ClearState();
   icbm_hdr_release_d3d11_textures(hdr);
   for (int i = 0; i < ICBM_HDR_BUFFERS; i++) {
      ICBM_RELEASE(hdr->q9[i]);
      ICBM_RELEASE(hdr->rt[i]);
      hdr->share[i] = NULL;
   }
   hdr->w = hdr->h = 0;
   hdr->pending_idx = -1;
}

/* The swap chain only; the D3D11 device stays for a later resume. */
static void icbm_hdr_release_output(ICBM_HDR *hdr)
{
   if (hdr->ctx) {
      hdr->ctx->ClearState();
      hdr->ctx->Flush();
   }
   ICBM_RELEASE(hdr->rtv);
   ICBM_RELEASE(hdr->sc);
}

static void icbm_hdr_release_d3d11(ICBM_HDR *hdr)
{
   icbm_hdr_release_output(hdr);
   icbm_hdr_release_d3d11_textures(hdr);
   for (int i = 0; i < ICBM_HDR_BUFFERS; i++)
      ICBM_RELEASE(hdr->q11[i]);
   ICBM_RELEASE(hdr->cb);
   ICBM_RELEASE(hdr->ps);
   ICBM_RELEASE(hdr->vs);
   ICBM_RELEASE(hdr->ctx);
   ICBM_RELEASE(hdr->dev);
   hdr->ready = false;
}

/* Point the display (and the device, if it draws there) at surface s. */
static void icbm_hdr_set_backbuffer(ALLEGRO_DISPLAY_D3D *d, IDirect3DSurface9 *s)
{
   IDirect3DSurface9 *old = d->render_target;
   IDirect3DSurface9 *cur = NULL;

   d->render_target = s;
   if (d->backbuffer_bmp_extra.render_target == old)
      d->backbuffer_bmp_extra.render_target = s;

   if (old && d->device && d->device->GetRenderTarget(0, &cur) == D3D_OK) {
      if (cur == old)
         d->device->SetRenderTarget(0, s);
      cur->Release();
   }
}

/* Shared FP16 targets the size of surface bb. */
static bool icbm_hdr_create_targets(ALLEGRO_DISPLAY_D3D *d, ICBM_HDR *hdr, IDirect3DSurface9 *bb)
{
   D3DSURFACE_DESC desc;

   if (bb->GetDesc(&desc) != D3D_OK || desc.MultiSampleType != D3DMULTISAMPLE_NONE)
      return false;
   for (int i = 0; i < ICBM_HDR_BUFFERS; i++) {
      if (d->device->CreateRenderTarget(desc.Width, desc.Height,
            D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_NONE, 0, FALSE,
            &hdr->rt[i], &hdr->share[i]) != D3D_OK || !hdr->share[i] ||
          d->device->CreateQuery(D3DQUERYTYPE_EVENT, &hdr->q9[i]) != D3D_OK) {
         icbm_hdr_release_targets(hdr);
         return false;
      }
   }
   hdr->w = desc.Width;
   hdr->h = desc.Height;
   hdr->write_idx = 0;
   hdr->pending_idx = -1;
   return true;
}

/* Give up on HDR for this display: back to the real backbuffer and D3D9
 * Present. Only for setup failures and a lost D3D11 device: once a flip-model
 * swap chain has presented to the window, D3D9 Present no longer shows up in
 * it (seen when going back to D3D9 after Windows HDR was switched off), so a
 * monitor without HDR is handled by icbm_hdr_set_shown() instead.
 */
static void icbm_hdr_fall_back(ALLEGRO_DISPLAY_D3D *d, const char *why)
{
   ICBM_HDR *hdr = icbm_hdr(d);

   ALLEGRO_WARN("HDR off for good: %s\n", why);
   if (hdr->real_bb) {
      icbm_hdr_set_backbuffer(d, hdr->real_bb);
      hdr->real_bb = NULL;
   }
   hdr->active = false;
   icbm_hdr_release_d3d11(hdr);
   icbm_hdr_release_targets(hdr);
   hdr->failed = true;
}

/* After Allegro got the backbuffer (creation, reset): substitute ours. */
static void icbm_hdr_backbuffer_acquired(ALLEGRO_DISPLAY_D3D *d)
{
   ALLEGRO_DISPLAY *al_display = (ALLEGRO_DISPLAY *)d;
   ICBM_HDR *hdr = icbm_hdr(d);
   D3DSURFACE_DESC desc;

   if (!d->render_target)
      return;

   if (!hdr) {
      if (icbm_hdr_create_mode == ALLEGRO_HDR_OFF)
         return;
      hdr = (ICBM_HDR *)al_calloc(1, sizeof *hdr);
      hdr->mode = icbm_hdr_create_mode == ALLEGRO_HDR_HDR10 ? ALLEGRO_HDR_HDR10 : ALLEGRO_HDR_SCRGB;
      hdr->enabled = true;
      hdr->pending_idx = -1;
      d->icbm_hdr = hdr;
      if (!icbm_d3d9ex) {
         ALLEGRO_WARN("HDR off: no Direct3D 9Ex\n");
         hdr->failed = true;
      }
      else if (al_display->flags & ALLEGRO_FULLSCREEN) {
         ALLEGRO_WARN("HDR off: exclusive fullscreen (use ALLEGRO_FULLSCREEN_WINDOW)\n");
         hdr->failed = true;
      }
      return;  /* icbm_hdr_display_created() takes over once the display exists */
   }
   if (hdr->failed || !hdr->active)
      return;

   /* Reset while active (resize): new targets if the size changed. */
   if (d->render_target->GetDesc(&desc) != D3D_OK) {
      icbm_hdr_fall_back(d, "GetDesc failed");
      return;
   }
   if ((int)desc.Width != hdr->w || (int)desc.Height != hdr->h) {
      icbm_hdr_release_targets(hdr);
      if (!icbm_hdr_create_targets(d, hdr, d->render_target)) {
         icbm_hdr_fall_back(d, "cannot create shared FP16 render targets");
         return;
      }
   }
   hdr->real_bb = d->render_target;
   d->render_target = hdr->rt[hdr->write_idx];
}

/* Before Allegro releases its backbuffer (reset): give it back. */
static void icbm_hdr_backbuffer_release(ALLEGRO_DISPLAY_D3D *d)
{
   ICBM_HDR *hdr = icbm_hdr(d);

   if (hdr && hdr->real_bb) {
      d->render_target = hdr->real_bb;
      hdr->real_bb = NULL;
   }
}

/* Before the D3D9 device is destroyed. */
static void icbm_hdr_destroy(ALLEGRO_DISPLAY_D3D *d)
{
   ICBM_HDR *hdr = icbm_hdr(d);

   if (!hdr)
      return;
   icbm_hdr_backbuffer_release(d);
   icbm_hdr_release_d3d11(hdr);
   icbm_hdr_release_targets(hdr);
   if (hdr->compiler_module)
      FreeLibrary(hdr->compiler_module);
   if (hdr->d3d11_module)
      FreeLibrary(hdr->d3d11_module);
   al_free(hdr);
   d->icbm_hdr = NULL;
}


/* ---- D3D11 output --------------------------------------------------- */

static const char icbm_hdr_shader[] =
   "Texture2D<float4> src : register(t0);\n"
   "cbuffer Levels : register(b0) { float sdr_white; float max_nits; float knee; float hdr10; };\n"
   /* hdr10: 0 = scRGB, 1 = PQ (HDR10), 2 = sRGB (HDR10 swap chain, SDR monitor) */
   "float4 vs_main(uint id : SV_VertexID) : SV_Position {\n"
   "   float2 p = float2((id << 1) & 2, id & 2);\n"
   "   return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);\n"
   "}\n"
   "float3 srgb_to_linear(float3 c) {\n"
   "   return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);\n"
   "}\n"
   "float4 ps_main(float4 pos : SV_Position) : SV_Target {\n"
   "   float3 c = max(src.Load(int3(pos.xy, 0)).rgb, 0);\n"
   /* 0..1 is sRGB as before, everything above 1 is extra linear light */
   "   float3 nits = (srgb_to_linear(min(c, 1)) + (c - min(c, 1))) * sdr_white;\n"
   "   float range = max(max_nits - knee, 1);\n"
   "   nits = nits > knee ? knee + range * (1 - exp(-(nits - knee) / range)) : nits;\n"
   /* HDR10 swap chain while the monitor is SDR (sRGB colour space) */
   "   if (hdr10 > 1.5)\n"
   "      return float4(min(c, 1), 1);\n"
   "   if (hdr10 > 0) {\n"
   "      float3x3 to2020 = { 0.6274, 0.3293, 0.0433, 0.0691, 0.9195, 0.0114, 0.0164, 0.0880, 0.8956 };\n"
   "      float3 y = pow(saturate(mul(to2020, nits) / 10000), 0.1593017578125);\n"
   "      return float4(pow((0.8359375 + 18.8515625 * y) / (1 + 18.6875 * y), 78.84375), 1);\n"
   "   }\n"
   "   return float4(nits / 80, 1);\n"
   "}\n";

/* Levels from the monitor, unless the game set its own. While the output is
 * SDR: white is scRGB 1.0 (80 nits, what Windows shows as SDR white) and
 * nothing goes above it.
 */
static void icbm_hdr_update_levels(ICBM_HDR *hdr)
{
   ICBM_HDR_CONSTANTS *l = &hdr->levels;

   if (!hdr->shown) {
      l->sdr_white = l->max_nits = l->knee = 80.0f;
      l->hdr10 = hdr->mode == ALLEGRO_HDR_HDR10 ? 2.0f : 0.0f;
      hdr->levels_dirty = true;
      return;
   }

   l->sdr_white = hdr->user_sdr_white > 0.0f ? hdr->user_sdr_white :
      hdr->output.sdr_white > 0.0f ? hdr->output.sdr_white : 200.0f;
   l->max_nits = hdr->user_max_nits > 0.0f ? hdr->user_max_nits :
      hdr->output.max_nits > 0.0f ? hdr->output.max_nits : 1000.0f;
   if (l->max_nits < l->sdr_white)
      l->max_nits = l->sdr_white;
   l->knee = l->sdr_white + 0.75f * (l->max_nits - l->sdr_white);
   l->hdr10 = hdr->mode == ALLEGRO_HDR_HDR10 ? 1.0f : 0.0f;
   hdr->levels_dirty = true;
}

static bool icbm_hdr_compile(ICBM_HDR *hdr, const char *entry, const char *target, ID3DBlob **code)
{
   ICBM_D3D_COMPILE compile = (ICBM_D3D_COMPILE)GetProcAddress(hdr->compiler_module, "D3DCompile");
   ID3DBlob *errors = NULL;
   HRESULT hr;

   if (!compile)
      return false;
   hr = compile(icbm_hdr_shader, sizeof icbm_hdr_shader - 1, "icbm_hdr", NULL, NULL,
      entry, target, 0, 0, code, &errors);
   if (errors) {
      ALLEGRO_ERROR("HDR shader: %s\n", (const char *)errors->GetBufferPointer());
      errors->Release();
   }
   return SUCCEEDED(hr);
}

/* The DXGI adapter Direct3D 9 renders on (surfaces are shared per adapter). */
static IDXGIAdapter1 *icbm_hdr_find_adapter(ALLEGRO_DISPLAY_D3D *d)
{
   IDXGIFactory1 *factory = icbm_hdr_create_factory();
   IDXGIAdapter1 *adapter = NULL, *found = NULL;
   LUID luid;
   int n = d->win_display.adapter < 0 ? 0 : d->win_display.adapter;

   if (!factory || ((IDirect3D9Ex *)_al_d3d)->GetAdapterLUID(n, &luid) != D3D_OK) {
      ICBM_RELEASE(factory);
      return NULL;
   }
   for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);
      if (desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) {
         found = adapter;
         break;
      }
      adapter->Release();
   }
   factory->Release();
   return found;
}

/* D3D11 device, shaders, constants, queries: everything but the swap chain. */
static const char *icbm_hdr_setup_d3d11(ALLEGRO_DISPLAY_D3D *d, ICBM_HDR *hdr)
{
   ICBM_D3D11_CREATE_DEVICE create_device;
   IDXGIAdapter1 *adapter;
   ID3DBlob *code = NULL;
   D3D11_BUFFER_DESC bd;
   HRESULT hr;

   hdr->d3d11_module = LoadLibraryA("d3d11.dll");
   hdr->compiler_module = LoadLibraryA("d3dcompiler_47.dll");
   create_device = hdr->d3d11_module ? (ICBM_D3D11_CREATE_DEVICE)GetProcAddress(hdr->d3d11_module, "D3D11CreateDevice") : NULL;
   if (!create_device || !hdr->compiler_module)
      return "d3d11.dll or d3dcompiler_47.dll missing";

   adapter = icbm_hdr_find_adapter(d);
   if (!adapter)
      return "no DXGI adapter for the Direct3D 9 adapter";
   hr = create_device(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0,
      D3D11_SDK_VERSION, &hdr->dev, NULL, &hdr->ctx);
   adapter->Release();
   if (FAILED(hr))
      return "D3D11CreateDevice failed";

   if (!icbm_hdr_compile(hdr, "vs_main", "vs_4_0", &code) ||
       FAILED(hdr->dev->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), NULL, &hdr->vs))) {
      ICBM_RELEASE(code);
      return "vertex shader";
   }
   ICBM_RELEASE(code);
   if (!icbm_hdr_compile(hdr, "ps_main", "ps_4_0", &code) ||
       FAILED(hdr->dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), NULL, &hdr->ps))) {
      ICBM_RELEASE(code);
      return "pixel shader";
   }
   ICBM_RELEASE(code);

   memset(&bd, 0, sizeof bd);
   bd.ByteWidth = sizeof(ICBM_HDR_CONSTANTS);
   bd.Usage = D3D11_USAGE_DEFAULT;
   bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
   if (FAILED(hdr->dev->CreateBuffer(&bd, NULL, &hdr->cb)))
      return "constant buffer";

   for (int i = 0; i < ICBM_HDR_BUFFERS; i++) {
      D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
      if (FAILED(hdr->dev->CreateQuery(&qd, &hdr->q11[i])))
         return "queries";
   }
   hdr->ready = true;
   return NULL;
}

/* Flip-model swap chain on the display's window, in the output colour space. */
static const char *icbm_hdr_create_swap_chain(ALLEGRO_DISPLAY_D3D *d, ICBM_HDR *hdr)
{
   IDXGIDevice *xdev = NULL;
   IDXGIAdapter *adapter = NULL;
   IDXGIFactory2 *factory = NULL;
   IDXGISwapChain3 *sc3 = NULL;
   DXGI_SWAP_CHAIN_DESC1 sd;
   DXGI_COLOR_SPACE_TYPE space;
   UINT support = 0;
   const char *error = NULL;

   if (FAILED(hdr->dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&xdev)) ||
       FAILED(xdev->GetAdapter(&adapter)) ||
       FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&factory)))
      error = "no IDXGIFactory2";

   if (!error) {
      memset(&sd, 0, sizeof sd);
      sd.Width = hdr->w;
      sd.Height = hdr->h;
      sd.Format = hdr->mode == ALLEGRO_HDR_HDR10 ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
      sd.SampleDesc.Count = 1;
      sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      sd.BufferCount = 2;
      sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

      /* Without vsync a flip-model swap chain still blocks at the refresh
       * rate once both buffers are queued, unless tearing is allowed (D3D9
       * without vsync was not capped).
       */
      IDXGIFactory5 *factory5 = NULL;
      BOOL tearing = FALSE;
      if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), (void **)&factory5))) {
         if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof tearing)))
            tearing = FALSE;
         factory5->Release();
      }
      hdr->sc_flags = tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
      sd.Flags = hdr->sc_flags;

      if (FAILED(factory->CreateSwapChainForHwnd(hdr->dev, d->win_display.window, &sd, NULL, NULL, &hdr->sc)))
         error = "CreateSwapChainForHwnd failed";
   }
   if (!error) {
      factory->MakeWindowAssociation(d->win_display.window, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
      hdr->sc_w = hdr->w;
      hdr->sc_h = hdr->h;

      /* scRGB also works on an SDR monitor (Windows shows 1.0 as SDR white);
       * an HDR10 swap chain switches between PQ and sRGB (icbm_hdr_set_shown).
       */
      space = hdr->mode != ALLEGRO_HDR_HDR10 ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 :
         hdr->shown ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
      if (FAILED(hdr->sc->QueryInterface(__uuidof(IDXGISwapChain3), (void **)&sc3)) ||
          FAILED(sc3->CheckColorSpaceSupport(space, &support)) ||
          !(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ||
          FAILED(sc3->SetColorSpace1(space)))
         error = "colour space not supported";
   }

   ICBM_RELEASE(sc3);
   ICBM_RELEASE(factory);
   ICBM_RELEASE(adapter);
   ICBM_RELEASE(xdev);
   if (error)
      ICBM_RELEASE(hdr->sc);
   return error;
}

/* HDR or SDR output (present_mutex held, or nothing presents yet). */
static void icbm_hdr_set_shown(ICBM_HDR *hdr, bool shown)
{
   hdr->shown = shown;
   icbm_hdr_update_levels(hdr);

   if (hdr->sc && hdr->mode == ALLEGRO_HDR_HDR10) {
      IDXGISwapChain3 *sc3 = NULL;
      if (SUCCEEDED(hdr->sc->QueryInterface(__uuidof(IDXGISwapChain3), (void **)&sc3))) {
         sc3->SetColorSpace1(shown ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
         sc3->Release();
      }
   }
   ALLEGRO_INFO("HDR output: %s (SDR white %.0f nits, peak %.0f nits)\n",
      shown ? "HDR" : "SDR, Windows HDR off or disabled", hdr->levels.sdr_white, hdr->levels.max_nits);
}

/* After the display is created (calling thread): set up D3D11 and render
 * into our targets from now on. It stays so for the display's life; only the
 * output follows Windows HDR (icbm_hdr_set_shown), because once a flip-model
 * swap chain has presented to the window, D3D9 Present does not show there.
 */
static void icbm_hdr_display_created(ALLEGRO_DISPLAY_D3D *d)
{
   ICBM_HDR *hdr = icbm_hdr(d);
   IDirect3DSurface9 *bb = d->render_target;
   const char *error;

   if (!hdr || hdr->failed || !bb)
      return;

   if ((error = icbm_hdr_setup_d3d11(d, hdr)) != NULL) {
      icbm_hdr_fall_back(d, error);
      return;
   }

   icbm_hdr_query_monitor(MonitorFromWindow(d->win_display.window, MONITOR_DEFAULTTONEAREST), &hdr->output);
   hdr->output_hdr = hdr->output.hdr;
   hdr->last_check = al_get_time();
   hdr->shown = hdr->enabled && hdr->output_hdr;

   if (!icbm_hdr_create_targets(d, hdr, bb)) {
      icbm_hdr_fall_back(d, "cannot create shared FP16 render targets");
      return;
   }
   if ((error = icbm_hdr_create_swap_chain(d, hdr)) != NULL) {
      icbm_hdr_fall_back(d, error);
      return;
   }

   hdr->real_bb = bb;
   icbm_hdr_set_backbuffer(d, hdr->rt[hdr->write_idx]);
   hdr->last_flip = al_get_time();
   hdr->active = true;
   ALLEGRO_INFO("HDR path on (%s)\n", hdr->mode == ALLEGRO_HDR_HDR10 ? "HDR10" : "scRGB");
   icbm_hdr_set_shown(hdr, hdr->shown);
}

static void icbm_hdr_wait9(IDirect3DQuery9 *q)
{
   while (q->GetData(NULL, 0, D3DGETDATA_FLUSH) == S_FALSE)
      SwitchToThread();
}

/* Present the frame in rt[idx] through D3D11. Returns NULL, or why it failed
 * (the caller falls back to SDR: only the drawing thread may do that).
 */
static const char *icbm_hdr_present_target(ALLEGRO_DISPLAY_D3D *d, ICBM_HDR *hdr, int idx)
{
   ID3D11DeviceContext *ctx = hdr->ctx;

   if (hdr->sc_w != hdr->w || hdr->sc_h != hdr->h) {
      ctx->ClearState();
      ctx->Flush();
      ICBM_RELEASE(hdr->rtv);
      if (FAILED(hdr->sc->ResizeBuffers(0, hdr->w, hdr->h, DXGI_FORMAT_UNKNOWN, hdr->sc_flags)))
         return "ResizeBuffers failed";
      hdr->sc_w = hdr->w;
      hdr->sc_h = hdr->h;
   }
   if (!hdr->rtv) {
      ID3D11Texture2D *bb = NULL;
      if (FAILED(hdr->sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb)) ||
          FAILED(hdr->dev->CreateRenderTargetView(bb, NULL, &hdr->rtv))) {
         ICBM_RELEASE(bb);
         return "swap chain buffer unusable";
      }
      bb->Release();
   }
   if (hdr->opened[idx] != hdr->share[idx]) {
      ICBM_RELEASE(hdr->srv[idx]);
      ICBM_RELEASE(hdr->tex[idx]);
      if (FAILED(hdr->dev->OpenSharedResource(hdr->share[idx], __uuidof(ID3D11Texture2D), (void **)&hdr->tex[idx])) ||
          FAILED(hdr->dev->CreateShaderResourceView(hdr->tex[idx], NULL, &hdr->srv[idx])))
         return "OpenSharedResource failed";
      hdr->opened[idx] = hdr->share[idx];
   }
   if (hdr->levels_dirty) {
      ctx->UpdateSubresource(hdr->cb, 0, NULL, &hdr->levels, 0, 0);
      hdr->levels_dirty = false;
   }

   D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)hdr->w, (float)hdr->h, 0.0f, 1.0f };
   ctx->OMSetRenderTargets(1, &hdr->rtv, NULL);
   ctx->RSSetViewports(1, &vp);
   ctx->IASetInputLayout(NULL);
   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   ctx->VSSetShader(hdr->vs, NULL, 0);
   ctx->PSSetShader(hdr->ps, NULL, 0);
   ctx->PSSetConstantBuffers(0, 1, &hdr->cb);
   ctx->PSSetShaderResources(0, 1, &hdr->srv[idx]);
   ctx->Draw(3, 0);

   /* Unbind, so the next frame's source can be written by D3D9. */
   ID3D11ShaderResourceView *none = NULL;
   ctx->PSSetShaderResources(0, 1, &none);
   ctx->End(hdr->q11[idx]);
   hdr->q11_pending[idx] = true;

   HRESULT hr = hdr->sc->Present(d->vsync ? 1 : 0,
      !d->vsync && hdr->sc_flags ? DXGI_PRESENT_ALLOW_TEARING : 0);
   if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
      return "D3D11 device removed";
   return NULL;
}

/* Replaces IDirect3DDevice9::Present in d3d_flip_display (after EndScene,
 * present_mutex held). Switches the output between HDR and SDR when the
 * monitor or al_set_display_hdr_enabled() asks for it. Returns false if the
 * HDR path is not active: Allegro then presents the real backbuffer.
 */
static bool icbm_hdr_flip(ALLEGRO_DISPLAY_D3D *d)
{
   ICBM_HDR *hdr = icbm_hdr(d);
   const char *error;
   bool want;
   int w, next;

   if (!hdr || hdr->failed || !hdr->active)
      return false;
   if (hdr->present_error) {
      icbm_hdr_fall_back(d, hdr->present_error);
      return false;
   }

   want = hdr->enabled && hdr->output_hdr;
   if (want != hdr->shown)
      icbm_hdr_set_shown(hdr, want);

   /* Frame in rt[w] is complete once this query is signalled. */
   w = hdr->write_idx;
   hdr->q9[w]->Issue(D3DISSUE_END);
   hdr->q9[w]->GetData(NULL, 0, D3DGETDATA_FLUSH);

   /* Show the previous frame (finished by now, or nearly), unless
    * icbm_hdr_idle already did.
    */
   if (hdr->pending_idx >= 0) {
      icbm_hdr_wait9(hdr->q9[hdr->pending_idx]);
      if ((error = icbm_hdr_present_target(d, hdr, hdr->pending_idx)) != NULL) {
         icbm_hdr_fall_back(d, error);
         return false;
      }
   }
   hdr->pending_idx = w;
   hdr->last_flip = al_get_time();

   /* Draw the next frame into the target D3D11 read one flip ago. */
   next = (w + 1) % ICBM_HDR_BUFFERS;
   if (hdr->q11_pending[next]) {
      while (hdr->ctx->GetData(hdr->q11[next], NULL, 0, 0) == S_FALSE)
         SwitchToThread();
      hdr->q11_pending[next] = false;
   }
   hdr->write_idx = next;
   icbm_hdr_set_backbuffer(d, hdr->rt[next]);
   return true;
}

/* Display thread, every pass of its loop (at least every millisecond):
 * - whether Windows HDR is on for the monitor showing the window, and its
 *   levels: after WM_DISPLAYCHANGE, a window move etc.
 *   (_al_d3d_icbm_window_message) right away and once more a second later,
 *   otherwise every 5 seconds (the SDR brightness slider sends nothing);
 *   the next flip acts on it;
 * - a frame is shown one flip late, so when no flip follows (a loading
 *   screen drawn once before a long load) present the waiting frame anyway.
 */
static void icbm_hdr_idle(ALLEGRO_DISPLAY_D3D *d)
{
   ICBM_HDR *hdr = icbm_hdr(d);
   const char *error;
   double now;

   if (!hdr || hdr->failed || !hdr->active)
      return;
   now = al_get_time();

   if (hdr->check_requested) {
      hdr->check_requested = false;
      hdr->recheck_at = now + ICBM_HDR_RECHECK;
      hdr->last_check = 0.0;
   }
   else if (hdr->recheck_at > 0.0 && now >= hdr->recheck_at) {
      hdr->recheck_at = 0.0;
      hdr->last_check = 0.0;
   }

   if (now - hdr->last_check >= ICBM_HDR_CHECK_PERIOD) {
      ICBM_HDR_MONITOR monitor;
      hdr->last_check = now;
      if (icbm_hdr_query_monitor(MonitorFromWindow(d->win_display.window, MONITOR_DEFAULTTONEAREST), &monitor)) {
         if (monitor.hdr != hdr->output_hdr)
            ALLEGRO_INFO("HDR: Windows HDR is now %s for the monitor showing the window\n", monitor.hdr ? "on" : "off");
         if (monitor.sdr_white != hdr->output.sdr_white || monitor.max_nits != hdr->output.max_nits) {
            al_lock_mutex(present_mutex);
            hdr->output = monitor;
            icbm_hdr_update_levels(hdr);
            al_unlock_mutex(present_mutex);
         }
         hdr->output_hdr = monitor.hdr;
      }
   }

   if (hdr->pending_idx < 0 || hdr->present_error || now - hdr->last_flip < ICBM_HDR_IDLE_PRESENT)
      return;

   al_lock_mutex(present_mutex);
   if (hdr->active && hdr->pending_idx >= 0 && !hdr->present_error &&
       hdr->q9[hdr->pending_idx]->GetData(NULL, 0, D3DGETDATA_FLUSH) == S_OK) {
      if ((error = icbm_hdr_present_target(d, hdr, hdr->pending_idx)) != NULL)
         hdr->present_error = error;
      else
         hdr->pending_idx = -1;
   }
   al_unlock_mutex(present_mutex);
}

/* ---- public API ------------------------------------------------------ */

static ALLEGRO_DISPLAY_D3D *icbm_hdr_d3d_display(ALLEGRO_DISPLAY *display)
{
   if (!display || !(al_get_display_flags(display) & ALLEGRO_DIRECT3D_INTERNAL))
      return NULL;
   return (ALLEGRO_DISPLAY_D3D *)display;
}

/* Function: al_set_new_display_hdr
 * ALLEGRO_HDR_OFF, ALLEGRO_HDR_SCRGB, ALLEGRO_HDR_HDR10 or ALLEGRO_HDR_AUTO
 * (scRGB if Windows HDR is on for any monitor,
 * otherwise as ALLEGRO_HDR_OFF) for displays created after this call
 * (Direct3D only).
 */
void al_set_new_display_hdr(int mode)
{
   icbm_hdr_new_mode = mode;
}

/* Function: al_get_display_hdr
 * The HDR mode of the display's HDR path (drawing into a floating-point
 * target, nothing clamped to 1), ALLEGRO_HDR_OFF if it has none. Whether the
 * monitor shows HDR right now follows from al_get_display_hdr_levels(): the
 * same value for both levels means SDR output.
 */
int al_get_display_hdr(ALLEGRO_DISPLAY *display)
{
   ALLEGRO_DISPLAY_D3D *d = icbm_hdr_d3d_display(display);
   ICBM_HDR *hdr = d ? icbm_hdr(d) : NULL;

   return hdr && hdr->active ? hdr->mode : ALLEGRO_HDR_OFF;
}

/* Function: al_set_display_hdr_enabled
 * false: SDR output (as with Windows HDR off), true (the default): HDR output
 * while Windows HDR is on for the monitor showing the window. From the next
 * flip on; the display keeps its HDR path either way.
 */
void al_set_display_hdr_enabled(ALLEGRO_DISPLAY *display, bool enabled)
{
   ALLEGRO_DISPLAY_D3D *d = icbm_hdr_d3d_display(display);
   ICBM_HDR *hdr = d ? icbm_hdr(d) : NULL;

   if (hdr)
      hdr->enabled = enabled;
}

/* Function: al_get_display_hdr_levels
 * Brightness of SDR white (Windows' "SDR content brightness") and the peak
 * the output rolls off to, in nits. A colour value of max / sdr_white reaches
 * the peak. Both are 80 (no headroom) while the output is SDR, 0 without an
 * HDR path.
 */
void al_get_display_hdr_levels(ALLEGRO_DISPLAY *display, float *sdr_white_nits, float *max_nits)
{
   ALLEGRO_DISPLAY_D3D *d = icbm_hdr_d3d_display(display);
   ICBM_HDR *hdr = d ? icbm_hdr(d) : NULL;
   bool on = hdr && hdr->active;

   if (sdr_white_nits)
      *sdr_white_nits = on ? hdr->levels.sdr_white : 0.0f;
   if (max_nits)
      *max_nits = on ? hdr->levels.max_nits : 0.0f;
}

/* Function: al_set_display_hdr_levels
 * Overrides the levels read from Windows (kept while the output is SDR, used
 * again when it is HDR); values <= 0 go back to the monitor's.
 */
void al_set_display_hdr_levels(ALLEGRO_DISPLAY *display, float sdr_white_nits, float max_nits)
{
   ALLEGRO_DISPLAY_D3D *d = icbm_hdr_d3d_display(display);
   ICBM_HDR *hdr = d ? icbm_hdr(d) : NULL;

   if (!hdr)
      return;
   al_lock_mutex(present_mutex);
   hdr->user_sdr_white = sdr_white_nits > 0.0f ? sdr_white_nits : 0.0f;
   hdr->user_max_nits = max_nits > 0.0f ? max_nits : 0.0f;
   if (hdr->active)
      icbm_hdr_update_levels(hdr);
   al_unlock_mutex(present_mutex);
}

/* vim: set sts=3 sw=3 et: */
