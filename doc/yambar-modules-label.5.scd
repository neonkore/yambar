yambar-modules-label(5)

# NAME
label - This module renders the provided _content_ particle

# DESCRIPTION 

This module renders the provided _content_ particle, but provides no
additional data.

# TAGS

None

# CONFIGURATION

No additional attributes supported, only the generic ones (see
*GENERIC CONFIGURATION* in *yambar-modules*(5))

# EXAMPLES

```
bar:
  left:
    - label:
        content: {string: {text: hello world}}
```

# SEE ALSO

*yambar-modules*(5), *yambar-particles*(5), *yambar-tags*(5), *yambar-decorations*(5)

