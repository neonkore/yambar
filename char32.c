#include "char32.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <wchar.h>

#if defined __has_include
 #if __has_include (<stdc-predef.h>)
   #include <stdc-predef.h>
 #endif
#endif

#define LOG_MODULE "char32"
#define LOG_ENABLE_DBG 0
#include "log.h"

/*
 * For now, assume we can map directly to the corresponding wchar_t
 * functions. This is true if:
 *
 *  - both data types have the same size
 *  - both use the same encoding (though we require that encoding to be UTF-32)
 */

_Static_assert(
    sizeof(wchar_t) == sizeof(char32_t), "wchar_t vs. char32_t size mismatch");

#if !defined(__STDC_UTF_32__) || !__STDC_UTF_32__
 #error "char32_t does not use UTF-32"
#endif
#if (!defined(__STDC_ISO_10646__) || !__STDC_ISO_10646__) && !defined(__FreeBSD__)
 #error "wchar_t does not use UTF-32"
#endif

size_t
c32len(const char32_t *s)
{
    return wcslen((const wchar_t *)s);
}

char32_t *
ambstoc32(const char *src)
{
    if (src == NULL)
        return NULL;

    const size_t src_len = strlen(src);

    char32_t *ret = malloc((src_len + 1) * sizeof(ret[0]));
    if (ret == NULL)
        return NULL;

    mbstate_t ps = {0};
    char32_t *out = ret;
    const char *in = src;
    const char *const end = src + src_len + 1;

    size_t chars = 0;
    size_t rc;

    while ((rc = mbrtoc32(out, in, end - in, &ps)) != 0) {
        switch (rc) {
        case (size_t)-1:
        case (size_t)-2:
        case (size_t)-3:
            goto err;
        }

        in += rc;
        out++;
        chars++;
    }

    *out = U'\0';

    ret = realloc(ret, (chars + 1) * sizeof(ret[0]));
    return ret;

err:
    free(ret);
    return NULL;
}
