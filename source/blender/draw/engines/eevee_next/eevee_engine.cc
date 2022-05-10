/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2021 Blender Foundation.
 */

#include "BKE_global.h"
#include "BLI_rect.h"

#include "GPU_framebuffer.h"

#include "ED_view3d.h"

#include "DRW_render.h"

#include "eevee_engine.h" /* Own include. */

#include "eevee_instance.hh"

using namespace blender;

struct EEVEE_Data {
  DrawEngineType *engine_type;
  DRWViewportEmptyList *fbl;
  DRWViewportEmptyList *txl;
  DRWViewportEmptyList *psl;
  DRWViewportEmptyList *stl;
  eevee::Instance *instance;
};

static void eevee_engine_init(void *vedata)
{
  EEVEE_Data *ved = reinterpret_cast<EEVEE_Data *>(vedata);
  if (ved->instance == nullptr) {
    ved->instance = new eevee::Instance();
  }

  const DRWContextState *ctx_state = DRW_context_state_get();
  Depsgraph *depsgraph = ctx_state->depsgraph;
  Scene *scene = ctx_state->scene;
  View3D *v3d = ctx_state->v3d;
  const ARegion *region = ctx_state->region;
  RegionView3D *rv3d = ctx_state->rv3d;

  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();
  int2 size = int2(GPU_texture_width(dtxl->color), GPU_texture_height(dtxl->color));

  const DRWView *default_view = DRW_view_default_get();

  Object *camera = nullptr;
  /* Get render borders. */
  rcti rect;
  BLI_rcti_init(&rect, 0, size[0], 0, size[1]);
  if (v3d) {
    if (rv3d && (rv3d->persp == RV3D_CAMOB)) {
      camera = v3d->camera;
    }

    if (v3d->flag2 & V3D_RENDER_BORDER) {
      if (camera) {
        rctf viewborder;
        /* TODO(fclem) Might be better to get it from DRW. */
        ED_view3d_calc_camera_border(scene, depsgraph, region, v3d, rv3d, &viewborder, false);
        float viewborder_sizex = BLI_rctf_size_x(&viewborder);
        float viewborder_sizey = BLI_rctf_size_y(&viewborder);
        rect.xmin = floorf(viewborder.xmin + (scene->r.border.xmin * viewborder_sizex));
        rect.ymin = floorf(viewborder.ymin + (scene->r.border.ymin * viewborder_sizey));
        rect.xmax = floorf(viewborder.xmin + (scene->r.border.xmax * viewborder_sizex));
        rect.ymax = floorf(viewborder.ymin + (scene->r.border.ymax * viewborder_sizey));
      }
      else {
        rect.xmin = v3d->render_border.xmin * size[0];
        rect.ymin = v3d->render_border.ymin * size[1];
        rect.xmax = v3d->render_border.xmax * size[0];
        rect.ymax = v3d->render_border.ymax * size[1];
      }
    }
  }

  ved->instance->init(
      size, &rect, nullptr, depsgraph, nullptr, camera, nullptr, default_view, v3d, rv3d);
}

static void eevee_draw_scene(void *vedata)
{
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
  reinterpret_cast<EEVEE_Data *>(vedata)->instance->draw_viewport(dfbl);
}

static void eevee_cache_init(void *vedata)
{
  reinterpret_cast<EEVEE_Data *>(vedata)->instance->begin_sync();
}

static void eevee_cache_populate(void *vedata, Object *object)
{
  reinterpret_cast<EEVEE_Data *>(vedata)->instance->object_sync(object);
}

static void eevee_cache_finish(void *vedata)
{
  reinterpret_cast<EEVEE_Data *>(vedata)->instance->end_sync();
}

static void eevee_engine_free()
{
}

static void eevee_instance_free(void *instance)
{
  delete reinterpret_cast<eevee::Instance *>(instance);
}

static void eevee_render_to_image(void *UNUSED(vedata),
                                  struct RenderEngine *engine,
                                  struct RenderLayer *layer,
                                  const struct rcti *UNUSED(rect))
{
  UNUSED_VARS(engine, layer);
}

static void eevee_render_update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer)
{
  UNUSED_VARS(engine, scene, view_layer);
}

static const DrawEngineDataSize eevee_data_size = DRW_VIEWPORT_DATA_SIZE(EEVEE_Data);

extern "C" {

DrawEngineType draw_engine_eevee_next_type = {
    nullptr,
    nullptr,
    N_("Eevee"),
    &eevee_data_size,
    &eevee_engine_init,
    &eevee_engine_free,
    &eevee_instance_free,
    &eevee_cache_init,
    &eevee_cache_populate,
    &eevee_cache_finish,
    &eevee_draw_scene,
    nullptr,
    nullptr,
    &eevee_render_to_image,
    nullptr,
};

RenderEngineType DRW_engine_viewport_eevee_next_type = {
    nullptr,
    nullptr,
    "BLENDER_EEVEE_NEXT",
    N_("Eevee Next"),
    RE_INTERNAL | RE_USE_PREVIEW | RE_USE_STEREO_VIEWPORT | RE_USE_GPU_CONTEXT,
    nullptr,
    &DRW_render_to_image,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &eevee_render_update_passes,
    &draw_engine_eevee_next_type,
    {nullptr, nullptr, nullptr},
};
}