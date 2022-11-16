/*
 *  OpenSlide, a library for reading whole slide image files
 *
 *  Copyright (c) 2007-2013 Carnegie Mellon University
 *  Copyright (c) 2011 Google, Inc.
 *  All rights reserved.
 *
 *  OpenSlide is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, version 2.1.
 *
 *  OpenSlide is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with OpenSlide. If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

/*
 * Huron BigTIFF support
 *
 * quickhash comes from _openslide_tifflike_init_properties_and_hash
 *
 */

#include <config.h>

#include "openslide-private.h"
#include "openslide-decode-tiff.h"
#include "openslide-decode-tifflike.h"

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <tiffio.h>

static const char HURON_MAKER[] = "Huron";
static const char HURON_MODEL[] = "LE";
static const char MACRO_DESCRIPTION[] = "macro";
static const char LABEL_DESCRIPTION[] = "label";

#define GET_FIELD_OR_FAIL(tiff, tag, type, result)               \
  do {                                                           \
    type tmp;                                                    \
    if (!TIFFGetField(tiff, tag, &tmp)) {                        \
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,  \
                  "Cannot get required TIFF tag: %d", tag);      \
      goto FAIL;                                                 \
    }                                                            \
    result = tmp;                                                \
  } while (0)

struct huron_ops_data {
  struct _openslide_tiffcache *tc;
};

struct level {
  struct _openslide_level base;
  struct _openslide_tiff_level tiffl;
  struct _openslide_grid *grid;
};

static void destroy(openslide_t *osr) {
  struct huron_ops_data *data = osr->data;
  _openslide_tiffcache_destroy(data->tc);
  g_slice_free(struct huron_ops_data, data);

  for (int32_t i = 0; i < osr->level_count; i++) {
    struct level *l = (struct level *) osr->levels[i];
    _openslide_grid_destroy(l->grid);
    g_slice_free(struct level, l);
  }
  g_free(osr->levels);
}

static bool read_tile(openslide_t *osr,
                      cairo_t *cr,
                      struct _openslide_level *level,
                      int64_t tile_col, int64_t tile_row,
                      void *arg,
                      GError **err) {
  struct level *l = (struct level *) level;
  struct _openslide_tiff_level *tiffl = &l->tiffl;
  TIFF *tiff = arg;

  // tile size
  int64_t tw = tiffl->tile_w;
  int64_t th = tiffl->tile_h;

  // cache
  struct _openslide_cache_entry *cache_entry;
  uint32_t *tiledata = _openslide_cache_get(osr->cache,
                                            level, tile_col, tile_row,
                                            &cache_entry);
  if (!tiledata) {
    tiledata = g_slice_alloc(tw * th * 4);
    if (!_openslide_tiff_read_tile(tiffl, tiff,
                                   tiledata, tile_col, tile_row,
                                   err)) {
      g_slice_free1(tw * th * 4, tiledata);
      return false;
    }

    // clip, if necessary
    if (!_openslide_tiff_clip_tile(tiffl, tiledata,
                                   tile_col, tile_row,
                                   err)) {
      g_slice_free1(tw * th * 4, tiledata);
      return false;
    }

    // put it in the cache
    _openslide_cache_put(osr->cache, level, tile_col, tile_row,
                         tiledata, tw * th * 4,
                         &cache_entry);
  }

  // draw it
  cairo_surface_t *surface = cairo_image_surface_create_for_data((unsigned char *) tiledata,
                                                                 CAIRO_FORMAT_ARGB32,
                                                                 tw, th,
                                                                 tw * 4);
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_surface_destroy(surface);
  cairo_paint(cr);

  // done with the cache entry, release it
  _openslide_cache_entry_unref(cache_entry);

  return true;
}

static bool paint_region(openslide_t *osr, cairo_t *cr,
                         int64_t x, int64_t y,
                         struct _openslide_level *level,
                         int32_t w, int32_t h,
                         GError **err) {
  struct huron_ops_data *data = osr->data;
  struct level *l = (struct level *) level;

  TIFF *tiff = _openslide_tiffcache_get(data->tc, err);
  if (tiff == NULL) {
    return false;
  }

  bool success = _openslide_grid_paint_region(l->grid, cr, tiff,
                                              x / l->base.downsample,
                                              y / l->base.downsample,
                                              level, w, h,
                                              err);
  _openslide_tiffcache_put(data->tc, tiff);

  return success;
}

