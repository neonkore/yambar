#include "clock.h"
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

#include <poll.h>

#include "../../bar.h"

struct private {
    struct particle *label;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);
    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    char time_str[1024];
    strftime(time_str, sizeof(time_str), "%H:%M", tm);

    char date_str[1024];
    strftime(date_str, sizeof(date_str), "%e %b", tm);

    struct tag_set tags = {
        .tags = (struct tag *[]){tag_new_string("time", time_str),
                                 tag_new_string("date", date_str)},
        .count = 2,
    };

    struct exposable *exposable = m->label->instantiate(m->label, &tags);

    tag_set_destroy(&tags);
    return exposable;
}

static int
run(struct module_run_context *ctx)
{
    const struct bar *bar = ctx->module->bar;

    write(ctx->ready_fd, &(uint64_t){1}, sizeof(uint64_t));

    while (true) {
        time_t now = time(NULL);
        time_t now_no_secs = now / 60 * 60;
        assert(now_no_secs % 60 == 0);

        time_t next_min = now_no_secs + 60;
        time_t timeout = next_min - now;
        assert(timeout >= 0 && timeout <= 60);

        struct pollfd fds[] = {{.fd = ctx->abort_fd, .events = POLLIN}};
        poll(fds, 1, timeout * 1000);

        if (fds[0].revents & POLLIN)
            break;

        bar->refresh(bar);
    }

    return 0;
}

struct module *
module_clock(struct particle *label)
{
    struct private *m = malloc(sizeof(*m));
    m->label = label;

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}
