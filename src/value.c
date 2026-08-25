#define _POSIX_C_SOURCE 200809L
#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Value val_number(double n) {
    Value v; v.type = VAL_NUMBER; v.as.number = n; return v;
}
Value val_string(const char *s) {
    Value v; v.type = VAL_STRING; v.as.string = strdup(s); return v;
}
Value val_bool(int b) {
    Value v; v.type = VAL_BOOL; v.as.boolean = b; return v;
}
Value val_nil(void) {
    Value v; v.type = VAL_NIL; v.as.number = 0; return v;
}
Value val_function(char **params, int param_count, AstNode *body, Environment *closure) {
    Value v; v.type = VAL_FUNCTION;
    v.as.function.params = params;
    v.as.function.param_count = param_count;
    v.as.function.body = body;
    v.as.function.closure = closure;
    return v;
}
Value val_native(NativeFn fn, const char *name) {
    Value v; v.type = VAL_NATIVE;
    v.as.native.fn = fn;
    v.as.native.name = strdup(name);
    return v;
}
Value val_array(void) {
    Value v; v.type = VAL_ARRAY;
    v.as.array.items = NULL; v.as.array.count = 0; v.as.array.capacity = 0;
    return v;
}
Value val_map(void) {
    Value v; v.type = VAL_MAP;
    v.as.map.entries = NULL; v.as.map.count = 0; v.as.map.capacity = 0;
    return v;
}
Value val_instance(ClassDef *class_def) {
    Value v; v.type = VAL_INSTANCE;
    v.as.instance.class_def = class_def;
    v.as.instance.field_count = class_def->field_count;
    v.as.instance.fields = calloc(class_def->field_count > 0 ? class_def->field_count : 1, sizeof(Value));
    for (int i = 0; i < class_def->field_count; i++)
        v.as.instance.fields[i] = val_nil();
    return v;
}
Value val_class(ClassDef *class_def) {
    Value v; v.type = VAL_CLASS;
    v.as.class_def = class_def;
    return v;
}
Value val_error(const char *message) {
    Value v; v.type = VAL_ERROR; v.as.error.message = strdup(message); return v;
}

void val_free(Value *v) {
    if (v->type == VAL_STRING) { free(v->as.string); v->as.string = NULL; }
    if (v->type == VAL_NATIVE) { free(v->as.native.name); v->as.native.name = NULL; }
    if (v->type == VAL_ARRAY) {
        for (int i = 0; i < v->as.array.count; i++) val_free(&v->as.array.items[i]);
        free(v->as.array.items);
    }
    if (v->type == VAL_MAP) {
        for (int i = 0; i < v->as.map.count; i++) {
            free(v->as.map.entries[i].key);
            val_free(v->as.map.entries[i].value);
        }
        free(v->as.map.entries);
    }
    if (v->type == VAL_CLASS) {
        for (int i = 0; i < v->as.class_def->field_count; i++) free(v->as.class_def->fields[i]);
        free(v->as.class_def->fields);
        for (int i = 0; i < v->as.class_def->method_count; i++) free(v->as.class_def->methods[i].name);
        free(v->as.class_def->methods);
        free(v->as.class_def);
    }
    if (v->type == VAL_INSTANCE) free(v->as.instance.fields);
    if (v->type == VAL_ERROR) free(v->as.error.message);
}

int val_is_truthy(Value *v) {
    if (v->type == VAL_NIL) return 0;
    if (v->type == VAL_BOOL) return v->as.boolean;
    if (v->type == VAL_NUMBER) return v->as.number != 0;
    if (v->type == VAL_STRING) return v->as.string[0] != '\0';
    return 1;
}

const char *val_type_name(Value *v) {
    switch (v->type) {
        case VAL_NUMBER: return "number";
        case VAL_STRING: return "text";
        case VAL_BOOL: return "truth";
        case VAL_NIL: return "nothing";
        case VAL_FUNCTION: case VAL_NATIVE: return "function";
        case VAL_ARRAY: return "list";
        case VAL_CLASS: return "class";
        case VAL_INSTANCE: return "object";
        case VAL_ERROR: return "error";
        case VAL_MAP: return "map";
    }
    return "unknown";
}

