#include "plugin.h"

#include <string.h>
#include <dlfcn.h>

#define LOG_MODULE "plugin"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "config.h"
#include "tllist.h"

#if !defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)

#define EXTERN_MODULE(plug_name)                                        \
    extern bool plug_name##_verify_conf(                                \
        keychain_t *chain, const struct yml_node *node);                \
    extern struct module *plug_name##_from_conf(                        \
        const struct yml_node *node, struct conf_inherit inherited);

#define EXTERN_PARTICLE(plug_name)                              \
    extern bool plug_name##_verify_conf(                        \
        keychain_t *chain, const struct yml_node *node);        \
    extern struct particle *plug_name##_from_conf(              \
        const struct yml_node *node, struct particle *common);

#define EXTERN_DECORATION(plug_name)                                    \
    extern bool plug_name##_verify_conf(                                \
        keychain_t *chain, const struct yml_node *node);                \
    extern struct deco *plug_name##_from_conf(const struct yml_node *node);

EXTERN_MODULE(alsa);
EXTERN_MODULE(backlight);
EXTERN_MODULE(battery);
EXTERN_MODULE(clock);
EXTERN_MODULE(i3);
EXTERN_MODULE(label);
EXTERN_MODULE(mpd);
EXTERN_MODULE(network);
EXTERN_MODULE(removables);
EXTERN_MODULE(xkb);
EXTERN_MODULE(xwindow);

EXTERN_PARTICLE(empty);
EXTERN_PARTICLE(list);
EXTERN_PARTICLE(map);
EXTERN_PARTICLE(progress_bar);
EXTERN_PARTICLE(ramp);
EXTERN_PARTICLE(string);

EXTERN_DECORATION(background);
EXTERN_DECORATION(stack);
EXTERN_DECORATION(underline);

#undef EXTERN_DECORATION
#undef EXTERN_PARTICLE
#undef EXTERN_MODULE

#endif

static tll(struct plugin) plugins = tll_init();

static const char *
type2str(enum plugin_type type)
{
    switch (type) {
    case PLUGIN_MODULE:     return "module";
    case PLUGIN_PARTICLE:   return "particle";
    case PLUGIN_DECORATION: return "decoration";
    }

    return NULL;
}

static void __attribute__((constructor))
init(void)
{
#if !defined(CORE_PLUGINS_AS_SHARED_LIBRARIES)

#define REGISTER_CORE_PLUGIN(plug_name, func_prefix, plug_type)   \
    do {                                                          \
        tll_push_back(                                            \
            plugins,                                              \
            ((struct plugin){                                     \
                .name = strdup(#plug_name),                       \
                .type = (plug_type),                              \
                .lib = NULL,                                      \
                .dummy = {                                        \
                    .sym1 = &func_prefix##_verify_conf,           \
                    .sym2 = &func_prefix##_from_conf,             \
                }                                                 \
            }));                                                  \
    } while (0)

#define REGISTER_CORE_MODULE(plug_name, func_prefix)                \
    REGISTER_CORE_PLUGIN(plug_name, func_prefix, PLUGIN_MODULE)
#define REGISTER_CORE_PARTICLE(plug_name, func_prefix)              \
    REGISTER_CORE_PLUGIN(plug_name, func_prefix, PLUGIN_PARTICLE)
#define REGISTER_CORE_DECORATION(plug_name, func_prefix)            \
    REGISTER_CORE_PLUGIN(plug_name, func_prefix, PLUGIN_DECORATION)

    REGISTER_CORE_MODULE(alsa, alsa);
    REGISTER_CORE_MODULE(backlight, backlight);
    REGISTER_CORE_MODULE(battery, battery);
    REGISTER_CORE_MODULE(clock, clock);
    REGISTER_CORE_MODULE(i3, i3);
    REGISTER_CORE_MODULE(label, label);
    REGISTER_CORE_MODULE(mpd, mpd);
    REGISTER_CORE_MODULE(network, network);
    REGISTER_CORE_MODULE(removables, removables);
    REGISTER_CORE_MODULE(xkb, xkb);
    REGISTER_CORE_MODULE(xwindow, xwindow);

    REGISTER_CORE_PARTICLE(empty, empty);
    REGISTER_CORE_PARTICLE(list, list);
    REGISTER_CORE_PARTICLE(map, map);
    REGISTER_CORE_PARTICLE(progress-bar, progress_bar);
    REGISTER_CORE_PARTICLE(ramp, ramp);
    REGISTER_CORE_PARTICLE(string, string);

    REGISTER_CORE_DECORATION(background, background);
    REGISTER_CORE_DECORATION(stack, stack);
    REGISTER_CORE_DECORATION(underline, underline);

#undef REGISTER_CORE_DECORATION
#undef REGISTER_CORE_PARTICLE
#undef REGISTER_CORE_PLUGIN

#endif /* !CORE_PLUGINS_AS_SHARED_LIBRARIES */
}

static void
free_plugin(struct plugin plug)
{
    dlerror();

    if (plug.lib != NULL)
        dlclose(plug.lib);

    const char *dl_error = dlerror();
    if (dl_error != NULL)
        LOG_ERR("%s: %s: dlclose(): %s", type2str(plug.type), plug.name, dl_error);

    free(plug.name);
}

static void __attribute__((destructor))
fini(void)
{
    tll_free_and_free(plugins, free_plugin);
}

const struct plugin *
plugin_load(const char *name, enum plugin_type type)
{
    tll_foreach(plugins, plug) {
        if (plug->item.type == type && strcmp(plug->item.name, name) == 0) {
            LOG_DBG("%s: %s already loaded: %p", type2str(type), name, plug->item.lib);
            assert(plug->item.dummy.sym1 != NULL);
            assert(plug->item.dummy.sym2 != NULL);
            return &plug->item;
        }
    }


    char path[128];
    snprintf(path, sizeof(path), "%s_%s.so", type2str(type), name);

    /* Not loaded - do it now */
    void *lib = dlopen(path, RTLD_LOCAL | RTLD_NOW);
    LOG_DBG("%s: %s: dlopened to %p", type2str(type), name, lib);

    if (lib == NULL) {
        LOG_ERR("%s: %s: dlopen: %s", type2str(type), name, dlerror());
        return NULL;
    }

    tll_push_back(plugins, ((struct plugin){strdup(name), type, lib, {{NULL}}}));
    struct plugin *plug = &tll_back(plugins);

    dlerror(); /* Clear previous error */
    const char *dl_error = NULL;

    plug->dummy.sym1 = dlsym(lib, "verify_conf");
    dl_error = dlerror();

    if (dl_error == NULL) {
        plug->dummy.sym2 = dlsym(lib, "from_conf");
        dl_error = dlerror();
    }

    if (dl_error != NULL) {
        LOG_ERR("%s: %s: dlsym: %s", type2str(type), name, dl_error);
        return NULL;
    }

    return plug;
}

const struct module_iface *
plugin_load_module(const char *name)
{
    const struct plugin *plug = plugin_load(name, PLUGIN_MODULE);
    return plug != NULL ? &plug->module : NULL;
}

const struct particle_iface *
plugin_load_particle(const char *name)
{
    const struct plugin *plug = plugin_load(name, PLUGIN_PARTICLE);
    return plug != NULL ? &plug->particle : NULL;
}

const struct deco_iface *
plugin_load_deco(const char *name)
{
    const struct plugin *plug = plugin_load(name, PLUGIN_DECORATION);
    return plug != NULL ? &plug->decoration : NULL;
}
