#pragma once

#include "bar.h"
#include "yml.h"
#include "font.h"

struct bar *conf_to_bar(const struct yml_node *bar);
