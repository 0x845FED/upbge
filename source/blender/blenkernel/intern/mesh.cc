/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup bke
 */

#include "MEM_guardedalloc.h"

/* Allow using deprecated functionality for .blend file I/O. */
#define DNA_DEPRECATED_ALLOW

#include "DNA_defaults.h"
#include "DNA_key_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"

#include "BLI_bounds.hh"
#include "BLI_endian_switch.h"
#include "BLI_ghash.h"
#include "BLI_hash.h"
#include "BLI_implicit_sharing.hh"
#include "BLI_index_range.hh"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math_matrix.h"
#include "BLI_math_vector.hh"
#include "BLI_memarena.h"
#include "BLI_ordered_edge.hh"
#include "BLI_resource_scope.hh"
#include "BLI_set.hh"
#include "BLI_span.hh"
#include "BLI_string.h"
#include "BLI_task.hh"
#include "BLI_utildefines.h"
#include "BLI_vector.hh"
#include "BLI_virtual_array.hh"

#include "BLT_translation.h"

#include "BKE_anim_data.h"
#include "BKE_attribute.hh"
#include "BKE_bpath.h"
#include "BKE_deform.h"
#include "BKE_editmesh.hh"
#include "BKE_editmesh_cache.hh"
#include "BKE_global.h"
#include "BKE_idtype.h"
#include "BKE_key.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_main.hh"
#include "BKE_material.h"
#include "BKE_mesh.hh"
#include "BKE_mesh_legacy_convert.hh"
#include "BKE_mesh_runtime.hh"
#include "BKE_mesh_wrapper.hh"
#include "BKE_modifier.hh"
#include "BKE_multires.hh"
#include "BKE_object.hh"

#include "PIL_time.h"

#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_query.hh"

#include "BLO_read_write.hh"

using blender::float3;
using blender::MutableSpan;
using blender::Span;
using blender::StringRef;
using blender::VArray;
using blender::Vector;

static void mesh_tessface_clear_intern(Mesh *mesh, int free_customdata);

static void mesh_init_data(ID *id)
{
  Mesh *mesh = reinterpret_cast<Mesh *>(id);

  BLI_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(mesh, id));

  MEMCPY_STRUCT_AFTER(mesh, DNA_struct_default_get(Mesh), id);

  CustomData_reset(&mesh->vert_data);
  CustomData_reset(&mesh->edge_data);
  CustomData_reset(&mesh->fdata_legacy);
  CustomData_reset(&mesh->face_data);
  CustomData_reset(&mesh->loop_data);

  mesh->runtime = new blender::bke::MeshRuntime();

  mesh->face_sets_color_seed = BLI_hash_int(PIL_check_seconds_timer_i() & UINT_MAX);
}

static void mesh_copy_data(Main *bmain, ID *id_dst, const ID *id_src, const int flag)
{
  Mesh *mesh_dst = reinterpret_cast<Mesh *>(id_dst);
  const Mesh *mesh_src = reinterpret_cast<const Mesh *>(id_src);

  mesh_dst->runtime = new blender::bke::MeshRuntime();
  mesh_dst->runtime->deformed_only = mesh_src->runtime->deformed_only;
  mesh_dst->runtime->wrapper_type = mesh_src->runtime->wrapper_type;
  mesh_dst->runtime->wrapper_type_finalize = mesh_src->runtime->wrapper_type_finalize;
  mesh_dst->runtime->subsurf_runtime_data = mesh_src->runtime->subsurf_runtime_data;
  mesh_dst->runtime->cd_mask_extra = mesh_src->runtime->cd_mask_extra;
  /* Copy face dot tags and edge tags, since meshes may be duplicated after a subsurf modifier or
   * node, but we still need to be able to draw face center vertices and "optimal edges"
   * differently. The tags may be cleared explicitly when the topology is changed. */
  mesh_dst->runtime->subsurf_face_dot_tags = mesh_src->runtime->subsurf_face_dot_tags;
  mesh_dst->runtime->subsurf_optimal_display_edges =
      mesh_src->runtime->subsurf_optimal_display_edges;
  if ((mesh_src->id.tag & LIB_TAG_NO_MAIN) == 0) {
    /* This is a direct copy of a main mesh, so for now it has the same topology. */
    mesh_dst->runtime->deformed_only = true;
  }
  /* This option is set for run-time meshes that have been copied from the current object's mode.
   * Currently this is used for edit-mesh although it could be used for sculpt or other
   * kinds of data specific to an object's mode.
   *
   * The flag signals that the mesh hasn't been modified from the data that generated it,
   * allowing us to use the object-mode data for drawing.
   *
   * While this could be the caller's responsibility, keep here since it's
   * highly unlikely we want to create a duplicate and not use it for drawing. */
  mesh_dst->runtime->is_original_bmesh = false;

  /* Share various derived caches between the source and destination mesh for improved performance
   * when the source is persistent and edits to the destination mesh don't affect the caches.
   * Caches will be "un-shared" as necessary later on. */
  mesh_dst->runtime->bounds_cache = mesh_src->runtime->bounds_cache;
  mesh_dst->runtime->vert_normals_cache = mesh_src->runtime->vert_normals_cache;
  mesh_dst->runtime->face_normals_cache = mesh_src->runtime->face_normals_cache;
  mesh_dst->runtime->loose_verts_cache = mesh_src->runtime->loose_verts_cache;
  mesh_dst->runtime->verts_no_face_cache = mesh_src->runtime->verts_no_face_cache;
  mesh_dst->runtime->loose_edges_cache = mesh_src->runtime->loose_edges_cache;
  mesh_dst->runtime->looptris_cache = mesh_src->runtime->looptris_cache;
  mesh_dst->runtime->looptri_faces_cache = mesh_src->runtime->looptri_faces_cache;
  mesh_dst->runtime->vert_to_face_offset_cache = mesh_src->runtime->vert_to_face_offset_cache;
  mesh_dst->runtime->vert_to_face_map_cache = mesh_src->runtime->vert_to_face_map_cache;
  mesh_dst->runtime->vert_to_corner_map_cache = mesh_src->runtime->vert_to_corner_map_cache;
  mesh_dst->runtime->corner_to_face_map_cache = mesh_src->runtime->corner_to_face_map_cache;

  /* Only do tessface if we have no faces. */
  const bool do_tessface = ((mesh_src->totface_legacy != 0) && (mesh_src->faces_num == 0));

  CustomData_MeshMasks mask = CD_MASK_MESH;

  if (mesh_src->id.tag & LIB_TAG_NO_MAIN) {
    /* For copies in depsgraph, keep data like #CD_ORIGINDEX and #CD_ORCO. */
    CustomData_MeshMasks_update(&mask, &CD_MASK_DERIVEDMESH);
  }

  mesh_dst->mat = (Material **)MEM_dupallocN(mesh_src->mat);

  BKE_defgroup_copy_list(&mesh_dst->vertex_group_names, &mesh_src->vertex_group_names);
  mesh_dst->active_color_attribute = static_cast<char *>(
      MEM_dupallocN(mesh_src->active_color_attribute));
  mesh_dst->default_color_attribute = static_cast<char *>(
      MEM_dupallocN(mesh_src->default_color_attribute));

  CustomData_copy(&mesh_src->vert_data, &mesh_dst->vert_data, mask.vmask, mesh_dst->totvert);
  CustomData_copy(&mesh_src->edge_data, &mesh_dst->edge_data, mask.emask, mesh_dst->totedge);
  CustomData_copy(&mesh_src->loop_data, &mesh_dst->loop_data, mask.lmask, mesh_dst->totloop);
  CustomData_copy(&mesh_src->face_data, &mesh_dst->face_data, mask.pmask, mesh_dst->faces_num);
  blender::implicit_sharing::copy_shared_pointer(mesh_src->face_offset_indices,
                                                 mesh_src->runtime->face_offsets_sharing_info,
                                                 &mesh_dst->face_offset_indices,
                                                 &mesh_dst->runtime->face_offsets_sharing_info);
  if (do_tessface) {
    CustomData_copy(
        &mesh_src->fdata_legacy, &mesh_dst->fdata_legacy, mask.fmask, mesh_dst->totface_legacy);
  }
  else {
    mesh_tessface_clear_intern(mesh_dst, false);
  }

  mesh_dst->edit_mesh = nullptr;

  mesh_dst->mselect = (MSelect *)MEM_dupallocN(mesh_dst->mselect);

  /* TODO: Do we want to add flag to prevent this? */
  if (mesh_src->key && (flag & LIB_ID_COPY_SHAPEKEY)) {
    BKE_id_copy_ex(bmain, &mesh_src->key->id, (ID **)&mesh_dst->key, flag);
    /* XXX This is not nice, we need to make BKE_id_copy_ex fully re-entrant... */
    mesh_dst->key->from = &mesh_dst->id;
  }
}

