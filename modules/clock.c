#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include <poll.h>
#include <sys/time.h>

#define LOG_MODULE "clock"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config.h"
#include "../config-verify.h"
#include "../plugin.h"

struct private {
    struct particle *label;
    enum {
        UPDATE_GRANULARITY_SECONDS,
        UPDATE_GRANULARITY_MINUTES,
    } update_granularity;
    char *date_format;
    char *time_format;
    bool utc;
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

static const char *
description(struct module *mod)
{
    return "clock";
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;
    time_t t = time(NULL);
    struct tm *tm = m->utc ? gmtime(&t) : localtime(&t);

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
run(struct module *mod)
{
    const struct private *m = mod->private;
    const struct bar *bar = mod->bar;
    bar->refresh(bar);

    while (true) {
        struct timespec _now;
        clock_gettime(CLOCK_REALTIME, &_now);

        const struct timeval now = {
            .tv_sec = _now.tv_sec,
            .tv_usec = _now.tv_nsec / 1000,
        };

        int timeout_ms = 1000;

        switch (m->update_granularity) {
        case UPDATE_GRANULARITY_SECONDS: {
            const struct timeval next_second = {
                .tv_sec = now.tv_sec + 1,
                .tv_usec = 0};

            struct timeval _timeout;
            timersub(&next_second, &now, &_timeout);

            assert(_timeout.tv_sec == 0 ||
                   (_timeout.tv_sec == 1 && _timeout.tv_usec == 0));
            timeout_ms = _timeout.tv_usec / 1000;
            break;
        }

        case UPDATE_GRANULARITY_MINUTES: {
            const struct timeval next_minute = {
                .tv_sec = now.tv_sec / 60 * 60 + 60,
                .tv_usec = 0,
            };

            struct timeval _timeout;
            timersub(&next_minute, &now, &_timeout);
            timeout_ms = _timeout.tv_sec * 1000 + _timeout.tv_usec / 1000;
        }
        }

        /* Add 1ms to account for rounding errors */
        timeout_ms++;

        LOG_DBG("now: %lds %ldµs -> timeout: %dms",
                now.tv_sec, now.tv_usec, timeout_ms);

        struct pollfd fds[] = {{.fd = mod->abort_fd, .events = POLLIN}};
        poll(fds, 1, timeout_ms);

        if (fds[0].revents & POLLIN)
            break;

        bar->refresh(bar);
    }

    return 0;
}

static struct module *
clock_new(struct particle *label, const char *date_format,
          const char *time_format, bool utc)
{
    struct private *m = calloc(1, sizeof(*m));
    m->label = label;
    m->date_format = strdup(date_format);
    m->time_format = strdup(time_format);
    m->utc = utc;

    static const char *const seconds_formatters[] = {
        "%c",
        "%s",
        "%S",
        "%T",
        "%r",
        "%X",
    };

    m->update_granularity = UPDATE_GRANULARITY_MINUTES;

    for (size_t i = 0;
         i < sizeof(seconds_formatters) / sizeof(seconds_formatters[0]);
         i++)
    {
        if (strstr(time_format, seconds_formatters[i]) != NULL) {
            m->update_granularity = UPDATE_GRANULARITY_SECONDS;
            break;
        }
    }

    LOG_DBG("using %s update granularity",
            (m->update_granularity == UPDATE_GRANULARITY_MINUTES
             ? "minutes" : "seconds"));

    struct module *mod = module_common_new();
    mod->private = m;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->description = &description;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *c = yml_get_value(node, "content");
    const struct yml_node *date_format = yml_get_value(node, "date-format");
    const struct yml_node *time_format = yml_get_value(node, "time-format");
    const struct yml_node *utc = yml_get_value(node, "utc");

    return clock_new(
        conf_to_particle(c, inherited),
        date_format != NULL ? yml_value_as_string(date_format) : "%x",
        time_format != NULL ? yml_value_as_string(time_format) : "%H:%M",
        utc != NULL ? yml_value_as_bool(utc) : false);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"date-format", false, &conf_verify_string},
        {"time-format", false, &conf_verify_string},
        {"utc", false, &conf_verify_bool},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_clock_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_clock_iface")));
#endif
