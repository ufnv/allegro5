/*
 *    ICBM extension, included at the end of addons/primitives/primitives.c.
 *
 *    Thread-safe ("_TS") vertex/index buffer lock/unlock/destroy.
 *
 *    The stock functions dispatch through al_get_current_display(), which is
 *    thread-local and NULL on worker threads. The _TS variants use the
 *    display backend that created the buffers instead, so buffers can be
 *    filled from worker threads. `is_opengl` is kept for API compatibility
 *    with older ICBM builds (where it selected the backend directly).
 */


#include "allegro5/internal/aintern_display.h"


static ALLEGRO_DISPLAY_INTERFACE *icbm_buffer_vt = NULL;


/* Called from al_create_vertex_buffer / al_create_index_buffer. */
static void icbm_remember_buffer_backend(void)
{
   ALLEGRO_DISPLAY *disp = al_get_current_display();
   if (disp)
      icbm_buffer_vt = disp->vt;
}


static ALLEGRO_DISPLAY_INTERFACE *icbm_buffer_backend(bool is_opengl)
{
   ALLEGRO_DISPLAY *disp = al_get_current_display();
   (void)is_opengl;

   /* Prefer the current display when this thread has one. */
   if (disp)
      return disp->vt;
   ASSERT(icbm_buffer_vt);
   return icbm_buffer_vt;
}


/* Same as the (static) lock_buffer_common in src/primitives.c; sizes in bytes. */
static bool icbm_lock_buffer_common(_AL_BUFFER_COMMON *common, int offset,
   int length, int flags)
{
   if (common->is_locked || (common->write_only && flags != ALLEGRO_LOCK_WRITEONLY))
      return false;

   common->lock_offset = offset;
   common->lock_length = length;
   common->lock_flags = flags;
   common->is_locked = true;
   return true;
}


/* Function: al_lock_vertex_buffer_TS
 */
void *al_lock_vertex_buffer_TS(ALLEGRO_VERTEX_BUFFER *buffer, int offset,
   int length, int flags, bool is_opengl)
{
   ALLEGRO_DISPLAY_INTERFACE *vt;
   int stride;
   ASSERT(buffer);
   ASSERT(addon_initialized);

   if (offset + length > buffer->common.size)
      return NULL;

   stride = buffer->decl ? buffer->decl->stride : (int)sizeof(ALLEGRO_VERTEX);

   vt = icbm_buffer_backend(is_opengl);
   if (!vt || !vt->lock_vertex_buffer)
      return NULL;

   if (!icbm_lock_buffer_common(&buffer->common, offset * stride, length * stride, flags))
      return NULL;

   return vt->lock_vertex_buffer(buffer);
}


/* Function: al_unlock_vertex_buffer_TS
 */
void al_unlock_vertex_buffer_TS(ALLEGRO_VERTEX_BUFFER *buffer, bool is_opengl)
{
   ALLEGRO_DISPLAY_INTERFACE *vt;
   ASSERT(buffer);
   ASSERT(addon_initialized);

   if (!buffer->common.is_locked)
      return;

   buffer->common.is_locked = false;

   vt = icbm_buffer_backend(is_opengl);
   if (vt && vt->unlock_vertex_buffer)
      vt->unlock_vertex_buffer(buffer);
}


/* Function: al_destroy_vertex_buffer_TS
 */
void al_destroy_vertex_buffer_TS(ALLEGRO_VERTEX_BUFFER *buffer, bool is_opengl)
{
   ALLEGRO_DISPLAY_INTERFACE *vt;

   if (!buffer)
      return;

   al_unlock_vertex_buffer_TS(buffer, is_opengl);

   vt = icbm_buffer_backend(is_opengl);
   if (vt && vt->destroy_vertex_buffer)
      vt->destroy_vertex_buffer(buffer);

   al_free(buffer);
}


/* Function: al_lock_index_buffer_TS
 */
void *al_lock_index_buffer_TS(ALLEGRO_INDEX_BUFFER *buffer, int offset,
   int length, int flags, bool is_opengl)
{
   ALLEGRO_DISPLAY_INTERFACE *vt;
   ASSERT(buffer);
   ASSERT(addon_initialized);

   if (offset + length > buffer->common.size)
      return NULL;

   vt = icbm_buffer_backend(is_opengl);
   if (!vt || !vt->lock_index_buffer)
      return NULL;

   if (!icbm_lock_buffer_common(&buffer->common, offset * buffer->index_size,
         length * buffer->index_size, flags))
      return NULL;

   return vt->lock_index_buffer(buffer);
}


/* Function: al_unlock_index_buffer_TS
 */
void al_unlock_index_buffer_TS(ALLEGRO_INDEX_BUFFER *buffer, bool is_opengl)
{
   ALLEGRO_DISPLAY_INTERFACE *vt;
   ASSERT(buffer);
   ASSERT(addon_initialized);

   if (!buffer->common.is_locked)
      return;

   buffer->common.is_locked = false;

   vt = icbm_buffer_backend(is_opengl);
   if (vt && vt->unlock_index_buffer)
      vt->unlock_index_buffer(buffer);
}


/* Function: al_destroy_index_buffer_TS
 */
void al_destroy_index_buffer_TS(ALLEGRO_INDEX_BUFFER *buffer, bool is_opengl)
{
   ALLEGRO_DISPLAY_INTERFACE *vt;

   if (!buffer)
      return;

   al_unlock_index_buffer_TS(buffer, is_opengl);

   vt = icbm_buffer_backend(is_opengl);
   if (vt && vt->destroy_index_buffer)
      vt->destroy_index_buffer(buffer);

   al_free(buffer);
}

/* Function: al_draw_indexed_prim_range
 *
 * al_draw_indexed_prim for indices that all lie in vertices 0 .. num_vertices
 * - 1 (ICBM's vertex batches: FinishVertexDrawing): Direct3D is given that
 * range instead of the one found by scanning all the indices.
 */
int al_draw_indexed_prim_range(const void* vtxs, const ALLEGRO_VERTEX_DECL* decl,
   ALLEGRO_BITMAP* texture, const int* indices, int num_vtx, int type, int num_vertices)
{
   int ret;

   _al_icbm_set_indexed_vertex_count(num_vertices);
   ret = al_draw_indexed_prim(vtxs, decl, texture, indices, num_vtx, type);
   _al_icbm_set_indexed_vertex_count(-1);

   return ret;
}

/* vim: set sts=3 sw=3 et: */
