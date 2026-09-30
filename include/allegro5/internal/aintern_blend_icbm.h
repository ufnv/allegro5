/*
 *    ICBM extension, included by allegro5/internal/aintern_blend.h.
 *
 *    ALLEGRO_MIN / ALLEGRO_MAX for the software blender. As in OpenGL and
 *    Direct3D, the blend factors are ignored for these operations.
 */
#ifndef __al_included_allegro5_aintern_blend_icbm_h
#define __al_included_allegro5_aintern_blend_icbm_h

/* Color channels; used inside `switch (op)`. */
#define _AL_ICBM_BLEND_MINMAX_RGB_CASES                          \
      case ALLEGRO_MIN:                                          \
         result->r = _ALLEGRO_MIN(result->r, dcol->r);            \
         result->g = _ALLEGRO_MIN(result->g, dcol->g);            \
         result->b = _ALLEGRO_MIN(result->b, dcol->b);            \
         break;                                                  \
      case ALLEGRO_MAX:                                          \
         result->r = _ALLEGRO_MAX(result->r, dcol->r);            \
         result->g = _ALLEGRO_MAX(result->g, dcol->g);            \
         result->b = _ALLEGRO_MAX(result->b, dcol->b);            \
         break;

/* Alpha channel; used inside `switch (aop)`. */
#define _AL_ICBM_BLEND_MINMAX_ALPHA_CASES                        \
      case ALLEGRO_MIN:                                          \
         result->a = _ALLEGRO_MIN(result->a, dcol->a);            \
         break;                                                  \
      case ALLEGRO_MAX:                                          \
         result->a = _ALLEGRO_MAX(result->a, dcol->a);            \
         break;

#endif