void BKE_mesh_free_editmesh(Mesh *mesh)
{
  if (mesh->edit_mesh == nullptr) {
    return;
  }

  if (mesh->edit_mesh->is_shallow_copy == false) {
    BKE_editmesh_free_data(mesh->edit_mesh);
  }
  MEM_freeN(mesh->edit_mesh);
  mesh->edit_mesh = nullptr;
}

static void mesh_free_data(ID *id)
{
  Mesh *mesh = reinterpret_cast<Mesh *>(id);

  BKE_mesh_free_editmesh(mesh);

  BKE_mesh_clear_geometry_and_metadata(mesh);
  MEM_SAFE_FREE(mesh->mat);

  delete mesh->runtime;
}

static void mesh_foreach_id(ID *id, LibraryForeachIDData *data)
{
  Mesh *mesh = reinterpret_cast<Mesh *>(id);
  const int flag = BKE_lib_query_foreachid_process_flags_get(data);

  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, mesh->texcomesh, IDWALK_CB_NEVER_SELF);
  BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, mesh->key, IDWALK_CB_USER);
  for (int i = 0; i < mesh->totcol; i++) {
    BKE_LIB_FOREACHID_PROCESS_IDSUPER(data, mesh->mat[i], IDWALK_CB_USER);
  }

  if (flag & IDWALK_DO_DEPRECATED_POINTERS) {
    BKE_LIB_FOREACHID_PROCESS_ID_NOCHECK(data, mesh->ipo, IDWALK_CB_USER);
  }
}

static void mesh_foreach_path(ID *id, BPathForeachPathData *bpath_data)
{
  Mesh *me = reinterpret_cast<Mesh *>(id);
  if (me->loop_data.external) {
    BKE_bpath_foreach_path_fixed_process(
        bpath_data, me->loop_data.external->filepath, sizeof(me->loop_data.external->filepath));
  }
}

static void mesh_blend_write(BlendWriter *writer, ID *id, const void *id_address)
{
  using namespace blender;
  using namespace blender::bke;
  Mesh *mesh = reinterpret_cast<Mesh *>(id);
  const bool is_undo = BLO_write_is_undo(writer);

  Vector<CustomDataLayer, 16> vert_layers;
  Vector<CustomDataLayer, 16> edge_layers;
  Vector<CustomDataLayer, 16> loop_layers;
  Vector<CustomDataLayer, 16> face_layers;

  /* Cache only - don't write. */
  mesh->mface = nullptr;
  mesh->totface_legacy = 0;
  memset(&mesh->fdata_legacy, 0, sizeof(mesh->fdata_legacy));

  /* Do not store actual geometry data in case this is a library override ID. */
  if (ID_IS_OVERRIDE_LIBRARY(mesh) && !is_undo) {
    mesh->totvert = 0;
    memset(&mesh->vert_data, 0, sizeof(mesh->vert_data));

    mesh->totedge = 0;
    memset(&mesh->edge_data, 0, sizeof(mesh->edge_data));

    mesh->totloop = 0;
    memset(&mesh->loop_data, 0, sizeof(mesh->loop_data));

    mesh->faces_num = 0;
    memset(&mesh->face_data, 0, sizeof(mesh->face_data));
    mesh->face_offset_indices = nullptr;
  }
  else {
    CustomData_blend_write_prepare(mesh->vert_data, vert_layers, {});
    CustomData_blend_write_prepare(mesh->edge_data, edge_layers, {});
    CustomData_blend_write_prepare(mesh->loop_data, loop_layers, {});
    CustomData_blend_write_prepare(mesh->face_data, face_layers, {});
    if (!is_undo) {
      mesh_sculpt_mask_to_legacy(vert_layers);
    }
  }

  mesh->runtime = nullptr;

  BLO_write_id_struct(writer, Mesh, id_address, &mesh->id);
  BKE_id_blend_write(writer, &mesh->id);

  BKE_defbase_blend_write(writer, &mesh->vertex_group_names);
  BLO_write_string(writer, mesh->active_color_attribute);
  BLO_write_string(writer, mesh->default_color_attribute);

  BLO_write_pointer_array(writer, mesh->totcol, mesh->mat);
  BLO_write_raw(writer, sizeof(MSelect) * mesh->totselect, mesh->mselect);

  CustomData_blend_write(
      writer, &mesh->vert_data, vert_layers, mesh->totvert, CD_MASK_MESH.vmask, &mesh->id);
  CustomData_blend_write(
      writer, &mesh->edge_data, edge_layers, mesh->totedge, CD_MASK_MESH.emask, &mesh->id);
  /* `fdata` is cleared above but written so slots align. */
  CustomData_blend_write(
      writer, &mesh->fdata_legacy, {}, mesh->totface_legacy, CD_MASK_MESH.fmask, &mesh->id);
  CustomData_blend_write(
      writer, &mesh->loop_data, loop_layers, mesh->totloop, CD_MASK_MESH.lmask, &mesh->id);
  CustomData_blend_write(
      writer, &mesh->face_data, face_layers, mesh->faces_num, CD_MASK_MESH.pmask, &mesh->id);

  if (mesh->face_offset_indices) {
    BLO_write_int32_array(writer, mesh->faces_num + 1, mesh->face_offset_indices);
  }
}