static const struct _openslide_ops huron_ops = {
  .paint_region = paint_region,
  .destroy = destroy,
};

static bool huron_detect(const char *filename G_GNUC_UNUSED,
                         struct _openslide_tifflike *tl,
                         GError **err) {
  // ensure we have a TIFF
  if (!tl) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Not a TIFF file");
    return false;
  }

  // ensure TIFF is tiled
  if (!_openslide_tifflike_is_tiled(tl, 0)) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "TIFF is not tiled");
    return false;
  }

  return true;

  // check manufacturer name
  const char *maker = _openslide_tifflike_get_buffer(tl, 0,
                                                     TIFFTAG_MAKE,
                                                     err);
  if (!maker) {
    return false;
  }
  if (!g_str_has_prefix(maker, HURON_MAKER)) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Not a Huron slide");
    
    return false;
  }

  // check scanner model
  const char *model = _openslide_tifflike_get_buffer(tl, 0,
                                                     TIFFTAG_MODEL,
                                                     err);
  if (!model) {
    return false;
  }
  if (!g_str_has_prefix(model, HURON_MODEL)) {
    g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                "Not valid scanner model");
    return false;
  }
  return true;
}

static int width_compare(gconstpointer a, gconstpointer b) {
  const struct level *la = *(const struct level **) a;
  const struct level *lb = *(const struct level **) b;

  if (la->tiffl.image_w > lb->tiffl.image_w) {
    return -1;
  } else if (la->tiffl.image_w == lb->tiffl.image_w) {
    return 0;
  } else {
    return 1;
  }
}

static void huron_set_resolution_prop(openslide_t *osr,
                                     struct _openslide_tifflike *tl,
                                     int64_t dir, int32_t tag,
                                     const char *property_name) {
  GError *tmp_err = NULL;
  uint64_t unit = _openslide_tifflike_get_uint(tl, dir,
                                               TIFFTAG_RESOLUTIONUNIT,
                                               &tmp_err);
  if (g_error_matches(tmp_err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_NO_VALUE)) {
    unit = RESUNIT_INCH;  // default
    g_clear_error(&tmp_err);
  } else if (tmp_err) {
    g_clear_error(&tmp_err);
    return;
  }

  double res = _openslide_tifflike_get_float(tl, dir, tag, &tmp_err);
  if (!tmp_err && unit == RESUNIT_CENTIMETER) {
    g_hash_table_insert(osr->properties,
                        g_strdup(property_name),
                        _openslide_format_double(10000.0 / res));
  }
  g_clear_error(&tmp_err);
}

static void huron_set_props(openslide_t *osr,
                            struct _openslide_tifflike *tl,
                            int64_t dir) {
  // MPP
  huron_set_resolution_prop(osr, tl, dir, TIFFTAG_XRESOLUTION,
                            OPENSLIDE_PROPERTY_NAME_MPP_X);

}

