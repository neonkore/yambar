#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>

#include <tllist.h>

#include "../particles/dynlist.h"
#include "../bar/bar.h"
#include "../config-verify.h"
#include "../config.h"
#include "../log.h"
#include "../plugin.h"

#define LOG_MODULE "disk-io"
#define LOG_ENABLE_DBG 0
#define SMALLEST_INTERVAL 500

struct device_stats {
    char *name;
    bool is_disk;

    uint64_t prev_sectors_read;
    uint64_t cur_sectors_read;

    uint64_t prev_sectors_written;
    uint64_t cur_sectors_written;

    uint32_t ios_in_progress;

    bool exists;
};

struct private {
    struct particle *label;
    uint16_t interval;
    tll(struct device_stats *) devices;
};

static bool
is_disk(char const *name)
{
    DIR *dir = opendir("/sys/block");
    if (dir == NULL) {
        LOG_ERRNO("failed to read /sys/block directory");
        return false;
    }

    struct dirent *entry;

    bool found = false;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(name, entry->d_name) == 0) {
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

static struct device_stats*
new_device_stats(char const *name)
{
    struct device_stats *dev = malloc(sizeof(*dev));
    dev->name = strdup(name);
    dev->is_disk = is_disk(name);
    return dev;
}

static void
free_device_stats(struct device_stats *dev)
{
    free(dev->name);
    free(dev);
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);
    tll_foreach(m->devices, it) {
        free_device_stats(it->item);
    }
    tll_free(m->devices);
    free(m);
    module_default_destroy(mod);
}

static const char *
description(const struct module *mod)
{
    return "disk-io";
}