static void mesh_blend_read_data(BlendDataReader *reader, ID *id)
{
  Mesh *mesh = reinterpret_cast<Mesh *>(id);
  BLO_read_pointer_array(reader, (void **)&mesh->mat);
  /* This check added for python created meshes. */
  if (!mesh->mat) {
    mesh->totcol = 0;
  }

  /* Deprecated pointers to custom data layers are read here for backward compatibility
   * with files where these were owning pointers rather than a view into custom data. */
  BLO_read_data_address(reader, &mesh->mvert);
  BLO_read_data_address(reader, &mesh->medge);
  BLO_read_data_address(reader, &mesh->mface);
  BLO_read_data_address(reader, &mesh->mtface);
  BLO_read_data_address(reader, &mesh->dvert);
  BLO_read_data_address(reader, &mesh->tface);
  BLO_read_data_address(reader, &mesh->mcol);

  BLO_read_data_address(reader, &mesh->mselect);

  BLO_read_list(reader, &mesh->vertex_group_names);

  CustomData_blend_read(reader, &mesh->vert_data, mesh->totvert);
  CustomData_blend_read(reader, &mesh->edge_data, mesh->totedge);
  CustomData_blend_read(reader, &mesh->fdata_legacy, mesh->totface_legacy);
  CustomData_blend_read(reader, &mesh->loop_data, mesh->totloop);
  CustomData_blend_read(reader, &mesh->face_data, mesh->faces_num);
  if (mesh->deform_verts().is_empty()) {
    /* Vertex group data was also an owning pointer in old Blender versions.
     * Don't read them again if they were read as part of #CustomData. */
    BKE_defvert_blend_read(reader, mesh->totvert, mesh->dvert);
  }
  BLO_read_data_address(reader, &mesh->active_color_attribute);
  BLO_read_data_address(reader, &mesh->default_color_attribute);

  mesh->texspace_flag &= ~ME_TEXSPACE_FLAG_AUTO_EVALUATED;
  mesh->edit_mesh = nullptr;

  mesh->runtime = new blender::bke::MeshRuntime();

  if (mesh->face_offset_indices) {
    BLO_read_int32_array(reader, mesh->faces_num + 1, &mesh->face_offset_indices);
    mesh->runtime->face_offsets_sharing_info = blender::implicit_sharing::info_for_mem_free(
        mesh->face_offset_indices);
  }

  if (mesh->mselect == nullptr) {
    mesh->totselect = 0;
  }

  if (BLO_read_requires_endian_switch(reader) && mesh->tface) {
    TFace *tf = mesh->tface;
    for (int i = 0; i < mesh->totface_legacy; i++, tf++) {
      BLI_endian_switch_uint32_array(tf->col, 4);
    }
  }
}

IDTypeInfo IDType_ID_ME = {
    /*id_code*/ ID_ME,
    /*id_filter*/ FILTER_ID_ME,
    /*main_listbase_index*/ INDEX_ID_ME,
    /*struct_size*/ sizeof(Mesh),
    /*name*/ "Mesh",
    /*name_plural*/ N_("meshes"),
    /*translation_context*/ BLT_I18NCONTEXT_ID_MESH,
    /*flags*/ IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    /*asset_type_info*/ nullptr,

    /*init_data*/ mesh_init_data,
    /*copy_data*/ mesh_copy_data,
    /*free_data*/ mesh_free_data,
    /*make_local*/ nullptr,
    /*foreach_id*/ mesh_foreach_id,
    /*foreach_cache*/ nullptr,
    /*foreach_path*/ mesh_foreach_path,
    /*owner_pointer_get*/ nullptr,

    /*blend_write*/ mesh_blend_write,
    /*blend_read_data*/ mesh_blend_read_data,
    /*blend_read_after_liblink*/ nullptr,

    /*blend_read_undo_preserve*/ nullptr,

    /*lib_override_apply_post*/ nullptr,
};

bool BKE_mesh_attribute_required(const char *name)
{
  return ELEM(StringRef(name), "position", ".corner_vert", ".corner_edge", ".edge_verts");
}

void BKE_mesh_ensure_skin_customdata(Mesh *me)
{
  BMesh *bm = me->edit_mesh ? me->edit_mesh->bm : nullptr;
  MVertSkin *vs;

  if (bm) {
    if (!CustomData_has_layer(&bm->vdata, CD_MVERT_SKIN)) {
      BMVert *v;
      BMIter iter;

      BM_data_layer_add(bm, &bm->vdata, CD_MVERT_SKIN);

      /* Mark an arbitrary vertex as root */
      BM_ITER_MESH (v, &iter, bm, BM_VERTS_OF_MESH) {
        vs = (MVertSkin *)CustomData_bmesh_get(&bm->vdata, v->head.data, CD_MVERT_SKIN);
        vs->flag |= MVERT_SKIN_ROOT;
        break;
      }
    }
  }
  else {
    if (!CustomData_has_layer(&me->vert_data, CD_MVERT_SKIN)) {
      vs = (MVertSkin *)CustomData_add_layer(
          &me->vert_data, CD_MVERT_SKIN, CD_SET_DEFAULT, me->totvert);

      /* Mark an arbitrary vertex as root */
      if (vs) {
        vs->flag |= MVERT_SKIN_ROOT;
      }
    }
  }
}

bool BKE_mesh_has_custom_loop_normals(Mesh *me)
{
  if (me->edit_mesh) {
    return CustomData_has_layer(&me->edit_mesh->bm->ldata, CD_CUSTOMLOOPNORMAL);
  }

  return CustomData_has_layer(&me->loop_data, CD_CUSTOMLOOPNORMAL);
}

void BKE_mesh_free_data_for_undo(Mesh *me)
{
  mesh_free_data(&me->id);
}

