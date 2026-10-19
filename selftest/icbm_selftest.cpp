// Runtime self-test for the ICBM Allegro patches (Windows: Direct3D and OpenGL).
// Usage: icbm_selftest <dir with plain.png, interlaced.png> <ttf font>
#include <allegro5/allegro.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_direct3d.h>
#include <allegro5/allegro_opengl.h>
#include <allegro5/allegro_windows.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) printf("  ok   "); else { printf("  FAIL "); failures++; } \
   printf(__VA_ARGS__); printf("\n"); } while (0)

/* ---- symbols the game provides / uses ---- */
extern "C" bool StoreTUTVForGlyph;
extern "C" bool GlyphAlignCenter;
extern "C" bool SEND_REPEAT_EVENTS;
extern "C" float ALLEGRO_PRIMITIVE_STD_Z;
extern "C" int _al_icbm_get_indexed_vertex_count(void); /* src/primitives_icbm.inc */

static int glyph_calls = 0;
extern "C" void DrawGlyphCallback(ALLEGRO_BITMAP *Map, float gx, float gy, float gw, float gh, float xo, float yo)
{
   (void)Map; (void)gx; (void)gy; (void)gw; (void)gh; (void)xo; (void)yo;
   glyph_calls++;
}
/* C++ linkage, like ICBM's graph/Graph.cpp */
void ReleaseAllVideoMemoryPossible() {}
void DisplayResetFinished() {}


/* ------------------------------------------------------------------ png */

static void expected_pixel(int x, int y, unsigned char *r, unsigned char *g, unsigned char *b, unsigned char *a)
{
   *r = (x * 16) & 255; *g = (y * 32) & 255; *b = 200; *a = (x * 8 + y * 16) & 255;
}

static void test_png(const char *dir, const char *name)
{
   char path[1024];
   snprintf(path, sizeof path, "%s/%s", dir, name);
   printf("[png existing] %s\n", name);

   al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP);
   ALLEGRO_BITMAP *bmp = al_create_bitmap(32, 16); /* larger than the 16x8 image */
   al_set_target_bitmap(bmp);
   al_clear_to_color(al_map_rgba(1, 2, 3, 4));

   uint8_t alpha[16 * 8 + 16];
   memset(alpha, 0xCD, sizeof alpha);   /* canary after 128 bytes */

   ALLEGRO_FILE *f = al_fopen(path, "rb");
   ALLEGRO_BITMAP *ret = al_load_existing_bitmap_flags_f(f, bmp, ".png", ALLEGRO_NO_PREMULTIPLIED_ALPHA, alpha);
   al_fclose(f);
   CHECK(ret == bmp, "returns the existing bitmap");

   int bad = 0, bad_alpha = 0;
   for (int y = 0; y < 8; y++)
      for (int x = 0; x < 16; x++) {
         unsigned char r, g, b, a, er, eg, eb, ea;
         al_unmap_rgba(al_get_pixel(bmp, x, y), &r, &g, &b, &a);
         expected_pixel(x, y, &er, &eg, &eb, &ea);
         if (r != er || g != eg || b != eb || a != ea) bad++;
         if (alpha[y * 16 + x] != ea) bad_alpha++;
      }
   CHECK(bad == 0, "pixels match (%d wrong)", bad);
   CHECK(bad_alpha == 0, "alpha map matches (%d wrong)", bad_alpha);
   bool canary = true;
   for (int i = 128; i < (int)sizeof alpha; i++) if (alpha[i] != 0xCD) canary = false;
   CHECK(canary, "alpha map not overrun");
   unsigned char r, g, b, a;
   al_unmap_rgba(al_get_pixel(bmp, 20, 12), &r, &g, &b, &a);
   CHECK(r == 1 && g == 2 && b == 3 && a == 4, "pixels outside the image untouched");

   ALLEGRO_BITMAP *tiny_bmp = al_create_bitmap(8, 8);
   f = al_fopen(path, "rb");
   ret = al_load_existing_bitmap_flags_f(f, tiny_bmp, ".png", 0, NULL);
   al_fclose(f);
   CHECK(ret == NULL, "target smaller than the image rejected");
   al_destroy_bitmap(tiny_bmp);

   f = al_fopen(path, "rb");
   ret = al_load_existing_bitmap_flags_f(f, bmp, ".jpg", 0, NULL);
   al_fclose(f);
   CHECK(ret == NULL, "format without loader returns NULL (no crash)");
   al_destroy_bitmap(bmp);
}


/* ---------------------------------------------------------------- blend */