static bool huron_open(openslide_t *osr,
                       const char *filename,
                       struct _openslide_tifflike *tl,
                       struct _openslide_hash *quickhash1,
                       GError **err) {
  GPtrArray *level_array = g_ptr_array_new();

  // open TIFF
  struct _openslide_tiffcache *tc = _openslide_tiffcache_create(filename);
  TIFF *tiff = _openslide_tiffcache_get(tc, err);
  if (!tiff) {
    goto FAIL;
  }

  // accumulate tiled levels
  do {
    // get directory
    tdir_t dir = TIFFCurrentDirectory(tiff);

    uint32_t subfiletype = 0;
    if (!TIFFGetField(tiff, TIFFTAG_SUBFILETYPE, &subfiletype)) {
      fprintf(stderr, "failed to fetch subfiletype at dir %d\n", dir);
      continue;
    }

    // non-tile flat-image
    if (!TIFFIsTiled(tiff)) {
      // read image dimension
      int64_t iw = 0, ih = 0, ir = 0;
      GET_FIELD_OR_FAIL(tiff, TIFFTAG_IMAGEWIDTH, uint32_t, iw);
      GET_FIELD_OR_FAIL(tiff, TIFFTAG_IMAGELENGTH, uint32_t, ih);
      GET_FIELD_OR_FAIL(tiff, TIFFTAG_ROWSPERSTRIP, uint32_t, ir);

      if ((ir != 1) || (iw <= 0) || (ih <= 0)) {
        continue;
      }

      // read ImageDescription
      char *image_desc;
      if (!TIFFGetField(tiff, TIFFTAG_IMAGEDESCRIPTION, &image_desc)) {
        continue;
      }
      image_desc = g_strstrip(image_desc); // Removes leading and trailing whitespace from a string

      if ((dir == 1) && (subfiletype == 0)) {
        if (!_openslide_tiff_add_associated_image(osr, "thumbnail", tc, dir, err)) {
          goto FAIL;
        }
      } else if (image_desc && g_str_has_prefix(image_desc, LABEL_DESCRIPTION)) {
        if (!_openslide_tiff_add_associated_image(osr, "label", tc, dir, err)) {
          goto FAIL;
        }
      } else if (image_desc && g_str_has_prefix(image_desc, MACRO_DESCRIPTION)) {
        if (!_openslide_tiff_add_associated_image(osr, "macro", tc, dir, err)) {
          goto FAIL;
        }
      } else {
        // fprintf(stderr, "unsupported sub-type %d at dir %d\n", subfiletype, dir);
        //g_debug("unsupported sub-type %d at dir %d", subfiletype, dir);
      }

      // end of non-tile handling
      continue;
    }

    // handle tiled directory now, ignore subtype

    // verify that we can read this compression (hard fail if not)
    uint16_t compression;
    if (!TIFFGetField(tiff, TIFFTAG_COMPRESSION, &compression)) {
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                  "Can't read compression scheme");
      goto FAIL;
    };
    if (!TIFFIsCODECConfigured(compression)) {
      g_set_error(err, OPENSLIDE_ERROR, OPENSLIDE_ERROR_FAILED,
                  "Unsupported TIFF compression: %u", compression);
      goto FAIL;
    }

    // create level
    struct level *l = g_slice_new0(struct level);
    struct _openslide_tiff_level *tiffl = &l->tiffl;
    if (!_openslide_tiff_level_init(tiff,
                                    TIFFCurrentDirectory(tiff),
                                    (struct _openslide_level *) l,
                                    tiffl,
                                    err)) {
      g_slice_free(struct level, l);
      goto FAIL;
    }
    l->grid = _openslide_grid_create_simple(osr,
                                            tiffl->tiles_across,
                                            tiffl->tiles_down,
                                            tiffl->tile_w,
                                            tiffl->tile_h,
                                            read_tile);

    // add to array
    g_ptr_array_add(level_array, l);
  } while (TIFFReadDirectory(tiff));

  // sort tiled levels
  g_ptr_array_sort(level_array, width_compare);

  // set hash and properties
  struct level *top_level = level_array->pdata[level_array->len - 1];
  if (!_openslide_tifflike_init_properties_and_hash(osr, tl, quickhash1,
                                                    top_level->tiffl.dir,
                                                    0,
                                                    err)) {
    goto FAIL;
  }

  // unwrap level array
  int32_t level_count = level_array->len;
  struct level **levels =
    (struct level **) g_ptr_array_free(level_array, false);
  level_array = NULL;

  // allocate private data
  struct huron_ops_data *data =
    g_slice_new0(struct huron_ops_data);

  // store osr data
  g_assert(osr->data == NULL);
  g_assert(osr->levels == NULL);
  osr->levels = (struct _openslide_level **) levels;
  osr->level_count = level_count;
  osr->data = data;
  osr->ops = &huron_ops;

  // put TIFF handle and store tiffcache reference
  _openslide_tiffcache_put(tc, tiff);
  data->tc = tc;

  huron_set_props(osr, tl, 0);

  return true;

 FAIL:
  // free the level array
  if (level_array) {
    for (uint32_t n = 0; n < level_array->len; n++) {
      struct level *l = level_array->pdata[n];
      _openslide_grid_destroy(l->grid);
      g_slice_free(struct level, l);
    }
    g_ptr_array_free(level_array, true);
  }
  // free TIFF
  _openslide_tiffcache_put(tc, tiff);
  _openslide_tiffcache_destroy(tc);
  return false;
}

const struct _openslide_format _openslide_format_huron = {
  .name = "huron",
  .vendor = "huron",
  .detect = huron_detect,
  .open = huron_open,
};