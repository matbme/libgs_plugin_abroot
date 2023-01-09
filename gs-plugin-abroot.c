/*
 * Copyright (C) 2023 Mateus Melchiades
 */

#include <config.h>
#include <glib/gi18n-lib.h>

#include <glib.h>
#include <gnome-software.h>
#include <packagekit-glib2/packagekit.h>

#include "gs-packagekit-helper.h"
#include "gs-packagekit-task.h"
#include "gs-plugin-abroot.h"
#include "packagekit-common.h"

struct _GsPluginAbroot
{
    GsPlugin parent;
};

static void refresh_metadata_cb (GObject *source_object, GAsyncResult *result,
                                 gpointer user_data);

G_DEFINE_TYPE (GsPluginAbroot, gs_plugin_abroot, GS_TYPE_PLUGIN)

static void
gs_plugin_abroot_init (GsPluginAbroot *self)
{
    GsPlugin *plugin = GS_PLUGIN (self);

    // Print distro name for debugging purposes
    GsOsRelease *os_release = gs_os_release_new (NULL);
    g_debug ("Distro name: %s",
             gs_os_release_get_pretty_name (
                 os_release)); // Returns "VanillaOS 22.10 all"

    gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "appstream");
}

static gboolean
gs_plugin_add_sources_related (GsPlugin *plugin, GHashTable *hash,
                               GCancellable *cancellable, GError **error)
{
    guint i;
    GsApp *app;
    GsApp *app_tmp;
    PkBitfield filter;
    g_autoptr (GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
    g_autoptr (PkTask) task_related = NULL;
    const gchar *id;
    gboolean ret = TRUE;
    g_autoptr (GsAppList) installed = gs_app_list_new ();
    g_autoptr (PkResults) results = NULL;

    filter = pk_bitfield_from_enums (
        PK_FILTER_ENUM_INSTALLED, PK_FILTER_ENUM_NEWEST, PK_FILTER_ENUM_ARCH,
        PK_FILTER_ENUM_NOT_COLLECTIONS, -1);

    task_related = gs_packagekit_task_new (plugin);
    gs_packagekit_task_setup (
        GS_PACKAGEKIT_TASK (task_related), GS_PLUGIN_ACTION_GET_SOURCES,
        gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

    results = pk_client_get_packages (PK_CLIENT (task_related), filter,
                                      cancellable, gs_packagekit_helper_cb,
                                      helper, error);

    if (!gs_plugin_packagekit_results_valid (results, error))
        {
            g_prefix_error (error, "failed to get sources related: ");
            return FALSE;
        }
    ret = gs_plugin_packagekit_add_results (plugin, installed, results, error);
    if (!ret)
        return FALSE;
    for (i = 0; i < gs_app_list_length (installed); i++)
        {
            g_auto (GStrv) split = NULL;
            app = gs_app_list_index (installed, i);
            split = pk_package_id_split (gs_app_get_source_id_default (app));
            if (split == NULL)
                {
                    g_set_error (error, GS_PLUGIN_ERROR,
                                 GS_PLUGIN_ERROR_INVALID_FORMAT,
                                 "invalid package-id: %s",
                                 gs_app_get_source_id_default (app));
                    return FALSE;
                }
            if (g_str_has_prefix (split[PK_PACKAGE_ID_DATA], "installed:"))
                {
                    id = split[PK_PACKAGE_ID_DATA] + 10;
                    app_tmp = g_hash_table_lookup (hash, id);
                    if (app_tmp != NULL)
                        {
                            g_debug ("found package %s from %s",
                                     gs_app_get_source_default (app), id);
                            gs_app_add_related (app_tmp, app);
                        }
                }
        }
    return TRUE;
}

gboolean
gs_plugin_add_sources (GsPlugin *plugin, GsAppList *list,
                       GCancellable *cancellable, GError **error)
{
    PkBitfield filter;
    PkRepoDetail *rd;
    g_autoptr (GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
    g_autoptr (PkTask) task_sources = NULL;
    const gchar *id;
    guint i;
    g_autoptr (GHashTable) hash = NULL;
    g_autoptr (PkResults) results = NULL;
    g_autoptr (GPtrArray) array = NULL;

    /* ask PK for the repo details */
    filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_SOURCE,
                                     PK_FILTER_ENUM_NOT_DEVELOPMENT, -1);

    task_sources = gs_packagekit_task_new (plugin);
    gs_packagekit_task_setup (
        GS_PACKAGEKIT_TASK (task_sources), GS_PLUGIN_ACTION_GET_SOURCES,
        gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

    results = pk_client_get_repo_list (PK_CLIENT (task_sources), filter,
                                       cancellable, gs_packagekit_helper_cb,
                                       helper, error);

    if (!gs_plugin_packagekit_results_valid (results, error))
        return FALSE;
    hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    array = pk_results_get_repo_detail_array (results);
    for (i = 0; i < array->len; i++)
        {
            g_autoptr (GsApp) app = NULL;
            rd = g_ptr_array_index (array, i);
            id = pk_repo_detail_get_id (rd);
            app = gs_app_new (id);
            gs_app_set_management_plugin (app, plugin);
            gs_app_set_kind (app, AS_COMPONENT_KIND_REPOSITORY);
            gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
            gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
            gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
            gs_app_set_state (app, pk_repo_detail_get_enabled (rd)
                                       ? GS_APP_STATE_INSTALLED
                                       : GS_APP_STATE_AVAILABLE);
            gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
                             pk_repo_detail_get_description (rd));
            gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
                                pk_repo_detail_get_description (rd));
            gs_plugin_packagekit_set_packaging_format (plugin, app);
            gs_app_set_metadata (app, "GnomeSoftware::SortKey", "300");
            gs_app_set_origin_ui (app, _ ("Packages"));
            gs_app_list_add (list, app);
            g_hash_table_insert (hash, g_strdup (id), (gpointer)app);
        }

    /* get every application on the system and add it as a related package
     * if it matches */
    return gs_plugin_add_sources_related (plugin, hash, cancellable, error);
}

static void
gs_plugin_abroot_refresh_metadata_async (GsPlugin *plugin,
                                         guint64 cache_age_secs,
                                         GsPluginRefreshMetadataFlags flags,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    g_autoptr (GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
    g_autoptr (GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));
    gboolean interactive
        = (flags & GS_PLUGIN_REFRESH_METADATA_FLAGS_INTERACTIVE);
    g_autoptr (GTask) task = NULL;
    g_autoptr (PkTask) task_refresh = NULL;

    task = g_task_new (plugin, cancellable, callback, user_data);
    g_task_set_source_tag (task, gs_plugin_abroot_refresh_metadata_async);
    g_task_set_task_data (task, g_object_ref (helper), g_object_unref);

    gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
    gs_packagekit_helper_set_progress_app (helper, app_dl);

    task_refresh = gs_packagekit_task_new (plugin);
    pk_task_set_only_download (task_refresh, TRUE);
    gs_packagekit_task_setup (GS_PACKAGEKIT_TASK (task_refresh),
                              GS_PLUGIN_ACTION_UNKNOWN, interactive);
    pk_client_set_cache_age (PK_CLIENT (task_refresh), cache_age_secs);

    /* refresh the metadata */
    pk_client_refresh_cache_async (PK_CLIENT (task_refresh), FALSE /* force */,
                                   cancellable, gs_packagekit_helper_cb,
                                   helper, refresh_metadata_cb,
                                   g_steal_pointer (&task));
}

static void
refresh_metadata_cb (GObject *source_object, GAsyncResult *result,
                     gpointer user_data)
{
    PkClient *client = PK_CLIENT (source_object);
    g_autoptr (GTask) task = g_steal_pointer (&user_data);
    GsPlugin *plugin = g_task_get_source_object (task);
    g_autoptr (PkResults) results = NULL;
    g_autoptr (GError) local_error = NULL;

    results = pk_client_generic_finish (client, result, &local_error);

    if (!gs_plugin_packagekit_results_valid (results, &local_error))
        {
            g_task_return_error (task, g_steal_pointer (&local_error));
        }
    else
        {
            gs_plugin_updates_changed (plugin);
            g_task_return_boolean (task, TRUE);
        }
}

static GsApp *
gs_plugin_abroot_build_update_app (GsPlugin *plugin, PkPackage *package)
{
    GsApp *app = gs_plugin_cache_lookup (plugin, pk_package_get_id (package));
    if (app != NULL)
        {
            if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN)
                gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
            return app;
        }
    app = gs_app_new (NULL);
    gs_plugin_packagekit_set_packaging_format (plugin, app);
    gs_app_add_source (app, pk_package_get_name (package));
    gs_app_add_source_id (app, pk_package_get_id (package));
    gs_app_set_name (app, GS_APP_QUALITY_LOWEST,
                     pk_package_get_name (package));
    gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
                        pk_package_get_summary (package));
    gs_app_set_metadata (app, "GnomeSoftware::Creator",
                         gs_plugin_get_name (plugin));
    gs_app_set_management_plugin (app, plugin);
    gs_app_set_update_version (app, pk_package_get_version (package));
    gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
    gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
    gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
    gs_app_set_state (app, GS_APP_STATE_UPDATABLE);
    gs_plugin_cache_add (plugin, pk_package_get_id (package), app);
    return app;
}