static void test_blend(int bitmap_flags, const char *what)
{
   printf("[blend min/max] %s\n", what);
   al_set_new_bitmap_flags(bitmap_flags);
   al_set_new_bitmap_format((bitmap_flags & ALLEGRO_MEMORY_BITMAP) ? ALLEGRO_PIXEL_FORMAT_ABGR_8888 : ALLEGRO_PIXEL_FORMAT_ANY_32_WITH_ALPHA);
   ALLEGRO_BITMAP *dst = al_create_bitmap(4, 4);
   ALLEGRO_BITMAP *src = al_create_bitmap(4, 4);
   CHECK(dst && src, "bitmaps created");
   if (!dst || !src) return;
   al_set_target_bitmap(src);
   al_clear_to_color(al_map_rgba(150, 50, 100, 255));
   int ops[2] = { ALLEGRO_MAX, ALLEGRO_MIN };
   for (int i = 0; i < 2; i++) {
      al_set_target_bitmap(dst);
      al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ZERO);
      al_clear_to_color(al_map_rgba(100, 200, 100, 255));
      /* factors deliberately "wrong": MIN/MAX must ignore them */
      al_set_blender(ops[i], ALLEGRO_ZERO, ALLEGRO_ZERO);
      al_draw_bitmap(src, 0, 0, 0);
      al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);
      unsigned char r, g, b, a;
      al_unmap_rgba(al_get_pixel(dst, 1, 1), &r, &g, &b, &a);
      if (ops[i] == ALLEGRO_MAX)
         CHECK(r == 150 && g == 200 && b == 100, "MAX -> (%d,%d,%d) expected (150,200,100)", r, g, b);
      else
         CHECK(r == 100 && g == 50 && b == 100, "MIN -> (%d,%d,%d) expected (100,50,100)", r, g, b);
   }
   al_destroy_bitmap(src);
   al_destroy_bitmap(dst);
   al_set_new_bitmap_format(ALLEGRO_PIXEL_FORMAT_ANY);
}


/* ----------------------------------------------------------- _TS buffers */

struct TsJob {
   ALLEGRO_VERTEX_BUFFER *vb; ALLEGRO_INDEX_BUFFER *ib;
   bool is_opengl, unlock_in_worker;
   bool had_display, had_gl_context, ok, crashed;
};

static void ts_fill(TsJob *job)
{
   ALLEGRO_VERTEX *v = (ALLEGRO_VERTEX *)al_lock_vertex_buffer_TS(job->vb, 0, 3, ALLEGRO_LOCK_WRITEONLY, job->is_opengl);
   int *idx = (int *)al_lock_index_buffer_TS(job->ib, 0, 3, ALLEGRO_LOCK_WRITEONLY, job->is_opengl);
   job->ok = v && idx;
   const float k = job->unlock_in_worker ? 7.0f : 10.0f;   /* distinct data per case (GL may recycle freed buffers) */
   if (v) for (int i = 0; i < 3; i++) { v[i].x = k * i; v[i].y = 1; v[i].z = 0; }
   if (idx) for (int i = 0; i < 3; i++) idx[i] = (job->unlock_in_worker ? 5 : 2) - i;
   if (job->unlock_in_worker) {
      al_unlock_vertex_buffer_TS(job->vb, job->is_opengl);
      al_unlock_index_buffer_TS(job->ib, job->is_opengl);
   }
}

static void ts_fill_guarded(TsJob *job)
{
   __try { ts_fill(job); }
   __except (EXCEPTION_EXECUTE_HANDLER) { job->crashed = true; }
}

static void *ts_worker(ALLEGRO_THREAD *t, void *arg)
{
   (void)t;
   TsJob *job = (TsJob *)arg;
   job->had_display = al_get_current_display() != NULL;
   job->had_gl_context = wglGetCurrentContext() != NULL;
   ts_fill_guarded(job);
   return NULL;
}

/* Fill buffers on a worker thread; unlock there or on the main thread. */
static void ts_case(bool is_opengl, bool unlock_in_worker, bool must_reach_gpu)
{
   TsJob job;
   memset(&job, 0, sizeof job);
   job.is_opengl = is_opengl;
   job.unlock_in_worker = unlock_in_worker;
   job.vb = al_create_vertex_buffer(NULL, NULL, 3, ALLEGRO_PRIM_BUFFER_READWRITE);
   job.ib = al_create_index_buffer(4, NULL, 3, ALLEGRO_PRIM_BUFFER_READWRITE);
   const char *mode = unlock_in_worker ? "lock+unlock in worker" : "lock in worker, unlock on main";
   CHECK(job.vb && job.ib, "%s: buffers created", mode);
   if (!job.vb || !job.ib) return;

   ALLEGRO_THREAD *th = al_create_thread(ts_worker, &job);
   al_start_thread(th);
   al_join_thread(th, NULL);
   al_destroy_thread(th);
   if (!unlock_in_worker) {
      al_unlock_vertex_buffer_TS(job.vb, is_opengl);
      al_unlock_index_buffer_TS(job.ib, is_opengl);
   }

   CHECK(!job.had_display, "%s: worker thread has no current display", mode);
   CHECK(!job.crashed, "%s: no crash in the worker", mode);
   CHECK(job.ok, "%s: locked from the worker thread", mode);

   /* Read back from the GPU buffer (GL: glGetBufferSubData). */
   ALLEGRO_VERTEX *v = (ALLEGRO_VERTEX *)al_lock_vertex_buffer(job.vb, 0, 3, ALLEGRO_LOCK_READONLY);
   bool vok = v && v[2].x == (unlock_in_worker ? 14.0f : 20.0f);
   al_unlock_vertex_buffer(job.vb);
   int *idx = (int *)al_lock_index_buffer(job.ib, 0, 3, ALLEGRO_LOCK_READONLY);
   bool iok = idx && idx[0] == (unlock_in_worker ? 5 : 2) && idx[2] == (unlock_in_worker ? 3 : 0);
   al_unlock_index_buffer(job.ib);
   if (must_reach_gpu)
      CHECK(vok && iok, "%s: data reached the GPU buffer", mode);
   else
      printf("  info %s: data %s the GPU buffer (worker %s a GL context)\n", mode,
         (vok && iok) ? "reached" : "did NOT reach", job.had_gl_context ? "had" : "did not have");
   al_destroy_vertex_buffer_TS(job.vb, is_opengl);
   al_destroy_index_buffer_TS(job.ib, is_opengl);
}

