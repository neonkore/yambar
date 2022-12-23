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

static const long min_poll_interval = 250;
static const long default_poll_interval = 60 * 1000;

enum state { STATE_FULL, STATE_NOTCHARGING, STATE_CHARGING, STATE_DISCHARGING, STATE_UNKNOWN };

struct private {
    struct particle *label;

    long poll_interval;
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

static const char *
description(const struct module *mod)
{
    static char desc[32];
    const struct private *m = mod->private;
    snprintf(desc, sizeof(desc), "bat(%s)", m->battery);
    return desc;
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&mod->lock);

    assert(m->state == STATE_FULL ||
           m->state == STATE_NOTCHARGING ||
           m->state == STATE_CHARGING ||
           m->state == STATE_DISCHARGING ||
           m->state == STATE_UNKNOWN);

    unsigned long hours;
    unsigned long minutes;

    if (m->time_to_empty >= 0) {
        hours = m->time_to_empty / 60;
        minutes = m->time_to_empty % 60;
    } else if  (m->energy_full >= 0 && m->charge && m->power >= 0) {
        unsigned long energy = m->state == STATE_CHARGING
            ? m->energy_full - m->energy : m->energy;

        double hours_as_float;
        if (m->state == STATE_FULL || m->state == STATE_NOTCHARGING)
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
        if (m->state == STATE_FULL || m->state == STATE_NOTCHARGING)
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
                           m->state == STATE_NOTCHARGING ? "not charging" :
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
readline_from_fd(int fd, size_t sz, char buf[static sz])
{
    ssize_t bytes = read(fd, buf, sz - 1);
    lseek(fd, 0, SEEK_SET);

    if (bytes < 0) {
        LOG_WARN("failed to read from FD=%d", fd);
        return NULL;
    }

    buf[bytes] = '\0';
    for (ssize_t i = bytes - 1; i >= 0 && buf[i] == '\n'; bytes--)
        buf[i] = '\0';

    return buf;
}

static long
readint_from_fd(int fd)
{
    char buf[512];
    const char *s = readline_from_fd(fd, sizeof(buf), buf);
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

static bool
initialize(struct private *m)
{
    char line_buf[512];

    int pw_fd = open("/sys/class/power_supply", O_RDONLY);
    if (pw_fd < 0) {
        LOG_ERRNO("/sys/class/power_supply");
        return false;
    }

    int base_dir_fd = openat(pw_fd, m->battery, O_RDONLY);
    close(pw_fd);

    if (base_dir_fd < 0) {
        LOG_ERRNO("/sys/class/power_supply/%s", m->battery);
        return false;
    }

    {
        int fd = openat(base_dir_fd, "manufacturer", O_RDONLY);
        if (fd == -1) {
            LOG_WARN("/sys/class/power_supply/%s/manufacturer: %s",
                     m->battery, strerror(errno));
            m->manufacturer = NULL;
        } else {
            m->manufacturer = strdup(readline_from_fd(fd, sizeof(line_buf), line_buf));
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
            m->model = strdup(readline_from_fd(fd, sizeof(line_buf), line_buf));
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

    close(base_dir_fd);
    return true;

err:
    close(base_dir_fd);
    return false;
}

static bool
update_status(struct module *mod)
{
    struct private *m = mod->private;

    int pw_fd = open("/sys/class/power_supply", O_RDONLY);
    if (pw_fd < 0) {
        LOG_ERRNO("/sys/class/power_supply");
        return false;
    }

    int base_dir_fd = openat(pw_fd, m->battery, O_RDONLY);
    close(pw_fd);

    if (base_dir_fd < 0) {
        LOG_ERRNO("/sys/class/power_supply/%s", m->battery);
        return false;
    }

    int status_fd = openat(base_dir_fd, "status", O_RDONLY);
    if (status_fd < 0) {
        LOG_ERRNO("/sys/class/power_supply/%s/status", m->battery);
        close(base_dir_fd);
        return false;
    }

    int capacity_fd = openat(base_dir_fd, "capacity", O_RDONLY);
    if (capacity_fd < 0) {
        LOG_ERRNO("/sys/class/power_supply/%s/capacity", m->battery);
        close(status_fd);
        close(base_dir_fd);
        return false;
    }

    int energy_fd = openat(base_dir_fd, "energy_now", O_RDONLY);
    int power_fd = openat(base_dir_fd, "power_now", O_RDONLY);
    int charge_fd = openat(base_dir_fd, "charge_now", O_RDONLY);
    int current_fd = openat(base_dir_fd, "current_now", O_RDONLY);
    int time_to_empty_fd = openat(base_dir_fd, "time_to_empty_now", O_RDONLY);

    long capacity = readint_from_fd(capacity_fd);
    long energy = energy_fd >= 0 ? readint_from_fd(energy_fd) : -1;
    long power = power_fd >= 0 ? readint_from_fd(power_fd) : -1;
    long charge = charge_fd >= 0 ? readint_from_fd(charge_fd) : -1;
    long current = current_fd >= 0 ? readint_from_fd(current_fd) : -1;
    long time_to_empty = time_to_empty_fd >= 0 ? readint_from_fd(time_to_empty_fd) : -1;

    char buf[512];
    const char *status = readline_from_fd(status_fd, sizeof(buf), buf);

    if (status_fd >= 0)
        close(status_fd);
    if (capacity_fd >= 0)
        close(capacity_fd);
    if (energy_fd >= 0)
        close(energy_fd);
    if (power_fd >= 0)
        close(power_fd);
    if (charge_fd >= 0)
        close(charge_fd);
    if (current_fd >= 0)
        close(current_fd);
    if (time_to_empty_fd >= 0)
        close(time_to_empty_fd);
    if (base_dir_fd >= 0)
        close(base_dir_fd);

    enum state state;

    if (status == NULL) {
        LOG_WARN("failed to read battery state");
        state = STATE_UNKNOWN;
    } else if (strcmp(status, "Full") == 0)
        state = STATE_FULL;
    else if (strcmp(status, "Not charging") == 0)
        state = STATE_NOTCHARGING;
    else if (strcmp(status, "Charging") == 0)
        state = STATE_CHARGING;
    else if (strcmp(status, "Discharging") == 0)
        state = STATE_DISCHARGING;
    else if (strcmp(status, "Unknown") == 0)
        state = STATE_UNKNOWN;
    else {
        LOG_ERR("unrecognized battery state: %s", status);
        state = STATE_UNKNOWN;
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
    return true;
}

static int
run(struct module *mod)
{
    const struct bar *bar = mod->bar;
    struct private *m = mod->private;

    if (!initialize(m))
        return -1;

    LOG_INFO("%s: %s %s (at %.1f%% of original capacity)",
             m->battery, m->manufacturer, m->model,
             (m->energy_full > 0
              ? 100.0 * m->energy_full / m->energy_full_design
              : m->charge_full > 0
              ? 100.0 * m->charge_full / m->charge_full_design
              : 0.0));

    int ret = 1;

    struct udev *udev = udev_new();
    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");

    if (udev == NULL || mon == NULL)
        goto out;

    udev_monitor_filter_add_match_subsystem_devtype(mon, "power_supply", NULL);
    udev_monitor_enable_receiving(mon);

    if (!update_status(mod))
        goto out;

    bar->refresh(bar);

    while (true) {
        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = udev_monitor_get_fd(mon), .events = POLLIN},
        };
        if (poll(fds, sizeof(fds) / sizeof(fds[0]),
                 m->poll_interval > 0 ? m->poll_interval : -1) < 0)
        {
            if (errno == EINTR)
                continue;

            LOG_ERRNO("failed to poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            ret = 0;
            break;
        }

        if (fds[1].revents & POLLIN) {
            struct udev_device *dev = udev_monitor_receive_device(mon);
            if (dev == NULL)
                continue;

            const char *sysname = udev_device_get_sysname(dev);
            bool is_us = sysname != NULL && strcmp(sysname, m->battery) == 0;
            udev_device_unref(dev);

            if (!is_us)
                continue;
        }

        if (update_status(mod))
            bar->refresh(bar);
    }

out:
    if (mon != NULL)
        udev_monitor_unref(mon);
    if (udev != NULL)
        udev_unref(udev);
    return ret;
}

static struct module *
battery_new(const char *battery, struct particle *label, long poll_interval_msecs)
{
    struct private *m = calloc(1, sizeof(*m));
    m->label = label;
    m->poll_interval = poll_interval_msecs;
    m->battery = strdup(battery);
    m->state = STATE_UNKNOWN;

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
    const struct yml_node *name = yml_get_value(node, "name");
    const struct yml_node *poll_interval = yml_get_value(node, "poll-interval");

    return battery_new(
        yml_value_as_string(name),
        conf_to_particle(c, inherited),
        (poll_interval != NULL
         ? yml_value_as_int(poll_interval)
         : default_poll_interval));
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
        {"name", true, &conf_verify_string},
        {"poll-interval", false, &conf_verify_poll_interval},
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
