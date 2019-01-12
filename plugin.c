#include "plugin.h"

#include <string.h>
#include <dlfcn.h>

#define LOG_MODULE "plugin"
#define LOG_ENABLE_DBG 0
#include "log.h"
#include "config.h"
#include "tllist.h"

struct plugin {
    char *name;
    void *lib;
    const void *sym;
};

static tll(struct plugin) libs = tll_init();

static void __attribute__((destructor))
fini(void)
{
    tll_foreach(libs, plug) {
        dlerror();
        dlclose(plug->item.lib);

        const char *dl_error = dlerror();
        if (dl_error != NULL)
            LOG_ERR("%s: dlclose(): %s", plug->item.name, dl_error);

        free(plug->item.name);
    }

    tll_free(libs);
}

const struct module_info *
plugin_load_module(const char *name)
{
    char path[128];
    snprintf(path, sizeof(path), "./modules/lib%s.so", name);

    /* Have we already loaded it? */
    tll_foreach(libs, plug) {
        if (strcmp(plug->item.name, name) == 0) {
            LOG_DBG("%s already loaded: %p", name, plug->item.lib);
            assert(plug->item.sym != NULL);
            return plug->item.sym;
        }
    }

    /* Not loaded - do it now */
    void *lib = dlopen(path, RTLD_LOCAL | RTLD_NOW);
    LOG_DBG("%s: dlopened to %p", name, lib);

    if (lib == NULL) {
        LOG_ERR("%s: dlopen: %s", name, dlerror());
        return NULL;
    }

    tll_push_back(libs, ((struct plugin){strdup(name), lib}));
    struct plugin *plug = &tll_back(libs);

    dlerror(); /* Clear previous error */
    plug->sym = dlsym(lib, "module_info");

    const char *dlsym_error = dlerror();
    if (dlsym_error != NULL) {
        LOG_ERR("%s: dlsym: %s", name, dlsym_error);
        return NULL;
    }

    assert(plug->sym != NULL);
    return plug->sym;
}
