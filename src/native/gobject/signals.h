#pragma once
#include <glib-object.h>
/** Receives the emitter and caller-owned state for a typed signal. */
typedef void (*SgVoidCallback)(void *instance, void *userdata);
/** Releases state after the final callback or a rejected connection. */
typedef void (*SgDestroyCallback)(void *userdata);
/** Callbacks are non-null named Sun functions. Ownership of userdata transfers
 * to the connection; destroy runs once on disconnect or object finalization. */
gulong sg_connect_void(gpointer object, const char *signal, SgVoidCallback callback,
                       gpointer userdata, SgDestroyCallback destroy);
/** Connects a property notification with the same userdata ownership contract. */
gulong sg_connect_notify(gpointer object, const char *property, SgVoidCallback callback,
                         gpointer userdata, SgDestroyCallback destroy);