/**
 * \note on data that this function intentionally doesn't free:
 *
 * - Materials and shape keys are not freed here (#Mesh.mat & #Mesh.key).
 *   As freeing shape keys requires tagging the depsgraph for updated relations,
 *   which is expensive.
 *   Material slots should be kept in sync with the object.
 *
 * - Edit-Mesh (#Mesh.edit_mesh)
 *   Since edit-mesh is tied to the object's mode, which crashes when called in edit-mode.
 *   See: #90972.
 */
static void mesh_clear_geometry(Mesh &mesh)
{
  CustomData_free(&mesh.vert_data, mesh.totvert);
  CustomData_free(&mesh.edge_data, mesh.totedge);
  CustomData_free(&mesh.fdata_legacy, mesh.totface_legacy);
  CustomData_free(&mesh.loop_data, mesh.totloop);
  CustomData_free(&mesh.face_data, mesh.faces_num);
  if (mesh.face_offset_indices) {
    blender::implicit_sharing::free_shared_data(&mesh.face_offset_indices,
                                                &mesh.runtime->face_offsets_sharing_info);
  }
  MEM_SAFE_FREE(mesh.mselect);

  mesh.totvert = 0;
  mesh.totedge = 0;
  mesh.totface_legacy = 0;
  mesh.totloop = 0;
  mesh.faces_num = 0;
  mesh.act_face = -1;
  mesh.totselect = 0;
}

static void clear_attribute_names(Mesh &mesh)
{
  BLI_freelistN(&mesh.vertex_group_names);
  MEM_SAFE_FREE(mesh.active_color_attribute);
  MEM_SAFE_FREE(mesh.default_color_attribute);
}

void BKE_mesh_clear_geometry(Mesh *mesh)
{
  BKE_mesh_runtime_clear_cache(mesh);
  mesh_clear_geometry(*mesh);
}

void BKE_mesh_clear_geometry_and_metadata(Mesh *mesh)
{
  BKE_mesh_runtime_clear_cache(mesh);
  mesh_clear_geometry(*mesh);
  clear_attribute_names(*mesh);
}

static void mesh_tessface_clear_intern(Mesh *mesh, int free_customdata)
{
  if (free_customdata) {
    CustomData_free(&mesh->fdata_legacy, mesh->totface_legacy);
  }
  else {
    CustomData_reset(&mesh->fdata_legacy);
  }

  mesh->totface_legacy = 0;
}

Mesh *BKE_mesh_add(Main *bmain, const char *name)
{
  return static_cast<Mesh *>(BKE_id_new(bmain, ID_ME, name));
}

void BKE_mesh_face_offsets_ensure_alloc(Mesh *mesh)
{
  BLI_assert(mesh->face_offset_indices == nullptr);
  BLI_assert(mesh->runtime->face_offsets_sharing_info == nullptr);
  if (mesh->faces_num == 0) {
    return;
  }
  mesh->face_offset_indices = static_cast<int *>(
      MEM_malloc_arrayN(mesh->faces_num + 1, sizeof(int), __func__));
  mesh->runtime->face_offsets_sharing_info = blender::implicit_sharing::info_for_mem_free(
      mesh->face_offset_indices);

#ifndef NDEBUG
  /* Fill offsets with obviously bad values to simplify finding missing initialization. */
  mesh->face_offsets_for_write().fill(-1);
#endif
  /* Set common values for convenience. */
  mesh->face_offset_indices[0] = 0;
  mesh->face_offset_indices[mesh->faces_num] = mesh->totloop;
}

MutableSpan<int> Mesh::face_offsets_for_write()
{
  if (this->faces_num == 0) {
    return {};
  }
  blender::implicit_sharing::make_trivial_data_mutable(
      &this->face_offset_indices, &this->runtime->face_offsets_sharing_info, this->faces_num + 1);
  return {this->face_offset_indices, this->faces_num + 1};
}

static void mesh_ensure_cdlayers_primary(Mesh &mesh)
{
  blender::bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  blender::bke::AttributeInitConstruct attribute_init;

  /* Try to create attributes if they do not exist. */
  attributes.add("position", ATTR_DOMAIN_POINT, CD_PROP_FLOAT3, attribute_init);
  attributes.add(".edge_verts", ATTR_DOMAIN_EDGE, CD_PROP_INT32_2D, attribute_init);
  attributes.add(".corner_vert", ATTR_DOMAIN_CORNER, CD_PROP_INT32, attribute_init);
  attributes.add(".corner_edge", ATTR_DOMAIN_CORNER, CD_PROP_INT32, attribute_init);
}

Mesh *BKE_mesh_new_nomain(const int verts_num,
                          const int edges_num,
                          const int faces_num,
                          const int loops_num)
{
  Mesh *mesh = static_cast<Mesh *>(BKE_libblock_alloc(
      nullptr, ID_ME, BKE_idtype_idcode_to_name(ID_ME), LIB_ID_CREATE_LOCALIZE));
  BKE_libblock_init_empty(&mesh->id);

  mesh->totvert = verts_num;
  mesh->totedge = edges_num;
  mesh->faces_num = faces_num;
  mesh->totloop = loops_num;

  mesh_ensure_cdlayers_primary(*mesh);
  BKE_mesh_face_offsets_ensure_alloc(mesh);

  return mesh;
}

static void copy_attribute_names(const Mesh &mesh_src, Mesh &mesh_dst)
{
  if (mesh_src.active_color_attribute) {
    MEM_SAFE_FREE(mesh_dst.active_color_attribute);
    mesh_dst.active_color_attribute = BLI_strdup(mesh_src.active_color_attribute);
  }
  if (mesh_src.default_color_attribute) {
    MEM_SAFE_FREE(mesh_dst.default_color_attribute);
    mesh_dst.default_color_attribute = BLI_strdup(mesh_src.default_color_attribute);
  }
}

void BKE_mesh_copy_parameters(Mesh *me_dst, const Mesh *me_src)
{
  /* Copy general settings. */
  me_dst->editflag = me_src->editflag;
  me_dst->flag = me_src->flag;
  me_dst->remesh_voxel_size = me_src->remesh_voxel_size;
  me_dst->remesh_voxel_adaptivity = me_src->remesh_voxel_adaptivity;
  me_dst->remesh_mode = me_src->remesh_mode;
  me_dst->symmetry = me_src->symmetry;

  me_dst->face_sets_color_seed = me_src->face_sets_color_seed;
  me_dst->face_sets_color_default = me_src->face_sets_color_default;

  /* Copy texture space. */
  me_dst->texspace_flag = me_src->texspace_flag;
  copy_v3_v3(me_dst->texspace_location, me_src->texspace_location);
  copy_v3_v3(me_dst->texspace_size, me_src->texspace_size);

  me_dst->vertex_group_active_index = me_src->vertex_group_active_index;
  me_dst->attributes_active_index = me_src->attributes_active_index;
}

