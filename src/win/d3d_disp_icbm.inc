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
