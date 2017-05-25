/* PipeWire
 * Copyright (C) 2016 Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __PIPEWIRE_UTILS_H__
#define __PIPEWIRE_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <spa/defs.h>
#include <spa/pod-utils.h>

const char * pw_split_walk   (const char  *str,
                              const char  *delimiter,
                              size_t      *len,
                              const char **state);
char **      pw_split_strv   (const char *str,
                              const char *delimiter,
                              int         max_tokens,
                              int        *n_tokens);
void         pw_free_strv    (char **str);

char *       pw_strip        (char       *str,
                              const char *whitespace);

static inline struct spa_pod *
pw_spa_pod_copy (const struct spa_pod *pod)
{
  return pod ? memcpy (malloc (SPA_POD_SIZE (pod)), pod, SPA_POD_SIZE (pod)) : NULL;
}

#define spa_format_copy(f)      ((struct spa_format*)pw_spa_pod_copy(&(f)->pod))
#define spa_props_copy(p)       ((struct spa_prop*)pw_spa_pod_copy(&(p)->object.pod))
#define spa_param_copy(p)       ((struct spa_param*)pw_spa_pod_copy(&(p)->object.pod))

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* __PIPEWIRE_UTILS_H__ */