void BKE_mesh_copy_parameters_for_eval(Mesh *me_dst, const Mesh *me_src)
{
  /* User counts aren't handled, don't copy into a mesh from #G_MAIN. */
  BLI_assert(me_dst->id.tag & (LIB_TAG_NO_MAIN | LIB_TAG_COPIED_ON_WRITE));

  BKE_mesh_copy_parameters(me_dst, me_src);
  copy_attribute_names(*me_src, *me_dst);

  /* Copy vertex group names. */
  BLI_assert(BLI_listbase_is_empty(&me_dst->vertex_group_names));
  BKE_defgroup_copy_list(&me_dst->vertex_group_names, &me_src->vertex_group_names);

  /* Copy materials. */
  if (me_dst->mat != nullptr) {
    MEM_freeN(me_dst->mat);
  }
  me_dst->mat = (Material **)MEM_dupallocN(me_src->mat);
  me_dst->totcol = me_src->totcol;
}

Mesh *BKE_mesh_new_nomain_from_template_ex(const Mesh *me_src,
                                           const int verts_num,
                                           const int edges_num,
                                           const int tessface_num,
                                           const int faces_num,
                                           const int loops_num,
                                           const CustomData_MeshMasks mask)
{
  /* Only do tessface if we are creating tessfaces or copying from mesh with only tessfaces. */
  const bool do_tessface = (tessface_num ||
                            ((me_src->totface_legacy != 0) && (me_src->faces_num == 0)));

  Mesh *me_dst = static_cast<Mesh *>(BKE_id_new_nomain(ID_ME, nullptr));

  me_dst->mselect = (MSelect *)MEM_dupallocN(me_src->mselect);

  me_dst->totvert = verts_num;
  me_dst->totedge = edges_num;
  me_dst->faces_num = faces_num;
  me_dst->totloop = loops_num;
  me_dst->totface_legacy = tessface_num;

  BKE_mesh_copy_parameters_for_eval(me_dst, me_src);

  CustomData_copy_layout(
      &me_src->vert_data, &me_dst->vert_data, mask.vmask, CD_SET_DEFAULT, verts_num);
  CustomData_copy_layout(
      &me_src->edge_data, &me_dst->edge_data, mask.emask, CD_SET_DEFAULT, edges_num);
  CustomData_copy_layout(
      &me_src->face_data, &me_dst->face_data, mask.pmask, CD_SET_DEFAULT, faces_num);
  CustomData_copy_layout(
      &me_src->loop_data, &me_dst->loop_data, mask.lmask, CD_SET_DEFAULT, loops_num);
  if (do_tessface) {
    CustomData_copy_layout(
        &me_src->fdata_legacy, &me_dst->fdata_legacy, mask.fmask, CD_SET_DEFAULT, tessface_num);
  }
  else {
    mesh_tessface_clear_intern(me_dst, false);
  }

  /* The destination mesh should at least have valid primary CD layers,
   * even in cases where the source mesh does not. */
  mesh_ensure_cdlayers_primary(*me_dst);
  BKE_mesh_face_offsets_ensure_alloc(me_dst);
  if (do_tessface && !CustomData_get_layer(&me_dst->fdata_legacy, CD_MFACE)) {
    CustomData_add_layer(&me_dst->fdata_legacy, CD_MFACE, CD_SET_DEFAULT, me_dst->totface_legacy);
  }

  return me_dst;
}

Mesh *BKE_mesh_new_nomain_from_template(const Mesh *me_src,
                                        const int verts_num,
                                        const int edges_num,
                                        const int faces_num,
                                        const int loops_num)
{
  return BKE_mesh_new_nomain_from_template_ex(
      me_src, verts_num, edges_num, 0, faces_num, loops_num, CD_MASK_EVERYTHING);
}

void BKE_mesh_eval_delete(Mesh *mesh_eval)
{
  /* Evaluated mesh may point to edit mesh, but never owns it. */
  mesh_eval->edit_mesh = nullptr;
  mesh_free_data(&mesh_eval->id);
  BKE_libblock_free_data(&mesh_eval->id, false);
  MEM_freeN(mesh_eval);
}

Mesh *BKE_mesh_copy_for_eval(const Mesh *source)
{
  return reinterpret_cast<Mesh *>(
      BKE_id_copy_ex(nullptr, &source->id, nullptr, LIB_ID_COPY_LOCALIZE));
}

BMesh *BKE_mesh_to_bmesh_ex(const Mesh *me,
                            const BMeshCreateParams *create_params,
                            const BMeshFromMeshParams *convert_params)
{
  const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_ME(me);

  BMesh *bm = BM_mesh_create(&allocsize, create_params);
  BM_mesh_bm_from_me(bm, me, convert_params);

  return bm;
}

BMesh *BKE_mesh_to_bmesh(Mesh *me,
                         Object *ob,
                         const bool add_key_index,
                         const BMeshCreateParams *params)
{
  BMeshFromMeshParams bmesh_from_mesh_params{};
  bmesh_from_mesh_params.calc_face_normal = false;
  bmesh_from_mesh_params.calc_vert_normal = false;
  bmesh_from_mesh_params.add_key_index = add_key_index;
  bmesh_from_mesh_params.use_shapekey = true;
  bmesh_from_mesh_params.active_shapekey = ob->shapenr;
  return BKE_mesh_to_bmesh_ex(me, params, &bmesh_from_mesh_params);
}

Mesh *BKE_mesh_from_bmesh_nomain(BMesh *bm,
                                 const BMeshToMeshParams *params,
                                 const Mesh *me_settings)
{
  BLI_assert(params->calc_object_remap == false);
  Mesh *mesh = static_cast<Mesh *>(BKE_id_new_nomain(ID_ME, nullptr));
  BM_mesh_bm_to_me(nullptr, bm, mesh, params);
  BKE_mesh_copy_parameters_for_eval(mesh, me_settings);
  return mesh;
}

Mesh *BKE_mesh_from_bmesh_for_eval_nomain(BMesh *bm,
                                          const CustomData_MeshMasks *cd_mask_extra,
                                          const Mesh *me_settings)
{
  Mesh *mesh = static_cast<Mesh *>(BKE_id_new_nomain(ID_ME, nullptr));
  BM_mesh_bm_to_me_for_eval(bm, mesh, cd_mask_extra);
  BKE_mesh_copy_parameters_for_eval(mesh, me_settings);
  return mesh;
}

