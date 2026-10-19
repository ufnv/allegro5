/*
 *    ICBM extension, included at the end of src/bitmap_io.c.
 *
 *    Loading an image into an already existing bitmap (keeps the texture,
 *    avoids re-allocating video memory), optionally exporting the alpha
 *    channel as a width*height byte map.
 */


/* Function: al_register_bitmap_loader_existing_f
 */
bool al_register_bitmap_loader_existing_f(const char *extension,
   ALLEGRO_IIO_FS_EXISTING_LOADER_FUNCTION fs_existing_loader)
{
   REGISTER(fs_existing_loader)
}


/* Function: al_load_existing_bitmap_flags_f
 *
 * Like al_load_bitmap_flags_f, but decodes into `bitmap`, which must be at
 * least as large as the image. If alpha_map is not NULL it receives the
 * alpha channel (width * height bytes). Returns `bitmap`, or NULL on error
 * or if the format has no existing-bitmap loader.
 */
ALLEGRO_BITMAP *al_load_existing_bitmap_flags_f(ALLEGRO_FILE *fp,
   ALLEGRO_BITMAP *bitmap, const char *ident, int flags, uint8_t *alpha_map)
{
   Handler *h;
   ASSERT(bitmap);

   if (ident)
      h = find_handler(ident, false);
   else
      h = find_handler_for_file(fp);
   if (h && h->fs_existing_loader)
      return h->fs_existing_loader(fp, bitmap, flags, alpha_map);
   else
      return NULL;
}

/* vim: set sts=3 sw=3 et: */
