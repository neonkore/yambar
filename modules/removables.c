#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <poll.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libudev.h>

#include <tllist.h>

#define LOG_MODULE "removables"
#define LOG_ENABLE_DBG 0
#include "../log.h"
#include "../bar/bar.h"
#include "../config.h"
#include "../config-verify.h"
#include "../particles/dynlist.h"
#include "../plugin.h"

typedef tll(char *) mount_point_list_t;

struct partition {
    const struct block_device *block;

    char *sys_path;
    char *dev_path;
    char *label;

    uint64_t size;

    /*tll(char *) mount_points;*/
    mount_point_list_t mount_points;
};

struct block_device {
    char *sys_path;
    char *dev_path;

    uint64_t size;
    char *vendor;
    char *model;
    bool optical;
    bool media;

    tll(struct partition) partitions;
};

struct private {
    struct particle *label;
    int left_spacing;
    int right_spacing;

    tll(char *) ignore;
    tll(struct block_device) devices;
};

static void
free_partition(struct partition *p)
{
    free(p->sys_path);
    free(p->dev_path);
    free(p->label);
    tll_free_and_free(p->mount_points, free);
}

static void
free_device(struct block_device *b)
{
    tll_foreach(b->partitions, it)
        free_partition(&it->item);
    tll_free(b->partitions);

    free(b->sys_path);
    free(b->dev_path);
    free(b->vendor);
    free(b->model);
}

static void
destroy(struct module *mod)
{
    struct private *m = mod->private;
    m->label->destroy(m->label);

    tll_foreach(m->devices, it)
        free_device(&it->item);
    tll_free(m->devices);
    tll_free_and_free(m->ignore, free);

    free(m);
    module_default_destroy(mod);
}

static struct exposable *
content(struct module *mod)
{
    struct private *m = mod->private;

    tll(const struct partition *) partitions = tll_init();

    tll_foreach(m->devices, dev) {
        tll_foreach(dev->item.partitions, part) {
            tll_push_back(partitions, &part->item);
        }
    }

    struct exposable *exposables[tll_length(partitions)];
    size_t idx = 0;

    tll_foreach(partitions, it) {
        const struct partition *p = it->item;

        char dummy_label[16];
        const char *label = p->label;

        if (label == NULL) {
            snprintf(dummy_label, sizeof(dummy_label),
                     "%.1f GB", (double)p->size / 1024 / 1024 / 1024 * 512);
            label = dummy_label;
        }

        bool is_mounted = tll_length(p->mount_points) > 0;
        const char *mount_point = is_mounted ? tll_front(p->mount_points) : "";

        struct tag_set tags = {
            .tags = (struct tag *[]){
                tag_new_string(mod, "vendor", p->block->vendor),
                tag_new_string(mod, "model", p->block->model),
                tag_new_bool(mod, "optical", p->block->optical),
                tag_new_string(mod, "device", p->dev_path),
                tag_new_int_range(mod, "size", p->size, 0, p->block->size),
                tag_new_string(mod, "label", label),
                tag_new_bool(mod, "mounted", is_mounted),
                tag_new_string(mod, "mount_point", mount_point),
            },
            .count = 8,
        };

        exposables[idx++] = m->label->instantiate(m->label, &tags);
        tag_set_destroy(&tags);
    }

    tll_free(partitions);
    return dynlist_exposable_new(
        exposables, idx, m->left_spacing, m->right_spacing);
}

static void
find_mount_points(const char *dev_path, mount_point_list_t *mount_points)
{
    FILE *f = fopen("/proc/self/mountinfo", "r");
    assert(f != NULL);

    char line[4096];

    while (fgets(line, sizeof(line), f) != NULL) {
        char *dev = NULL, *path = NULL;

        if (sscanf(line, "%*u %*u %*u:%*u %*s %ms %*[^-] - %*s %ms %*s",
                   &path, &dev) != 2)
        {
            LOG_ERR("failed to parse /proc/self/mountinfo: %s", line);
            free(dev);
            free(path);
            break;
        }

        if (strcmp(dev, dev_path) == 0)
            tll_push_back(*mount_points, strdup(path));

        free(dev);
        free(path);
    }

    fclose(f);
}

