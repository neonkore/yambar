#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#define ARR_LEN(x) (sizeof((x)) / sizeof((x)[0]))

#include "../config-verify.h"
#include "../config.h"
#include "../log.h"
#include "../module.h"
#include "../particles/dynlist.h"
#include "../plugin.h"

#define LOG_MODULE "dwl"
#define LOG_ENABLE_DBG 0

struct dwl_tag {
    int id;
    char *name;
    bool selected;
    bool empty;
    bool urgent;
};

struct private
{
    struct particle *label;

    char const *monitor;

    unsigned int number_of_tags;
    char *dwl_info_filename;

    /* dwl data */
    char *title;
    bool fullscreen;
    bool floating;
    bool selmon;
    tll(struct dwl_tag *) tags;
    char *layout;
};

enum LINE_MODE {
    LINE_MODE_0,
    LINE_MODE_TITLE,
    LINE_MODE_FULLSCREEN,
    LINE_MODE_FLOATING,
    LINE_MODE_SELMON,
    LINE_MODE_TAGS,
    LINE_MODE_LAYOUT,
};

static void
free_dwl_tag(struct dwl_tag *tag)
{
    free(tag->name);
    free(tag);
}

static void
destroy(struct module *module)
{
    struct private *private = module->private;
    private->label->destroy(private->label);

    tll_free_and_free(private->tags, free_dwl_tag);
    free(private->dwl_info_filename);
    free(private->title);
    free(private->layout);
    free(private);

    module_default_destroy(module);
}

static char const *
description(const struct module *module)
{
    return "dwl";
}

static struct exposable *
content(struct module *module)
{
    struct private const *private = module->private;
    mtx_lock(&module->lock);

    size_t i = 0;
    /* + 1 for `default` tag */
    struct exposable *exposable[tll_length(private->tags) + 1];
    tll_foreach(private->tags, it)
    {
        struct tag_set tags = {
            .tags = (struct tag*[]){
                tag_new_string(module, "title", private->title),
                tag_new_bool(module, "fullscreen", private->fullscreen),
                tag_new_bool(module, "floating", private->floating),
                tag_new_bool(module, "selmon", private->selmon),
                tag_new_string(module, "layout", private->layout),
                tag_new_int(module, "id", it->item->id),
                tag_new_string(module, "name", it->item->name),
                tag_new_bool(module, "selected", it->item->selected),
                tag_new_bool(module, "empty", it->item->empty),
                tag_new_bool(module, "urgent", it->item->urgent),
            },
            .count = 10,
        };
        exposable[i++] = private->label->instantiate(private->label, &tags);
        tag_set_destroy(&tags);
    }

    /* default tag (used for title, layout, etc) */
    struct tag_set tags = {
        .tags = (struct tag*[]){
            tag_new_string(module, "title", private->title),
            tag_new_bool(module, "fullscreen", private->fullscreen),
            tag_new_bool(module, "floating", private->floating),
            tag_new_bool(module, "selmon", private->selmon),
            tag_new_string(module, "layout", private->layout),
            tag_new_int(module, "id", 0),
            tag_new_string(module, "name", "0"),
            tag_new_bool(module, "selected", false),
            tag_new_bool(module, "empty", true),
            tag_new_bool(module, "urgent", false),
        },
        .count = 10,
    };
    exposable[i++] = private->label->instantiate(private->label, &tags);
    tag_set_destroy(&tags);

    mtx_unlock(&module->lock);
    return dynlist_exposable_new(exposable, i, 0, 0);
}

static struct dwl_tag *
dwl_tag_from_id(struct private *private, uint32_t id)
{
    tll_foreach(private->tags, it)
    {
        if (it->item->id == id)
            return it->item;
    }

    assert(false); /* unreachable */
    return NULL;
}