static void test_ts_buffers(bool is_opengl)
{
   printf("[primitives _TS]\n");
   ts_case(is_opengl, false, true);
   /* D3D: unlocking in the worker works. OpenGL: the upload in unlock needs
    * the GL context, which only the display's thread has - informational. */
   ts_case(is_opengl, true, !is_opengl);

   ALLEGRO_VERTEX_ELEMENT el[] = {
      { ALLEGRO_PRIM_POSITION, ALLEGRO_PRIM_FLOAT_4, 0 },
      { ALLEGRO_PRIM_TEX_COORD_PIXEL, ALLEGRO_PRIM_FLOAT_3, 16 },
      { 0, 0, 0 } };
   ALLEGRO_VERTEX_DECL *decl = al_create_vertex_decl(el, 28);
   CHECK(decl != NULL, "vertex decl with FLOAT_4 position + FLOAT_3 texcoord accepted");
   al_destroy_vertex_decl(decl);
}


/* ----------------------------------------------------------------- font */

static void test_font(const char *ttf)
{
   printf("[font]\n");
   int x = 7, y = 7, w = 7, h = 7;
   CHECK(al_get_text_width(NULL, "abc") == 1, "NULL font width = 1");
   CHECK(al_get_font_ascent(NULL) == 0 && al_get_font_descent(NULL) == 0, "NULL font ascent/descent = 0");
   al_get_text_dimensions(NULL, "abc", &x, &y, &w, &h);
   CHECK(x == 0 && y == 0 && w == 0 && h == 0, "NULL font dimensions zeroed");
   al_draw_text(NULL, al_map_rgb(255, 255, 255), 0, 0, 0, "no crash");
   CHECK(true, "al_draw_text(NULL font) does not crash");

   ALLEGRO_FONT *font = al_load_ttf_font(ttf, 20, 0);
   CHECK(font != NULL, "ttf loaded");
   if (!font) return;

   glyph_calls = 0;
   StoreTUTVForGlyph = true;
   al_draw_text(font, al_map_rgb(255, 255, 255), 10, 10, 0, "Hello");
   StoreTUTVForGlyph = false;
   CHECK(glyph_calls == 5, "application's DrawGlyphCallback used (%d calls, expected 5)", glyph_calls);

   /* Normal drawing still produces pixels (border/offset math intact). */
   ALLEGRO_BITMAP *t = al_create_bitmap(64, 32);
   al_set_target_bitmap(t);
   al_clear_to_color(al_map_rgba(0, 0, 0, 0));
   al_draw_text(font, al_map_rgb(255, 255, 255), 2, 2, 0, "Hi");
   int lit = 0;
   for (int yy = 0; yy < 32; yy++)
      for (int xx = 0; xx < 64; xx++)
         if (al_get_pixel(t, xx, yy).r > 0.5f) lit++;
   CHECK(lit > 20 && lit < 400, "text renders (%d lit pixels, bitmap format %d)", lit, al_get_bitmap_format(t));
   al_set_target_backbuffer(al_get_current_display());
   al_destroy_bitmap(t);
   al_destroy_font(font);
}


/* --------------------------------------------------------------- shader */

static void test_shader(bool is_opengl)
{
   printf("[shader]\n");
   ALLEGRO_SHADER *sh = al_create_shader(ALLEGRO_SHADER_AUTO);
   if (is_opengl)
      CHECK(sh && al_get_shader_platform(sh) == ALLEGRO_SHADER_GLSL, "AUTO on OpenGL reports ALLEGRO_SHADER_GLSL");
   else
      CHECK(sh && al_get_shader_platform(sh) == ALLEGRO_SHADER_HLSL, "AUTO on Direct3D reports ALLEGRO_SHADER_HLSL");
   bool ok = sh &&
      al_attach_shader_source(sh, ALLEGRO_VERTEX_SHADER, al_get_default_shader_source(ALLEGRO_SHADER_AUTO, ALLEGRO_VERTEX_SHADER)) &&
      al_attach_shader_source(sh, ALLEGRO_PIXEL_SHADER, al_get_default_shader_source(ALLEGRO_SHADER_AUTO, ALLEGRO_PIXEL_SHADER)) &&
      al_build_shader(sh);
   CHECK(ok, "default shader builds%s%s", ok ? "" : ": ", ok ? "" : (sh ? al_get_shader_log(sh) : "no shader"));
   if (ok) {
      CHECK(al_use_shader(sh), "shader can be used");
      al_use_shader(NULL);
   }
   if (sh) al_destroy_shader(sh);
}


/* ------------------------------------------------ ALLEGRO_PRIMITIVE_STD_Z */