static bool
update_mount_points(struct partition *partition)
{
    mount_point_list_t new_mounts = tll_init();
    find_mount_points(partition->dev_path, &new_mounts);

    bool updated = false;

    /* Remove mount points that no longer exists (i.e. old mount
     * points that aren't in the new list) */
    tll_foreach(partition->mount_points, old) {
        bool gone = true;
        tll_foreach(new_mounts, new) {
            if (strcmp(new->item, old->item) == 0) {
                /* Remove from new list, as it's already in the
                 * partitions list */
                tll_remove_and_free(new_mounts, new, free);
                gone = false;
                break;
            }
        }

        if (gone) {
            LOG_DBG("%s: unmounted from %s", partition->dev_path, old->item);
            tll_remove_and_free(partition->mount_points, old, free);
            updated = true;
        }
    }

    /* Add new mount points (i.e. mount points in the new list, that
     * aren't in the old list) */
    tll_foreach(new_mounts, new) {
        LOG_DBG("%s: mounted on %s", partition->dev_path, new->item);
        tll_push_back(partition->mount_points, new->item);

        /* Remove, but don't free, since it's now owned by partition's list */
        tll_remove(new_mounts, new);
        updated = true;
    }

    assert(tll_length(new_mounts) == 0);
    return updated;
}

static struct partition *
add_partition(struct module *mod, struct block_device *block,
              struct udev_device *dev)
{
    struct private *m = mod->private;
    const char *_size = udev_device_get_sysattr_value(dev, "size");
    uint64_t size = 0;
    if (_size != NULL)
        sscanf(_size, "%"SCNu64, &size);

#if 0
    struct udev_list_entry *e = NULL;
    udev_list_entry_foreach(e, udev_device_get_properties_list_entry(dev)) {
        LOG_DBG("%s -> %s", udev_list_entry_get_name(e), udev_list_entry_get_value(e));
    }
#endif

    const char *devname = udev_device_get_property_value(dev, "DEVNAME");
    if (devname != NULL) {
        tll_foreach(m->ignore, it) {
            if (strcmp(it->item, devname) == 0) {
                LOG_DBG("ignoring %s because it is on the ignore list", devname);
                return NULL;
            }
        }
    }

    const char *label = udev_device_get_property_value(dev, "ID_FS_LABEL");
    if (label == NULL)
        label = udev_device_get_property_value(dev, "ID_LABEL");

    LOG_INFO("partition: add: %s: label=%s, size=%"PRIu64,
             udev_device_get_devnode(dev), label, size);

    mtx_lock(&mod->lock);

    tll_push_back(
        block->partitions,
        ((struct partition){
            .block = block,
            .sys_path = strdup(udev_device_get_devpath(dev)),
            .dev_path = strdup(udev_device_get_devnode(dev)),
            .label = label != NULL ? strdup(label) : NULL,
            .size = size,
            .mount_points = tll_init()}));

    struct partition *p = &tll_back(block->partitions);
    update_mount_points(p);
    mtx_unlock(&mod->lock);

    return p;
}

static bool
del_partition(struct module *mod, struct block_device *block,
              struct udev_device *dev)
{
    const char *sys_path = udev_device_get_devpath(dev);
    mtx_lock(&mod->lock);

    tll_foreach(block->partitions, it) {
        if (strcmp(it->item.sys_path, sys_path) == 0) {
            LOG_INFO("partition: del: %s", it->item.dev_path);

            free_partition(&it->item);
            tll_remove(block->partitions, it);
            mtx_unlock(&mod->lock);
            return true;
        }
    }