static void
refresh_device_stats(struct private *m)
{
    FILE *fp = NULL;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    fp = fopen("/proc/diskstats", "r");
    if (NULL == fp) {
        LOG_ERRNO("unable to open /proc/diskstats");
        return;
    }

    /*
     * Devices may be added or removed during the bar's lifetime, as external
     * block devices are connected or disconnected from the machine. /proc/diskstats
     * reports data only for the devices that are currently connected.
     *
     * This means that if we have a device that ISN'T in /proc/diskstats, it was
     * disconnected, and we need to remove it from the list.
     *
     * On the other hand, if a device IS in /proc/diskstats, but not in our list, we
     * must create a new device_stats struct and add it to the list.
     *
     * The 'exists' variable is what keep tracks of whether or not /proc/diskstats
     * is still reporting the device (i.e., it is still connected).
     */
    tll_foreach(m->devices, it) {
        it->item->exists = false;
    }

    while ((read = getline(&line, &len, fp)) != -1) {
        /*
         * For an explanation of the fields bellow, see
         * https://www.kernel.org/doc/Documentation/ABI/testing/procfs-diskstats
         */
        uint8_t major_number = 0;
        uint8_t minor_number = 0;
        char *device_name = NULL;
        uint32_t completed_reads = 0;
        uint32_t merged_reads = 0;
        uint64_t sectors_read = 0;
        uint32_t reading_time = 0;
        uint32_t completed_writes = 0;
        uint32_t merged_writes = 0;
        uint64_t sectors_written = 0;
        uint32_t writting_time = 0;
        uint32_t ios_in_progress = 0;
        uint32_t io_time = 0;
        uint32_t io_weighted_time = 0;
        uint32_t completed_discards = 0;
        uint32_t merged_discards = 0;
        uint32_t sectors_discarded = 0;
        uint32_t discarding_time = 0;
        uint32_t completed_flushes = 0;
        uint32_t flushing_time = 0;
        if (!sscanf(line,
                " %" SCNu8 " %" SCNu8 " %ms %" SCNu32 " %" SCNu32 " %" SCNu64 " %" SCNu32
                " %" SCNu32 " %" SCNu32 " %" SCNu64 " %" SCNu32 " %" SCNu32 " %" SCNu32
                " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32 " %" SCNu32
                " %" SCNu32,
                &major_number, &minor_number, &device_name, &completed_reads,
                &merged_reads, &sectors_read, &reading_time, &completed_writes,
                &merged_writes, &sectors_written, &writting_time, &ios_in_progress,
                &io_time, &io_weighted_time, &completed_discards, &merged_discards,
                &sectors_discarded, &discarding_time, &completed_flushes, &flushing_time))
        {
            LOG_ERR("unable to parse /proc/diskstats line");
            free(device_name);
            goto exit;
        }

        bool found = false;
        tll_foreach(m->devices, it) {
            struct device_stats *dev = it->item;
            if (strcmp(dev->name, device_name) == 0){
                dev->prev_sectors_read = dev->cur_sectors_read;
                dev->prev_sectors_written = dev->cur_sectors_written;
                dev->ios_in_progress = ios_in_progress;
                dev->cur_sectors_read = sectors_read;
                dev->cur_sectors_written = sectors_written;
                dev->exists = true;
                found = true;
                break;
            }
        }

        if (!found) {
            struct device_stats *new_dev = new_device_stats(device_name);
            new_dev->ios_in_progress = ios_in_progress;
            new_dev->prev_sectors_read = sectors_read;
            new_dev->cur_sectors_read = sectors_read;
            new_dev->prev_sectors_written = sectors_written;
            new_dev->cur_sectors_written = sectors_written;
            new_dev->exists = true;
            tll_push_back(m->devices, new_dev);
        }

        free(device_name);
    }

    tll_foreach(m->devices, it) {
        if (!it->item->exists){
            free_device_stats(it->item);
            tll_remove(m->devices, it);
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
    uint64_t total_bytes_read = 0;
    uint64_t total_bytes_written = 0;
    uint32_t total_ios_in_progress = 0;
    mtx_lock(&mod->lock);
    struct exposable *tag_parts[p->devices.length + 1];
    int i = 0;
    tll_foreach(p->devices, it) {
        struct device_stats *dev = it->item;
        uint64_t bytes_read = (dev->cur_sectors_read - dev->prev_sectors_read) * 512;
        uint64_t bytes_written = (dev->cur_sectors_written - dev->prev_sectors_written) * 512;

        if (dev->is_disk){
            total_bytes_read += bytes_read;
            total_bytes_written += bytes_written;
            total_ios_in_progress += dev->ios_in_progress;
        }

        struct tag_set tags = {
            .tags = (struct tag *[]) {
                tag_new_string(mod, "device", dev->name),
                tag_new_bool(mod, "is_disk", dev->is_disk),
                tag_new_int(mod, "read_speed", (bytes_read * 1000) / p->interval),
                tag_new_int(mod, "write_speed", (bytes_written * 1000) / p->interval),
                tag_new_int(mod, "ios_in_progress", dev->ios_in_progress),
            },
            .count = 5,
        };
        tag_parts[i++] = p->label->instantiate(p->label, &tags);
        tag_set_destroy(&tags);
    }
    struct tag_set tags = {
        .tags = (struct tag *[]) {
            tag_new_string(mod, "device", "Total"),
            tag_new_bool(mod, "is_disk", true),
            tag_new_int(mod, "read_speed", (total_bytes_read * 1000) / p->interval),
            tag_new_int(mod, "write_speed", (total_bytes_written * 1000) / p->interval),
            tag_new_int(mod, "ios_in_progress", total_ios_in_progress),
        },
        .count = 5,
    };
    tag_parts[i] = p->label->instantiate(p->label, &tags);
    tag_set_destroy(&tags);
    mtx_unlock(&mod->lock);

    return dynlist_exposable_new(tag_parts, p->devices.length + 1, 0, 0);
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
        refresh_device_stats(p);
        mtx_unlock(&mod->lock);
        bar->refresh(bar);
    }

    return 0;
}

static struct module *
disk_io_new(uint16_t interval, struct particle *label)
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
    const struct yml_node *interval = yml_get_value(node, "interval");
    const struct yml_node *c = yml_get_value(node, "content");

    return disk_io_new(
            interval == NULL ? SMALLEST_INTERVAL : yml_value_as_int(interval),
            conf_to_particle(c, inherited));
}

static bool
conf_verify_interval(keychain_t *chain, const struct yml_node *node)
{
    if (!conf_verify_unsigned(chain, node))
        return false;

    if (yml_value_as_int(node) < SMALLEST_INTERVAL) {
        LOG_ERR(
            "%s: interval value cannot be less than %d ms",
            conf_err_prefix(chain, node), SMALLEST_INTERVAL);
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

const struct module_iface module_disk_io_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_disk_io_iface")));
#endif
