#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#include <poll.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <libudev.h>

#define LOG_MODULE "battery"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config.h"
#include "../config-verify.h"
#include "../plugin.h"

enum state { STATE_FULL, STATE_CHARGING, STATE_DISCHARGING };

struct private {
    struct particle *label;

    int poll_interval;
    char *battery;
    char *manufacturer;
    char *model;
    long energy_full_design;
    long energy_full;
    long charge_full_design;
    long charge_full;

    enum state state;
    long capacity;
    long energy;
    long power;
    long charge;
    long current;
    long time_to_empty;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    free(m->battery);
    free(m->manufacturer);
    free(m->model);

    m->label->destroy(m->label);

    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&mod->lock);

    assert(m->state == STATE_FULL ||
           m->state == STATE_CHARGING ||
           m->state == STATE_DISCHARGING);

    unsigned long hours;
    unsigned long minutes;

    if (m->time_to_empty >= 0) {
        hours = m->time_to_empty / 60;
        minutes = m->time_to_empty % 60;
    } else if  (m->energy_full >= 0 && m->charge && m->power >= 0) {
        unsigned long energy = m->state == STATE_CHARGING
            ? m->energy_full - m->energy : m->energy;

        double hours_as_float;
        if (m->state == STATE_FULL)
            hours_as_float = 0.0;
        else if (m->power > 0)
            hours_as_float = (double)energy / m->power;
        else
            hours_as_float = 99.0;

        hours = hours_as_float;
        minutes = (hours_as_float - (double)hours) * 60;
    } else if (m->charge_full >= 0 && m->charge >= 0 && m->current >= 0) {
        unsigned long charge = m->state == STATE_CHARGING
            ? m->charge_full - m->charge : m->charge;

        double hours_as_float;
        if (m->state == STATE_FULL)
            hours_as_float = 0.0;
        else if (m->current > 0)
            hours_as_float = (double)charge / m->current;
        else
            hours_as_float = 99.0;

        hours = hours_as_float;
        minutes = (hours_as_float - (double)hours) * 60;
    } else {
        hours = 99;
        minutes = 0;
    }

    char estimate[64];
    snprintf(estimate, sizeof(estimate), "%02lu:%02lu", hours, minutes);

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_string(mod, "name", m->battery),
            tag_new_string(mod, "manufacturer", m->manufacturer),
            tag_new_string(mod, "model", m->model),
            tag_new_string(mod, "state",
                           m->state == STATE_FULL ? "full" :
                           m->state == STATE_CHARGING ? "charging" :
                           m->state == STATE_DISCHARGING ? "discharging" :
                           "unknown"),
            tag_new_int_range(mod, "capacity", m->capacity, 0, 100),
            tag_new_string(mod, "estimate", estimate),
        },
        .count = 6,
    };

    mtx_unlock(&mod->lock);

    struct exposable *exposable = m->label->instantiate(m->label, &tags);

    tag_set_destroy(&tags);
    return exposable;
}

static const char *
readline_from_fd(int fd)
{
    static char buf[4096];

    ssize_t sz = read(fd, buf, sizeof(buf) - 1);
    lseek(fd, 0, SEEK_SET);

    if (sz < 0) {
        LOG_WARN("failed to read from FD=%d", fd);
        return NULL;
    }

    buf[sz] = '\0';
    for (ssize_t i = sz - 1; i >= 0 && buf[i] == '\n'; sz--)
        buf[i] = '\0';

    return buf;
}

static long
readint_from_fd(int fd)
{
    const char *s = readline_from_fd(fd);
    if (s == NULL)
        return 0;

    long ret;
    int r = sscanf(s, "%ld", &ret);
    if (r != 1) {
        LOG_WARN("failed to convert \"%s\" to an integer", s);
        return 0;
    }

    return ret;
}

