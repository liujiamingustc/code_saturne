/*============================================================================
 * Boundary zones handling.
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2017 EDF S.A.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any later
  version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
  Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*----------------------------------------------------------------------------*/

#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#if defined(HAVE_MPI)
#include <mpi.h>
#endif

/*----------------------------------------------------------------------------
 * Local headers
 *----------------------------------------------------------------------------*/

#include <ple_locator.h>

#include "bft_mem.h"
#include "bft_error.h"
#include "bft_printf.h"

#include "cs_base.h"
#include "cs_flag_check.h"
#include "cs_log.h"
#include "cs_map.h"
#include "cs_mesh.h"
#include "cs_mesh_location.h"
#include "cs_mesh_quantities.h"
#include "cs_parall.h"
#include "cs_parameters.h"

/*----------------------------------------------------------------------------
 *  Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_boundary_zone.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*=============================================================================
 * Additional doxygen documentation
 *============================================================================*/

/*!
  \file cs_boundary_zone.c
        Boundary zone handling.
*/

/*! \cond DOXYGEN_SHOULD_SKIP_THIS */

/*=============================================================================
 * Local Macro Definitions
 *============================================================================*/

/* Zone descriptor allocation block size */

#define _CS_ZONE_S_ALLOC_SIZE       16

/*=============================================================================
 * Local Type Definitions
 *============================================================================*/

/*! Boundary zone ids */

typedef struct {
  cs_lnum_t         n_elts;   /*!< local number of associated elements */
  const cs_lnum_t  *max_id;   /*!< local face ids, or NULL if trivial */
} cs_boundary_zone_id_t;

/*============================================================================
 *  Global variables
 *============================================================================*/

/* Zone definitions */

static int  _n_zones = 0;
static int  _n_zones_max = 0;
static cs_boundary_zone_t     **_zones = NULL;
static cs_map_name_to_id_t   *_zone_map = NULL;

/* Boundary zone id associated with boundary faces */

static int *_zone_id = NULL;

/*============================================================================
 * Prototypes for functions intended for use only by Fortran wrappers.
 * (descriptions follow, with function bodies).
 *============================================================================*/

/*============================================================================
 * Private function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return a pointer to a boundary zone based on its name if present.
 *
 * If no boundary zone of the given name is defined, NULL is returned.
 *
 * \param[in]  name  boundary zone name
 *
 * \return  pointer to (read only) zone structure, or NULL
 */
/*----------------------------------------------------------------------------*/

