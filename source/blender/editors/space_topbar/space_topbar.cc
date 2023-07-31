/* SPDX-FileCopyrightText: 2017 Blender Foundation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup sptopbar
 */

#include <cstdio>
#include <cstring>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "BKE_blendfile.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_screen.h"
#include "BKE_undo_system.h"

#include "ED_screen.h"
#include "ED_space_api.h"

#include "UI_interface.h"
#include "UI_resources.h"
#include "UI_view2d.h"

#include "BLO_read_write.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_message.h"
#include "WM_types.h"

/* ******************** default callbacks for topbar space ***************** */

static SpaceLink *topbar_create(const ScrArea * /*area*/, const Scene * /*scene*/)
{
  ARegion *region;
  SpaceTopBar *stopbar;

  stopbar = static_cast<SpaceTopBar *>(MEM_callocN(sizeof(*stopbar), "init topbar"));
  stopbar->spacetype = SPACE_TOPBAR;

  /* header */
  region = static_cast<ARegion *>(MEM_callocN(sizeof(ARegion), "left aligned header for topbar"));
  BLI_addtail(&stopbar->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = RGN_ALIGN_TOP;
  region = static_cast<ARegion *>(MEM_callocN(sizeof(ARegion), "right aligned header for topbar"));
  BLI_addtail(&stopbar->regionbase, region);
  region->regiontype = RGN_TYPE_HEADER;
  region->alignment = RGN_ALIGN_RIGHT | RGN_SPLIT_PREV;

  /* main regions */
  region = static_cast<ARegion *>(MEM_callocN(sizeof(ARegion), "main region of topbar"));
  BLI_addtail(&stopbar->regionbase, region);
  region->regiontype = RGN_TYPE_WINDOW;

  return (SpaceLink *)stopbar;
}

/* Doesn't free the space-link itself. */
static void topbar_free(SpaceLink * /*sl*/) {}

/* spacetype; init callback */
static void topbar_init(wmWindowManager * /*wm*/, ScrArea * /*area*/) {}

static SpaceLink *topbar_duplicate(SpaceLink *sl)
{
  SpaceTopBar *stopbarn = static_cast<SpaceTopBar *>(MEM_dupallocN(sl));

  /* clear or remove stuff from old */

  return (SpaceLink *)stopbarn;
}

/* add handlers, stuff you only do once or on area/region changes */
static void topbar_main_region_init(wmWindowManager *wm, ARegion *region)
{
  wmKeyMap *keymap;

  /* force delayed UI_view2d_region_reinit call */
  if (ELEM(RGN_ALIGN_ENUM_FROM_MASK(region->alignment), RGN_ALIGN_RIGHT)) {
    region->flag |= RGN_FLAG_DYNAMIC_SIZE;
  }
  UI_view2d_region_reinit(&region->v2d, V2D_COMMONVIEW_HEADER, region->winx, region->winy);

  keymap = WM_keymap_ensure(wm->defaultconf, "View2D Buttons List", 0, 0);
  WM_event_add_keymap_handler(&region->handlers, keymap);
}

static void topbar_operatortypes() {}

static void topbar_keymap(wmKeyConfig * /*keyconf*/) {}

/* add handlers, stuff you only do once or on area/region changes */
static void topbar_header_region_init(wmWindowManager * /*wm*/, ARegion *region)
{
  if (RGN_ALIGN_ENUM_FROM_MASK(region->alignment) == RGN_ALIGN_RIGHT) {
    region->flag |= RGN_FLAG_DYNAMIC_SIZE;
  }
  ED_region_header_init(region);
}

static void topbar_main_region_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_WM:
      if (wmn->data == ND_HISTORY) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SCENE:
      if (wmn->data == ND_MODE) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_VIEW3D) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_GPENCIL:
      if (wmn->data == ND_DATA) {
        ED_region_tag_redraw(region);
      }
      break;
  }
}

static void topbar_header_listener(const wmRegionListenerParams *params)
{
  ARegion *region = params->region;
  const wmNotifier *wmn = params->notifier;

  /* context changes */
  switch (wmn->category) {
    case NC_WM:
      if (wmn->data == ND_JOB) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_WORKSPACE:
      ED_region_tag_redraw(region);
      break;
    case NC_SPACE:
      if (wmn->data == ND_SPACE_INFO) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SCREEN:
      if (wmn->data == ND_LAYER) {
        ED_region_tag_redraw(region);
      }
      break;
    case NC_SCENE:
      if (wmn->data == ND_SCENEBROWSE) {
        ED_region_tag_redraw(region);
      }
      break;
  }
}

static void topbar_header_region_message_subscribe(const wmRegionMessageSubscribeParams *params)
{
  wmMsgBus *mbus = params->message_bus;
  WorkSpace *workspace = params->workspace;
  ARegion *region = params->region;

  wmMsgSubscribeValue msg_sub_value_region_tag_redraw{};
  msg_sub_value_region_tag_redraw.owner = region;
  msg_sub_value_region_tag_redraw.user_data = region;
  msg_sub_value_region_tag_redraw.notify = ED_region_do_msg_notify_tag_redraw;

  WM_msg_subscribe_rna_prop(
      mbus, &workspace->id, workspace, WorkSpace, tools, &msg_sub_value_region_tag_redraw);
}

