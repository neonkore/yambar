#include "particle.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <sys/wait.h>

#define LOG_MODULE "particle"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "bar.h"

void
particle_default_destroy(struct particle *particle)
{
    if (particle->deco != NULL)
        particle->deco->destroy(particle->deco);
    free(particle->on_click_template);
    free(particle);
}

struct particle *
particle_common_new(int left_margin, int right_margin,
                    const char *on_click_template)
{
    struct particle *p = malloc(sizeof(*p));
    p->left_margin = left_margin;
    p->right_margin = right_margin;
    p->on_click_template = on_click_template != NULL ? strdup(on_click_template) : NULL;
    p->deco = NULL;
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
        const char *end = cmd + strlen(cmd);

        char *argv[1024];
        size_t tokens = 0;

        /* Tokenize the command string */
        for (char *ctx, *tok = strtok_r(cmd, " ", &ctx);
             tok != NULL;
             /*tok = strtok_r(NULL, " ", &ctx)*/)
        {
            argv[tokens++] = tok;

            /* Is the beginning of the next token a quote? */
            bool next_is_quoted = &tok[strlen(tok) + 1] < end &&
                tok[strlen(tok) + 1] == '"';
            tok = strtok_r(NULL, next_is_quoted ? "\"" : " ", &ctx);
        }

        /* NULL-terminate list (for execvp) */
        argv[tokens] = NULL;

        pid_t pid = fork();
        if (pid == -1)
            LOG_ERRNO("failed to run on_click handler (fork)");
        else if (pid > 0) {
            /* Parent */
            free(cmd);

            if (waitpid(pid, NULL, 0) == -1)
                LOG_ERRNO("failed to wait for on_click handler");
        } else {

            LOG_DBG("ARGV:");
            for (size_t i = 0; i < tokens; i++)
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
    struct exposable *exposable = malloc(sizeof(*exposable));
    exposable->particle = particle;
    exposable->private = NULL;
    exposable->width = 0;
    exposable->on_click = on_click != NULL ? strdup(on_click) : NULL;
    exposable->destroy = &exposable_default_destroy;
    exposable->on_mouse = &exposable_default_on_mouse;
    exposable->begin_expose = NULL;
    exposable->expose = NULL;
    return exposable;
}