    mtx_unlock(&mod->lock);
    return false;
}

static struct block_device *
add_device(struct module *mod, struct udev_device *dev)
{
    struct private *m = mod->private;

    const char *_removable = udev_device_get_sysattr_value(dev, "removable");
    bool removable = _removable != NULL && strcmp(_removable, "1") == 0;

    const char *_sd = udev_device_get_property_value(dev, "ID_DRIVE_FLASH_SD");
    bool sd = _sd != NULL && strcmp(_sd, "1") == 0;

    if (!removable && !sd)
        return NULL;

    const char *devname = udev_device_get_property_value(dev, "DEVNAME");
    if (devname != NULL) {
        tll_foreach(m->ignore, it) {
            if (strcmp(it->item, devname) == 0) {
                LOG_DBG("ignoring %s because it is on the ignore list", devname);
                return NULL;
            }
        }
    }

    const char *_size = udev_device_get_sysattr_value(dev, "size");
    uint64_t size = 0;
    if (_size != NULL)
        sscanf(_size, "%"SCNu64, &size);

#if 1
    struct udev_list_entry *e = NULL;
    udev_list_entry_foreach(e, udev_device_get_properties_list_entry(dev)) {
        LOG_DBG("%s -> %s", udev_list_entry_get_name(e), udev_list_entry_get_value(e));
    }
#endif

    const char *vendor = udev_device_get_property_value(dev, "ID_VENDOR");
    const char *model = udev_device_get_property_value(dev, "ID_MODEL");

    const char *_optical = udev_device_get_property_value(dev, "ID_CDROM");
    bool optical = _optical != NULL && strcmp(_optical, "1") == 0;

    const char *_fs_usage = udev_device_get_property_value(dev, "ID_FS_USAGE");
    bool media = _fs_usage != NULL && strcmp(_fs_usage, "filesystem") == 0;

    LOG_DBG("device: add: %s: vendor=%s, model=%s, optical=%d, size=%"PRIu64,
            udev_device_get_devnode(dev), vendor, model, optical, size);

    mtx_lock(&mod->lock);

    tll_push_back(
        m->devices,
        ((struct block_device){
            .sys_path = strdup(udev_device_get_devpath(dev)),
            .dev_path = strdup(udev_device_get_devnode(dev)),
            .size = size,
            .vendor = vendor != NULL ? strdup(vendor) : NULL,
            .model = model != NULL ? strdup(model) : NULL,
            .optical = optical,
            .media = media,
            .partitions = tll_init()}));

    mtx_unlock(&mod->lock);

    struct block_device *block = &tll_back(m->devices);
    if (optical && media)
        add_partition(mod, block, dev);

    return &tll_back(m->devices);
}

static bool
del_device(struct module *mod, struct udev_device *dev)
{
    struct private *m = mod->private;
    const char *sys_path = udev_device_get_devpath(dev);
    mtx_lock(&mod->lock);

    tll_foreach(m->devices, it) {
        if (strcmp(it->item.sys_path, sys_path) == 0) {
            LOG_DBG("device: del: %s", it->item.dev_path);

            free_device(&it->item);
            tll_remove(m->devices, it);
            mtx_unlock(&mod->lock);
            return true;
        }
    }

    mtx_unlock(&mod->lock);
    return false;
}

static bool
change_device(struct module *mod, struct udev_device *dev)
{
    struct private *m = mod->private;
    const char *sys_path = udev_device_get_devpath(dev);
    mtx_lock(&mod->lock);

    tll_foreach(m->devices, it) {
        if (strcmp(it->item.sys_path, sys_path) == 0) {
            LOG_DBG("device: change: %s", it->item.dev_path);

            if (it->item.optical) {
                const char *_media = udev_device_get_property_value(dev, "ID_FS_USAGE");
                bool media = _media != NULL && strcmp(_media, "filesystem") == 0;
                bool media_change = media != it->item.media;

                it->item.media = media;
                mtx_unlock(&mod->lock);

                if (media_change) {
                    LOG_INFO("device: change: %s: media %s",
                             it->item.dev_path, media ? "inserted" : "removed");

                    if (media)
                        return add_partition(mod, &it->item, dev) != NULL;
                    else
                        return del_partition(mod, &it->item, dev);
                }
            }
        }
    }

    mtx_unlock(&mod->lock);
    return false;
}

