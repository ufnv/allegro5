/*
 *    ICBM extension, included by src/win/d3d_disp.cpp (C++, outside any
 *    extern "C" block, because the game's hooks have C++ linkage).
 *
 *    - Device-reset hooks: before a lost device is reset the game releases
 *      all of its video memory (ReleaseAllVideoMemoryPossible), afterwards it
 *      is told the reset is over (DisplayResetFinished). No-op defaults are
 *      provided, so programs that do not define the hooks still link.
 *    - Message pump for the display thread that handles every pending
 *      message and then sleeps until the next one (at most 1 ms), instead of
 *      handling one message per al_rest(0.001).
 *    - The index range of an indexed draw (draw_prim_common) found four
 *      indices at a time, without a branch per index.
 */


/* Defined by the game (graph/Graph.cpp in ICBM). */
void ReleaseAllVideoMemoryPossible();
void DisplayResetFinished();

#if defined(_MSC_VER)
   void _al_icbm_default_release_all_video_memory_possible() {}
   void _al_icbm_default_display_reset_finished() {}
   /* C++ names are decorated the same way for x86 and x64. */
   #pragma comment(linker, "/alternatename:?ReleaseAllVideoMemoryPossible@@YAXXZ=?_al_icbm_default_release_all_video_memory_possible@@YAXXZ")
   #pragma comment(linker, "/alternatename:?DisplayResetFinished@@YAXXZ=?_al_icbm_default_display_reset_finished@@YAXXZ")
#elif defined(__GNUC__) || defined(__clang__)
   __attribute__((weak)) void ReleaseAllVideoMemoryPossible() {}
   __attribute__((weak)) void DisplayResetFinished() {}
#endif


/* Returns false when WM_QUIT was received. */
static bool icbm_d3d_pump_messages(MSG *msg)
{
   while (PeekMessage(msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg->message == WM_QUIT)
         return false;
      DispatchMessage(msg);
   }

   /* Sleep until new input/messages arrive, but keep polling the device
    * state (lost / reset requests) at least every millisecond.
    */
   MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT);
   return true;
}

/* vim: set sts=3 sw=3 et: */


/* The lowest and the highest of the num (> 0) indices: the vertex range
 * DrawIndexedPrimitiveUP is given. Every al_draw_indexed_prim scans all its
 * indices for it; upstream's loop branched on each one (a few % of the
 * game's main thread with its big vertex batches). The same result.
 */
#if defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
   #include <emmintrin.h>
   #define ICBM_INDEX_RANGE_SSE2
#endif

static void icbm_index_range(const int *indices, int num, int *min_idx, int *max_idx)
{
   int lo = indices[0];
   int hi = indices[0];
   int i = 0;

#ifdef ICBM_INDEX_RANGE_SSE2
   if (num >= 8) {
      __m128i vlo = _mm_loadu_si128((const __m128i *)indices);
      __m128i vhi = vlo;
      int l[4], h[4], k;

      for (i = 4; i + 4 <= num; i += 4) {
         __m128i v = _mm_loadu_si128((const __m128i *)(indices + i));
         __m128i lt = _mm_cmplt_epi32(v, vlo);
         __m128i gt = _mm_cmpgt_epi32(v, vhi);
         vlo = _mm_or_si128(_mm_and_si128(lt, v), _mm_andnot_si128(lt, vlo));
         vhi = _mm_or_si128(_mm_and_si128(gt, v), _mm_andnot_si128(gt, vhi));
      }

      _mm_storeu_si128((__m128i *)l, vlo);
      _mm_storeu_si128((__m128i *)h, vhi);

      for (k = 0; k < 4; k++) {
         lo = l[k] < lo ? l[k] : lo;
         hi = h[k] > hi ? h[k] : hi;
      }
   }
#endif

   for (; i < num; i++) {
      int idx = indices[i];
      lo = idx < lo ? idx : lo;
      hi = idx > hi ? idx : hi;
   }

   *min_idx = lo;
   *max_idx = hi;
}