/* Draws a rotated bitmap against a depth buffer cleared to 0.5 with the
 * given Z; returns whether it passed the depth test. */
static bool draw_rotated_at_z(ALLEGRO_BITMAP *sprite, float z)
{
   ALLEGRO_DISPLAY *d = al_get_current_display();
   al_set_target_backbuffer(d);
   al_clear_to_color(al_map_rgb(0, 0, 0));
   al_clear_depth_buffer(0.5f);
   al_set_render_state(ALLEGRO_DEPTH_TEST, 1);
   al_set_render_state(ALLEGRO_DEPTH_FUNCTION, ALLEGRO_RENDER_LESS);
   ALLEGRO_PRIMITIVE_STD_Z = z;
   al_draw_rotated_bitmap(sprite, 8, 8, 40, 40, 0.1f, 0);
   ALLEGRO_PRIMITIVE_STD_Z = 0.0f;
   al_set_render_state(ALLEGRO_DEPTH_TEST, 0);
   unsigned char r, g, b;
   al_unmap_rgb(al_get_pixel(al_get_backbuffer(d), 40, 40), &r, &g, &b);
   return r > 128;
}

static void test_std_z(void)
{
   printf("[ALLEGRO_PRIMITIVE_STD_Z]\n");
   ALLEGRO_DISPLAY *d = al_get_current_display();
   if (al_get_display_option(d, ALLEGRO_DEPTH_SIZE) <= 0) {
      printf("  info no depth buffer, skipped\n");
      return;
   }
   ALLEGRO_BITMAP *sprite = al_create_bitmap(16, 16);
   al_set_target_bitmap(sprite);
   al_clear_to_color(al_map_rgb(255, 0, 0));
   bool near_drawn = draw_rotated_at_z(sprite, -0.9f);
   bool far_drawn = draw_rotated_at_z(sprite, 0.9f);
   CHECK(near_drawn != far_drawn, "Z of rotated bitmap reaches the depth test (z=-0.9: %s, z=+0.9: %s)",
      near_drawn ? "drawn" : "hidden", far_drawn ? "drawn" : "hidden");
   al_destroy_bitmap(sprite);
}


/* -------------------------------------------------------------- monitor */

static void test_monitors(void)
{
   printf("[monitors]\n");
   int n = al_get_num_video_adapters();
   for (int i = 0; i < n; i++) {
      ALLEGRO_MONITOR_INFO mi;
      al_get_monitor_info(i, &mi);
      printf("       adapter %d: (%d,%d)-(%d,%d)\n", i, mi.x1, mi.y1, mi.x2, mi.y2);
   }
   ALLEGRO_MONITOR_INFO mi;
   al_get_monitor_info(0, &mi);
   CHECK(mi.x1 == 0 && mi.y1 == 0, "adapter 0 is the primary monitor");

   /* The real bug: Allegro passes its adapter number straight to Direct3D
    * (CreateDevice) but positions the window with al_get_monitor_info().
    * Both must describe the same monitor for every adapter number. */
   IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
   if (!d3d) {
      printf("  info Direct3D 9 not available, skipped D3D adapter check\n");
      return;
   }
   int d3d_count = (int)d3d->GetAdapterCount();
   CHECK(d3d_count == n, "Direct3D sees %d adapters, Allegro %d", d3d_count, n);
   for (int i = 0; i < n && i < d3d_count; i++) {
      MONITORINFO wmi;
      memset(&wmi, 0, sizeof wmi);
      wmi.cbSize = sizeof wmi;
      GetMonitorInfo(d3d->GetAdapterMonitor(i), &wmi);
      al_get_monitor_info(i, &mi);
      RECT r = wmi.rcMonitor;
      CHECK(r.left == mi.x1 && r.top == mi.y1 && r.right == mi.x2 && r.bottom == mi.y2,
         "adapter %d: Direct3D monitor (%ld,%ld)-(%ld,%ld) == Allegro monitor (%d,%d)-(%d,%d)",
         i, r.left, r.top, r.right, r.bottom, mi.x1, mi.y1, mi.x2, mi.y2);
   }
   d3d->Release();
}


/* ----------------------------------------------------------------- idle */

static double process_cpu_seconds(void)
{
   FILETIME c, e, k, u;
   GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
   ULONGLONG kk = ((ULONGLONG)k.dwHighDateTime << 32) | k.dwLowDateTime;
   ULONGLONG uu = ((ULONGLONG)u.dwHighDateTime << 32) | u.dwLowDateTime;
   return (kk + uu) / 1e7;
}

static void test_idle_cpu(void)
{
   printf("[display thread idle]\n");
   double t0 = process_cpu_seconds();
   al_rest(2.0);
   double used = process_cpu_seconds() - t0;
   CHECK(used < 0.5, "idle for 2 s used %.2f s CPU (a spinning thread would use ~2 s)", used);
}


/* ------------------------------------------------------------------ hdr */

static float half_to_float(unsigned short h)
{
   int e = (h >> 10) & 31, m = h & 1023;
   float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? INFINITY : ldexpf((float)(m | 1024), e - 25);
   return (h & 0x8000) ? -v : v;
}