static gboolean
gs_plugin_abroot_add_updates (GsPlugin *plugin, GsAppList *list,
                                  GCancellable *cancellable, GError **error)
{
    g_autoptr (GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
    g_autoptr (PkTask) task_updates = NULL;
    g_autoptr (PkResults) results = NULL;
    g_autoptr (GPtrArray) array = NULL;
    g_autoptr (GsApp) first_app = NULL;
    gboolean all_downloaded = TRUE;

    /* do sync call */
    gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

    task_updates = gs_packagekit_task_new (plugin);
    gs_packagekit_task_setup (
        GS_PACKAGEKIT_TASK (task_updates), GS_PLUGIN_ACTION_GET_UPDATES,
        gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

    results = pk_client_get_updates (
        PK_CLIENT (task_updates), pk_bitfield_value (PK_FILTER_ENUM_NONE),
        cancellable, gs_packagekit_helper_cb, helper, error);

    if (!gs_plugin_packagekit_results_valid (results, error))
        return FALSE;

    /* add results */
    array = pk_results_get_package_array (results);
    for (guint i = 0; i < array->len; i++)
        {
            PkPackage *package = g_ptr_array_index (array, i);
            g_autoptr (GsApp) app = NULL;
            guint64 size_download_bytes;

            app = gs_plugin_abroot_build_update_app (plugin, package);
            all_downloaded
                = (all_downloaded
                   && gs_app_get_size_download (app, &size_download_bytes)
                          == GS_SIZE_TYPE_VALID
                   && size_download_bytes == 0);
            if (all_downloaded && first_app == NULL)
                first_app = g_object_ref (app);
            gs_app_list_add (list, app);
        }
    /* Having all packages downloaded doesn't mean the update is also prepared,
       because the 'prepared-update' file can be missing, thus verify it and
       if not found, then set one application as needed download, to have
       the update properly prepared. */
    if (all_downloaded && first_app != NULL)
        {
            g_auto (GStrv) prepared_ids = NULL;
            /* It's an overhead to get all the package IDs, but there's no
               easier way to verify the prepared-update file exists. */
            prepared_ids = pk_offline_get_prepared_ids (NULL);
            if (prepared_ids == NULL || prepared_ids[0] == NULL)
                gs_app_set_size_download (first_app, GS_SIZE_TYPE_VALID, 1);
        }

    return TRUE;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin, GsAppList *list,
                       GCancellable *cancellable, GError **error)
{
    g_autoptr (GError) local_error = NULL;
    if (!gs_plugin_abroot_add_updates (plugin, list, cancellable,
                                           &local_error))
        g_debug ("Failed to get updates: %s", local_error->message);
    return TRUE;
}

static gboolean
gs_plugin_abroot_refresh_metadata_finish (GsPlugin *plugin,
                                          GAsyncResult *result, GError **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_abroot_class_init (GsPluginAbrootClass *klass)
{
    GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

    plugin_class->refresh_metadata_async
        = gs_plugin_abroot_refresh_metadata_async;
    plugin_class->refresh_metadata_finish
        = gs_plugin_abroot_refresh_metadata_finish;
}

GType
gs_plugin_query_type (void)
{
    return GS_TYPE_PLUGIN_ABROOT;
}
