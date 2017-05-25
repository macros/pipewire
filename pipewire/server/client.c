/* PipeWire
 * Copyright (C) 2015 Wim Taymans <wim.taymans@gmail.com>
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

#include <string.h>

#include "pipewire/client/pipewire.h"
#include "pipewire/client/interfaces.h"

#include "pipewire/server/client.h"
#include "pipewire/server/resource.h"

struct impl
{
  struct pw_client this;
};

static void
client_unbind_func (void *data)
{
  struct pw_resource *resource = data;
  spa_list_remove (&resource->link);
}

static int
client_bind_func (struct pw_global *global,
                  struct pw_client *client,
                  uint32_t          version,
                  uint32_t          id)
{
  struct pw_client *this = global->object;
  struct pw_resource *resource;

  resource = pw_resource_new (client,
                                 id,
                                 global->type,
                                 global->object,
                                 client_unbind_func);
  if (resource == NULL)
    goto no_mem;

  pw_log_debug ("client %p: bound to %d", global->object, resource->id);

  spa_list_insert (this->resource_list.prev, &resource->link);

  this->info.change_mask = ~0;
  pw_client_notify_info (resource, &this->info);

  return SPA_RESULT_OK;

no_mem:
  pw_log_error ("can't create client resource");
  pw_core_notify_error (client->core_resource,
                           client->core_resource->id,
                           SPA_RESULT_NO_MEMORY,
                           "no memory");
  return SPA_RESULT_NO_MEMORY;
}

/**
 * pw_client_new:
 * @core: a #pw_core
 * @properties: extra client properties
 *
 * Make a new #struct pw_client object and register it to @core
 *
 * Returns: a new #struct pw_client
 */
struct pw_client *
pw_client_new (struct pw_core       *core,
               struct ucred         *ucred,
               struct pw_properties *properties)
{
  struct pw_client *this;
  struct impl *impl;

  impl = calloc (1, sizeof (struct impl));
  if (impl == NULL)
    return NULL;

  pw_log_debug ("client %p: new", impl);

  this = &impl->this;
  this->core = core;
  if ((this->ucred_valid = (ucred != NULL)))
    this->ucred = *ucred;
  this->properties = properties;

  spa_list_init (&this->resource_list);
  pw_signal_init (&this->properties_changed);
  pw_signal_init (&this->resource_added);
  pw_signal_init (&this->resource_removed);

  pw_map_init (&this->objects, 0, 32);
  pw_map_init (&this->types, 0, 32);
  pw_signal_init (&this->destroy_signal);

  spa_list_insert (core->client_list.prev, &this->link);

  pw_core_add_global (core,
                         this,
                         core->type.client,
                         0,
                         this,
                         client_bind_func,
                         &this->global);

  this->info.id = this->global->id;
  this->info.props = this->properties ? &this->properties->dict : NULL;

  return this;
}

static void
destroy_resource (void *object,
                  void *data)
{
  pw_resource_destroy (object);
}

/**
 * pw_client_destroy:
 * @client: a #struct pw_client
 *
 * Trigger removal of @client
 */
void
pw_client_destroy (struct pw_client * client)
{
  struct pw_resource *resource, *tmp;
  struct impl *impl = SPA_CONTAINER_OF (client, struct impl, this);

  pw_log_debug ("client %p: destroy", client);
  pw_signal_emit (&client->destroy_signal, client);

  spa_list_remove (&client->link);
  pw_global_destroy (client->global);

  spa_list_for_each_safe (resource, tmp, &client->resource_list, link)
    pw_resource_destroy (resource);

  pw_map_for_each (&client->objects, destroy_resource, client);

  pw_log_debug ("client %p: free", impl);
  pw_map_clear (&client->objects);

  if (client->properties)
    pw_properties_free (client->properties);

  free (impl);
}

void
pw_client_update_properties (struct pw_client      *client,
                             const struct spa_dict *dict)
{
  struct pw_resource *resource;

  if (client->properties == NULL) {
    if (dict)
      client->properties = pw_properties_new_dict (dict);
  } else {
    uint32_t i;

    for (i = 0; i < dict->n_items; i++)
      pw_properties_set (client->properties,
                            dict->items[i].key,
                            dict->items[i].value);
  }

  client->info.change_mask = 1 << 0;
  client->info.props = client->properties ? &client->properties->dict : NULL;

  pw_signal_emit (&client->properties_changed, client);

  spa_list_for_each (resource, &client->resource_list, link) {
    pw_client_notify_info (resource, &client->info);
  }
}
