/*
 *    ICBM extension, included near the top of addons/ttf/ttf.c.
 *
 *    - Wider glyph border (BORDER_SIZE pixels on each side instead of 2), so
 *      glyphs stay clean when the text is scaled/rotated with linear filtering.
 *    - StoreTUTVForGlyph: instead of drawing, hand every glyph to the
 *      application's DrawGlyphCallback (ICBM batches glyphs with a shader).
 *    - GlyphAlignCenter: center each glyph on the pen position.
 */


/* Border around every glyph in the glyph cache, per side. */
#define BORDER_SIZE 4


/* Set by the application (declared extern "C" in ICBM's graph/FONT.H). */
bool StoreTUTVForGlyph = false;
bool GlyphAlignCenter = false;


/* Implemented by the application when it uses StoreTUTVForGlyph.
 * A no-op default is provided so that programs which do not define it
 * (e.g. the Allegro examples) still link.
 */
void DrawGlyphCallback(ALLEGRO_BITMAP *Map, float glyph_x, float glyph_y,
   float glyph_width, float glyph_height, float x_offset, float y_offset);

#if defined(_MSC_VER)
   void _al_icbm_default_draw_glyph_callback(ALLEGRO_BITMAP *Map,
      float glyph_x, float glyph_y, float glyph_width, float glyph_height,
      float x_offset, float y_offset)
   {
      (void)Map; (void)glyph_x; (void)glyph_y; (void)glyph_width;
      (void)glyph_height; (void)x_offset; (void)y_offset;
   }
   #if defined(_M_IX86)
      #pragma comment(linker, "/alternatename:_DrawGlyphCallback=__al_icbm_default_draw_glyph_callback")
   #else
      #pragma comment(linker, "/alternatename:DrawGlyphCallback=_al_icbm_default_draw_glyph_callback")
   #endif
#elif defined(__GNUC__) || defined(__clang__)
   __attribute__((weak)) void DrawGlyphCallback(ALLEGRO_BITMAP *Map,
      float glyph_x, float glyph_y, float glyph_width, float glyph_height,
      float x_offset, float y_offset)
   {
      (void)Map; (void)glyph_x; (void)glyph_y; (void)glyph_width;
      (void)glyph_height; (void)x_offset; (void)y_offset;
   }
#endif


/* Replaces the al_draw_tinted_bitmap_region call in render_glyph.
 * Includes BORDER_SIZE/2 pixels of the border, which improves results when
 * rotating and scaling.
 */
static void icbm_draw_glyph(ALLEGRO_FONT const *f, ALLEGRO_COLOR color,
   ALLEGRO_GLYPH const *glyph, float xpos, float ypos)
{
   const float half = BORDER_SIZE / 2;
   float dx = glyph->offset_x + glyph->kerning - half;
   float dy = glyph->offset_y - half;

   if (GlyphAlignCenter) {
      dx -= glyph->w / 2;
      dy -= f->height / 2;
   }

   if (StoreTUTVForGlyph) {
      DrawGlyphCallback(glyph->bitmap, glyph->x - half, glyph->y - half,
         glyph->w + BORDER_SIZE, glyph->h + BORDER_SIZE, dx, dy);
   }
   else {
      al_draw_tinted_bitmap_region(glyph->bitmap, color,
         glyph->x - half, glyph->y - half,
         glyph->w + BORDER_SIZE, glyph->h + BORDER_SIZE,
         xpos + dx, ypos + dy, 0);
   }
}

/* vim: set sts=3 sw=3 et: */