static cs_boundary_zone_t  *
_zone_by_name_try(const char  *name)
{
  cs_boundary_zone_t *z = NULL;
  int id = cs_map_name_to_id_try(_zone_map, name);

  if (id > -1)
    z = _zones[id];

  return z;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Create a boundary zone.
 *
 * \param[in]  name  zone name
 *
 * \return   pointer to new zone.
 */
/*----------------------------------------------------------------------------*/

static cs_boundary_zone_t *
_zone_define(const char  *name)
{
  int zone_id = -1;
  const char *addr_0 = NULL, *addr_1 = NULL;

  cs_boundary_zone_t *z = _zone_by_name_try(name);

  /* Check this name was not already used */

  if (z != NULL)
    return z;

  /* Initialize if necessary */

  if (_zone_map == NULL)
    _zone_map = cs_map_name_to_id_create();

  else
    addr_0 = cs_map_name_to_id_reverse(_zone_map, 0);

  size_t l = 0;
  if (name != NULL)
    l = strlen(name);
  if (l == 0)
    bft_error(__FILE__, __LINE__, 0, _("Defining a zone requires a name."));

  /* Insert entry in map */

  zone_id = cs_map_name_to_id(_zone_map, name);

  /* Move name pointers of previous zones if necessary
     (i.e. reallocation of map names array) */

  addr_1 = cs_map_name_to_id_reverse(_zone_map, 0);

  if (addr_1 != addr_0) {
    ptrdiff_t addr_shift = addr_1 - addr_0;
    for (int i = 0; i < zone_id; i++)
      _zones[i]->name += addr_shift;
  }

  if (zone_id == _n_zones)
    _n_zones = zone_id + 1;

  /* Reallocate zones pointer if necessary */

  if (_n_zones > _n_zones_max) {
    if (_n_zones_max == 0)
      _n_zones_max = 8;
    else
      _n_zones_max *= 2;
    BFT_REALLOC(_zones, _n_zones_max, cs_boundary_zone_t *);
  }

  /* Allocate zones descriptor block if necessary
     (to reduce fragmentation and improve locality of zone
     descriptors, they are allocated in blocks) */

  int shift_in_alloc_block = zone_id % _CS_ZONE_S_ALLOC_SIZE;
  if (shift_in_alloc_block == 0)
    BFT_MALLOC(_zones[zone_id], _CS_ZONE_S_ALLOC_SIZE, cs_boundary_zone_t);
  else
    _zones[zone_id] =   _zones[zone_id - shift_in_alloc_block]
                      + shift_in_alloc_block;

  /* Assign zone */

  z = _zones[zone_id];

  z->name = cs_map_name_to_id_reverse(_zone_map, zone_id);

  z->id = zone_id;
  z->type = 0;

  z->location_id = 0;

  z->n_faces = 0;
  z->face_id = NULL;

  z->time_varying = false;
  z->allow_overlay = false;

  return z;
}

/*----------------------------------------------------------------------------
 * Add type flag info to the current position in the setup log.
 *
 * parameters:
 *   type <-- type flag
 *----------------------------------------------------------------------------*/

static inline void
_log_type(int type)
{
  if (type == 0)
    return;

  cs_log_printf(CS_LOG_SETUP,
                _("    type:                       %d"), type);
  cs_log_printf(CS_LOG_SETUP, "\n");
}

/*============================================================================
 * Fortran wrapper function definitions
 *============================================================================*/

/*! (DOXYGEN_SHOULD_SKIP_THIS) \endcond */

/*============================================================================
 * Public function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief Initialize boundary zone structures.
 *
 * This defines a default boundary zone. This is the first function of
 * the boundary zone handling functions which should be called, and it should
 * only be called after \ref cs_mesh_location_initialize.
 */
/*----------------------------------------------------------------------------*/

void
cs_boundary_zone_initialize(void)
{
  assert(_n_zones == 0);

  cs_mesh_location_set_explicit_ids(CS_MESH_LOCATION_BOUNDARY_FACES, true);

  const char *name = cs_mesh_location_get_name(CS_MESH_LOCATION_BOUNDARY_FACES);

  cs_boundary_zone_t *z = _zone_define(name);

  z->location_id = CS_MESH_LOCATION_BOUNDARY_FACES;

  z->type = 0;

  z->allow_overlay = true;

  assert(z->id == 0);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Free all boundary zone structures.
 */
/*----------------------------------------------------------------------------*/

void
cs_boundary_zone_finalize(void)
{
  BFT_FREE(_zone_id);

  for (int i = 0; i < _n_zones; i++) {
    if (i % _CS_ZONE_S_ALLOC_SIZE == 0)
      BFT_FREE(_zones[i]);
  }

  BFT_FREE(_zones);

  cs_map_name_to_id_destroy(&_zone_map);

  _n_zones = 0;
  _n_zones_max = 0;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return number of boundary zones defined.
 */
/*----------------------------------------------------------------------------*/

int
cs_boundary_zone_n_zones(void)
{
  return _n_zones;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return number of boundary zones which may vary in time.
 *
 * \return  number of zones which may vary in time
 */
/*----------------------------------------------------------------------------*/

int
cs_boundary_zone_n_zones_time_varying(void)
{
  int count = 0;

  for (int i = 0; i < _n_zones; i++) {
    if (_zones[i]->time_varying)
      count += 1;
  }

  return count;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Update association of boundary zones with a mesh.
 *
 * For time-varying zones, the associated mesh location is updated.
 *
 * \param[in]  mesh_modified  indicate if mesh has been modified
 */
/*----------------------------------------------------------------------------*/

void
cs_boundary_zone_build_all(bool  mesh_modified)
{
  cs_mesh_t  *m = cs_glob_mesh;
  bool has_time_varying = false;

  /* update zone lists */

  for (int i = 0; i < _n_zones; i++) {
    cs_boundary_zone_t *z = _zones[i];
    if (z->time_varying) {
      cs_mesh_location_build(m, z->location_id);
      has_time_varying = true;
    }
    z->n_faces = cs_mesh_location_get_n_elts(z->location_id)[0];
    z->face_id = cs_mesh_location_get_elt_ids(z->location_id);
  }

  /* Assign maximum zone id and check for overlap errors
     (start with zone 1, as 0 is default) */

  if (mesh_modified)
    BFT_REALLOC(_zone_id, m->n_b_faces, int);

  if (mesh_modified || has_time_varying) {

    cs_lnum_t n_faces = m->n_b_faces;

#   pragma omp parallel for if (n_faces > CS_THR_MIN)
    for (cs_lnum_t i = 0; i <n_faces; i++)
      _zone_id[i] = 0;

    int overlap_error[2] = {_n_zones, _n_zones};

    for (int i = 1; i < _n_zones; i++) {
      cs_boundary_zone_t *z = _zones[i];
      for (cs_lnum_t j = 0; j < z->n_faces; j++) {
        cs_lnum_t c_id = (z->face_id != NULL) ? z->face_id[j] : j;
        int z_id_prev = _zone_id[c_id];
        if (z_id_prev == 0)
          _zone_id[c_id] = z->id;
        else if (_zones[z_id_prev]->allow_overlay)
          _zone_id[c_id] = z->id;
        else if (overlap_error[0] == _n_zones) {
          overlap_error[0] = z_id_prev;
          overlap_error[1] = z->id;
          break;
        }
      }
    }

    cs_parall_min(2, CS_INT_TYPE, overlap_error);

    if (overlap_error[0] < _n_zones) {

      for (int i = 1; i < _n_zones; i++) {
        cs_boundary_zone_t *z = _zones[i];
        for (cs_lnum_t j = 0; j < z->n_faces; j++) {
          cs_lnum_t c_id = (z->face_id != NULL) ? z->face_id[j] : j;
          int z_id_prev = CS_ABS(_zone_id[c_id]);
          if (z_id_prev == 0)
            _zone_id[c_id] = z->id;
          else if (   _zones[z_id_prev]->allow_overlay
                   && _zone_id[c_id] > 0)
            _zone_id[c_id] = z->id;
          else
            _zone_id[c_id] = -z->id;
        }
      }

      cs_flag_check_error_info(_("face with forbidden zone overlap"),
                               _("zone id"),
                               _("zone_id"),
                               _("Faces with zone error"),
                               _("Faces with valid zones"),
                               CS_MESH_LOCATION_BOUNDARY_FACES,
                               0, /* min_flag */
                               _zone_id);

      int i0 = overlap_error[0], i1 = overlap_error[1];

      bft_error(__FILE__, __LINE__, 0,
                _("Boundary zone %i (\"%s\") contains at least\n"
                  "one face already marked with zone id %d (\"%s\").\n\n"
                  "Check definitions or allow overlays for this zone."),
                i1, _zones[i1]->name, i0, _zones[i0]->name);

    }
  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Define a new boundary zone using a selection criteria string.
 *
 * \param[in]  name       name of location to define
 * \param[in]  criteria   selection criteria for associated elements
 * \param[in]  type_flag  mask of zone category values
 *
 * \return  id of newly defined boundary zone
 */
/*----------------------------------------------------------------------------*/

int
cs_boundary_zone_define(const char  *name,
                        const char  *criteria,
                        int          type_flag)
{
  if (criteria == NULL)
    bft_error(__FILE__, __LINE__, 0,
              _("%s: selection criteria string must be non-null."),
              __func__);

  cs_boundary_zone_t *z = _zone_define(name);

  if (strcmp(criteria, "all[]"))
    z->location_id = cs_mesh_location_add(name,
                                          CS_MESH_LOCATION_BOUNDARY_FACES,
                                          criteria);
  else
    z->location_id = CS_MESH_LOCATION_BOUNDARY_FACES;

  z->type = type_flag;

  return z->id;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Define a new mesh location with an associated selection function.
 *
 * So as to define a subset of mesh entities of a given type, a pointer
 * to a selection function may be given.
 *
 * This requires more programming but allows finer control than selection
 * criteria, as the function has access to the complete mesh structure.
 *
 * \param[in]  name  name of location to define
 * \param[in]  func  pointer to selection function for associated elements
 * \param[in]  type_flag  mask of zone category values
 *
 * \return  id of newly defined created mesh location
 */
/*----------------------------------------------------------------------------*/

int
cs_boundary_zone_define_by_func(const char                 *name,
                                cs_mesh_location_select_t  *func,
                                int                         type_flag)
{
  if (func == NULL)
    bft_error(__FILE__, __LINE__, 0,
              _("%s: selection function pointer must be non-null."),
              __func__);

  cs_boundary_zone_t *z = _zone_define(name);

  z->location_id = cs_mesh_location_add_by_func(name,
                                                CS_MESH_LOCATION_BOUNDARY_FACES,
                                                func);

  z->type = type_flag;

  return z->id;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return a pointer to a boundary zone based on its id.
 *
 * This function requires that a boundary zone of the given id is defined.
 *
 * \param[in]  id   zone id
 *
 * \return  pointer to the boundary zone structure
 */
/*----------------------------------------------------------------------------*/

const cs_boundary_zone_t  *
cs_boundary_zone_by_id(int  id)
{
  if (id > -1 && id < _n_zones)
    return _zones[id];
  else {
    bft_error(__FILE__, __LINE__, 0,
              _("Boundary zone with id %d is not defined."), id);
    return NULL;
  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return a pointer to a boundary zone based on its name if present.
 *
 * This function requires that a boundary zone of the given name is defined.
 *
 * \param[in]  name  boundary zone name
 *
 * \return  pointer to (read-only) zone structure
 */
/*----------------------------------------------------------------------------*/

const cs_boundary_zone_t  *
cs_boundary_zone_by_name(const char  *name)
{
  int id = cs_map_name_to_id_try(_zone_map, name);

  if (id > -1)
    return _zones[id];
  else {
    bft_error(__FILE__, __LINE__, 0,
              _("Boundary zone \"%s\" is not defined."), name);
    return NULL;
  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return a pointer to a boundary zone based on its name if present.
 *
 * If no boundary zone of the given name is defined, NULL is returned.
 *
 * \param[in]  name  boundary zone name
 *
 * \return  pointer to (read only) zone structure, or NULL
 */
/*----------------------------------------------------------------------------*/

const cs_boundary_zone_t  *
cs_boundary_zone_by_name_try(const char  *name)
{
  const cs_boundary_zone_t *z = NULL;
  int id = cs_map_name_to_id_try(_zone_map, name);

  if (id > -1)
    z = _zones[id];

  return z;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set type flag for a given boundary zone.
 *
 * \param[in]  id         boundary zone id
 * \param[in]  type_flag  true if the zone's definition varies in time
 */
/*----------------------------------------------------------------------------*/

void
cs_boundary_zone_set_type(int   id,
                          bool  type_flag)
{
  const cs_boundary_zone_t *z0 = cs_boundary_zone_by_id(id);

  _zones[z0->id]->type = type_flag;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set time varying behavior for a given boundary zone.
 *
 * \param[in]  id            boundary zone id
 * \param[in]  time_varying  true if the zone's definition varies in time
 */
/*----------------------------------------------------------------------------*/

void
cs_boundary_zone_set_time_varying(int   id,
                                  bool  time_varying)
{
  const cs_boundary_zone_t *z0 = cs_boundary_zone_by_id(id);

  _zones[z0->id]->time_varying = time_varying;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Set overlay behavior for a given boundary zone.
 *
 * \param[in]  id             boundary zone id
 * \param[in]  allow_overlay  true if the zone may be overlayed by another
 */
/*----------------------------------------------------------------------------*/

void
cs_boundary_zone_set_overlay(int   id,
                             bool  allow_overlay)
{
  const cs_boundary_zone_t *z0 = cs_boundary_zone_by_id(id);

  _zones[z0->id]->allow_overlay = allow_overlay;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return pointer to zone id associated with each boundary face.
 *
 * In case of overlayed zones, the highest zone id associated with
 * a given face is given.
 */
/*----------------------------------------------------------------------------*/

const int *
cs_boundary_zone_face_zone_id(void)
{
  return (const int *)_zone_id;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Print info relative to a given boundary zone to log file.
 *
 * \param[in]  z   pointer to boundary zone structure
 */
/*----------------------------------------------------------------------------*/

void
cs_boundary_zone_log_info(const cs_boundary_zone_t  *z)
{
  if (z == NULL)
    return;

  /* Global indicators */
  /*-------------------*/

  cs_log_printf(CS_LOG_SETUP,
                _("\n"
                  "  Zone: \"%s\"\n"
                  "    id:                         %d\n"),
                z->name, z->id);


  _log_type(z->type);

  cs_log_printf(CS_LOG_SETUP,
                _("    location_id:                %d\n"),
                z->location_id);

  if (z->time_varying)
    cs_log_printf(CS_LOG_SETUP, _("    time varying\n"));
  if (z->allow_overlay)
    cs_log_printf(CS_LOG_SETUP, _("    allow overlay\n"));

  const char *sel_str = cs_mesh_location_get_selection_string(z->location_id);
  if (sel_str != NULL)
    cs_log_printf(CS_LOG_SETUP,
                  _("    selection criteria:         \"%s\"\n"),
                  sel_str);
  else {
    cs_mesh_location_select_t *sel_fp
      = cs_mesh_location_get_selection_function(z->location_id);
    if (sel_fp != NULL)
      cs_log_printf(CS_LOG_SETUP,
                    _("    selection function:         %p\n"),
                    (void *)sel_fp);
  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Log setup information relative to defined boundary zones.
 */
/*----------------------------------------------------------------------------*/

void
cs_boundary_zone_log_setup(void)
{
  if (_n_zones == 0)
    return;

  cs_log_printf(CS_LOG_SETUP,
                _("\n"
                  "Boundary zones\n"
                  "--------------\n"));

  for (int i = 0; i < _n_zones; i++)
    cs_boundary_zone_log_info(_zones[i]);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Return number of boundary zones associated with a
 *        given zone flag.
 *
 * \param[in]  type_flag  flag to compare to zone type
 *
 * \return  number of zones matching the given type flag
 */
/*----------------------------------------------------------------------------*/

int
cs_boundary_zone_n_type_zones(int  type_flag)
{
  int count = 0;

  for (int i = 0; i < _n_zones; i++) {
    if (_zones[i]->type & type_flag)
      count += 1;
  }

  return count;
}

/*----------------------------------------------------------------------------*/

END_C_DECLS