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
#include <unistd.h>

#define LOG_MODULE "cpu"
#define LOG_ENABLE_DBG 0
#include "../log.h"

#include "../bar/bar.h"
#include "../config-verify.h"
#include "../config.h"
#include "../particles/dynlist.h"
#include "../plugin.h"

static const long min_poll_interval = 250;

struct cpu_stats {
    uint32_t *prev_cores_idle;
    uint32_t *prev_cores_nidle;

    uint32_t *cur_cores_idle;
    uint32_t *cur_cores_nidle;
};

struct private {
    struct particle *template;
    uint16_t interval;
    size_t core_count;
    struct cpu_stats cpu_stats;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;

    m->template->destroy(m->template);
    free(m->cpu_stats.prev_cores_idle);
    free(m->cpu_stats.prev_cores_nidle);
    free(m->cpu_stats.cur_cores_idle);
    free(m->cpu_stats.cur_cores_nidle);
    free(m);

    module_default_destroy(mod);
}

static const char *
description(const struct module *mod)
{
    return "cpu";
}

static uint32_t
get_cpu_nb_cores()
{
    int nb_cores = sysconf(_SC_NPROCESSORS_ONLN);
    LOG_DBG("CPU count: %d", nb_cores);

    return nb_cores;
}

static bool
parse_proc_stat_line(const char *line, uint32_t *user, uint32_t *nice,
                     uint32_t *system, uint32_t *idle, uint32_t *iowait,
                     uint32_t *irq, uint32_t *softirq, uint32_t *steal,
                     uint32_t *guest, uint32_t *guestnice)
{
    int32_t core_id;
    if (line[sizeof("cpu") - 1] == ' ') {
        int read = sscanf(
            line,
            "cpu %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
            " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32,
            user, nice, system, idle, iowait, irq, softirq, steal, guest,
            guestnice);
        return read == 10;
    } else {
        int read = sscanf(
            line,
            "cpu%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
            " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
            " %" SCNu32,
            &core_id, user, nice, system, idle, iowait, irq, softirq, steal,
            guest, guestnice);
        return read == 11;
    }
}

static uint8_t
get_cpu_usage_percent(const struct cpu_stats *cpu_stats, int8_t core_idx)
{
    uint32_t prev_total =
        cpu_stats->prev_cores_idle[core_idx + 1] +
        cpu_stats->prev_cores_nidle[core_idx + 1];

    uint32_t cur_total =
        cpu_stats->cur_cores_idle[core_idx + 1] +
        cpu_stats->cur_cores_nidle[core_idx + 1];

    double totald = cur_total - prev_total;
    double nidled =
        cpu_stats->cur_cores_nidle[core_idx + 1] -
        cpu_stats->prev_cores_nidle[core_idx + 1];

    double percent = (nidled * 100) / (totald + 1);
    return round(percent);
}

static void
refresh_cpu_stats(struct cpu_stats *cpu_stats, size_t core_count)
{
    int32_t core = 0;
    uint32_t user = 0;
    uint32_t nice = 0;
    uint32_t system = 0;
    uint32_t idle = 0;
    uint32_t iowait = 0;
    uint32_t irq = 0;
    uint32_t softirq = 0;
    uint32_t steal = 0;
    uint32_t guest = 0;
    uint32_t guestnice = 0;

    FILE *fp = NULL;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    fp = fopen("/proc/stat", "r");
    if (NULL == fp) {
        LOG_ERRNO("unable to open /proc/stat");
        return;
    }

    while ((read = getline(&line, &len, fp)) != -1 && core <= core_count) {
        if (strncmp(line, "cpu", sizeof("cpu") - 1) == 0) {
            if (!parse_proc_stat_line(
                    line, &user, &nice, &system, &idle, &iowait, &irq, &softirq,
                    &steal, &guest, &guestnice))
            {
                LOG_ERR("unable to parse /proc/stat line");
                goto exit;
            }

            cpu_stats->prev_cores_idle[core] = cpu_stats->cur_cores_idle[core];
            cpu_stats->prev_cores_nidle[core] = cpu_stats->cur_cores_nidle[core];

            cpu_stats->cur_cores_idle[core] = idle + iowait;
            cpu_stats->cur_cores_nidle[core] = user + nice + system + irq + softirq + steal;

            core++;
        }
    }

exit:
    fclose(fp);
    free(line);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&mod->lock);

    const size_t list_count = m->core_count + 1;
    struct exposable *parts[list_count];

    {
        uint8_t total_usage = get_cpu_usage_percent(&m->cpu_stats, -1);

        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_int(mod, "id", -1),
                tag_new_int_range(mod, "cpu", total_usage, 0, 100),
            },
            .count = 2,
        };

        parts[0] = m->template->instantiate(m->template, &tags);
        tag_set_destroy(&tags);
    }

    for (size_t i = 0; i < m->core_count; i++) {
        uint8_t core_usage = get_cpu_usage_percent(&m->cpu_stats, i);

        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_int(mod, "id", i),
                tag_new_int_range(mod, "cpu", core_usage, 0, 100),
            },
            .count = 2,
        };

        parts[i + 1] = m->template->instantiate(m->template, &tags);
        tag_set_destroy(&tags);
    }

    mtx_unlock(&mod->lock);
    return dynlist_exposable_new(parts, list_count, 0, 0);
}

static int
run(struct module *mod)
{
    const struct bar *bar = mod->bar;
    bar->refresh(bar);

    struct private *p = mod->private;
    while (true) {
        struct pollfd fds[] = {{.fd = mod->abort_fd, .events = POLLIN}};

        int res = poll(fds, sizeof(fds) / sizeof(*fds), p->interval);

        if (res < 0) {
            if (EINTR == errno)
                continue;
            LOG_ERRNO("unable to poll abort fd");
            return -1;
        }

        if (fds[0].revents & POLLIN)
            break;

        mtx_lock(&mod->lock);
        refresh_cpu_stats(&p->cpu_stats, p->core_count);
        mtx_unlock(&mod->lock);
        bar->refresh(bar);
    }

    return 0;
}

static struct module *
cpu_new(uint16_t interval, struct particle *template)
{
    uint32_t nb_cores = get_cpu_nb_cores();

    struct private *p = calloc(1, sizeof(*p));
    p->template = template;
    p->interval = interval;
    p->core_count = nb_cores;

    p->cpu_stats.prev_cores_nidle = calloc(
        nb_cores + 1, sizeof(*p->cpu_stats.prev_cores_nidle));
    p->cpu_stats.prev_cores_idle = calloc(
        nb_cores + 1, sizeof(*p->cpu_stats.prev_cores_idle));

    p->cpu_stats.cur_cores_nidle = calloc(
        nb_cores + 1, sizeof(*p->cpu_stats.cur_cores_nidle));
    p->cpu_stats.cur_cores_idle = calloc(
        nb_cores + 1, sizeof(*p->cpu_stats.cur_cores_idle));

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

    return cpu_new(
        interval == NULL ? min_poll_interval : yml_value_as_int(interval),
        conf_to_particle(c, inherited));
}

static bool
conf_verify_poll_interval(keychain_t *chain, const struct yml_node *node)
{
    if (!conf_verify_unsigned(chain, node))
        return false;

    if (yml_value_as_int(node) < min_poll_interval) {
        LOG_ERR("%s: interval value cannot be less than %ldms",
                conf_err_prefix(chain, node), min_poll_interval);
        return false;
    }

    return true;
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"poll-interval", false, &conf_verify_poll_interval},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_cpu_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_cpu_iface")));
#endif