static void recent_files_menu_draw(const bContext * /*C*/, Menu *menu)
{
  RecentFile *recent;
  uiLayout *layout = menu->layout;
  uiLayoutSetOperatorContext(layout, WM_OP_INVOKE_DEFAULT);
  if (!BLI_listbase_is_empty(&G.recent_files)) {
    for (recent = static_cast<RecentFile *>(G.recent_files.first); (recent); recent = recent->next)
    {
      const char *file = BLI_path_basename(recent->filepath);
      const int icon = BKE_blendfile_extension_check(file) ? ICON_FILE_BLEND : ICON_FILE_BACKUP;
      PointerRNA ptr;
      uiItemFullO(layout,
                  "WM_OT_open_mainfile",
                  file,
                  icon,
                  nullptr,
                  WM_OP_INVOKE_DEFAULT,
                  UI_ITEM_NONE,
                  &ptr);
      RNA_string_set(&ptr, "filepath", recent->filepath);
      RNA_boolean_set(&ptr, "display_file_selector", false);
    }
  }
  else {
    uiItemL(layout, IFACE_("No Recent Files"), ICON_NONE);
  }
}

static void recent_files_menu_register()
{
  MenuType *mt;

  mt = static_cast<MenuType *>(MEM_callocN(sizeof(MenuType), "spacetype info menu recent files"));
  STRNCPY(mt->idname, "TOPBAR_MT_file_open_recent");
  STRNCPY(mt->label, N_("Open Recent"));
  STRNCPY(mt->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
  mt->draw = recent_files_menu_draw;
  WM_menutype_add(mt);
}

static void undo_history_draw_menu(const bContext *C, Menu *menu)
{
  wmWindowManager *wm = CTX_wm_manager(C);
  if (wm->undo_stack == nullptr) {
    return;
  }

  int undo_step_count = 0;
  int undo_step_count_all = 0;
  for (UndoStep *us = static_cast<UndoStep *>(wm->undo_stack->steps.last); us; us = us->prev) {
    undo_step_count_all += 1;
    if (us->skip) {
      continue;
    }
    undo_step_count += 1;
  }

  uiLayout *split = uiLayoutSplit(menu->layout, 0.0f, false);
  uiLayout *column = nullptr;

  const int col_size = 20 + (undo_step_count / 12);

  undo_step_count = 0;

  /* Reverse the order so the most recent state is first in the menu. */
  int i = undo_step_count_all - 1;
  for (UndoStep *us = static_cast<UndoStep *>(wm->undo_stack->steps.last); us; us = us->prev, i--)
  {
    if (us->skip) {
      continue;
    }
    if (!(undo_step_count % col_size)) {
      column = uiLayoutColumn(split, false);
    }
    const bool is_active = (us == wm->undo_stack->step_active);
    uiLayout *row = uiLayoutRow(column, false);
    uiLayoutSetEnabled(row, !is_active);
    uiItemIntO(row,
               CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, us->name),
               is_active ? ICON_LAYER_ACTIVE : ICON_NONE,
               "ED_OT_undo_history",
               "item",
               i);
    undo_step_count += 1;
  }
}

static void undo_history_menu_register()
{
  MenuType *mt;

  mt = static_cast<MenuType *>(MEM_callocN(sizeof(MenuType), __func__));
  STRNCPY(mt->idname, "TOPBAR_MT_undo_history");
  STRNCPY(mt->label, N_("Undo History"));
  STRNCPY(mt->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);
  mt->draw = undo_history_draw_menu;
  WM_menutype_add(mt);
}

static void topbar_space_blend_write(BlendWriter *writer, SpaceLink *sl)
{
  BLO_write_struct(writer, SpaceTopBar, sl);
}

void ED_spacetype_topbar()
{
  SpaceType *st = static_cast<SpaceType *>(MEM_callocN(sizeof(SpaceType), "spacetype topbar"));
  ARegionType *art;

  st->spaceid = SPACE_TOPBAR;
  STRNCPY(st->name, "Top Bar");

  st->create = topbar_create;
  st->free = topbar_free;
  st->init = topbar_init;
  st->duplicate = topbar_duplicate;
  st->operatortypes = topbar_operatortypes;
  st->keymap = topbar_keymap;
  st->blend_write = topbar_space_blend_write;

  /* regions: main window */
  art = static_cast<ARegionType *>(
      MEM_callocN(sizeof(ARegionType), "spacetype topbar main region"));
  art->regionid = RGN_TYPE_WINDOW;
  art->init = topbar_main_region_init;
  art->layout = ED_region_header_layout;
  art->draw = ED_region_header_draw;
  art->listener = topbar_main_region_listener;
  art->prefsizex = UI_UNIT_X * 5; /* Mainly to avoid glitches */
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;

  BLI_addhead(&st->regiontypes, art);

  /* regions: header */
  art = static_cast<ARegionType *>(
      MEM_callocN(sizeof(ARegionType), "spacetype topbar header region"));
  art->regionid = RGN_TYPE_HEADER;
  art->prefsizey = HEADERY;
  art->prefsizex = UI_UNIT_X * 5; /* Mainly to avoid glitches */
  art->keymapflag = ED_KEYMAP_UI | ED_KEYMAP_VIEW2D | ED_KEYMAP_HEADER;
  art->listener = topbar_header_listener;
  art->message_subscribe = topbar_header_region_message_subscribe;
  art->init = topbar_header_region_init;
  art->layout = ED_region_header_layout;
  art->draw = ED_region_header_draw;

  BLI_addhead(&st->regiontypes, art);

  recent_files_menu_register();
  undo_history_menu_register();

  BKE_spacetype_register(st);
}