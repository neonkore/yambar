#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <poll.h>

#include "../bar.h"
#include "../config.h"

struct private {
    struct particle *label;
    char *date_format;
    char *time_format;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);
    free(m->time_format);
    free(m->date_format);
    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    char date_str[1024];
    strftime(date_str, sizeof(date_str), m->date_format, tm);

    char time_str[1024];
    strftime(time_str, sizeof(time_str), m->time_format, tm);

    struct tag_set tags = {
        .tags = (struct tag *[]){tag_new_string(mod, "time", time_str),
                                 tag_new_string(mod, "date", date_str)},
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

    module_signal_ready(ctx);

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

static struct module *
clock_new(struct particle *label, const char *date_format, const char *time_format)
{
    struct private *m = malloc(sizeof(*m));
    m->label = label;
    m->date_format = strdup(date_format);
    m->time_format = strdup(time_format);

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *c = yml_get_value(node, "content");
    const struct yml_node *date_format = yml_get_value(node, "date-format");
    const struct yml_node *time_format = yml_get_value(node, "time-format");

    return clock_new(
        conf_to_particle(c, inherited),
        date_format != NULL ? yml_value_as_string(date_format) : "%x",
        time_format != NULL ? yml_value_as_string(time_format) : "%H:%M");
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"date-format", false, &conf_verify_string},
        {"time-format", false, &conf_verify_string},
        {"content", true, &conf_verify_particle},
        {"anchors", false, NULL},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_info plugin_info = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};