static void ensure_orig_index_layer(CustomData &data, const int size)
{
  if (CustomData_has_layer(&data, CD_ORIGINDEX)) {
    return;
  }
  int *indices = (int *)CustomData_add_layer(&data, CD_ORIGINDEX, CD_SET_DEFAULT, size);
  range_vn_i(indices, size, 0);
}

void BKE_mesh_ensure_default_orig_index_customdata(Mesh *mesh)
{
  BLI_assert(mesh->runtime->wrapper_type == ME_WRAPPER_TYPE_MDATA);
  BKE_mesh_ensure_default_orig_index_customdata_no_check(mesh);
}

void BKE_mesh_ensure_default_orig_index_customdata_no_check(Mesh *mesh)
{
  ensure_orig_index_layer(mesh->vert_data, mesh->totvert);
  ensure_orig_index_layer(mesh->edge_data, mesh->totedge);
  ensure_orig_index_layer(mesh->face_data, mesh->faces_num);
}

void BKE_mesh_texspace_calc(Mesh *me)
{
  using namespace blender;
  if (me->texspace_flag & ME_TEXSPACE_FLAG_AUTO) {
    const Bounds<float3> bounds = me->bounds_min_max().value_or(
        Bounds(float3(-1.0f), float3(1.0f)));

    float texspace_location[3], texspace_size[3];
    mid_v3_v3v3(texspace_location, bounds.min, bounds.max);

    texspace_size[0] = (bounds.max[0] - bounds.min[0]) / 2.0f;
    texspace_size[1] = (bounds.max[1] - bounds.min[1]) / 2.0f;
    texspace_size[2] = (bounds.max[2] - bounds.min[2]) / 2.0f;

    for (int a = 0; a < 3; a++) {
      if (texspace_size[a] == 0.0f) {
        texspace_size[a] = 1.0f;
      }
      else if (texspace_size[a] > 0.0f && texspace_size[a] < 0.00001f) {
        texspace_size[a] = 0.00001f;
      }
      else if (texspace_size[a] < 0.0f && texspace_size[a] > -0.00001f) {
        texspace_size[a] = -0.00001f;
      }
    }

    copy_v3_v3(me->texspace_location, texspace_location);
    copy_v3_v3(me->texspace_size, texspace_size);

    me->texspace_flag |= ME_TEXSPACE_FLAG_AUTO_EVALUATED;
  }
}

void BKE_mesh_texspace_ensure(Mesh *me)
{
  if ((me->texspace_flag & ME_TEXSPACE_FLAG_AUTO) &&
      !(me->texspace_flag & ME_TEXSPACE_FLAG_AUTO_EVALUATED))
  {
    BKE_mesh_texspace_calc(me);
  }
}

void BKE_mesh_texspace_get(Mesh *me, float r_texspace_location[3], float r_texspace_size[3])
{
  BKE_mesh_texspace_ensure(me);

  if (r_texspace_location) {
    copy_v3_v3(r_texspace_location, me->texspace_location);
  }
  if (r_texspace_size) {
    copy_v3_v3(r_texspace_size, me->texspace_size);
  }
}

void BKE_mesh_texspace_get_reference(Mesh *me,
                                     char **r_texspace_flag,
                                     float **r_texspace_location,
                                     float **r_texspace_size)
{
  BKE_mesh_texspace_ensure(me);

  if (r_texspace_flag != nullptr) {
    *r_texspace_flag = &me->texspace_flag;
  }
  if (r_texspace_location != nullptr) {
    *r_texspace_location = me->texspace_location;
  }
  if (r_texspace_size != nullptr) {
    *r_texspace_size = me->texspace_size;
  }
}

float (*BKE_mesh_orco_verts_get(Object *ob))[3]
{
  Mesh *me = static_cast<Mesh *>(ob->data);
  Mesh *tme = me->texcomesh ? me->texcomesh : me;

  /* Get appropriate vertex coordinates */
  float(*vcos)[3] = (float(*)[3])MEM_calloc_arrayN(me->totvert, sizeof(*vcos), "orco mesh");
  const Span<float3> positions = tme->vert_positions();

  int totvert = min_ii(tme->totvert, me->totvert);

  for (int a = 0; a < totvert; a++) {
    copy_v3_v3(vcos[a], positions[a]);
  }

  return vcos;
}

void BKE_mesh_orco_verts_transform(Mesh *me, float (*orco)[3], int totvert, const bool invert)
{
  float texspace_location[3], texspace_size[3];

  BKE_mesh_texspace_get(me->texcomesh ? me->texcomesh : me, texspace_location, texspace_size);

  if (invert) {
    for (int a = 0; a < totvert; a++) {
      float *co = orco[a];
      madd_v3_v3v3v3(co, texspace_location, co, texspace_size);
    }
  }
  else {
    for (int a = 0; a < totvert; a++) {
      float *co = orco[a];
      co[0] = (co[0] - texspace_location[0]) / texspace_size[0];
      co[1] = (co[1] - texspace_location[1]) / texspace_size[1];
      co[2] = (co[2] - texspace_location[2]) / texspace_size[2];
    }
  }
}

void BKE_mesh_orco_ensure(Object *ob, Mesh *mesh)
{
  if (CustomData_has_layer(&mesh->vert_data, CD_ORCO)) {
    return;
  }

  /* Orcos are stored in normalized 0..1 range by convention. */
  float(*orcodata)[3] = BKE_mesh_orco_verts_get(ob);
  BKE_mesh_orco_verts_transform(mesh, orcodata, mesh->totvert, false);
  CustomData_add_layer_with_data(&mesh->vert_data, CD_ORCO, orcodata, mesh->totvert, nullptr);
}

Mesh *BKE_mesh_from_object(Object *ob)
{
  if (ob == nullptr) {
    return nullptr;
  }
  if (ob->type == OB_MESH) {
    return static_cast<Mesh *>(ob->data);
  }

  return nullptr;
}

void BKE_mesh_assign_object(Main *bmain, Object *ob, Mesh *me)
{
  Mesh *old = nullptr;

  if (ob == nullptr) {
    return;
  }

  multires_force_sculpt_rebuild(ob);

  if (ob->type == OB_MESH) {
    old = static_cast<Mesh *>(ob->data);
    if (old) {
      id_us_min(&old->id);
    }
    ob->data = me;
    id_us_plus((ID *)me);
  }

  BKE_object_materials_test(bmain, ob, (ID *)me);

  BKE_modifiers_test_object(ob);
}