/* Reads pixel (x, y) of the current D3D render target; false unless FP16. */
static bool read_fp16(ALLEGRO_DISPLAY *d, int x, int y, float out[4])
{
   IDirect3DDevice9 *dev = al_get_d3d_device(d);
   IDirect3DSurface9 *rt = NULL, *sys = NULL;
   D3DSURFACE_DESC desc;
   D3DLOCKED_RECT lr;
   bool ok = false;

   if (dev->GetRenderTarget(0, &rt) != D3D_OK)
      return false;
   rt->GetDesc(&desc);
   if (desc.Format == D3DFMT_A16B16G16R16F &&
       dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, NULL) == D3D_OK &&
       dev->GetRenderTargetData(rt, sys) == D3D_OK &&
       sys->LockRect(&lr, NULL, D3DLOCK_READONLY) == D3D_OK) {
      unsigned short *p = (unsigned short *)((char *)lr.pBits + y * lr.Pitch) + x * 4;
      for (int i = 0; i < 4; i++)
         out[i] = half_to_float(p[i]);
      sys->UnlockRect();
      ok = true;
   }
   if (sys) sys->Release();
   rt->Release();
   return ok;
}

static void rt_size(ALLEGRO_DISPLAY *d, int *w, int *h, D3DFORMAT *f)
{
   IDirect3DSurface9 *rt = NULL;
   D3DSURFACE_DESC desc;
   *w = *h = 0; *f = D3DFMT_UNKNOWN;
   if (al_get_d3d_device(d)->GetRenderTarget(0, &rt) == D3D_OK) {
      rt->GetDesc(&desc);
      *w = desc.Width; *h = desc.Height; *f = desc.Format;
      rt->Release();
   }
}

/* Colour from a uniform, like ICBM's ext_vertex_rd, passed in the given
 * interpolator (COLOR0 or TEXCOORD1).
 */
static ALLEGRO_SHADER *hdr_shader(const char *semantic)
{
   char vs[1024];
   snprintf(vs, sizeof vs,
      "struct VS_INPUT { float4 Position : POSITION0; float2 TexCoord : TEXCOORD0; float4 Color : TEXCOORD1; };\n"
      "struct VS_OUTPUT { float4 Position : POSITION0; float4 Color : %s; float2 TexCoord : TEXCOORD0; };\n"
      "float4x4 al_projview_matrix;\n"
      "float4 RangeColor;\n"
      "VS_OUTPUT vs_main(VS_INPUT Input) {\n"
      "   VS_OUTPUT Output;\n"
      "   Output.Position = mul(Input.Position, al_projview_matrix);\n"
      "   Output.Color = RangeColor;\n"
      "   Output.TexCoord = Input.TexCoord;\n"
      "   return Output;\n"
      "}\n", semantic);
   ALLEGRO_SHADER *sh = al_create_shader(ALLEGRO_SHADER_AUTO);
   bool built = sh && al_attach_shader_source(sh, ALLEGRO_VERTEX_SHADER, vs) &&
      al_attach_shader_source(sh, ALLEGRO_PIXEL_SHADER,
         "float4 ps_main(VS_OUTPUT Input) : COLOR0 { return Input.Color; }\n") &&
      al_build_shader(sh);
   CHECK(built, "%s test shader builds%s%s", semantic, built ? "" : ": ", built ? "" : (sh ? al_get_shader_log(sh) : ""));
   if (!built && sh) {
      al_destroy_shader(sh);
      sh = NULL;
   }
   return sh;
}

static void hdr_fill(ALLEGRO_COLOR c)
{
   al_draw_filled_rectangle(0, 0, 64, 64, c);
}

/* Draws with the shader and returns the red channel written. */
static float hdr_shader_red(ALLEGRO_DISPLAY *d, ALLEGRO_SHADER *sh)
{
   float c[4] = { 3, 2, 1.5f, 1 }, px[4] = {0};
   al_clear_to_color(al_map_rgba_f(0.25f, 0.25f, 0.25f, 1));
   al_use_shader(sh);
   al_set_shader_float_vector("RangeColor", 4, c, 1);
   hdr_fill(al_map_rgba_f(1, 1, 1, 1));
   al_use_shader(NULL);
   return read_fp16(d, 10, 10, px) ? px[0] : -1.0f;
}

/* Clears to c and flips twice (HDR shows a frame one flip late), then gives
 * the compositor time to show it.
 */
static void hdr_show(ALLEGRO_DISPLAY *d, ALLEGRO_COLOR c)
{
   al_set_target_backbuffer(d);
   for (int i = 0; i < 2; i++) {
      al_clear_to_color(c);
      al_flip_display();
   }
   al_rest(0.25);
}

/* Pixel of the screen inside the window (what the compositor shows). */
static void screen_pixel(ALLEGRO_DISPLAY *d, int *r, int *g, int *b)
{
   POINT p = { 20, 20 };
   HDC dc = GetDC(NULL);
   ClientToScreen(al_get_win_window_handle(d), &p);
   COLORREF c = GetPixel(dc, p.x, p.y);
   ReleaseDC(NULL, dc);
   *r = GetRValue(c);
   *g = GetGValue(c);
   *b = GetBValue(c);
}

