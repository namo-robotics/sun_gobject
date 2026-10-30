/** Owns exclusive output files and process signals behind a small POSIX boundary. */
#include <glib.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

/** Retains one private output descriptor until publication or cleanup. */
typedef struct { int fd; char *path; char *error; } Output;
static volatile sig_atomic_t received_signal;

/** Records a signal using only signal-safe storage. */
static void interrupted(int number) { received_signal = number; }

/** Installs helper-process signal handling; this API is intended for a dedicated process. */
void sg_capture_signals(void) {
    struct sigaction action = {0};
    action.sa_handler = interrupted;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL);
}

/** Returns a persistent stop or cancel request without a read-clear race. */
int sg_capture_signal(void) { return received_signal == SIGTERM ? 2 : received_signal == SIGINT ? 1 : 0; }

/** Returns the configured media directory, falling back to the user's home. */
const char *sg_capture_directory(int video) {
    const char *path = g_get_user_special_dir(video ? G_USER_DIRECTORY_VIDEOS : G_USER_DIRECTORY_PICTURES);
    return path ? path : g_get_home_dir();
}

/** Returns an allocated filename component containing local time and a random identifier. */
char *sg_capture_stamp(void) {
    GDateTime *now = g_date_time_new_now_local();
    char *stamp = g_date_time_format(now, "%Y-%m-%d %H-%M-%S");
    char *uuid = g_uuid_string_random();
    char *result = g_strdup_printf("%s-%s", stamp, uuid);
    g_free(stamp); g_free(uuid); g_date_time_unref(now);
    return result;
}

/** Copies native UTF-8 text through the caller's bounded buffer. */
int sg_capture_text(const char *text, char *buffer, int length) {
    if (length <= 0) return 0;
    g_strlcpy(buffer, text, length);
    return (int)strlen(text);
}

/** Releases allocated strings returned by the adapter. */
void sg_capture_string_free(char *text) { g_free(text); }

/** Reports the host CPU count for an explicitly bounded encoder thread budget. */
int sg_capture_cpus(void) { return (int)g_get_num_processors(); }

/** Reserves an exclusive private file in the selected directory. */
void *sg_output_new(const char *directory) {
    Output *o = g_new0(Output, 1);
    o->fd = -1;
    if (g_mkdir_with_parents(directory, 0700) != 0) o->error = g_strdup(g_strerror(errno));
    else {
        o->path = g_build_filename(directory, ".sun-capture-XXXXXX", NULL);
        o->fd = g_mkstemp_full(o->path, O_RDWR | O_CLOEXEC, 0600);
        if (o->fd < 0) { o->error = g_strdup(g_strerror(errno)); g_clear_pointer(&o->path, g_free); }
    }
    return o;
}

/** Returns the owned output descriptor, or a negative value after failure or publication. */
int sg_output_fd(Output *o) { return o->fd; }

/** Returns a borrowed output diagnostic. */
const char *sg_output_error(Output *o) { return o->error ? o->error : "Cannot publish capture"; }

/** Flushes and links completed bytes without replacing an existing destination. */
int sg_output_publish(Output *o, const char *destination) {
    if (o->fd < 0 || fsync(o->fd) != 0) goto failure;
    int fd = o->fd;
    o->fd = -1;
    if (close(fd) != 0) goto failure;
    if (link(o->path, destination) != 0) goto failure;
    if (unlink(o->path) != 0) goto failure;
    g_clear_pointer(&o->path, g_free);
    return 1;
failure:
    g_free(o->error); o->error = g_strdup(g_strerror(errno));
    return 0;
}

/** Closes the descriptor and removes only the private file created by this owner. */
void sg_output_free(Output *o) {
    if (!o) return;
    if (o->fd >= 0) close(o->fd);
    if (o->path) unlink(o->path);
    g_free(o->path); g_free(o->error); g_free(o);
}
