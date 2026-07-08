#include "system/vars.h"

#include <string.h>

typedef struct {
    char name [VAR_NAME_MAX  + 1];
    char value[VAR_VALUE_MAX + 1];
    bool used;
} var_slot_t;

static var_slot_t g_slots[VAR_MAX_COUNT];

bool vars_name_char_first(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool vars_name_char_rest(char c) {
    return vars_name_char_first(c) || (c >= '0' && c <= '9');
}

static bool valid_name(const char *name) {
    if (!name || !*name) return false;
    if (!vars_name_char_first(name[0])) return false;
    size_t n = 0;
    for (const char *p = name; *p; p++, n++) {
        if (!vars_name_char_rest(*p)) return false;
        if (n >= VAR_NAME_MAX) return false;
    }
    return true;
}

static var_slot_t *find_slot(const char *name) {
    for (size_t i = 0; i < VAR_MAX_COUNT; i++) {
        if (g_slots[i].used && strcmp(g_slots[i].name, name) == 0) {
            return &g_slots[i];
        }
    }
    return NULL;
}

static var_slot_t *free_slot(void) {
    for (size_t i = 0; i < VAR_MAX_COUNT; i++) {
        if (!g_slots[i].used) return &g_slots[i];
    }
    return NULL;
}

bool vars_set(const char *name, const char *value) {
    if (!valid_name(name)) return false;
    if (!value) value = "";
    if (strlen(value) > VAR_VALUE_MAX) return false;

    var_slot_t *s = find_slot(name);
    if (!s) {
        s = free_slot();
        if (!s) return false;
        s->used = true;
        strncpy(s->name, name, VAR_NAME_MAX);
        s->name[VAR_NAME_MAX] = '\0';
    }
    strncpy(s->value, value, VAR_VALUE_MAX);
    s->value[VAR_VALUE_MAX] = '\0';
    return true;
}

bool vars_unset(const char *name) {
    var_slot_t *s = find_slot(name);
    if (!s) return false;
    s->used = false;
    return true;
}

const char *vars_get(const char *name) {
    if (!name) return NULL;
    var_slot_t *s = find_slot(name);
    return s ? s->value : NULL;
}

size_t vars_count(void) {
    size_t n = 0;
    for (size_t i = 0; i < VAR_MAX_COUNT; i++) if (g_slots[i].used) n++;
    return n;
}

void vars_iter(vars_iter_fn cb, void *user) {
    if (!cb) return;
    for (size_t i = 0; i < VAR_MAX_COUNT; i++) {
        if (g_slots[i].used) cb(g_slots[i].name, g_slots[i].value, user);
    }
}
