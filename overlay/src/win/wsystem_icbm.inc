/*
 *    ICBM extension, included by src/win/wsystem.c.
 *
 *    Monitor numbering: adapter 0 is always the primary monitor, like
 *    D3DADAPTER_DEFAULT in Direct3D; the other monitors keep their
 *    EnumDisplayDevices order. Without this, al_get_monitor_info(n) and the
 *    Direct3D adapter n can be different monitors, and the game opens on the
 *    wrong screen.
 */


/* Position of the primary monitor among the desktop-attached adapters, in
 * EnumDisplayDevices order.
 */
static int icbm_primary_adapter_index(void)
{
   DISPLAY_DEVICE dd;
   int count = 0;
   int c = 0;

   memset(&dd, 0, sizeof(dd));
   dd.cb = sizeof(dd);

   while (EnumDisplayDevices(NULL, count, &dd, 0)) {
      if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
         if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
            return c;
         c++;
      }
      count++;
   }

   return 0;
}


/* Allegro adapter number -> EnumDisplayDevices desktop index. */
static int icbm_translate_adapter(int adapter)
{
   int primary = icbm_primary_adapter_index();

   if (adapter == 0)
      return primary;
   if (adapter <= primary)
      return adapter - 1;
   return adapter;
}

/* vim: set sts=3 sw=3 et: */