static void
process_line(char *line, struct module *module)
{
    struct private *private = module->private;
    enum LINE_MODE line_mode = LINE_MODE_0;

    /* Remove \n */
    line[strcspn(line, "\n")] = '\0';

    /* Split line by space */
    size_t index = 1;
    char *save_pointer = NULL;
    char *string = strtok_r(line, " ", &save_pointer);
    while (string != NULL) {
        /* dwl logs are formatted like this
         * $1 -> monitor
         * $2 -> action
         * $3 -> arg1
         * $4 -> arg2
         * ... */

        /* monitor */
        if (index == 1) {
            /* Not our monitor */
            if (strcmp(string, private->monitor) != 0)
                break;
        }
        /* action */
        else if (index == 2) {
            if (strcmp(string, "title") == 0) {
                line_mode = LINE_MODE_TITLE;
                /* Update the title here, to avoid allocate and free memory on
                 * every iteration (the line is separated by spaces, then we
                 * join it again) a bit suboptimal, isn't it?) */
                free(private->title);
                private->title = strdup(save_pointer);
                break;
            } else if (strcmp(string, "fullscreen") == 0)
                line_mode = LINE_MODE_FULLSCREEN;
            else if (strcmp(string, "floating") == 0)
                line_mode = LINE_MODE_FLOATING;
            else if (strcmp(string, "selmon") == 0)
                line_mode = LINE_MODE_SELMON;
            else if (strcmp(string, "tags") == 0)
                line_mode = LINE_MODE_TAGS;
            else if (strcmp(string, "layout") == 0)
                line_mode = LINE_MODE_LAYOUT;
            else {
                LOG_WARN("UNKNOWN action, please open an issue on https://codeberg.org/dnkl/yambar");
                return;
            }
        }
        /* args */
        else {
            if (line_mode == LINE_MODE_TAGS) {
                static uint32_t occupied, selected, client_tags, urgent;
                static uint32_t *target = NULL;

                /* dwl tags action log are formatted like this
                 * $3 -> occupied
                 * $4 -> tags
                 * $5 -> clientTags (not needed)
                 * $6 -> urgent */
                if (index == 3)
                    target = &occupied;
                else if (index == 4)
                    target = &selected;
                else if (index == 5)
                    target = &client_tags;
                else if (index == 6)
                    target = &urgent;

                /* No need to check error IMHO */
                *target = strtoul(string, NULL, 10);

                /* Populate informations */
                if (index == 6) {
                    for (size_t id = 1; id <= private->number_of_tags; ++id) {
                        uint32_t mask = 1 << (id - 1);

                        struct dwl_tag *dwl_tag = dwl_tag_from_id(private, id);
                        dwl_tag->selected = mask & selected;
                        dwl_tag->empty = !(mask & occupied);
                        dwl_tag->urgent = mask & urgent;
                    }
                }
            } else
                switch (line_mode) {
                case LINE_MODE_TITLE:
                    assert(false); /* unreachable */
                    break;
                case LINE_MODE_FULLSCREEN:
                    private->fullscreen = (strcmp(string, "0") != 0);
                    break;
                case LINE_MODE_FLOATING:
                    private->floating = (strcmp(string, "0") != 0);
                    break;
                case LINE_MODE_SELMON:
                    private->selmon = (strcmp(string, "0") != 0);
                    break;
                case LINE_MODE_LAYOUT:
                    free(private->layout);
                    private->layout = strdup(string);
                    break;
                default:;
                    assert(false); /* unreachable */
                }
        }

        string = strtok_r(NULL, " ", &save_pointer);
        ++index;
    }
}

static int
file_read_content(FILE *file, struct module *module)
{
    static char buffer[1024];

    errno = 0;
    while (fgets(buffer, ARR_LEN(buffer), file) != NULL)
        process_line(buffer, module);

    fseek(file, 0, SEEK_END);

    /* Check whether error has been */
    if (ferror(file) != 0) {
        LOG_ERRNO("unable to read file's content.");
        return 1;
    }

    return 0;
}

