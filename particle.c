#include "particle.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <sys/wait.h>

#define LOG_MODULE "particle"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "bar/bar.h"

void
particle_default_destroy(struct particle *particle)
{
    if (particle->deco != NULL)
        particle->deco->destroy(particle->deco);
    font_destroy(particle->font);
    free(particle->on_click_template);
    free(particle);
}

struct particle *
particle_common_new(int left_margin, int right_margin,
                    const char *on_click_template,
                    struct font *font, struct rgba foreground,
                    struct deco *deco)
{
    struct particle *p = calloc(1, sizeof(*p));
    p->left_margin = left_margin;
    p->right_margin = right_margin;
    p->on_click_template =
        on_click_template != NULL ? strdup(on_click_template) : NULL;
    p->foreground = foreground;
    p->font = font;
    p->deco = deco;
    return p;
}

void
exposable_default_destroy(struct exposable *exposable)
{
    free(exposable->on_click);
    free(exposable);
}

void
exposable_render_deco(const struct exposable *exposable,
                      cairo_t *cr, int x, int y, int height)
{
    const struct deco *deco = exposable->particle->deco;
    if (deco != NULL)
        deco->expose(deco, cr, x, y, exposable->width, height);

}

static bool
push_argv(char ***argv, size_t *size, char *arg, size_t *argc)
{
    if (arg != NULL && arg[0] == '%')
        return true;

    if (*argc >= *size) {
        size_t new_size = *size > 0 ? 2 * *size : 10;
        char **new_argv = realloc(*argv, new_size * sizeof(new_argv[0]));

        if (new_argv == NULL)
            return false;

        *argv = new_argv;
        *size = new_size;
    }

    (*argv)[(*argc)++] = arg;
    return true;
}

static bool
tokenize_cmdline(char *cmdline, char ***argv)
{
    *argv = NULL;
    size_t argv_size = 0;

    bool first_token_is_quoted = cmdline[0] == '"' || cmdline[0] == '\'';
    char delim = first_token_is_quoted ? cmdline[0] : ' ';

    char *p = first_token_is_quoted ? &cmdline[1] : &cmdline[0];

    size_t idx = 0;
    while (*p != '\0') {
        char *end = strchr(p, delim);
        if (end == NULL) {
            if (delim != ' ') {
                LOG_ERR("unterminated %s quote\n", delim == '"' ? "double" : "single");
                free(*argv);
                return false;
            }

            if (!push_argv(argv, &argv_size, p, &idx) ||
                !push_argv(argv, &argv_size, NULL, &idx))
            {
                goto err;
            } else
                return true;
        }

        *end = '\0';

        if (!push_argv(argv, &argv_size, p, &idx))
            goto err;

        p = end + 1;
        while (*p == delim)
            p++;

        while (*p == ' ')
            p++;

        if (*p == '"' || *p == '\'') {
            delim = *p;
            p++;
        } else
            delim = ' ';
    }

    if (!push_argv(argv, &argv_size, NULL, &idx))
        goto err;

    return true;

err:
    free(*argv);
    return false;
}

void
exposable_default_on_mouse(struct exposable *exposable, struct bar *bar,
                           enum mouse_event event, int x, int y)
{
    LOG_DBG("on_mouse: exposable=%p, event=%s, x=%d, y=%d", exposable,
            event == ON_MOUSE_MOTION ? "motion" : "click", x, y);

    /* If we have a handler, change cursor to a hand */
    bar->set_cursor(bar, exposable->on_click == NULL ? "left_ptr" : "hand2");

    /* If this is a mouse click, and we have a handler, execute it */
    if (exposable->on_click != NULL && event == ON_MOUSE_CLICK) {
        /* Need a writeable copy, whose scope *we* control */
        char *cmd = strdup(exposable->on_click);
        LOG_DBG("cmd = \"%s\"", cmd);

        char **argv;
        if (!tokenize_cmdline(cmd, &argv)) {
            free(cmd);
            return;
        }

        pid_t pid = fork();
        if (pid == -1)
            LOG_ERRNO("failed to run on_click handler (fork)");
        else if (pid > 0) {
            /* Parent */
            free(cmd);
            free(argv);

            if (waitpid(pid, NULL, 0) == -1)
                LOG_ERRNO("failed to wait for on_click handler");
        } else {

            LOG_DBG("ARGV:");
            for (size_t i = 0; argv[i] != NULL; i++)
                LOG_DBG("  #%zu: \"%s\" ", i, argv[i]);

            LOG_DBG("daemonizing on-click handler");
            daemon(0, 0);

            LOG_DBG("executing on-click handler: %s", cmd);
            execvp(argv[0], argv);

            LOG_ERRNO("failed to run on_click handler (exec)");
        }
    }
}

struct exposable *
exposable_common_new(const struct particle *particle, const char *on_click)
{
    struct exposable *exposable = calloc(1, sizeof(*exposable));
    exposable->particle = particle;
    exposable->on_click = on_click != NULL ? strdup(on_click) : NULL;
    exposable->destroy = &exposable_default_destroy;
    exposable->on_mouse = &exposable_default_on_mouse;
    return exposable;
}