void BKE_mesh_material_index_remove(Mesh *me, short index)
{
  using namespace blender;
  using namespace blender::bke;
  MutableAttributeAccessor attributes = me->attributes_for_write();
  AttributeWriter<int> material_indices = attributes.lookup_for_write<int>("material_index");
  if (!material_indices) {
    return;
  }
  if (material_indices.domain != ATTR_DOMAIN_FACE) {
    BLI_assert_unreachable();
    return;
  }
  MutableVArraySpan<int> indices_span(material_indices.varray);
  for (const int i : indices_span.index_range()) {
    if (indices_span[i] > 0 && indices_span[i] >= index) {
      indices_span[i]--;
    }
  }
  indices_span.save();
  material_indices.finish();

  BKE_mesh_tessface_clear(me);
}

bool BKE_mesh_material_index_used(Mesh *me, short index)
{
  using namespace blender;
  using namespace blender::bke;
  const AttributeAccessor attributes = me->attributes();
  const VArray<int> material_indices = *attributes.lookup_or_default<int>(
      "material_index", ATTR_DOMAIN_FACE, 0);
  if (material_indices.is_single()) {
    return material_indices.get_internal_single() == index;
  }
  const VArraySpan<int> indices_span(material_indices);
  return indices_span.contains(index);
}

void BKE_mesh_material_index_clear(Mesh *me)
{
  using namespace blender;
  using namespace blender::bke;
  MutableAttributeAccessor attributes = me->attributes_for_write();
  attributes.remove("material_index");

  BKE_mesh_tessface_clear(me);
}

void BKE_mesh_material_remap(Mesh *me, const uint *remap, uint remap_len)
{
  using namespace blender;
  using namespace blender::bke;
  const short remap_len_short = short(remap_len);

#define MAT_NR_REMAP(n) \
  if (n < remap_len_short) { \
    BLI_assert(n >= 0 && remap[n] < remap_len_short); \
    n = remap[n]; \
  } \
  ((void)0)

  if (me->edit_mesh) {
    BMEditMesh *em = me->edit_mesh;
    BMIter iter;
    BMFace *efa;

    BM_ITER_MESH (efa, &iter, em->bm, BM_FACES_OF_MESH) {
      MAT_NR_REMAP(efa->mat_nr);
    }
  }
  else {
    MutableAttributeAccessor attributes = me->attributes_for_write();
    SpanAttributeWriter<int> material_indices = attributes.lookup_or_add_for_write_span<int>(
        "material_index", ATTR_DOMAIN_FACE);
    if (!material_indices) {
      return;
    }
    for (const int i : material_indices.span.index_range()) {
      MAT_NR_REMAP(material_indices.span[i]);
    }
    material_indices.span.save();
    material_indices.finish();
  }

#undef MAT_NR_REMAP
}

void BKE_mesh_smooth_flag_set(Mesh *me, const bool use_smooth)
{
  using namespace blender;
  using namespace blender::bke;
  MutableAttributeAccessor attributes = me->attributes_for_write();
  if (use_smooth) {
    attributes.remove("sharp_edge");
    attributes.remove("sharp_face");
  }
  else {
    attributes.remove("sharp_edge");
    SpanAttributeWriter<bool> sharp_faces = attributes.lookup_or_add_for_write_only_span<bool>(
        "sharp_face", ATTR_DOMAIN_FACE);
    sharp_faces.span.fill(true);
    sharp_faces.finish();
  }
}

void BKE_mesh_sharp_edges_set_from_angle(Mesh *me, const float angle)
{
  using namespace blender;
  using namespace blender::bke;
  bke::MutableAttributeAccessor attributes = me->attributes_for_write();
  if (angle >= M_PI) {
    attributes.remove("sharp_edge");
    attributes.remove("sharp_face");
    return;
  }
  if (angle == 0.0f) {
    BKE_mesh_smooth_flag_set(me, false);
    return;
  }
  bke::SpanAttributeWriter<bool> sharp_edges = attributes.lookup_or_add_for_write_span<bool>(
      "sharp_edge", ATTR_DOMAIN_EDGE);
  const bool *sharp_faces = static_cast<const bool *>(
      CustomData_get_layer_named(&me->face_data, CD_PROP_BOOL, "sharp_face"));
  bke::mesh::edges_sharp_from_angle_set(me->faces(),
                                        me->corner_verts(),
                                        me->corner_edges(),
                                        me->face_normals(),
                                        me->corner_to_face_map(),
                                        sharp_faces,
                                        angle,
                                        sharp_edges.span);
  sharp_edges.finish();
}

std::optional<blender::Bounds<blender::float3>> Mesh::bounds_min_max() const
{
  using namespace blender;
  const int verts_num = BKE_mesh_wrapper_vert_len(this);
  if (verts_num == 0) {
    return std::nullopt;
  }
  this->runtime->bounds_cache.ensure([&](Bounds<float3> &r_bounds) {
    switch (this->runtime->wrapper_type) {
      case ME_WRAPPER_TYPE_BMESH:
        r_bounds = *BKE_editmesh_cache_calc_minmax(*this->edit_mesh, *this->runtime->edit_data);
        break;
      case ME_WRAPPER_TYPE_MDATA:
      case ME_WRAPPER_TYPE_SUBD:
        r_bounds = *bounds::min_max(this->vert_positions());
        break;
    }
  });
  return this->runtime->bounds_cache.data();
}

void Mesh::bounds_set_eager(const blender::Bounds<float3> &bounds)
{
  this->runtime->bounds_cache.ensure([&](blender::Bounds<float3> &r_data) { r_data = bounds; });
}

void BKE_mesh_transform(Mesh *me, const float mat[4][4], bool do_keys)
{
  MutableSpan<float3> positions = me->vert_positions_for_write();

  for (float3 &position : positions) {
    mul_m4_v3(mat, position);
  }

  if (do_keys && me->key) {
    LISTBASE_FOREACH (KeyBlock *, kb, &me->key->block) {
      float *fp = (float *)kb->data;
      for (int i = kb->totelem; i--; fp += 3) {
        mul_m4_v3(mat, fp);
      }
    }
  }

  BKE_mesh_tag_positions_changed(me);
}

static void translate_positions(MutableSpan<float3> positions, const float3 &translation)
{
  using namespace blender;
  threading::parallel_for(positions.index_range(), 2048, [&](const IndexRange range) {
    for (float3 &position : positions.slice(range)) {
      position += translation;
    }
  });
}

void BKE_mesh_translate(Mesh *mesh, const float offset[3], const bool do_keys)
{
  using namespace blender;
  if (math::is_zero(float3(offset))) {
    return;
  }

  std::optional<Bounds<float3>> bounds;
  if (mesh->runtime->bounds_cache.is_cached()) {
    bounds = mesh->runtime->bounds_cache.data();
  }

  translate_positions(mesh->vert_positions_for_write(), offset);
  if (do_keys && mesh->key) {
    LISTBASE_FOREACH (KeyBlock *, kb, &mesh->key->block) {
      translate_positions({static_cast<float3 *>(kb->data), kb->totelem}, offset);
    }
  }

  BKE_mesh_tag_positions_changed_uniformly(mesh);

  if (bounds) {
    bounds->min += offset;
    bounds->max += offset;
    mesh->bounds_set_eager(*bounds);
  }
}

