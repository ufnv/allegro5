/*
 *    ICBM extension, included by src/win/wgl_disp.c.
 *
 *    Destroying a WGL display waits for the window thread to end, not only
 *    for its thread_ended flag. The thread sets the flag just before it
 *    returns, and its DLL_THREAD_DETACH notifications (the OpenGL driver's
 *    among them) run after that. With the NVIDIA driver a wglDeleteContext
 *    on the destroying thread at that moment deadlocks inside the driver:
 *    the context release polls for a driver thread to end while the window
 *    thread's detach spins, both forever (seen at ICBM's exit, in
 *    al_uninstall_system from atexit, intermittently).
 */


/* The window thread, opened before it is told to end. NULL when there is no
 * window (it failed to open) or when the window thread destroys the display
 * itself (its own failure paths).
 */
static HANDLE icbm_wgl_open_window_thread(ALLEGRO_DISPLAY_WIN *win_disp)
{
   DWORD id;

   if (!win_disp->window)
      return NULL;

   id = GetWindowThreadProcessId(win_disp->window, NULL);
   if (id == 0 || id == GetCurrentThreadId())
      return NULL;

   return OpenThread(SYNCHRONIZE, FALSE, id);
}


/* After thread_ended: wait until the thread has really ended (detach
 * notifications done). A timeout keeps a stuck thread from hanging the
 * caller; the old behaviour then follows.
 */
static void icbm_wgl_wait_window_thread(HANDLE thread)
{
   if (!thread)
      return;

   if (WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0)
      ALLEGRO_WARN("wgl display thread did not end within 5 s\n");

   CloseHandle(thread);
}