static void
file_seek_to_last_n_lines(FILE *file, int number_of_lines)
{
    if (number_of_lines == 0 || file == NULL)
        return;

    fseek(file, 0, SEEK_END);

    long position = ftell(file);
    while (position > 0) {
        /* Cannot go less than position 0 */
        if (fseek(file, --position, SEEK_SET) == EINVAL)
            break;

        if (fgetc(file) == '\n')
            if (number_of_lines-- == 0)
                break;
    }
}

static int
run_init(int *inotify_fd, int *inotify_wd, FILE **file, char *dwl_info_filename)
{
    *inotify_fd = inotify_init();
    if (*inotify_fd == -1) {
        LOG_ERRNO("unable to create inotify fd.");
        return -1;
    }

    *inotify_wd = inotify_add_watch(*inotify_fd, dwl_info_filename, IN_MODIFY);
    if (*inotify_wd == -1) {
        close(*inotify_fd);
        LOG_ERRNO("unable to add watch to inotify fd.");
        return 1;
    }

    *file = fopen(dwl_info_filename, "r");
    if (*file == NULL) {
        inotify_rm_watch(*inotify_fd, *inotify_wd);
        close(*inotify_fd);
        LOG_ERRNO("unable to open file.");
        return 1;
    }

    return 0;
}

static int
run_clean(int inotify_fd, int inotify_wd, FILE *file)
{
    if (inotify_fd != -1) {
        if (inotify_wd != -1)
            inotify_rm_watch(inotify_fd, inotify_wd);
        close(inotify_fd);
    }

    if (file != NULL) {
        if (fclose(file) == EOF) {
            LOG_ERRNO("unable to close file.");
            return 1;
        }
    }

    return 0;
};

static int
run(struct module *module)
{
    struct private *private = module->private;

    /* Ugly, but I didn't find better way for waiting
     * the monitor's name to be set */
    do {
        private->monitor = module->bar->output_name(module->bar);
        usleep(50);
    } while (private->monitor == NULL);

    int inotify_fd = -1, inotify_wd = -1;
    FILE *file = NULL;
    if (run_init(&inotify_fd, &inotify_wd, &file, private->dwl_info_filename) != 0)
        return 1;

    /* Dwl output is 6 lines per monitor, so let's assume that nobody has
     * more than 5 monitors (6 * 5 = 30) */
    mtx_lock(&module->lock);
    file_seek_to_last_n_lines(file, 30);
    if (file_read_content(file, module) != 0) {
        mtx_unlock(&module->lock);
        return run_clean(inotify_fd, inotify_wd, file);
    }
    mtx_unlock(&module->lock);

    module->bar->refresh(module->bar);

    while (true) {
        struct pollfd fds[] = {
            (struct pollfd){.fd = module->abort_fd, .events = POLLIN},
            (struct pollfd){.fd = inotify_fd, .events = POLLIN},
        };

        if (poll(fds, ARR_LEN(fds), -1) == -1) {
            if (errno == EINTR)
                continue;

            LOG_ERRNO("unable to poll.");
            break;
        }

        if (fds[0].revents & POLLIN)
            break;

        /* fds[1] (inotify_fd) must be POLLIN otherwise issue happen'd */
        if (!(fds[1].revents & POLLIN)) {
            LOG_ERR("expected POLLIN revent");
            break;
        }

        /* Block until event */
        static char buffer[1024];
        ssize_t length = read(inotify_fd, buffer, ARR_LEN(buffer));

        if (length == 0)
            break;

        if (length == -1) {
            if (errno == EAGAIN)
                continue;

            LOG_ERRNO("unable to read %s", private->dwl_info_filename);
            break;
        }

        mtx_lock(&module->lock);
        if (file_read_content(file, module) != 0) {
            mtx_unlock(&module->lock);
            break;
        }
        mtx_unlock(&module->lock);

        module->bar->refresh(module->bar);
    }

    return run_clean(inotify_fd, inotify_wd, file);
}