void BKE_mesh_tessface_clear(Mesh *mesh)
{
  mesh_tessface_clear_intern(mesh, true);
}

/* -------------------------------------------------------------------- */
/* MSelect functions (currently used in weight paint mode) */

void BKE_mesh_mselect_clear(Mesh *me)
{
  MEM_SAFE_FREE(me->mselect);
  me->totselect = 0;
}

void BKE_mesh_mselect_validate(Mesh *me)
{
  using namespace blender;
  using namespace blender::bke;
  MSelect *mselect_src, *mselect_dst;
  int i_src, i_dst;

  if (me->totselect == 0) {
    return;
  }

  mselect_src = me->mselect;
  mselect_dst = (MSelect *)MEM_malloc_arrayN(
      (me->totselect), sizeof(MSelect), "Mesh selection history");

  const AttributeAccessor attributes = me->attributes();
  const VArray<bool> select_vert = *attributes.lookup_or_default<bool>(
      ".select_vert", ATTR_DOMAIN_POINT, false);
  const VArray<bool> select_edge = *attributes.lookup_or_default<bool>(
      ".select_edge", ATTR_DOMAIN_EDGE, false);
  const VArray<bool> select_poly = *attributes.lookup_or_default<bool>(
      ".select_poly", ATTR_DOMAIN_FACE, false);

  for (i_src = 0, i_dst = 0; i_src < me->totselect; i_src++) {
    int index = mselect_src[i_src].index;
    switch (mselect_src[i_src].type) {
      case ME_VSEL: {
        if (select_vert[index]) {
          mselect_dst[i_dst] = mselect_src[i_src];
          i_dst++;
        }
        break;
      }
      case ME_ESEL: {
        if (select_edge[index]) {
          mselect_dst[i_dst] = mselect_src[i_src];
          i_dst++;
        }
        break;
      }
      case ME_FSEL: {
        if (select_poly[index]) {
          mselect_dst[i_dst] = mselect_src[i_src];
          i_dst++;
        }
        break;
      }
      default: {
        BLI_assert_unreachable();
        break;
      }
    }
  }

  MEM_freeN(mselect_src);

  if (i_dst == 0) {
    MEM_freeN(mselect_dst);
    mselect_dst = nullptr;
  }
  else if (i_dst != me->totselect) {
    mselect_dst = (MSelect *)MEM_reallocN(mselect_dst, sizeof(MSelect) * i_dst);
  }

  me->totselect = i_dst;
  me->mselect = mselect_dst;
}

int BKE_mesh_mselect_find(Mesh *me, int index, int type)
{
  BLI_assert(ELEM(type, ME_VSEL, ME_ESEL, ME_FSEL));

  for (int i = 0; i < me->totselect; i++) {
    if ((me->mselect[i].index == index) && (me->mselect[i].type == type)) {
      return i;
    }
  }

  return -1;
}

int BKE_mesh_mselect_active_get(Mesh *me, int type)
{
  BLI_assert(ELEM(type, ME_VSEL, ME_ESEL, ME_FSEL));

  if (me->totselect) {
    if (me->mselect[me->totselect - 1].type == type) {
      return me->mselect[me->totselect - 1].index;
    }
  }
  return -1;
}

void BKE_mesh_mselect_active_set(Mesh *me, int index, int type)
{
  const int msel_index = BKE_mesh_mselect_find(me, index, type);

  if (msel_index == -1) {
    /* add to the end */
    me->mselect = (MSelect *)MEM_reallocN(me->mselect, sizeof(MSelect) * (me->totselect + 1));
    me->mselect[me->totselect].index = index;
    me->mselect[me->totselect].type = type;
    me->totselect++;
  }
  else if (msel_index != me->totselect - 1) {
    /* move to the end */
    std::swap(me->mselect[msel_index], me->mselect[me->totselect - 1]);
  }

  BLI_assert((me->mselect[me->totselect - 1].index == index) &&
             (me->mselect[me->totselect - 1].type == type));
}

void BKE_mesh_count_selected_items(const Mesh *mesh, int r_count[3])
{
  r_count[0] = r_count[1] = r_count[2] = 0;
  if (mesh->edit_mesh) {
    BMesh *bm = mesh->edit_mesh->bm;
    r_count[0] = bm->totvertsel;
    r_count[1] = bm->totedgesel;
    r_count[2] = bm->totfacesel;
  }
  /* We could support faces in paint modes. */
}

/* **** Depsgraph evaluation **** */

void BKE_mesh_eval_geometry(Depsgraph *depsgraph, Mesh *mesh)
{
  DEG_debug_print_eval(depsgraph, __func__, mesh->id.name, mesh);
  BKE_mesh_texspace_calc(mesh);
  /* We are here because something did change in the mesh. This means we can not trust the existing
   * evaluated mesh, and we don't know what parts of the mesh did change. So we simply delete the
   * evaluated mesh and let objects to re-create it with updated settings. */
  if (mesh->runtime->mesh_eval != nullptr) {
    mesh->runtime->mesh_eval->edit_mesh = nullptr;
    BKE_id_free(nullptr, mesh->runtime->mesh_eval);
    mesh->runtime->mesh_eval = nullptr;
  }
  if (DEG_is_active(depsgraph)) {
    Mesh *mesh_orig = reinterpret_cast<Mesh *>(DEG_get_original_id(&mesh->id));
    if (mesh->texspace_flag & ME_TEXSPACE_FLAG_AUTO_EVALUATED) {
      mesh_orig->texspace_flag |= ME_TEXSPACE_FLAG_AUTO_EVALUATED;
      copy_v3_v3(mesh_orig->texspace_location, mesh->texspace_location);
      copy_v3_v3(mesh_orig->texspace_size, mesh->texspace_size);
    }
  }
}

/****************UPBGE****************/
void BKE_mesh_ensure_navmesh(Mesh *me)
{
  if (!CustomData_has_layer(&me->face_data, CD_RECAST)) {
    int i;
    int faces_len = me->faces_num;
    int *recastData;
    recastData = (int *)MEM_malloc_arrayN(faces_len, sizeof(int), __func__);
    for (i = 0; i < faces_len; i++) {
      recastData[i] = i + 1;
    }
    CustomData_add_layer_named_with_data(
        &me->face_data, CD_RECAST, recastData, faces_len, "recastData", nullptr);
  }
}
/************************************/