static bool
handle_udev_event(struct module *mod, struct udev_device *dev)
{
    struct private *m = mod->private;

    const char *action = udev_device_get_action(dev);
    bool add = strcmp(action, "add") == 0;
    bool del = strcmp(action, "remove") == 0;
    bool change = strcmp(action, "change") == 0;

    if (!add && !del && !change) {
        LOG_WARN("%s: unhandled action: %s", udev_device_get_devpath(dev), action);
        return false;
    }

    const char *devtype = udev_device_get_property_value(dev, "DEVTYPE");

    if (strcmp(devtype, "disk") == 0) {
        if (add)
            return add_device(mod, dev) != NULL;
        else if (del)
            return del_device(mod, dev);
        else
            return change_device(mod, dev);
    }

    if (strcmp(devtype, "partition") == 0) {
        struct udev_device *parent = udev_device_get_parent(dev);
        const char *parent_sys_path = udev_device_get_devpath(parent);

        tll_foreach(m->devices, it) {
            if (strcmp(it->item.sys_path, parent_sys_path) != 0)
                continue;

            if (add)
                return add_partition(mod, &it->item, dev) != NULL;
            else if (del)
                return del_partition(mod, &it->item, dev);
            else {
                LOG_ERR("unimplemented: 'change' event on partition: %s",
                        udev_device_get_devpath(dev));
                return false;
            }
            break;
        }
    }

    return false;
}

static int
run(struct module *mod)
{
    struct private *m = mod->private;

    struct udev *udev = udev_new();
    struct udev_monitor *dev_mon = udev_monitor_new_from_netlink(udev, "udev");

    udev_monitor_filter_add_match_subsystem_devtype(dev_mon, "block", NULL);
    udev_monitor_enable_receiving(dev_mon);

    struct udev_enumerate *dev_enum = udev_enumerate_new(udev);
    assert(dev_enum != NULL);

    udev_enumerate_add_match_subsystem(dev_enum, "block");

    /* TODO: verify how an optical presents itself */
    //udev_enumerate_add_match_sysattr(dev_enum, "removable", "1");
    udev_enumerate_add_match_property(dev_enum, "DEVTYPE", "disk");
    udev_enumerate_scan_devices(dev_enum);

    /* Loop list, and for each device, enumerate its partitions */
    struct udev_list_entry *entry = NULL;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(dev_enum)) {
        struct udev_device *dev = udev_device_new_from_syspath(
            udev, udev_list_entry_get_name(entry));

        struct block_device *block = add_device(mod, dev);
        if (block == NULL) {
            udev_device_unref(dev);
            continue;
        }

        struct udev_enumerate *part_enum = udev_enumerate_new(udev);
        assert(dev_enum != NULL);

        udev_enumerate_add_match_subsystem(part_enum, "block");
        udev_enumerate_add_match_parent(part_enum, dev);
        udev_enumerate_add_match_property(part_enum, "DEVTYPE", "partition");
        udev_enumerate_scan_devices(part_enum);

        struct udev_list_entry *sub_entry = NULL;
        udev_list_entry_foreach(sub_entry, udev_enumerate_get_list_entry(part_enum)) {
            struct udev_device *partition = udev_device_new_from_syspath(
                udev, udev_list_entry_get_name(sub_entry));
            add_partition(mod, block, partition);
            udev_device_unref(partition);
        }

        udev_enumerate_unref(part_enum);
        udev_device_unref(dev);
    }

    udev_enumerate_unref(dev_enum);
    mod->bar->refresh(mod->bar);

    /* To be able to poll() mountinfo for changes, to detect
     * mount/unmount operations */
    int mount_info_fd = open("/proc/self/mountinfo", O_RDONLY);

    while (true) {
        struct pollfd fds[] = {
            {.fd = mod->abort_fd, .events = POLLIN},
            {.fd = udev_monitor_get_fd(dev_mon), .events = POLLIN},
            {.fd = mount_info_fd, .events = POLLPRI},
        };
        poll(fds, 3, -1);

        if (fds[0].revents & POLLIN)
            break;

        bool update = false;

        if (fds[2].revents & POLLPRI) {
            tll_foreach(m->devices, dev) {
                tll_foreach(dev->item.partitions, part) {
                    if (update_mount_points(&part->item))
                        update = true;
                }
            }
        }

        if (fds[1].revents & POLLIN) {
            struct udev_device *dev = udev_monitor_receive_device(dev_mon);
            if (handle_udev_event(mod, dev))
                update = true;
            udev_device_unref(dev);
        }

        if (update)
            mod->bar->refresh(mod->bar);
    }

    close(mount_info_fd);

    udev_monitor_unref(dev_mon);
    udev_unref(udev);
    return 0;
}

