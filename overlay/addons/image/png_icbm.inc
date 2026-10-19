/*
 *    ICBM extension, included by addons/image/png.c before really_load_png.
 *
 *    PNG loading into an existing bitmap (al_load_existing_bitmap_flags_f),
 *    with optional alpha-map export.
 */


static ALLEGRO_BITMAP *load_png_f(ALLEGRO_FILE *fp, int flags,
   ALLEGRO_BITMAP *existing, uint8_t *alpha_map);


/* The image is written to the top-left corner, so a larger bitmap is fine;
 * a smaller one would be overrun.
 */
static bool icbm_png_fits(ALLEGRO_BITMAP *bmp, png_uint_32 width,
   png_uint_32 height)
{
   if ((png_uint_32)al_get_bitmap_width(bmp) >= width &&
       (png_uint_32)al_get_bitmap_height(bmp) >= height)
      return true;

   ALLEGRO_ERROR("PNG (%ux%u) does not fit into the existing bitmap (%dx%d).\n",
      (unsigned)width, (unsigned)height,
      al_get_bitmap_width(bmp), al_get_bitmap_height(bmp));
   return false;
}


/* Copy the alpha of one converted row (ABGR_8888_LE, or the palette index
 * for ALLEGRO_KEEP_INDEX) into the alpha map.
 */
static void icbm_png_store_alpha_row(uint8_t *alpha_map, png_uint_32 y,
   png_uint_32 width, const unsigned char *row, bool index_only)
{
   uint8_t *out = alpha_map + (size_t)y * width;
   png_uint_32 i;

   if (index_only) {
      memcpy(out, row, width);
      return;
   }
   for (i = 0; i < width; i++)
      out[i] = row[i * 4 + 3];
}


ALLEGRO_BITMAP *_al_load_png_existing_f(ALLEGRO_FILE *fp, ALLEGRO_BITMAP *bmp,
   int flags, uint8_t *alpha_map)
{
   ALLEGRO_ASSERT(bmp);
   return load_png_f(fp, flags, bmp, alpha_map);
}

/* vim: set sts=3 sw=3 et: */