static void test_hdr(void)
{
   printf("\n===== Direct3D display with HDR (Direct3D 9Ex + D3D11 output) =====\n");
   al_set_new_display_hdr(ALLEGRO_HDR_SCRGB);
   /* Like ICBM: without the programmable pipeline Allegro packs vertex colours into 8 bits. */
   al_set_new_display_flags(ALLEGRO_DIRECT3D | ALLEGRO_WINDOWED | ALLEGRO_RESIZABLE | ALLEGRO_PROGRAMMABLE_PIPELINE);
   al_set_new_display_option(ALLEGRO_DEPTH_SIZE, 16, ALLEGRO_SUGGEST);
   ALLEGRO_DISPLAY *d = al_create_display(320, 200);
   al_set_new_display_hdr(ALLEGRO_HDR_OFF);
   CHECK(d != NULL, "display created");
   if (!d) return;

   /* Buffers/textures that used to be D3DPOOL_MANAGED must work on 9Ex. */
   test_blend(ALLEGRO_VIDEO_BITMAP, "video bitmap (Direct3D 9Ex)");
   test_ts_buffers(false);
   test_shader(false);
   al_set_new_bitmap_format(ALLEGRO_PIXEL_FORMAT_COMPRESSED_RGBA_DXT5);
   ALLEGRO_BITMAP *dxt = al_create_bitmap(64, 64);
   al_set_new_bitmap_format(ALLEGRO_PIXEL_FORMAT_ANY);
   bool dxt_ok = false;
   if (dxt) {
      ALLEGRO_LOCKED_REGION *lr = al_lock_bitmap_blocked(dxt, ALLEGRO_LOCK_WRITEONLY);
      if (lr) { memset(lr->data, 0, 16); al_unlock_bitmap(dxt); dxt_ok = true; }
      al_destroy_bitmap(dxt);
   }
   CHECK(dxt_ok, "compressed (DXT5) bitmap created and locked on 9Ex");

   int mode = al_get_display_hdr(d);
   if (mode == ALLEGRO_HDR_OFF) {
      printf("  info HDR not active (Windows HDR off for this monitor, or no D3D11): HDR checks skipped\n");
      al_destroy_display(d);
      return;
   }
   CHECK(mode == ALLEGRO_HDR_SCRGB, "HDR active in scRGB");
   float white, peak;
   al_get_display_hdr_levels(d, &white, &peak);
   CHECK(white > 0 && peak >= white, "levels: SDR white %.0f nits, peak %.0f nits", white, peak);
   al_set_display_hdr_levels(d, 0, 1000);
   float white2, peak2;
   al_get_display_hdr_levels(d, &white2, &peak2);
   CHECK(white2 == white && peak2 == 1000, "levels can be overridden (peak %.0f)", peak2);

   al_set_target_backbuffer(d);
   int w, h; D3DFORMAT f;
   rt_size(d, &w, &h, &f);
   CHECK(f == D3DFMT_A16B16G16R16F && w == 320 && h == 200, "backbuffer is a %dx%d FP16 target (format %d)", w, h, (int)f);

   float px[4] = {0};
   al_clear_to_color(al_map_rgba_f(0.75f, 0.75f, 0.75f, 1));
   al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ONE);
   hdr_fill(al_map_rgba_f(1, 1, 1, 1));
   hdr_fill(al_map_rgba_f(1, 1, 1, 1));
   bool rd = read_fp16(d, 10, 10, px);
   CHECK(rd && fabsf(px[0] - 2.75f) < 0.01f, "additive blending goes past 1.0 (%.3f, expected 2.75)", px[0]);

   /* Colours above 1 reach the target: vertex colours (float4 with the
    * programmable pipeline) and uniforms, through COLOR0 (ext_vertex_rd,
    * Allegro's default shader) or TEXCOORD (ext_vertex_ee) interpolators.
    */
   al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ZERO);
   al_clear_to_color(al_map_rgba_f(0.25f, 0.25f, 0.25f, 1));
   hdr_fill(al_map_rgba_f(3, 2, 1.5f, 1));
   rd = read_fp16(d, 10, 10, px);
   CHECK(rd && px[0] > 2.99f && fabsf(px[1] - 2) < 0.01f,
      "vertex colour 3.0 with Allegro's default shader is not clamped (%.2f %.2f %.2f)", px[0], px[1], px[2]);
   const char *semantics[] = { "COLOR0", "TEXCOORD1" };
   for (int i = 0; i < 2; i++) {
      ALLEGRO_SHADER *sh = hdr_shader(semantics[i]);
      if (sh) {
         float r = hdr_shader_red(d, sh);
         CHECK(r > 2.99f, "uniform colour 3.0 passed as %s is not clamped (%.2f)", semantics[i], r);
         al_destroy_shader(sh);
      }
   }
   al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);

   /* Targets rotate on flip; drawing continues in the next one. */
   IDirect3DSurface9 *before = NULL, *after = NULL;
   al_get_d3d_device(d)->GetRenderTarget(0, &before);
   al_flip_display();
   al_get_d3d_device(d)->GetRenderTarget(0, &after);
   CHECK(before && after && before != after, "flip moves drawing to the next shared target");
   if (before) before->Release();
   if (after) after->Release();

   double t0 = al_get_time();
   for (int i = 0; i < 60; i++) {
      al_clear_to_color(al_map_rgba_f(0.2f, 0.2f, 0.2f, 1));
      al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ONE);
      al_draw_filled_circle(160, 100, 20 + i, al_map_rgba_f(1, 1, 0.8f, 1));
      al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_INVERSE_ALPHA);
      al_flip_display();
   }
   double ms = (al_get_time() - t0) * 1000 / 60;
   CHECK(al_get_display_hdr(d) == ALLEGRO_HDR_SCRGB, "60 frames presented, still HDR (%.2f ms per frame)", ms);

   /* A frame followed by no flip (loading screen) is presented by the display
    * thread after 50 ms; flipping afterwards must still work.
    */
   al_clear_to_color(al_map_rgba_f(0.5f, 0.2f, 0.2f, 1));
   al_flip_display();
   al_rest(0.3);
   for (int i = 0; i < 3; i++) {
      al_clear_to_color(al_map_rgba_f(0.2f, 0.2f, 0.2f, 1));
      al_flip_display();
   }
   CHECK(al_get_display_hdr(d) == ALLEGRO_HDR_SCRGB, "idle 300 ms after a flip (display thread presents), then flips: still HDR");

   /* What the window shows: HDR output, SDR output (as with Windows HDR
    * off), HDR again. The HDR path (FP16 target, D3D11 present) stays.
    */
   int px_r, px_g, px_b;
   hdr_show(d, al_map_rgb(255, 0, 0));
   screen_pixel(d, &px_r, &px_g, &px_b);
   CHECK(px_r > 150 && px_g < 100 && px_b < 100, "HDR output: the window shows the frame (screen pixel %d,%d,%d)", px_r, px_g, px_b);

   al_set_display_hdr_enabled(d, false);
   hdr_show(d, al_map_rgb(0, 255, 0));
   rt_size(d, &w, &h, &f);
   al_get_display_hdr_levels(d, &white2, &peak2);
   CHECK(al_get_display_hdr(d) == ALLEGRO_HDR_SCRGB && f == D3DFMT_A16B16G16R16F && white2 == peak2,
      "SDR output: HDR path kept, no headroom (SDR white %.0f, peak %.0f)", white2, peak2);
   screen_pixel(d, &px_r, &px_g, &px_b);
   CHECK(px_g > 150 && px_r < 100 && px_b < 100, "SDR output: the window shows the frame (screen pixel %d,%d,%d)", px_r, px_g, px_b);

   al_set_display_hdr_enabled(d, true);
   hdr_show(d, al_map_rgb(0, 0, 255));
   screen_pixel(d, &px_r, &px_g, &px_b);
   CHECK(px_b > 150 && px_r < 100 && px_g < 100, "HDR output again: the window shows the frame (screen pixel %d,%d,%d)", px_r, px_g, px_b);
   al_get_display_hdr_levels(d, &white2, &peak2);
   CHECK(peak2 == 1000 && white2 == white, "level override kept through SDR output (SDR white %.0f, peak %.0f)", white2, peak2);

   CHECK(al_resize_display(d, 400, 300), "display resized");
   al_set_target_backbuffer(d);
   rt_size(d, &w, &h, &f);
   CHECK(f == D3DFMT_A16B16G16R16F && w == 400 && h == 300, "after resize: %dx%d FP16 target", w, h);
   for (int i = 0; i < 5; i++) {
      al_clear_to_color(al_map_rgba_f(0.2f, 0.2f, 0.2f, 1));
      al_flip_display();
   }
   CHECK(al_get_display_hdr(d) == ALLEGRO_HDR_SCRGB, "frames after resize presented, still HDR");

   al_destroy_display(d);
   CHECK(true, "HDR display destroyed");

   /* ALLEGRO_HDR_AUTO: HDR because Windows HDR is on for a monitor (the
    * test above got this far). Without it on any monitor, auto leaves plain
    * Direct3D 9; that needs a desktop without HDR to check.
    */
   al_set_new_display_hdr(ALLEGRO_HDR_AUTO);
   al_set_new_display_flags(ALLEGRO_DIRECT3D | ALLEGRO_WINDOWED | ALLEGRO_PROGRAMMABLE_PIPELINE);
   d = al_create_display(320, 200);
   al_set_new_display_hdr(ALLEGRO_HDR_OFF);
   CHECK(d && al_get_display_hdr(d) == ALLEGRO_HDR_SCRGB, "ALLEGRO_HDR_AUTO on a monitor with Windows HDR on: scRGB");
   if (d) {
      hdr_show(d, al_map_rgb(255, 0, 0));
      al_destroy_display(d);
   }
}