static int
initialize(struct private *m)
{
    int pw_fd = open("/sys/class/power_supply", O_RDONLY);
    if (pw_fd == -1) {
        LOG_ERRNO("/sys/class/power_supply");
        return -1;
    }

    int base_dir_fd = openat(pw_fd, m->battery, O_RDONLY);
    close(pw_fd);

    if (base_dir_fd == -1) {
        LOG_ERRNO("%s", m->battery);
        return -1;
    }

    {
        int fd = openat(base_dir_fd, "manufacturer", O_RDONLY);
        if (fd == -1) {
            LOG_WARN("/sys/class/power_supply/%s/manufacturer: %s",
                     m->battery, strerror(errno));
            m->manufacturer = NULL;
        } else {
            m->manufacturer = strdup(readline_from_fd(fd));
            close(fd);
        }
    }

    {
        int fd = openat(base_dir_fd, "model_name", O_RDONLY);
        if (fd == -1) {
            LOG_WARN("/sys/class/power_supply/%s/model_name: %s",
                     m->battery, strerror(errno));
            m->model = NULL;
        } else {
            m->model = strdup(readline_from_fd(fd));
            close(fd);
        }
    }

    if (faccessat(base_dir_fd, "energy_full_design", O_RDONLY, 0) == 0 &&
        faccessat(base_dir_fd, "energy_full", O_RDONLY, 0) == 0)
    {
        {
            int fd = openat(base_dir_fd, "energy_full_design", O_RDONLY);
            if (fd == -1) {
                LOG_ERRNO("/sys/class/power_supply/%s/energy_full_design", m->battery);
                goto err;
            }

            m->energy_full_design = readint_from_fd(fd);
            close(fd);
        }

        {
            int fd = openat(base_dir_fd, "energy_full", O_RDONLY);
            if (fd == -1) {
                LOG_ERRNO("/sys/class/power_supply/%s/energy_full", m->battery);
                goto err;
            }

            m->energy_full = readint_from_fd(fd);
            close(fd);
        }
    } else {
        m->energy_full = m->energy_full_design = -1;
    }

    if (faccessat(base_dir_fd, "charge_full_design", O_RDONLY, 0) == 0 &&
        faccessat(base_dir_fd, "charge_full", O_RDONLY, 0) == 0)
    {
        {
            int fd = openat(base_dir_fd, "charge_full_design", O_RDONLY);
            if (fd == -1) {
                LOG_ERRNO("/sys/class/power_supply/%s/charge_full_design", m->battery);
                goto err;
            }

            m->charge_full_design = readint_from_fd(fd);
            close(fd);
        }

        {
            int fd = openat(base_dir_fd, "charge_full", O_RDONLY);
            if (fd == -1) {
                LOG_ERRNO("/sys/class/power_supply/%s/charge_full", m->battery);
                goto err;
            }

            m->charge_full = readint_from_fd(fd);
            close(fd);
        }
    } else {
        m->charge_full = m->charge_full_design = -1;
    }

    return base_dir_fd;

err:
    close(base_dir_fd);
    return -1;
}

static void
update_status(struct module *mod, int capacity_fd, int energy_fd, int power_fd,
              int charge_fd, int current_fd, int status_fd, int time_to_empty_fd)
{
    struct private *m = mod->private;

    long capacity = readint_from_fd(capacity_fd);
    long energy = energy_fd >= 0 ? readint_from_fd(energy_fd) : -1;
    long power = power_fd >= 0 ? readint_from_fd(power_fd) : -1;
    long charge = charge_fd >= 0 ? readint_from_fd(charge_fd) : -1;
    long current = current_fd >= 0 ? readint_from_fd(current_fd) : -1;
    long time_to_empty = time_to_empty_fd >= 0 ? readint_from_fd(time_to_empty_fd) : -1;

    const char *status = readline_from_fd(status_fd);
    enum state state;

    if (strcmp(status, "Full") == 0)
        state = STATE_FULL;
    else if (strcmp(status, "Charging") == 0)
        state = STATE_CHARGING;
    else if (strcmp(status, "Discharging") == 0)
        state = STATE_DISCHARGING;
    else if (strcmp(state, "Not charging") == 0)
        state = STATE_DISCHARGING;
    else if (strcmp(status, "Unknown") == 0)
        state = STATE_DISCHARGING;
    else {
        LOG_ERR("unrecognized battery state: %s", status);
        state = STATE_DISCHARGING;
    }

    LOG_DBG("capacity: %ld, energy: %ld, power: %ld, charge=%ld, current=%ld, "
            "time-to-empty: %ld", capacity, energy, power, charge, current,
            time_to_empty);

    mtx_lock(&mod->lock);
    m->state = state;
    m->capacity = capacity;
    m->energy = energy;
    m->power = power;
    m->charge = charge;
    m->current = current;
    m->time_to_empty = time_to_empty;
    mtx_unlock(&mod->lock);
}

