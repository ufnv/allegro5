/* ICBM: included by ogl_draw.c.
 *
 * revert_state disabled the position, colour and texcoord arrays after a draw
 * but left the user attribute arrays (al_user_attr_N) enabled. Their locations
 * belong to the shader that drew, so the next shader's setup_state cannot turn
 * them off when it has no such attribute (its user_attr_loc is -1): the array
 * stays enabled on a location that shader may not use, pointing at the old
 * vertex buffer or client memory. A later draw with more vertices than that
 * buffer holds then lets the driver read past its end (ICBM: the NVIDIA
 * driver's thread crashed drawing the region borders with a shader whose
 * user attributes are optimised out, the arrays left by another border shader).
 * Direct3D is not affected: its vertex declaration names every stream.
 */
#ifdef ALLEGRO_CFG_OPENGL_PROGRAMMABLE_PIPELINE
static void icbm_disable_user_attr_arrays(ALLEGRO_DISPLAY *display)
{
   int i;

   for (i = 0; i < ALLEGRO_PRIM_MAX_USER_ATTR; i++) {
      if (display->ogl_extras->varlocs.user_attr_loc[i] >= 0)
         glDisableVertexAttribArray(display->ogl_extras->varlocs.user_attr_loc[i]);
   }
}
#endif
