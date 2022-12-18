#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define LOG_MODULE "mem"
#define LOG_ENABLE_DBG 0
#define SMALLEST_INTERVAL 500
#include "../bar/bar.h"
#include "../config-verify.h"
#include "../config.h"
#include "../log.h"
#include "../plugin.h"

struct private
{
    struct particle *label;
    uint16_t interval;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);
    free(m);
    module_default_destroy(mod);
}

static const char *
description(const struct module *mod)
{
    return "mem";
}

static bool
get_mem_stats(uint64_t *mem_free, uint64_t *mem_total)
{
    bool mem_total_found = false;
    bool mem_free_found = false;

    FILE *fp = NULL;
    char *line = NULL;
    size_t len = 0;
    ssize_t read = 0;

    fp = fopen("/proc/meminfo", "r");
    if (NULL == fp) {
        LOG_ERRNO("unable to open /proc/meminfo");
        return false;
    }

    while ((read = getline(&line, &len, fp)) != -1) {
        if (strncmp(line, "MemTotal:", sizeof("MemTotal:") - 1) == 0) {
            read = sscanf(line + sizeof("MemTotal:") - 1, "%" SCNu64, mem_total);
            mem_total_found = (read == 1);
        }
        if (strncmp(line, "MemAvailable:", sizeof("MemAvailable:") - 1) == 0) {
            read = sscanf(line + sizeof("MemAvailable:"), "%" SCNu64, mem_free);
            mem_free_found = (read == 1);
        }
    }
    free(line);

    fclose(fp);

    return mem_free_found && mem_total_found;
}

static struct exposable *
content(struct module *mod)
{
    const struct private *p = mod->private;
    uint64_t mem_free = 0;
    uint64_t mem_used = 0;
    uint64_t mem_total = 0;

    if (!get_mem_stats(&mem_free, &mem_total)) {
        LOG_ERR("unable to retrieve the memory stats");
    }

    mem_used = mem_total - mem_free;

    double percent_used = ((double)mem_used * 100) / (mem_total + 1);
    double percent_free = ((double)mem_free * 100) / (mem_total + 1);

    struct tag_set tags = {
        .tags = (struct tag *[]){tag_new_int(mod, "free", mem_free * 1024), tag_new_int(mod, "used", mem_used * 1024),
                                 tag_new_int(mod, "total", mem_total * 1024),
                                 tag_new_int_range(mod, "percent_free", round(percent_free), 0, 100),
                                 tag_new_int_range(mod, "percent_used", round(percent_used), 0, 100)},
        .count = 5,
    };

    struct exposable *exposable = p->label->instantiate(p->label, &tags);
    tag_set_destroy(&tags);
    return exposable;
}

static int
run(struct module *mod)
{
    const struct bar *bar = mod->bar;
    bar->refresh(bar);
    struct private *p = mod->private;
    while (true) {
        struct pollfd fds[] = {{.fd = mod->abort_fd, .events = POLLIN}};

        int res = poll(fds, 1, p->interval);
        if (res < 0) {
            if (EINTR == errno) {
                continue;
            }

            LOG_ERRNO("unable to poll abort fd");
            return -1;
        }

        if (fds[0].revents & POLLIN)
            break;

        bar->refresh(bar);
    }

    return 0;
}

static struct module *
mem_new(uint16_t interval, struct particle *label)
{
    struct private *p = calloc(1, sizeof(*p));
    p->label = label;
    p->interval = interval;

    struct module *mod = module_common_new();
    mod->private = p;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    mod->description = &description;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *interval = yml_get_value(node, "poll-interval");
    const struct yml_node *c = yml_get_value(node, "content");

    return mem_new(
        interval == NULL ? SMALLEST_INTERVAL : yml_value_as_int(interval),
        conf_to_particle(c, inherited));
}

static bool
conf_verify_interval(keychain_t *chain, const struct yml_node *node)
{
    if (!conf_verify_unsigned(chain, node))
        return false;

    if (yml_value_as_int(node) < SMALLEST_INTERVAL) {
        LOG_ERR("%s: interval value cannot be less than %d ms", conf_err_prefix(chain, node), SMALLEST_INTERVAL);
        return false;
    }

    return true;
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"poll-interval", false, &conf_verify_interval},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_mem_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_mem_iface")));
#endif