/* ------------------------------------------------ indexed draw's range */

/* draw_prim_common finds the indices' lowest and highest (d3d_disp_icbm.inc,
 * four at a time): indices in the middle of a bigger vertex array, out of
 * order, a remainder after the groups of four. A wrong range loses the
 * quad at the lowest or the highest vertex.
 */
static void test_indexed_range(void)
{
   printf("[indexed draw range]\n");
   ALLEGRO_DISPLAY *d = al_get_current_display();
   al_set_target_backbuffer(d);
   al_clear_to_color(al_map_rgb(0, 0, 0));

   ALLEGRO_VERTEX v[40];
   memset(v, 0, sizeof v);
   for (int i = 0; i < 40; i++) {
      v[i].x = -100; v[i].y = -100; v[i].color = al_map_rgb(0, 0, 255);
   }
   ALLEGRO_COLOR red = al_map_rgb(255, 0, 0);
   const float quad[4][2] = { {0, 0}, {20, 0}, {0, 20}, {20, 20} };
   for (int k = 0; k < 4; k++) {
      v[13 + k].x = 10 + quad[k][0]; v[13 + k].y = 10 + quad[k][1]; v[13 + k].color = red; /* the lowest: 13 */
      v[27 + k].x = 50 + quad[k][0]; v[27 + k].y = 50 + quad[k][1]; v[27 + k].color = red; /* the highest: 30 */
   }
   int idx[] = { 27, 28, 29, 29, 28, 30, 15, 14, 13, 15, 16, 14, 20, 20, 20 }; /* 15: groups of 4 and 3 left */
   al_draw_indexed_prim(v, NULL, NULL, idx, 15, ALLEGRO_PRIM_TRIANGLE_LIST);

   unsigned char r1, g1, b1, r2, g2, b2;
   al_unmap_rgb(al_get_pixel(al_get_backbuffer(d), 20, 20), &r1, &g1, &b1);
   al_unmap_rgb(al_get_pixel(al_get_backbuffer(d), 60, 60), &r2, &g2, &b2);
   CHECK(r1 > 200 && b1 < 50 && r2 > 200 && b2 < 50, "both quads drawn: at the lowest vertex (%d,%d,%d), at the highest (%d,%d,%d)",
      r1, g1, b1, r2, g2, b2);

   /* al_draw_indexed_prim_range: the vertices given (0 .. 39), no scan;
    * then a plain indexed draw scans again (the range is not left set).
    */
   al_clear_to_color(al_map_rgb(0, 0, 0));
   al_draw_indexed_prim_range(v, NULL, NULL, idx, 6, ALLEGRO_PRIM_TRIANGLE_LIST, 40);
   al_draw_indexed_prim(v, NULL, NULL, idx + 6, 6, ALLEGRO_PRIM_TRIANGLE_LIST);
   al_unmap_rgb(al_get_pixel(al_get_backbuffer(d), 20, 20), &r1, &g1, &b1);
   al_unmap_rgb(al_get_pixel(al_get_backbuffer(d), 60, 60), &r2, &g2, &b2);
   CHECK(r1 > 200 && b1 < 50 && r2 > 200 && b2 < 50,
      "al_draw_indexed_prim_range draws its quad (%d,%d,%d), a plain indexed draw after it its own (%d,%d,%d)", r2, g2, b2, r1, g1, b1);
   CHECK(_al_icbm_get_indexed_vertex_count() == -1, "the range is reset after al_draw_indexed_prim_range (%d)",
      _al_icbm_get_indexed_vertex_count());
}