static int
run(struct module *mod)
{
    const struct bar *bar = mod->bar;
    struct private *m = mod->private;

    int base_dir_fd = initialize(m);
    if (base_dir_fd == -1)
        return -1;

    LOG_INFO("%s: %s %s (at %.1f%% of original capacity)",
             m->battery, m->manufacturer, m->model,
             (m->energy_full > 0
              ? 100.0 * m->energy_full / m->energy_full_design
              : m->charge_full > 0
              ? 100.0 * m->charge_full / m->charge_full_design
              : 0.0));

    int ret = 1;
    int status_fd = openat(base_dir_fd, "status", O_RDONLY);
    int capacity_fd = openat(base_dir_fd, "capacity", O_RDONLY);
    int energy_fd = openat(base_dir_fd, "energy_now", O_RDONLY);
    int power_fd = openat(base_dir_fd, "power_now", O_RDONLY);
    int charge_fd = openat(base_dir_fd, "charge_now", O_RDONLY);
    int current_fd = openat(base_dir_fd, "current_now", O_RDONLY);
    int time_to_empty_fd = openat(base_dir_fd, "time_to_empty_now", O_RDONLY);

    struct udev *udev = udev_new();
    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");

    if (status_fd < 0 || capacity_fd < 0 || udev == NULL || mon == NULL)
        goto out;

    udev_monitor_filter_add_match_subsystem_devtype(mon, "power_supply", NULL);
    udev_monitor_enable_receiving(mon);

    update_status(
        mod, capacity_fd, energy_fd, power_fd,
        charge_fd, current_fd, status_fd, time_to_empty_fd);
    bar->refresh(bar);

    while (true) {
        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = udev_monitor_get_fd(mon), .events = POLLIN},
        };
        poll(fds, 2, m->poll_interval > 0 ? m->poll_interval * 1000 : -1);

        if (fds[0].revents & POLLIN) {
            ret = 0;
            break;
        }

        if (fds[1].revents & POLLIN) {
            struct udev_device *dev = udev_monitor_receive_device(mon);
            const char *sysname = udev_device_get_sysname(dev);

            bool is_us = sysname != NULL && strcmp(sysname, m->battery) == 0;
            udev_device_unref(dev);

            if (!is_us)
                continue;
        }

        update_status(
            mod, capacity_fd, energy_fd, power_fd,
            charge_fd, current_fd, status_fd, time_to_empty_fd);
        bar->refresh(bar);
    }

out:
    if (mon != NULL)
        udev_monitor_unref(mon);
    if (udev != NULL)
        udev_unref(udev);

    if (power_fd != -1)
        close(power_fd);
    if (energy_fd != -1)
        close(energy_fd);
    if (capacity_fd != -1)
        close(capacity_fd);
    if (status_fd != -1)
        close(status_fd);
    if (time_to_empty_fd != -1)
        close(time_to_empty_fd);

    close(base_dir_fd);
    return ret;
}

static struct module *
battery_new(const char *battery, struct particle *label, int poll_interval_secs)
{
    struct private *m = calloc(1, sizeof(*m));
    m->label = label;
    m->poll_interval = poll_interval_secs;
    m->battery = strdup(battery);
    m->state = STATE_DISCHARGING;

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
    const struct yml_node *name = yml_get_value(node, "name");
    const struct yml_node *poll_interval = yml_get_value(node, "poll-interval");

    return battery_new(
        yml_value_as_string(name),
        conf_to_particle(c, inherited),
        poll_interval != NULL ? yml_value_as_int(poll_interval) : 60);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"name", true, &conf_verify_string},
        {"poll-interval", false, &conf_verify_int},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_battery_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_battery_iface")));
#endif