static struct module *
removables_new(struct particle *label, int left_spacing, int right_spacing,
               size_t ignore_count, const char *ignore[static ignore_count])
{
    struct private *priv = calloc(1, sizeof(*priv));
    priv->label = label;
    priv->left_spacing = left_spacing;
    priv->right_spacing = right_spacing;

    for (size_t i = 0; i < ignore_count; i++)
        tll_push_back(priv->ignore, strdup(ignore[i]));

    struct module *mod = module_common_new();
    mod->private = priv;
    mod->run = &run;
    mod->destroy = &destroy;
    mod->content = &content;
    return mod;
}

static struct module *
from_conf(const struct yml_node *node, struct conf_inherit inherited)
{
    const struct yml_node *content = yml_get_value(node, "content");
    const struct yml_node *spacing = yml_get_value(node, "spacing");
    const struct yml_node *left_spacing = yml_get_value(node, "left-spacing");
    const struct yml_node *right_spacing = yml_get_value(node, "right-spacing");
    const struct yml_node *ignore_list = yml_get_value(node, "ignore");

    int left = spacing != NULL ? yml_value_as_int(spacing) :
        left_spacing != NULL ? yml_value_as_int(left_spacing) : 0;
    int right = spacing != NULL ? yml_value_as_int(spacing) :
        right_spacing != NULL ? yml_value_as_int(right_spacing) : 0;

    size_t ignore_count = ignore_list != NULL ? yml_list_length(ignore_list) : 0;
    const char *ignore[ignore_count];

    if (ignore_list != NULL) {
        size_t i = 0;
        for (struct yml_list_iter iter = yml_list_iter(ignore_list);
             iter.node != NULL;
             yml_list_next(&iter), i++)
        {
            ignore[i] = yml_value_as_string(iter.node);
        }
    }

    return removables_new(
        conf_to_particle(content, inherited), left, right, ignore_count, ignore);
}

static bool
verify_ignore(keychain_t *chain, const struct yml_node *node)
{
    return conf_verify_list(chain, node, &conf_verify_string);
}

static bool
verify_conf(keychain_t *chain, const struct yml_node *node)
{
    static const struct attr_info attrs[] = {
        {"spacing", false, &conf_verify_int},
        {"left-spacing", false, &conf_verify_int},
        {"right-spacing", false, &conf_verify_int},
        {"ignore", false, &verify_ignore},
        MODULE_COMMON_ATTRS,
    };

    return conf_verify_dict(chain, node, attrs);
}

const struct module_iface module_removables_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern const struct module_iface iface __attribute__((weak, alias("module_removables_iface")));
#endif