static struct module *
dwl_new(struct particle *label, int number_of_tags,
        struct yml_node const *name_of_tags, char const *dwl_info_filename)
{
    struct private *private = calloc(1, sizeof(struct private));
    private->label = label;
    private->number_of_tags = number_of_tags;
    private->dwl_info_filename = strdup(dwl_info_filename);

    struct yml_list_iter list = {0};
    if (name_of_tags)
        list = yml_list_iter(name_of_tags);

    for (int i = 1; i <= number_of_tags; i++) {
        struct dwl_tag *dwl_tag = calloc(1, sizeof(struct dwl_tag));
        dwl_tag->id = i;
        if (list.node) {
            dwl_tag->name = strdup(yml_value_as_string(list.node));
            yml_list_next(&list);
        } else if (asprintf(&dwl_tag->name, "%d", i) < 0) {
            LOG_ERRNO("asprintf");
        }
        tll_push_back(private->tags, dwl_tag);
    }

    struct module *module = module_common_new();
    module->private = private;
    module->run = &run;
    module->destroy = &destroy;
    module->content = &content;
    module->description = &description;

    return module;
}

static struct module *
from_conf(struct yml_node const *node, struct conf_inherit inherited)
{
    struct yml_node const *content = yml_get_value(node, "content");
    struct yml_node const *number_of_tags = yml_get_value(node, "number-of-tags");
    struct yml_node const *name_of_tags = yml_get_value(node, "name-of-tags");
    struct yml_node const *dwl_info_filename = yml_get_value(node, "dwl-info-filename");

    return dwl_new(conf_to_particle(content, inherited), yml_value_as_int(number_of_tags),
                   name_of_tags, yml_value_as_string(dwl_info_filename));
}

static bool
verify_names(keychain_t *keychain, const struct yml_node *node)
{
    if (!yml_is_list(node)) {
        LOG_ERR("%s: %s is not a list", conf_err_prefix(keychain, node), yml_value_as_string(node));
        return false;
    }
    return conf_verify_list(keychain, node, &conf_verify_string);
}

static bool
verify_conf(keychain_t *keychain, struct yml_node const *node)
{

    static struct attr_info const attrs[] = {
        {"number-of-tags", true, &conf_verify_unsigned},
        {"name-of-tags", false, &verify_names},
        {"dwl-info-filename", true, &conf_verify_string},
        MODULE_COMMON_ATTRS,
    };

    if (!conf_verify_dict(keychain, node, attrs))
        return false;

    /* No need to check whether is `number_of_tags` is a int
     * because `conf_verify_unsigned` already did it */
    struct yml_node const *ntags_key = yml_get_key(node, "number-of-tags");
    struct yml_node const *value = yml_get_value(node, "number-of-tags");
    int number_of_tags = yml_value_as_int(value);
    if (number_of_tags == 0) {
        LOG_ERR("%s: %s must not be 0", conf_err_prefix(keychain, ntags_key), yml_value_as_string(ntags_key));
        return false;
    }

    struct yml_node const *key = yml_get_key(node, "name-of-tags");
    value = yml_get_value(node, "name-of-tags");
    if (value && yml_list_length(value) != number_of_tags) {
        LOG_ERR("%s: %s must have the same number of elements that %s", conf_err_prefix(keychain, key),
                yml_value_as_string(key), yml_value_as_string(ntags_key));
        return false;
    }

    /* No need to check whether is `dwl_info_filename` is a string
     * because `conf_verify_string` already did it */
    key = yml_get_key(node, "dwl-info-filename");
    value = yml_get_value(node, "dwl-info-filename");
    if (strlen(yml_value_as_string(value)) == 0) {
        LOG_ERR("%s: %s must not be empty", conf_err_prefix(keychain, key), yml_value_as_string(key));
        return false;
    }

    return true;
}

struct module_iface const module_dwl_iface = {
    .verify_conf = &verify_conf,
    .from_conf = &from_conf,
};

#if defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)
extern struct module_iface const iface __attribute__((weak, alias("module_dwl_iface")));
#endif