/* ----------------------------------------------------------------- main */

static void run_display_tests(int api_flag, const char *api, const char *ttf)
{
   bool is_opengl = api_flag == ALLEGRO_OPENGL;
   printf("\n===== %s display =====\n", api);
   al_set_new_display_flags(api_flag | ALLEGRO_WINDOWED);
   al_set_new_display_option(ALLEGRO_DEPTH_SIZE, 16, ALLEGRO_SUGGEST);
   ALLEGRO_DISPLAY *d = al_create_display(320, 200);
   CHECK(d != NULL, "%s display created", api);
   if (!d) return;
   CHECK(((al_get_display_flags(d) & ALLEGRO_OPENGL) != 0) == is_opengl, "display really uses %s", api);
   if (is_opengl)
      printf("       OpenGL version 0x%08x\n", al_get_opengl_version());

   char what[64];
   snprintf(what, sizeof what, "video bitmap (%s)", api);
   test_blend(ALLEGRO_VIDEO_BITMAP, what);
   test_ts_buffers(is_opengl);
   test_font(ttf);
   test_shader(is_opengl);
   test_std_z();
   test_indexed_range();
   test_idle_cpu();
   al_destroy_display(d);
}

int main(int argc, char **argv)
{
   setvbuf(stdout, NULL, _IONBF, 0);
   if (argc < 3) { printf("usage: %s <png dir> <ttf>\n", argv[0]); return 2; }
   al_init(); al_init_image_addon(); al_init_font_addon(); al_init_ttf_addon(); al_init_primitives_addon();

   SEND_REPEAT_EVENTS = true; GlyphAlignCenter = false; /* symbols exist */

   if (getenv("ICBM_SELFTEST_HDR_ONLY")) {
      test_hdr();
      printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
      return failures ? 1 : 0;
   }

   test_png(argv[1], "plain.png");
   test_png(argv[1], "interlaced.png");
   test_blend(ALLEGRO_MEMORY_BITMAP, "memory bitmap (software blender)");
   test_monitors();

   run_display_tests(ALLEGRO_DIRECT3D, "Direct3D", argv[2]);
   run_display_tests(ALLEGRO_OPENGL, "OpenGL", argv[2]);
   test_hdr(); /* last: it switches the process to Direct3D 9Ex */

   printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
   return failures ? 1 : 0;
}
