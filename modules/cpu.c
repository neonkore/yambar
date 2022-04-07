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

#include <sys/sysinfo.h>

#define LOG_MODULE "cpu"
#define LOG_ENABLE_DBG 0
#define SMALLEST_INTERVAL 500
#include "../bar/bar.h"
#include "../config-verify.h"
#include "../config.h"
#include "../log.h"
#include "../plugin.h"
struct cpu_stats {
    uint32_t *prev_cores_idle;
    uint32_t *prev_cores_nidle;

    uint32_t *cur_cores_idle;
    uint32_t *cur_cores_nidle;
};

struct private
{
    struct particle *label;
    uint16_t interval;
    struct cpu_stats cpu_stats;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);
    free(m->cpu_stats.prev_cores_idle);
    free(m->cpu_stats.prev_cores_nidle);
    free(m->cpu_stats.cur_cores_idle);
    free(m->cpu_stats.cur_cores_nidle);
    free(m);
    module_default_destroy(mod);
}

static const char *
description(struct module *mod)
{
    return "cpu";
}

static uint32_t
get_cpu_nb_cores()
{
    int nb_cores = get_nprocs();
    LOG_DBG("CPU count: %d", nb_cores);

    return nb_cores;
}

static bool
parse_proc_stat_line(const char *line, int32_t *core, uint32_t *user, uint32_t *nice, uint32_t *system, uint32_t *idle,
                     uint32_t *iowait, uint32_t *irq, uint32_t *softirq, uint32_t *steal, uint32_t *guest,
                     uint32_t *guestnice)
{
    if (line[sizeof("cpu") - 1] == ' ') {
        int read = sscanf(line,
                          "cpu %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
                          " %" SCNu32 " %" SCNu32 " %" SCNu32,
                          user, nice, system, idle, iowait, irq, softirq, steal, guest, guestnice);
        *core = -1;
        return read == 10;
    } else {
        int read = sscanf(line,
                          "cpu%" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
                          " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32,
                          core, user, nice, system, idle, iowait, irq, softirq, steal, guest, guestnice);
        return read == 11;
    }
}

static uint8_t
get_cpu_usage_percent(const struct cpu_stats *cpu_stats, int8_t core_idx)
{
    uint32_t prev_total = cpu_stats->prev_cores_idle[core_idx + 1] + cpu_stats->prev_cores_nidle[core_idx + 1];
    uint32_t cur_total = cpu_stats->cur_cores_idle[core_idx + 1] + cpu_stats->cur_cores_nidle[core_idx + 1];

    double totald = cur_total - prev_total;
    double nidled = cpu_stats->cur_cores_nidle[core_idx + 1] - cpu_stats->prev_cores_nidle[core_idx + 1];

    double percent = (nidled * 100) / (totald + 1);

    return round(percent);
}

static void
refresh_cpu_stats(struct cpu_stats *cpu_stats)
{
    uint32_t nb_cores = get_cpu_nb_cores();
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

    while ((read = getline(&line, &len, fp)) != -1) {
        if (strncmp(line, "cpu", sizeof("cpu") - 1) == 0) {
            if (!parse_proc_stat_line(line, &core, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal,
                                            &guest, &guestnice)
                || core < -1 || core >= (int32_t)nb_cores) {
                LOG_ERR("unable to parse /proc/stat line");
                goto exit;
            }

            cpu_stats->prev_cores_idle[core + 1] = cpu_stats->cur_cores_idle[core + 1];
            cpu_stats->prev_cores_nidle[core + 1] = cpu_stats->cur_cores_nidle[core + 1];

            cpu_stats->cur_cores_idle[core + 1] = idle + iowait;
            cpu_stats->cur_cores_nidle[core + 1] = user + nice + system + irq + softirq + steal;
        }
    }
exit:
    fclose(fp);
    free(line);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *p = mod->private;
    uint32_t nb_cores = get_cpu_nb_cores();

    char cpu_name[32];
    struct tag_set tags;
    tags.count = nb_cores + 1;
    tags.tags = calloc(tags.count, sizeof(*tags.tags));
    mtx_lock(&mod->lock);
    uint8_t cpu_usage = get_cpu_usage_percent(&p->cpu_stats, -1);
    tags.tags[0] = tag_new_int_range(mod, "cpu", cpu_usage, 0, 100);

    for (uint32_t i = 0; i < nb_cores; ++i) {
        uint8_t cpu_usage = get_cpu_usage_percent(&p->cpu_stats, i);
        snprintf(cpu_name, sizeof(cpu_name), "cpu%u", i);
        tags.tags[i + 1] = tag_new_int_range(mod, cpu_name, cpu_usage, 0, 100);
    }
    mtx_unlock(&mod->lock);

    struct exposable *exposable = p->label->instantiate(p->label, &tags);

    tag_set_destroy(&tags);
    free(tags.tags);
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
        refresh_cpu_stats(&p->cpu_stats);
        mtx_unlock(&mod->lock);
        bar->refresh(bar);
    }

    return 0;
}

static struct module *
cpu_new(uint16_t interval, struct particle *label)
{
    struct private *p = calloc(1, sizeof(*p));
    p->label = label;
    uint32_t nb_cores = get_cpu_nb_cores();
    p->interval = interval;
    p->cpu_stats.prev_cores_nidle = calloc(nb_cores + 1, sizeof(*p->cpu_stats.prev_cores_nidle));
    p->cpu_stats.prev_cores_idle = calloc(nb_cores + 1, sizeof(*p->cpu_stats.prev_cores_idle));

    p->cpu_stats.cur_cores_nidle = calloc(nb_cores + 1, sizeof(*p->cpu_stats.cur_cores_nidle));
    p->cpu_stats.cur_cores_idle = calloc(nb_cores + 1, sizeof(*p->cpu_stats.cur_cores_idle));

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
    const struct yml_node *interval = yml_get_value(node, "interval");
    const struct yml_node *c = yml_get_value(node, "content");

    return cpu_new(interval == NULL ? SMALLEST_INTERVAL : yml_value_as_int(interval), conf_to_particle(c, inherited));
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
        {"interval", false, &conf_verify_interval},
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
