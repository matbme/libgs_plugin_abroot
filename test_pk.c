#include <glib.h>
#include <packagekit-glib2/packagekit.h>

#include "gs-packagekit-helper.h"
#include "gs-packagekit-task.h"
#include "packagekit-common.h"

int
main ()
{
    PkBitfield filter;
    PkRepoDetail *rd;
    g_autoptr (PkTask) task_sources = NULL;
    const gchar *id;
    guint i;
    g_autoptr (GHashTable) hash = NULL;
    g_autoptr (PkResults) results = NULL;
    g_autoptr (GPtrArray) array = NULL;

    /* ask PK for the repo details */
    filter = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_SOURCE,
                                     PK_FILTER_ENUM_NOT_DEVELOPMENT, -1);

	results = pk_client_get_repo_list (PK_CLIENT (task_sources),
					   filter,
					   FALSE,
					   gs_packagekit_helper_cb, helper,
					   error);
}