void val_print(Value *v) {
    switch (v->type) {
        case VAL_NUMBER:
            if (v->as.number == (long long)v->as.number)
                printf("%lld", (long long)v->as.number);
            else
                printf("%g", v->as.number);
            break;
        case VAL_STRING: printf("%s", v->as.string); break;
        case VAL_BOOL: printf("%s", v->as.boolean ? "true" : "false"); break;
        case VAL_NIL: printf("nothing"); break;
        case VAL_FUNCTION: case VAL_NATIVE: printf("<function>"); break;
        case VAL_CLASS: printf("<class>"); break;
        case VAL_INSTANCE: printf("<object>"); break;
        case VAL_ERROR: printf("error: %s", v->as.error.message); break;
        case VAL_MAP:
            printf("{");
            for (int i = 0; i < v->as.map.count; i++) {
                if (i > 0) printf(", ");
                printf("%s: ", v->as.map.entries[i].key);
                val_print(v->as.map.entries[i].value);
            }
            printf("}");
            break;
        case VAL_ARRAY:
            printf("[");
            for (int i = 0; i < v->as.array.count; i++) {
                if (i > 0) printf(", ");
                val_print(&v->as.array.items[i]);
            }
            printf("]");
            break;
    }
}

Value val_copy(Value v) {
    if (v.type == VAL_STRING) return val_string(v.as.string);
    if (v.type == VAL_ERROR) return val_error(v.as.error.message);
    if (v.type == VAL_ARRAY) {
        Value a = val_array();
        for (int i = 0; i < v.as.array.count; i++)
            val_array_push(&a.as.array, val_copy(v.as.array.items[i]));
        return a;
    }
    if (v.type == VAL_MAP) {
        Value m = val_map();
        for (int i = 0; i < v.as.map.count; i++)
            val_map_set(&m, v.as.map.entries[i].key, val_copy(*v.as.map.entries[i].value));
        return m;
    }
    if (v.type == VAL_INSTANCE) {
        Value cpy;
        cpy.type = VAL_INSTANCE;
        cpy.as.instance.class_def = v.as.instance.class_def;
        cpy.as.instance.field_count = v.as.instance.field_count;
        cpy.as.instance.fields = malloc(sizeof(Value) * (v.as.instance.field_count > 0 ? v.as.instance.field_count : 1));
        for (int i = 0; i < v.as.instance.field_count; i++)
            cpy.as.instance.fields[i] = val_copy(v.as.instance.fields[i]);
        return cpy;
    }
    if (v.type == VAL_NATIVE) {
        Value n; n.type = VAL_NATIVE;
        n.as.native.fn = v.as.native.fn;
        n.as.native.name = strdup(v.as.native.name);
        return n;
    }
    return v;
}

void val_array_push(ValueArray *arr, Value v) {
    if (arr->count >= arr->capacity) {
        arr->capacity = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->items = realloc(arr->items, sizeof(Value) * arr->capacity);
    }
    arr->items[arr->count++] = v;
}

Value val_array_get(ValueArray *arr, int index) {
    if (index < 0 || index >= arr->count) return val_nil();
    return arr->items[index];
}

Value val_array_remove(ValueArray *arr, int index) {
    if (index < 0 || index >= arr->count) return val_nil();
    Value removed = arr->items[index];
    for (int i = index; i < arr->count - 1; i++)
        arr->items[i] = arr->items[i + 1];
    arr->count--;
    return removed;
}

static void map_push(Value *m, const char *key, Value v) {
    if (m->as.map.count >= m->as.map.capacity) {
        m->as.map.capacity = m->as.map.capacity == 0 ? 8 : m->as.map.capacity * 2;
        m->as.map.entries = realloc(m->as.map.entries, sizeof(MapEntry) * m->as.map.capacity);
    }
    m->as.map.entries[m->as.map.count].key = strdup(key);
    m->as.map.entries[m->as.map.count].value = malloc(sizeof(Value));
    *m->as.map.entries[m->as.map.count].value = v;
    m->as.map.count++;
}

Value val_map_get(Value *m, const char *key) {
    for (int i = 0; i < m->as.map.count; i++)
        if (strcmp(m->as.map.entries[i].key, key) == 0)
            return *m->as.map.entries[i].value;
    return val_nil();
}

void val_map_set(Value *m, const char *key, Value v) {
    for (int i = 0; i < m->as.map.count; i++) {
        if (strcmp(m->as.map.entries[i].key, key) == 0) {
            val_free(m->as.map.entries[i].value);
            *m->as.map.entries[i].value = v;
            return;
        }
    }
    map_push(m, key, v);
}

int val_map_has(Value *m, const char *key) {
    for (int i = 0; i < m->as.map.count; i++)
        if (strcmp(m->as.map.entries[i].key, key) == 0) return 1;
    return 0;
}

Value val_map_remove(Value *m, const char *key) {
    for (int i = 0; i < m->as.map.count; i++) {
        if (strcmp(m->as.map.entries[i].key, key) == 0) {
            Value removed = *m->as.map.entries[i].value;
            free(m->as.map.entries[i].key);
            free(m->as.map.entries[i].value);
            for (int j = i; j < m->as.map.count - 1; j++)
                m->as.map.entries[j] = m->as.map.entries[j + 1];
            m->as.map.count--;
            return removed;
        }
    }
    return val_nil();
}
