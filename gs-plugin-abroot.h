/*
 * Copyright (C) 2023 Mateus Melchiades
 */

#pragma once

#include <glib.h>
#include <gnome-software.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_ABROOT (gs_plugin_abroot_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginAbroot, gs_plugin_abroot, GS, PLUGIN_ABROOT, GsPlugin)

G_END_DECLS

