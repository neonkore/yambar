#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <poll.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <libudev.h>

#define LOG_MODULE "backlight"
#include "../log.h"
#include "../bar/bar.h"
#include "../config.h"
#include "../config-verify.h"
#include "../plugin.h"

struct private {
    struct particle *label;

    char *device;
    long max_brightness;
    long current_brightness;
};

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    free(m->device);

    m->label->destroy(m->label);

    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    const struct private *m = mod->private;

    mtx_lock(&mod->lock);

    const long current = m->current_brightness;
    const long max = m->max_brightness;
    const long percent = max > 0 ? 100 * current / max : 0;

    struct tag_set tags = {
        .tags = (struct tag *[]){
            tag_new_int_range(mod, "brightness", current, 0, max),
            tag_new_int_range(mod, "percent", percent, 0, 100),
        },
        .count = 2,
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
    int backlight_fd = open("/sys/class/backlight", O_RDONLY);
    if (backlight_fd == -1) {
        LOG_ERRNO("/sys/class/backlight");
        return -1;
    }

    int base_dir_fd = openat(backlight_fd, m->device, O_RDONLY);
    close(backlight_fd);

    if (base_dir_fd == -1) {
        LOG_ERRNO("/sys/class/backlight/%s", m->device);
        return -1;
    }

    int max_fd = openat(base_dir_fd, "max_brightness", O_RDONLY);
    if (max_fd == -1) {
        LOG_ERRNO("/sys/class/backlight/%s/max_brightness", m->device);
        close(base_dir_fd);
        return -1;
    }

    m->max_brightness = readint_from_fd(max_fd);
    close(max_fd);

    int current_fd = openat(base_dir_fd, "brightness", O_RDONLY);
    close(base_dir_fd);

    if (current_fd == -1) {
        LOG_ERRNO("/sys/class/backlight/%s/brightness", m->device);
        return -1;
    }

    m->current_brightness = readint_from_fd(current_fd);

    LOG_INFO("%s: brightness: %ld (max: %ld)", m->device, m->current_brightness,
             m->max_brightness);

    return current_fd;
}

static int
run(struct module *mod)
{
    const struct bar *bar = mod->bar;
    struct private *m = mod->private;

    int current_fd = initialize(m);
    if (current_fd == -1)
        return 1;

    struct udev *udev = udev_new();
    struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");

    if (udev == NULL || mon == NULL) {
        LOG_ERR("failed to initialize udev monitor");

        if (udev == NULL)
            udev_unref(udev);

        close(current_fd);
        return 1;
    }

    udev_monitor_filter_add_match_subsystem_devtype(mon, "backlight", NULL);
    udev_monitor_enable_receiving(mon);

    bar->refresh(bar);

    while (true) {
        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = udev_monitor_get_fd(mon), .events = POLLIN},
        };
        poll(fds, 2, -1);

        if (fds[0].revents & POLLIN)
            break;

        struct udev_device *dev = udev_monitor_receive_device(mon);
        const char *sysname = udev_device_get_sysname(dev);

        bool is_us = strcmp(sysname, m->device) == 0;
        udev_device_unref(dev);

        if (!is_us)
            continue;

        mtx_lock(&mod->lock);
        m->current_brightness = readint_from_fd(current_fd);
        mtx_unlock(&mod->lock);
        bar->refresh(bar);
    }

    udev_monitor_unref(mon);
    udev_unref(udev);

    close(current_fd);
    return 0;
}

static struct module *
backlight_new(const char *device, struct particle *label)
{
    struct private *m = calloc(1, sizeof(*m));
    m->label = label;
    m->device = strdup(device);

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
    const struct yml_node *name = yml_get_value(node, "name");
    const struct yml_node *c = yml_get_value(node, "content");

    return backlight_new(
        yml_value_as_string(name), conf_to_particle(c, inherited));
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"name", true, &conf_verify_string},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_backlight_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_backlight_iface")));
#endif
