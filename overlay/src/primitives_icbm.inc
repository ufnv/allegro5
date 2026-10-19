/*
 *    ICBM extension, included at the end of src/primitives.c.
 *
 *    The vertex count of the indexed draw in progress on this thread, set by
 *    al_draw_indexed_prim_range (addons/primitives/primitives_icbm.inc) around
 *    al_draw_indexed_prim: Direct3D's DrawIndexedPrimitiveUP is given vertices
 *    0 .. count - 1 instead of the range found by scanning the indices
 *    (src/win/d3d_disp_icbm.inc). -1: not known, the indices are scanned.
 */


#if defined(_MSC_VER)
   #define ICBM_THREAD_LOCAL __declspec(thread)
#else
   #define ICBM_THREAD_LOCAL __thread
#endif

static ICBM_THREAD_LOCAL int icbm_indexed_vertex_count = -1;


void _al_icbm_set_indexed_vertex_count(int count)
{
   icbm_indexed_vertex_count = count;
}


int _al_icbm_get_indexed_vertex_count(void)
{
   return icbm_indexed_vertex_count;
}
