#include "particle.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#define LOG_MODULE "particle"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "bar/bar.h"

void
particle_default_destroy(struct particle *particle)
{
    if (particle->deco != NULL)
        particle->deco->destroy(particle->deco);
    fcft_destroy(particle->font);
    for (size_t i = 0; i < MOUSE_BTN_COUNT; i++)
        free(particle->on_click_templates[i]);
    free(particle);
}

struct particle *
particle_common_new(int left_margin, int right_margin,
                    const char **on_click_templates,
                    struct fcft_font *font, enum font_shaping font_shaping,
                    pixman_color_t foreground, struct deco *deco)
{
    struct particle *p = calloc(1, sizeof(*p));
    p->left_margin = left_margin;
    p->right_margin = right_margin;
    p->foreground = foreground;
    p->font = font;
    p->font_shaping = font_shaping;
    p->deco = deco;

    if (on_click_templates != NULL) {
        for (size_t i = 0; i < MOUSE_BTN_COUNT; i++) {
            if (on_click_templates[i] != NULL) {
                p->have_on_click_template = true;
                p->on_click_templates[i] = strdup(on_click_templates[i]);
            }
        }
    }

    return p;
}

void
exposable_default_destroy(struct exposable *exposable)
{
    for (size_t i = 0; i < MOUSE_BTN_COUNT; i++)
        free(exposable->on_click[i]);
    free(exposable);
}

void
exposable_render_deco(const struct exposable *exposable,
                      pixman_image_t *pix, int x, int y, int height)
{
    const struct deco *deco = exposable->particle->deco;
    if (deco != NULL)
        deco->expose(deco, pix, x, y, exposable->width, height);

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
                           enum mouse_event event, enum mouse_button btn,
                           int x, int y)
{
#if defined(LOG_ENABLE_DBG) && LOG_ENABLE_DBG
    static const char *button_name[] = {
        [MOUSE_BTN_NONE] = "none",
        [MOUSE_BTN_LEFT] = "left",
        [MOUSE_BTN_MIDDLE] = "middle",
        [MOUSE_BTN_RIGHT] = "right",
        [MOUSE_BTN_COUNT] = "count",
        [MOUSE_BTN_WHEEL_UP] = "wheel-up",
        [MOUSE_BTN_WHEEL_DOWN] = "wheel-down",
    };
    LOG_DBG("on_mouse: exposable=%p, event=%s, btn=%s, x=%d, y=%d (on-click=%s)",
            exposable, event == ON_MOUSE_MOTION ? "motion" : "click",
            button_name[btn], x, y, exposable->on_click[btn]);
#endif

    /* If we have a handler, change cursor to a hand */
    const char *cursor =
        (exposable->particle != NULL &&
         exposable->particle->have_on_click_template)
        ? "hand2"
        : "left_ptr";
    bar->set_cursor(bar, cursor);

    /* If this is a mouse click, and we have a handler, execute it */
    if (exposable->on_click[btn] != NULL && event == ON_MOUSE_CLICK) {
        /* Need a writeable copy, whose scope *we* control */
        char *cmd = strdup(exposable->on_click[btn]);
        LOG_DBG("cmd = \"%s\"", exposable->on_click[btn]);

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

            int wstatus;
            if (waitpid(pid, &wstatus, 0) == -1)
                LOG_ERRNO("%s: failed to wait for on_click handler", exposable->on_click[btn]);

            if (WIFEXITED(wstatus)) {
                if (WEXITSTATUS(wstatus) != 0)
                    LOG_ERRNO_P(WEXITSTATUS(wstatus), "%s: failed to execute", exposable->on_click[btn]);
            } else
                LOG_ERR("%s: did not exit normally", exposable->on_click[btn]);

            LOG_DBG("%s: launched", exposable->on_click[btn]);
        } else {
            /*
             * Use a pipe with O_CLOEXEC to communicate exec() failure
             * to parent process.
             *
             * If the child succeeds with exec(), the pipe is simply
             * closed. If it fails, we write the fail reason (errno)
             * to the pipe. The parent reads the pipe; if it receives
             * data, the child failed, else it succeeded.
             */
            int pipe_fds[2];
            if (pipe2(pipe_fds, O_CLOEXEC) == -1) {
                LOG_ERRNO("%s: failed to create pipe", cmd);
                free(cmd);
                return;
            }

            LOG_DBG("ARGV:");
            for (size_t i = 0; argv[i] != NULL; i++)
                LOG_DBG("  #%zu: \"%s\" ", i, argv[i]);

            LOG_DBG("double forking");

            switch (fork()) {
            case -1:
                close(pipe_fds[0]);
                close(pipe_fds[1]);

                LOG_ERRNO("failed to double fork");
                _exit(errno);
                break;

            case 0:
                /* Child */
                close(pipe_fds[0]);  /* Close read end */

                LOG_DBG("executing on-click handler: %s", cmd);

                sigset_t mask;
                sigemptyset(&mask);

                const struct sigaction sa = {.sa_handler = SIG_DFL};
                if (sigaction(SIGINT, &sa, NULL) < 0 ||
                    sigaction(SIGTERM, &sa, NULL) < 0 ||
                    sigaction(SIGCHLD, &sa, NULL) < 0 ||
                    sigprocmask(SIG_SETMASK, &mask, NULL) < 0)
                {
                    goto fail;
                }

                /* Redirect stdin/stdout/stderr to /dev/null */
                int dev_null_r = open("/dev/null", O_RDONLY | O_CLOEXEC);
                int dev_null_w = open("/dev/null", O_WRONLY | O_CLOEXEC);

                if (dev_null_r == -1 || dev_null_w == -1) {
                    LOG_ERRNO("/dev/null: failed to open");
                    goto fail;
                }

                if (dup2(dev_null_r, STDIN_FILENO) == -1 ||
                    dup2(dev_null_w, STDOUT_FILENO) == -1 ||
                    dup2(dev_null_w, STDERR_FILENO) == -1)
                {
                    LOG_ERRNO("failed to redirect stdin/stdout/stderr");
                    goto fail;
                }

                execvp(argv[0], argv);

            fail:
                /* Signal failure to parent process */
                (void)!write(pipe_fds[1], &errno, sizeof(errno));
                close(pipe_fds[1]);
                _exit(errno);
                break;

            default:
                /* Parent */
                close(pipe_fds[1]); /* Close write end */

                int _errno = 0;
                ssize_t ret = read(pipe_fds[0], &_errno, sizeof(_errno));
                close(pipe_fds[0]);

                if (ret == 0) {
                    /* Pipe was closed - child succeeded with exec() */
                    _exit(0);
                }

                LOG_DBG("second pipe failed: %s (%d)", strerror(_errno), _errno);
                _exit(_errno);
                break;
            }
        }
    }
}

struct exposable *
exposable_common_new(const struct particle *particle, const struct tag_set *tags)
{
    struct exposable *exposable = calloc(1, sizeof(*exposable));
    exposable->particle = particle;

    if (particle != NULL && particle->have_on_click_template) {
        tags_expand_templates(
            exposable->on_click,
            (const char **)particle->on_click_templates,
            MOUSE_BTN_COUNT, tags);
    }
    exposable->destroy = &exposable_default_destroy;
    exposable->on_mouse = &exposable_default_on_mouse;
    return exposable;
}
