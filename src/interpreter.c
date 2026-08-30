#define _POSIX_C_SOURCE 200809L
#include "interpreter.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(_WIN32)
#define SB_NO_GUI 1
#endif

#ifdef _WIN32
#define TokenType SB_WIN_TOKEN_TYPE  /* winnt.h claims this name; we take it back */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#undef TokenType
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#ifndef SB_NO_CURL
#include <curl/curl.h>
#endif

#ifndef SB_NO_GUI
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <dlfcn.h>
#include "ketiwe.h"

// lazy-load handles — libs stay on disk until first window/fetch (your idea: keep all, load only when used)
static void *shim_gtk_handle = NULL;
static void *shim_webkit_handle = NULL;
static void *shim_curl_handle = NULL;

// function pointers for lazy-loaded symbols
static gboolean (*shim_gtk_init_check)(int *, char ***) = NULL;
static GtkWidget* (*shim_gtk_window_new)(GtkWindowType) = NULL;
static void (*shim_gtk_window_set_title)(GtkWindow*, const gchar*) = NULL;
static void (*shim_gtk_window_set_default_size)(GtkWindow*, gint, gint) = NULL;
static GtkWidget* (*shim_gtk_box_new)(GtkOrientation, gint) = NULL;
static void (*shim_gtk_container_set_border_width)(GtkContainer*, guint) = NULL;
static void (*shim_gtk_container_add)(GtkContainer*, GtkWidget*) = NULL;
static void (*shim_gtk_widget_show_all)(GtkWidget*) = NULL;
static void (*shim_gtk_widget_destroy)(GtkWidget*) = NULL;
static void (*shim_gtk_widget_set_halign)(GtkWidget*, GtkAlign) = NULL;
static GtkWidget* (*shim_gtk_label_new)(const gchar*) = NULL;
static void (*shim_gtk_label_set_line_wrap)(GtkLabel*, gboolean) = NULL;
static GtkWidget* (*shim_gtk_button_new_with_label)(const gchar*) = NULL;
static GtkWidget* (*shim_gtk_entry_new)(void) = NULL;
static void (*shim_gtk_entry_set_placeholder_text)(GtkEntry*, const gchar*) = NULL;
static void (*shim_gtk_box_pack_start)(GtkBox*, GtkWidget*, gboolean, gboolean, guint) = NULL;
static void (*shim_gtk_main)(void) = NULL;
static void (*shim_gtk_main_quit)(void) = NULL;
static gulong (*shim_g_signal_connect_data)(gpointer, const gchar*, GCallback, gpointer, GClosureNotify, GConnectFlags) = NULL;
static CURL* (*shim_curl_easy_init)(void) = NULL;
static CURLcode (*shim_curl_easy_setopt)(CURL*, CURLoption, ...) = NULL;
static CURLcode (*shim_curl_easy_perform)(CURL*) = NULL;
static void (*shim_curl_easy_cleanup)(CURL*) = NULL;
static const char* (*shim_curl_easy_strerror)(CURLcode) = NULL;
static WebKitWebContext* (*shim_webkit_web_context_new_ephemeral)(void) = NULL;
static void (*shim_webkit_web_context_set_process_model)(WebKitWebContext*, WebKitProcessModel) = NULL;
static void (*shim_webkit_web_context_set_cache_model)(WebKitWebContext*, WebKitCacheModel) = NULL;
static void (*shim_webkit_web_view_load_uri)(WebKitWebView*, const gchar*) = NULL;
static void (*shim_webkit_web_view_load_html)(WebKitWebView*, const gchar*, const gchar*) = NULL;
static gboolean (*shim_g_str_has_prefix)(const gchar*, const gchar*) = NULL;
static gpointer (*shim_g_object_new)(GType, const gchar*, ...) = NULL;
static GType (*shim_webkit_web_view_get_type)(void) = NULL;

static int shim_load_gtk(void) {
    if (shim_gtk_handle && shim_gtk_init_check) return 1;
    shim_gtk_handle = dlopen("libgtk-3.so.0", RTLD_LAZY);
    if (!shim_gtk_handle) shim_gtk_handle = dlopen("libgtk-3.so", RTLD_LAZY);
    if (!shim_gtk_handle) return 0;
    shim_gtk_init_check = dlsym(shim_gtk_handle, "gtk_init_check");
    shim_gtk_window_new = dlsym(shim_gtk_handle, "gtk_window_new");
    shim_gtk_window_set_title = dlsym(shim_gtk_handle, "gtk_window_set_title");
    shim_gtk_window_set_default_size = dlsym(shim_gtk_handle, "gtk_window_set_default_size");
    shim_gtk_box_new = dlsym(shim_gtk_handle, "gtk_box_new");
    shim_gtk_container_set_border_width = dlsym(shim_gtk_handle, "gtk_container_set_border_width");
    shim_gtk_container_add = dlsym(shim_gtk_handle, "gtk_container_add");
    shim_gtk_widget_show_all = dlsym(shim_gtk_handle, "gtk_widget_show_all");
    shim_gtk_widget_destroy = dlsym(shim_gtk_handle, "gtk_widget_destroy");
    shim_gtk_widget_set_halign = dlsym(shim_gtk_handle, "gtk_widget_set_halign");
    shim_gtk_label_new = dlsym(shim_gtk_handle, "gtk_label_new");
    shim_gtk_label_set_line_wrap = dlsym(shim_gtk_handle, "gtk_label_set_line_wrap");
    shim_gtk_button_new_with_label = dlsym(shim_gtk_handle, "gtk_button_new_with_label");
    shim_gtk_entry_new = dlsym(shim_gtk_handle, "gtk_entry_new");
    shim_gtk_entry_set_placeholder_text = dlsym(shim_gtk_handle, "gtk_entry_set_placeholder_text");
    shim_gtk_box_pack_start = dlsym(shim_gtk_handle, "gtk_box_pack_start");
    shim_gtk_main = dlsym(shim_gtk_handle, "gtk_main");
    shim_gtk_main_quit = dlsym(shim_gtk_handle, "gtk_main_quit");
    // gobject signal is in libgobject, but dlopen gtk pulls it
    shim_g_signal_connect_data = dlsym(shim_gtk_handle, "g_signal_connect_data");
    if (!shim_g_signal_connect_data) {
        void *gobj = dlopen("libgobject-2.0.so.0", RTLD_LAZY);
        if (gobj) shim_g_signal_connect_data = dlsym(gobj, "g_signal_connect_data");
    }
    // glib helpers used in window URL checks
    void *glib = dlopen("libglib-2.0.so.0", RTLD_LAZY);
    if (glib) {
        shim_g_str_has_prefix = dlsym(glib, "g_str_has_prefix");
    }
    void *gobj2 = dlopen("libgobject-2.0.so.0", RTLD_LAZY);
    if (gobj2) {
        if (!shim_g_object_new) shim_g_object_new = dlsym(gobj2, "g_object_new");
        if (!shim_g_signal_connect_data) shim_g_signal_connect_data = dlsym(gobj2, "g_signal_connect_data");
    }
    return shim_gtk_init_check && shim_gtk_window_new;
}
static int shim_load_webkit(void) {
    if (shim_webkit_handle && shim_webkit_web_context_new_ephemeral) return 1;
    shim_webkit_handle = dlopen("libwebkit2gtk-4.1.so.0", RTLD_LAZY);
    if (!shim_webkit_handle) shim_webkit_handle = dlopen("libwebkit2gtk-4.0.so.0", RTLD_LAZY);
    if (!shim_webkit_handle) shim_webkit_handle = dlopen("libwebkit2gtk-4.1.so", RTLD_LAZY);
    if (!shim_webkit_handle) return 0;
    shim_webkit_web_context_new_ephemeral = dlsym(shim_webkit_handle, "webkit_web_context_new_ephemeral");
    shim_webkit_web_context_set_process_model = dlsym(shim_webkit_handle, "webkit_web_context_set_process_model");
    shim_webkit_web_context_set_cache_model = dlsym(shim_webkit_handle, "webkit_web_context_set_cache_model");
    shim_webkit_web_view_load_uri = dlsym(shim_webkit_handle, "webkit_web_view_load_uri");
    shim_webkit_web_view_load_html = dlsym(shim_webkit_handle, "webkit_web_view_load_html");
    shim_webkit_web_view_get_type = dlsym(shim_webkit_handle, "webkit_web_view_get_type");
    return shim_webkit_web_context_new_ephemeral != NULL;
}
static int shim_load_curl(void) {
    if (shim_curl_handle && shim_curl_easy_init) return 1;
    shim_curl_handle = dlopen("libcurl.so.4", RTLD_LAZY);
    if (!shim_curl_handle) shim_curl_handle = dlopen("libcurl.so", RTLD_LAZY);
    if (!shim_curl_handle) return 0;
    shim_curl_easy_init = dlsym(shim_curl_handle, "curl_easy_init");
    shim_curl_easy_setopt = dlsym(shim_curl_handle, "curl_easy_setopt");
    shim_curl_easy_perform = dlsym(shim_curl_handle, "curl_easy_perform");
    shim_curl_easy_cleanup = dlsym(shim_curl_handle, "curl_easy_cleanup");
    shim_curl_easy_strerror = dlsym(shim_curl_handle, "curl_easy_strerror");
    return shim_curl_easy_init != NULL;
}
#endif

static void interp_error_code(Interpreter *interp, int line, ErrorCode code, const char *msg) {
    if (!interp->had_error) {
        interp->had_error = 1;
        interp->error_code = code;
        interp->error_line = line;
        snprintf(interp->error_msg, sizeof(interp->error_msg), "%s", msg);
        // capture call chain (innermost first)
        interp->error_trace[0] = '\0';
        size_t off = 0;
        for (int i = interp->call_depth - 1; i >= 0 && off < sizeof(interp->error_trace) - 40; i--) {
            int w = snprintf(interp->error_trace + off, sizeof(interp->error_trace) - off,
                             "\n  at %s (line %d)", interp->call_stack[i], interp->call_lines[i]);
            if (w > 0) off += (size_t)w;
        }
    }
}

static void interp_error(Interpreter *interp, int line, const char *msg) {
    interp_error_code(interp, line, ERR_RUNTIME, msg);
}

static Value interp_eval(Interpreter *interp, Environment *env, AstNode *node);

// ── Environment ──────────────────────────────────────────────────────

static Environment *env_create(Environment *parent) {
    Environment *env = calloc(1, sizeof(Environment));
    env->parent = parent;
    return env;
}

static void env_free(Environment *env) {
    for (int i = 0; i < env->count; i++) {
        free(env->bindings[i].name);
        val_free(&env->bindings[i].value);
    }
    free(env->bindings);
    free(env);
}

static void env_set(Environment *env, const char *name, Value value) {
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->bindings[i].name, name) == 0) {
            val_free(&env->bindings[i].value);
            env->bindings[i].value = value;
            return;
        }
    }
    if (env->count >= env->capacity) {
        env->capacity = env->capacity == 0 ? 8 : env->capacity * 2;
        env->bindings = realloc(env->bindings, sizeof(EnvBinding) * env->capacity);
    }
    env->bindings[env->count].name = strdup(name);
    env->bindings[env->count].value = value;
    env->count++;
}

static Value env_get(Environment *env, const char *name) {
    for (int i = 0; i < env->count; i++)
        if (strcmp(env->bindings[i].name, name) == 0)
            return env->bindings[i].value;
    // dotted name: try map.field lookup (e.g. m.sin → env has map "m" with key "sin")
    const char *dot = strchr(name, '.');
    if (dot && dot[1] != '\0') {
        size_t prefix_len = (size_t)(dot - name);
        char prefix[256];
        if (prefix_len < sizeof(prefix)) {
            memcpy(prefix, name, prefix_len);
            prefix[prefix_len] = '\0';
            Value obj = env_get(env, prefix);
            if (obj.type == VAL_MAP) {
                const char *key = dot + 1;
                for (int i = 0; i < obj.as.map.count; i++)
                    if (strcmp(obj.as.map.entries[i].key, key) == 0)
                        return *obj.as.map.entries[i].value;
            }
        }
    }
    if (env->parent) return env_get(env->parent, name);
    return val_nil();
}

static Value *env_get_ptr(Environment *env, const char *name) {
    for (int i = 0; i < env->count; i++)
        if (strcmp(env->bindings[i].name, name) == 0)
            return &env->bindings[i].value;
    if (env->parent) return env_get_ptr(env->parent, name);
    return NULL;
}

static Value interp_block(Interpreter *interp, Environment *env, AstNode *block) {
    Value result = val_nil();
    if (block->type == NODE_BLOCK) {
        for (int i = 0; i < block->as.block.count; i++) {
            result = interp_eval(interp, env, block->as.block.items[i]);
            if (interp->had_error || interp->returning || interp->breaking || interp->continuing) break;
        }
    } else {
        result = interp_eval(interp, env, block);
    }
    return result;
}

// ── String helpers ───────────────────────────────────────────────────

static char *val_to_str(Value v);  /* recursive, malloc'd — caller frees */

static const char *value_to_cstr(Value v, char tmp[64]) {
    switch (v.type) {
        case VAL_NUMBER:
            if (v.as.number == (long long)v.as.number)
                snprintf(tmp, 64, "%lld", (long long)v.as.number);
            else
                snprintf(tmp, 64, "%g", v.as.number);
            return tmp;
        case VAL_STRING: return v.as.string;
        case VAL_BOOL: return v.as.boolean ? "true" : "false";
        case VAL_NIL: return "nothing";
        case VAL_ERROR: return v.as.error.message;
        case VAL_ARRAY: case VAL_MAP: return val_to_str(v);
        default: return "<value>";
    }
}

/* mirrors val_print but into a malloc'd string (for say/concat) */
static char *val_to_str(Value v) {
    size_t cap = 64, len = 0;
    char *buf = malloc(cap); buf[0]='\0';
    #define APPEND(sstr) do { \
        size_t sl = strlen(sstr); \
        while (len + sl + 1 > cap) { cap *= 2; buf = realloc(buf, cap); } \
        memcpy(buf+len, sstr, sl); len += sl; buf[len]='\0'; \
    } while(0)
    char tmp[64];
    switch (v.type) {
        case VAL_NUMBER:
            if (v.as.number == (long long)v.as.number) snprintf(tmp,sizeof(tmp),"%lld",(long long)v.as.number);
            else snprintf(tmp,sizeof(tmp),"%g",v.as.number);
            APPEND(tmp); break;
        case VAL_STRING: APPEND(v.as.string); break;
        case VAL_BOOL: APPEND(v.as.boolean?"true":"false"); break;
        case VAL_NIL: APPEND("nothing"); break;
        case VAL_ERROR: APPEND(v.as.error.message); break;
        case VAL_FUNCTION: case VAL_NATIVE: APPEND("<function>"); break;
        case VAL_CLASS: APPEND("<class>"); break;
        case VAL_INSTANCE: APPEND("<object>"); break;
        case VAL_ARRAY:
            APPEND("[");
            for (int i=0;i<v.as.array.count;i++) {
                if (i) APPEND(", ");
                char *inner = val_to_str(v.as.array.items[i]);
                APPEND(inner); free(inner);
            }
            APPEND("]"); break;
        case VAL_MAP:
            APPEND("{");
            for (int i=0;i<v.as.map.count;i++) {
                if (i) APPEND(", ");
                APPEND(v.as.map.entries[i].key);
                APPEND(": ");
                char *inner = val_to_str(*v.as.map.entries[i].value);
                APPEND(inner); free(inner);
            }
            APPEND("}"); break;
        default: APPEND("<value>"); break;
    }
    #undef APPEND
    return buf;
}

static Value value_concat(Value a, Value b) {
    char ta[64], tb[64];
    const char *sa = value_to_cstr(a, ta);
    const char *sb = value_to_cstr(b, tb);
    size_t n = strlen(sa) + strlen(sb) + 1;
    char *buf = malloc(n);
    snprintf(buf, n, "%s%s", sa, sb);
    Value v = val_string(buf);
    free(buf);
    return v;
}

// ── File / import helpers ────────────────────────────────────────────

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static char *read_file_cstr(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (fread(buf, 1, size, f) != (size_t)size) { fclose(f); free(buf); return NULL; }
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static int str_ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lx = strlen(suffix);
    if (ls < lx) return 0;
    return strcmp(s + ls - lx, suffix) == 0;
}

void interp_embed_module(Interpreter *interp, const char *name, const char *src) {
    if (interp->embed_count >= SB_MAX_EMBED) return;
    interp->embed_names[interp->embed_count] = strdup(name);
    interp->embed_srcs[interp->embed_count] = strdup(src);
    interp->embed_count++;
}

static const char *find_embedded(Interpreter *interp, const char *name) {
    for (int i = 0; i < interp->embed_count; i++)
        if (strcmp(interp->embed_names[i], name) == 0)
            return interp->embed_srcs[i];
    return NULL;
}

void interp_add_search_path(Interpreter *interp, const char *dir) {
    if (interp->search_path_count >= SB_MAX_SEARCH_PATHS) return;
    interp->search_paths[interp->search_path_count++] = strdup(dir);
}

static char *resolve_module(Interpreter *interp, const char *name) {
    char cand[1024];
    if (str_ends_with(name, ".sb")) {
        if (file_exists(name) && strcmp(name, interp->main_path) != 0) return strdup(name);
        for (int i = interp->search_path_count - 1; i >= 0; i--) {
            snprintf(cand, sizeof(cand), "%s/%s", interp->search_paths[i], name);
            if (file_exists(cand) && strcmp(cand, interp->main_path) != 0) return strdup(cand);
        }
        return NULL;
    }
    snprintf(cand, sizeof(cand), "%s.sb", name);
    if (file_exists(cand) && strcmp(cand, interp->main_path) != 0) return strdup(cand);
    for (int i = interp->search_path_count - 1; i >= 0; i--) {
        snprintf(cand, sizeof(cand), "%s/%s.sb", interp->search_paths[i], name);
        if (file_exists(cand) && strcmp(cand, interp->main_path) != 0) return strdup(cand);
    }
    // package convention: <name>/main.sb is importable as <name>
    for (int i = interp->search_path_count - 1; i >= 0; i--) {
        snprintf(cand, sizeof(cand), "%s/%s/main.sb", interp->search_paths[i], name);
        if (file_exists(cand) && strcmp(cand, interp->main_path) != 0) return strdup(cand);
    }
    return NULL;
}

static int module_already_loaded(Interpreter *ip, const char *path) {
    for (int i = 0; i < ip->loaded_count; i++)
        if (strcmp(ip->loaded[i], path) == 0) return 1;
    return 0;
}

static void mark_module_loaded(Interpreter *ip, const char *path) {
    if (ip->loaded_count >= SB_MAX_LOADED_MODULES) return;
    ip->loaded[ip->loaded_count++] = strdup(path);
}

static void run_import(Interpreter *interp, AstNode *node) {
    // "bring." alone → load ALL embedded std libs
    if (!node->as.import.module) {
        for (int i = 0; i < interp->embed_count; i++) {
            const char *emb = interp->embed_names[i];
            if (module_already_loaded(interp, emb)) continue;
            mark_module_loaded(interp, emb);
            Parser p;
            parser_init(&p, interp->embed_srcs[i]);
            AstNode *prog = parser_parse(&p);
            if (p.had_error) { node_free(prog); continue; }
            interp_eval(interp, interp->global, prog);
        }
        return;
    }

    const char *emb = find_embedded(interp, node->as.import.module);
    if (emb) {
        if (module_already_loaded(interp, emb)) return;
        mark_module_loaded(interp, emb);
        Parser parser;
        parser_init(&parser, emb);
        AstNode *program = parser_parse(&parser);
        if (parser.had_error) {
            char msg[768];
            snprintf(msg, sizeof(msg), "embedded module '%s' failed to parse", node->as.import.module);
            interp_error(interp, node->line, msg);
            node_free(program);
            return;
        }
        // alias: run in child env, then copy bindings as alias map + flat alias.name
        if (node->as.import.alias) {
            Environment *child = env_create(interp->global);
            interp_eval(interp, child, program);
            Value m_map = val_map();
            for (int i = 0; i < child->count; i++) {
                val_map_set(&m_map, child->bindings[i].name, val_copy(child->bindings[i].value));
                char qname[512];
                snprintf(qname, sizeof(qname), "%s.%s", node->as.import.alias, child->bindings[i].name);
                env_set(interp->global, qname, val_copy(child->bindings[i].value));
            }
            env_set(interp->global, node->as.import.alias, m_map);
            // keep child alive — functions close over it (don't free)
        } else {
            interp_eval(interp, interp->global, program);
        }
        /* keep AST alive — functions/classes point into it (same as file imports) */
        return;
    }
    char *path = resolve_module(interp, node->as.import.module);
    if (!path) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot find module '%s'", node->as.import.module);
        interp_error(interp, node->line, msg);
        return;
    }
    if (module_already_loaded(interp, path)) { free(path); return; }
    mark_module_loaded(interp, path);

    // let the imported module import ITS siblings: add its directory to search paths
    {
        char moddir[1024];
        const char *slash = strrchr(path, '/');
        if (slash) {
            size_t dl = (size_t)(slash - path);
            if (dl >= sizeof(moddir)) dl = sizeof(moddir) - 1;
            memcpy(moddir, path, dl);
            moddir[dl] = '\0';
            int already = 0;
            for (int i = 0; i < interp->search_path_count; i++)
                if (strcmp(interp->search_paths[i], moddir) == 0) already = 1;
            if (!already) interp_add_search_path(interp, moddir);
        }
    }

    char *source = read_file_cstr(path);
    if (!source) {
        char msg[512];
        snprintf(msg, sizeof(msg), "cannot read module '%s'", path);
        interp_error(interp, node->line, msg);
        free(path);
        return;
    }

    Parser parser;
    parser_init(&parser, source);
    AstNode *program = parser_parse(&parser);
    if (parser.had_error) {
        char msg[768];
        snprintf(msg, sizeof(msg), "parse error in %s: %s", path, parser.error_msg);
        interp_error(interp, node->line, msg);
        node_free(program);
        free(source);
        free(path);
        return;
    }

    if (node->as.import.alias) {
        Environment *child = env_create(interp->global);
        interp_eval(interp, child, program);
        Value m_map = val_map();
        for (int i = 0; i < child->count; i++) {
            val_map_set(&m_map, child->bindings[i].name, val_copy(child->bindings[i].value));
            char qname[512];
            snprintf(qname, sizeof(qname), "%s.%s", node->as.import.alias, child->bindings[i].name);
            env_set(interp->global, qname, val_copy(child->bindings[i].value));
        }
        env_set(interp->global, node->as.import.alias, m_map);
        // keep child alive — functions close over it
    } else {
        interp_eval(interp, interp->global, program);
    }
    // keep AST alive — classes/functions point into it
    free(source);
    free(path);
}

// ── Natives ──────────────────────────────────────────────────────────

static Value native_make_list(int argc, Value *args) {
    Value a = val_array();
    for (int i = 0; i < argc; i++) val_array_push(&a.as.array, val_copy(args[i]));
    return a;
}

static Value native_length(int argc, Value *args) {
    if (argc < 1) return val_number(0);
    if (args[0].type == VAL_STRING) return val_number((double)strlen(args[0].as.string));
    if (args[0].type == VAL_ARRAY) return val_number((double)args[0].as.array.count);
    if (args[0].type == VAL_MAP) return val_number((double)args[0].as.map.count);
    return val_number(0);
}

static Value native_item(int argc, Value *args) {
    if (argc < 2 || args[1].type != VAL_ARRAY) return val_nil();
    int idx = (int)args[0].as.number;
    return val_copy(val_array_get(&args[1].as.array, idx));
}

static Value native_add_to(int argc, Value *args) {
    if (argc < 2) return val_nil();
    // args[0] is the array value (copy); need to mutate in env — we mutate the copy that is also stored
    // But val_copy semantics: caller passed val_copy of stored array, so mutation is lost
    // So natives that mutate do it via side-effect on the Value* passed in? We handle add_to specially in interp_eval
    // Fallback: push and return copy
    if (args[0].type != VAL_ARRAY) return val_nil();
    val_array_push(&args[0].as.array, val_copy(args[1]));
    return args[0];
}

static Value native_remove_from(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_ARRAY) return val_nil();
    int idx = (int)args[1].as.number;
    return val_array_remove(&args[0].as.array, idx);
}

static Value native_upper(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_string("");
    char *s = strdup(args[0].as.string);
    for (char *p = s; *p; p++) *p = toupper((unsigned char)*p);
    Value v = val_string(s); free(s); return v;
}

static Value native_lower(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_string("");
    char *s = strdup(args[0].as.string);
    for (char *p = s; *p; p++) *p = tolower((unsigned char)*p);
    Value v = val_string(s); free(s); return v;
}

static Value native_trim(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_string("");
    const char *s = args[0].as.string;
    while (*s==' ' || *s=='\t' || *s=='\n' || *s=='\r') s++;
    int len = strlen(s);
    while (len>0 && (s[len-1]==' ' || s[len-1]=='\t' || s[len-1]=='\n' || s[len-1]=='\r')) len--;
    char *buf = malloc(len + 1);
    memcpy(buf, s, len); buf[len] = '\0';
    Value v = val_string(buf); free(buf); return v;
}

static Value native_split_str(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) return val_array();
    Value a = val_array();
    const char *str = args[0].as.string;
    const char *delim = args[1].as.string;
    int dlen = strlen(delim);
    if (dlen == 0) { val_array_push(&a.as.array, val_string(str)); return a; }
    const char *p = str;
    const char *found;
    while ((found = strstr(p, delim)) != NULL) {
        int seglen = found - p;
        char *seg = malloc(seglen + 1);
        memcpy(seg, p, seglen); seg[seglen] = '\0';
        val_array_push(&a.as.array, val_string(seg));
        free(seg);
        p = found + dlen;
    }
    val_array_push(&a.as.array, val_string(p));
    return a;
}

static Value native_join(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_STRING) return val_string("");
    const char *delim = args[1].as.string;
    size_t total = 0;
    char tmp2[64];
    for (int i = 0; i < args[0].as.array.count; i++) {
        total += strlen(value_to_cstr(args[0].as.array.items[i], tmp2));
        if (i > 0) total += strlen(delim);
    }
    char *buf = malloc(total + 1); buf[0] = '\0';
    for (int i = 0; i < args[0].as.array.count; i++) {
        if (i > 0) strcat(buf, delim);
        strcat(buf, value_to_cstr(args[0].as.array.items[i], tmp2));
    }
    Value v = val_string(buf); free(buf); return v;
}

static Value native_contains_str(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) return val_bool(0);
    return val_bool(strstr(args[0].as.string, args[1].as.string) != NULL);
}

static Value native_replace_str(int argc, Value *args) {
    if (argc < 3 || args[0].type != VAL_STRING || args[1].type != VAL_STRING || args[2].type != VAL_STRING)
        return argc > 0 ? val_copy(args[0]) : val_string("");
    const char *str = args[0].as.string, *from = args[1].as.string, *to = args[2].as.string;
    int flen = strlen(from);
    size_t cap = strlen(str) + 1; char *buf = malloc(cap); buf[0]='\0';
    const char *p = str; const char *found;
    while ((found = strstr(p, from)) != NULL) {
        size_t cur = strlen(buf); size_t seg = found - p;
        buf = realloc(buf, cur + seg + strlen(to) + 1);
        memcpy(buf + cur, p, seg); buf[cur+seg]='\0';
        strcat(buf, to); p = found + flen;
    }
    size_t cur = strlen(buf); buf = realloc(buf, cur + strlen(p) + 1); strcat(buf, p);
    Value v = val_string(buf); free(buf); return v;
}

static Value native_find_str(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING) return val_number(-1);
    const char *pos = strstr(args[0].as.string, args[1].as.string);
    return pos ? val_number((double)(pos - args[0].as.string)) : val_number(-1);
}

static Value native_reverse_str(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_string("");
    int len = strlen(args[0].as.string);
    char *buf = malloc(len + 1);
    for (int i = 0; i < len; i++) buf[i] = args[0].as.string[len-1-i];
    buf[len] = '\0';
    Value v = val_string(buf); free(buf); return v;
}

static Value native_sort_array(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_ARRAY) return val_array();
    Value sorted = val_copy(args[0]);
    for (int i = 0; i < sorted.as.array.count - 1; i++)
        for (int j = 0; j < sorted.as.array.count - i - 1; j++) {
            if (sorted.as.array.items[j].type==VAL_NUMBER && sorted.as.array.items[j+1].type==VAL_NUMBER) {
                if (sorted.as.array.items[j].as.number > sorted.as.array.items[j+1].as.number) {
                    Value tmp = sorted.as.array.items[j];
                    sorted.as.array.items[j] = sorted.as.array.items[j+1];
                    sorted.as.array.items[j+1] = tmp;
                }
            }
        }
    return sorted;
}

static Value native_has(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_ARRAY) return val_bool(0);
    for (int i = 0; i < args[0].as.array.count; i++) {
        Value it = args[0].as.array.items[i];
        if (it.type==VAL_NUMBER && args[1].type==VAL_NUMBER && it.as.number==args[1].as.number) return val_bool(1);
        if (it.type==VAL_STRING && args[1].type==VAL_STRING && strcmp(it.as.string, args[1].as.string)==0) return val_bool(1);
    }
    return val_bool(0);
}

static Value native_read_file(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("read needs a filename");
    char *content = read_file_cstr(args[0].as.string);
    if (!content) return val_error("cannot read file");
    Value v = val_string(content); free(content); return v;
}

static Value native_write_file(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("write needs filename and content");
    FILE *f = fopen(args[0].as.string, "wb");
    if (!f) return val_error("cannot write file");
    fputs(args[1].as.string, f); fclose(f); return val_nil();
}

static Value native_append_file(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_STRING || args[1].type != VAL_STRING)
        return val_error("append needs filename and content");
    FILE *f = fopen(args[0].as.string, "ab");
    if (!f) return val_error("cannot append to file");
    fputs(args[1].as.string, f); fclose(f); return val_nil();
}

static Value native_file_exists(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_bool(0);
    return val_bool(file_exists(args[0].as.string));
}

static Value native_play(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("play needs a filename");
#ifdef _WIN32
    char cmd[1400];
    snprintf(cmd, sizeof(cmd),
        "powershell -NoProfile -Command \"(New-Object Media.SoundPlayer '%s').PlaySync()\"", args[0].as.string);
#else
    char cmd[1200];
    snprintf(cmd, sizeof(cmd),
        "paplay '%s' 2>/dev/null || aplay '%s' 2>/dev/null || afplay '%s' 2>/dev/null",
        args[0].as.string, args[0].as.string, args[0].as.string);
#endif
    if (system(cmd) == 0) return val_bool(1);
    return val_error("no audio player found (paplay/aplay/afplay)");
}

static Value native_run_cmd(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("run needs a command");
    char buf[4096];
    FILE *p = popen(args[0].as.string, "r");
    if (!p) return val_error("cannot run command");
    size_t n = fread(buf, 1, sizeof(buf)-1, p);
    buf[n] = '\0'; pclose(p);
    while (n>0 && (buf[n-1]=='\n' || buf[n-1]=='\r')) buf[--n]='\0';
    return val_string(buf);
}

#ifndef SB_NO_CURL
struct FetchCtx { char *data; size_t size; };
static size_t fetch_write(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct FetchCtx *ctx = userp;
    char *ptr = realloc(ctx->data, ctx->size + realsize + 1);
    if (!ptr) return 0;
    ctx->data = ptr;
    memcpy(ctx->data + ctx->size, contents, realsize);
    ctx->size += realsize;
    ctx->data[ctx->size] = '\0';
    return realsize;
}
static Value native_fetch(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_STRING) return val_error("fetch needs a url");
    if (!shim_load_curl()) return val_error("fetch: libcurl not available (install libcurl4)");
    CURL *curl = shim_curl_easy_init();
    if (!curl) return val_error("curl init failed");
    struct FetchCtx ctx = { malloc(1), 0 };
    if (!ctx.data) { shim_curl_easy_cleanup(curl); return val_error("out of memory"); }
    ctx.data[0] = '\0';
    shim_curl_easy_setopt(curl, CURLOPT_URL, args[0].as.string);
    shim_curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetch_write);
    shim_curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    shim_curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    shim_curl_easy_setopt(curl, CURLOPT_USERAGENT, "ShimbaBomb/0.1");
    shim_curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    CURLcode res = shim_curl_easy_perform(curl);
    shim_curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(ctx.data); return val_error(shim_curl_easy_strerror(res)); }
    Value v = val_string(ctx.data);
    free(ctx.data);
    return v;
}
#else
static Value native_fetch(int argc, Value *args) {
    (void)argc; (void)args;
    return val_error("fetch: networking not available in this build");
}
#endif

#ifndef SB_NO_GUI
static WebKitWebContext *shared_ctx = NULL;
static Value native_window(int argc, Value *args) {
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
    if (!shim_load_gtk() || !shim_gtk_init_check(NULL, NULL)) return val_error("cannot open display");
    if (!shim_load_webkit()) return val_error("webkit not available");
    const char *title = (argc > 0 && args[0].type == VAL_STRING) ? args[0].as.string : "ShimbaBomb";
    const char *text  = (argc > 1 && args[1].type == VAL_STRING) ? args[1].as.string : NULL;
    const char *url   = NULL;
    // use shim_g_str_has_prefix if available, else manual check
    #define SHIM_HAS_PREFIX(s,p) (shim_g_str_has_prefix ? shim_g_str_has_prefix((s),(p)) : (strncmp((s),(p),strlen(p))==0))
    if (argc == 1 && title && (SHIM_HAS_PREFIX(title, "http://") || SHIM_HAS_PREFIX(title, "https://")))
        url = title;
    else if (text && (SHIM_HAS_PREFIX(text, "http://") || SHIM_HAS_PREFIX(text, "https://")))
        url = text;
    else if (argc == 1 && text == NULL)
        url = title;
    #undef SHIM_HAS_PREFIX

    if (!shared_ctx) {
        shared_ctx = shim_webkit_web_context_new_ephemeral();
        shim_webkit_web_context_set_process_model(shared_ctx, WEBKIT_PROCESS_MODEL_SHARED_SECONDARY_PROCESS);
        shim_webkit_web_context_set_cache_model(shared_ctx, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    }

    GtkWidget *win = shim_gtk_window_new(GTK_WINDOW_TOPLEVEL);
    shim_gtk_window_set_title(GTK_WINDOW(win), title);
    shim_gtk_window_set_default_size(GTK_WINDOW(win), 1024, 700);
    shim_g_signal_connect_data(win, "destroy", G_CALLBACK(shim_gtk_main_quit), NULL, NULL, 0);
    GType wtype = shim_webkit_web_view_get_type ? shim_webkit_web_view_get_type() : 0;
    WebKitWebView *view = WEBKIT_WEB_VIEW(shim_g_object_new(wtype, "web-context", shared_ctx, NULL));
    if (url) {
        shim_webkit_web_view_load_uri(view, url);
    } else if (text && *text) {
        shim_webkit_web_view_load_html(view, text, NULL);
    } else {
        shim_webkit_web_view_load_html(view, "<html><body><h1>ShimbaBomb</h1></body></html>", NULL);
    }
    shim_gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(view));
    shim_gtk_widget_show_all(win);
    shim_gtk_main();
    return val_nil();
}
#else
static Value native_window(int argc, Value *args) {
    (void)argc; (void)args;
    return val_error("window: GUI not supported in this build");
}
#endif

#ifndef SB_NO_GUI
static GtkWidget *shimgui_win = NULL;
static GtkWidget *shimgui_box = NULL;

static void shimgui_btn_clicked(GtkWidget *w, gpointer data) {
    const char *label = (const char*)data;
    printf("[shimgui] '%s' clicked\n", label);
    fflush(stdout);
}

static Value native_shimgui_window(int argc, Value *args) {
    if (!shim_load_gtk() || !shim_gtk_init_check(NULL, NULL)) return val_error("cannot open display");
    const char *title = (argc > 0 && args[0].type == VAL_STRING) ? args[0].as.string : "ShimGUI";
    int width = (argc > 1 && args[1].type == VAL_NUMBER) ? (int)args[1].as.number : 420;
    int height = (argc > 2 && args[2].type == VAL_NUMBER) ? (int)args[2].as.number : 320;
    if (shimgui_win) { shim_gtk_widget_destroy(shimgui_win); shimgui_win = NULL; shimgui_box = NULL; }
    shimgui_win = shim_gtk_window_new(GTK_WINDOW_TOPLEVEL);
    shim_gtk_window_set_title(GTK_WINDOW(shimgui_win), title);
    shim_gtk_window_set_default_size(GTK_WINDOW(shimgui_win), width, height);
    shim_g_signal_connect_data(shimgui_win, "destroy", G_CALLBACK(shim_gtk_main_quit), NULL, NULL, 0);
    shimgui_box = shim_gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    shim_gtk_container_set_border_width(GTK_CONTAINER(shimgui_box), 16);
    shim_gtk_container_add(GTK_CONTAINER(shimgui_win), shimgui_box);
    shim_gtk_widget_show_all(shimgui_win);
    return val_number(1);
}

static Value native_shimgui_label(int argc, Value *args) {
    if (!shimgui_box) return val_error("no window — call shimgui_window first");
    const char *text = (argc > 0 && args[0].type == VAL_STRING) ? args[0].as.string : "";
    GtkWidget *label = shim_gtk_label_new(text);
    shim_gtk_widget_set_halign(label, GTK_ALIGN_START);
    shim_gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    shim_gtk_box_pack_start(GTK_BOX(shimgui_box), label, FALSE, FALSE, 0);
    shim_gtk_widget_show_all(shimgui_win);
    return val_nil();
}

static Value native_shimgui_button(int argc, Value *args) {
    if (!shimgui_box) return val_error("no window");
    const char *label = (argc > 0 && args[0].type == VAL_STRING) ? args[0].as.string : "Button";
    GtkWidget *btn = shim_gtk_button_new_with_label(label);
    char *copy = strdup(label);
    shim_g_signal_connect_data(btn, "clicked", G_CALLBACK(shimgui_btn_clicked), copy, NULL, 0);
    shim_gtk_box_pack_start(GTK_BOX(shimgui_box), btn, FALSE, FALSE, 0);
    gtk_widget_show_all(shimgui_win);
    return val_nil();
}

static Value native_shimgui_entry(int argc, Value *args) {
    if (!shimgui_box) return val_error("no window");
    const char *ph = (argc > 0 && args[0].type == VAL_STRING) ? args[0].as.string : "";
    GtkWidget *entry = shim_gtk_entry_new();
    shim_gtk_entry_set_placeholder_text(GTK_ENTRY(entry), ph);
    shim_gtk_box_pack_start(GTK_BOX(shimgui_box), entry, FALSE, FALSE, 0);
    shim_gtk_widget_show_all(shimgui_win);
    return val_nil();
}

static Value native_shimgui_run(int argc, Value *args) {
    (void)argc; (void)args;
    if (!shimgui_win) return val_error("no window");
    shim_gtk_main();
    shimgui_win = NULL; shimgui_box = NULL;
    return val_nil();
}
#else
static Value native_shimgui_window(int argc, Value *args) { (void)argc;(void)args; return val_error("shimgui: GUI not available in this build"); }
static Value native_shimgui_label(int argc, Value *args) { (void)argc;(void)args; return val_error("shimgui: GUI not available"); }
static Value native_shimgui_button(int argc, Value *args) { (void)argc;(void)args; return val_error("shimgui: GUI not available"); }
static Value native_shimgui_entry(int argc, Value *args) { (void)argc;(void)args; return val_error("shimgui: GUI not available"); }
static Value native_shimgui_run(int argc, Value *args) { (void)argc;(void)args; return val_error("shimgui: GUI not available"); }
#endif

// ── Ketiwe GUI: from-scratch X11 pixel toolkit (no GTK/WebKit) ───────
#ifndef SB_NO_GUI
static Value native_ketiwe_window(int argc, Value *args) {
    const char *title = (argc > 0 && args[0].type == VAL_STRING) ? args[0].as.string : "SB Window";
    int w = (argc > 1 && args[1].type == VAL_NUMBER) ? (int)args[1].as.number : 640;
    int h = (argc > 2 && args[2].type == VAL_NUMBER) ? (int)args[2].as.number : 480;
    return ketiwe_window(title, w, h) ? val_number(1) : val_error("ketiwe_window: cannot open display");
}

static Value native_ketiwe_rect(int argc, Value *args) {
    if (argc < 5) return val_error("ketiwe_rect needs 5 args: x, y, w, h, color");
    int x = (int)args[0].as.number;
    int y = (int)args[1].as.number;
    int w = (int)args[2].as.number;
    int h = (int)args[3].as.number;
    unsigned color = 0;
    if (args[4].type == VAL_NUMBER) {
        color = (unsigned)args[4].as.number;
    } else if (args[4].type == VAL_STRING) {
        const char *s = args[4].as.string;
        if (s[0] == '#') s++;
        color = (unsigned)strtol(s, NULL, 16);
    }
    ketiwe_rect(x, y, w, h, color);
    return val_nil();
}

static Value native_ketiwe_text(int argc, Value *args) {
    if (argc < 3) return val_error("ketiwe_text needs 3 args: x, y, text");
    int x = (int)args[0].as.number;
    int y = (int)args[1].as.number;
    const char *text = (args[2].type == VAL_STRING) ? args[2].as.string : "";
    ketiwe_text(x, y, text);
    return val_nil();
}

static Value native_ketiwe_button(int argc, Value *args) {
    if (argc < 5) return val_error("ketiwe_button needs 5 args: x, y, w, h, label");
    int x = (int)args[0].as.number;
    int y = (int)args[1].as.number;
    int w = (int)args[2].as.number;
    int h = (int)args[3].as.number;
    const char *label = (args[4].type == VAL_STRING) ? args[4].as.string : "";
    return val_number(ketiwe_button(x, y, w, h, label));
}

static Value native_ketiwe_poll(int argc, Value *args) {
    (void)argc; (void)args;
    return val_number(ketiwe_poll());
}

static Value native_ketiwe_flip(int argc, Value *args) {
    (void)argc; (void)args;
    ketiwe_flip();
    return val_nil();
}
static Value native_ketiwe_mouse_x(int argc, Value *args) { (void)argc;(void)args; return val_number(ketiwe_mouse_x()); }
static Value native_ketiwe_mouse_y(int argc, Value *args) { (void)argc;(void)args; return val_number(ketiwe_mouse_y()); }
static Value native_ketiwe_mouse_down(int argc, Value *args) { (void)argc;(void)args; return val_number(ketiwe_mouse_down()); }

static Value native_ketiwe_circle(int argc, Value *args) {
    if (argc < 4) return val_error("ketiwe_circle needs 4 args: cx, cy, r, color");
    int cx = (int)args[0].as.number;
    int cy = (int)args[1].as.number;
    int r = (int)args[2].as.number;
    unsigned color = 0;
    if (args[3].type == VAL_NUMBER) color = (unsigned)args[3].as.number;
    else if (args[3].type == VAL_STRING) {
        const char *s = args[3].as.string;
        if (s[0] == '#') s++;
        color = (unsigned)strtol(s, NULL, 16);
    }
    ketiwe_circle(cx, cy, r, color);
    return val_nil();
}

static Value native_ketiwe_input(int argc, Value *args) {
    if (argc < 1) return val_error("ketiwe_input needs at least 1 arg: placeholder");
    const char *ph = (args[0].type == VAL_STRING) ? args[0].as.string : "";
    int x = (argc > 1 && args[1].type == VAL_NUMBER) ? (int)args[1].as.number : 0;
    int y = (argc > 2 && args[2].type == VAL_NUMBER) ? (int)args[2].as.number : 0;
    int w = (argc > 3 && args[3].type == VAL_NUMBER) ? (int)args[3].as.number : 200;
    int h = (argc > 4 && args[4].type == VAL_NUMBER) ? (int)args[4].as.number : 30;
    static char input_static[8][256];
    static int input_idx = 0;
    int idx = input_idx % 8;
    input_idx++;
    memset(input_static[idx], 0, 256);
    ketiwe_input(x, y, w, h, input_static[idx], 256, ph);
    return val_string(input_static[idx]);
}

static Value native_ketiwe_key_press(int argc, Value *args) { (void)argc;(void)args; return val_number(ketiwe_key_press()); }

static Value native_ketiwe_input_text(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_NUMBER) return val_string("");
    return val_string(ketiwe_input_text((int)args[0].as.number));
}

#else
static Value native_ketiwe_window(int argc, Value *args) { (void)argc;(void)args; return val_error("ketiwe: GUI not available"); }
static Value native_ketiwe_rect(int argc, Value *args) { (void)argc;(void)args; return val_error("ketiwe: GUI not available"); }
static Value native_ketiwe_circle(int argc, Value *args) { (void)argc;(void)args; return val_error("ketiwe: GUI not available"); }
static Value native_ketiwe_text(int argc, Value *args) { (void)argc;(void)args; return val_error("ketiwe: GUI not available"); }
static Value native_ketiwe_button(int argc, Value *args) { (void)argc;(void)args; return val_error("ketiwe: GUI not available"); }
static Value native_ketiwe_input(int argc, Value *args) { (void)argc;(void)args; return val_error("ketiwe: GUI not available"); }
static Value native_ketiwe_poll(int argc, Value *args) { (void)argc;(void)args; return val_number(1); }
static Value native_ketiwe_flip(int argc, Value *args) { (void)argc;(void)args; return val_nil(); }
static Value native_ketiwe_mouse_x(int argc, Value *args) { (void)argc;(void)args; return val_number(-1); }
static Value native_ketiwe_mouse_y(int argc, Value *args) { (void)argc;(void)args; return val_number(-1); }
static Value native_ketiwe_mouse_down(int argc, Value *args) { (void)argc;(void)args; return val_number(0); }
static Value native_ketiwe_key_press(int argc, Value *args) { (void)argc;(void)args; return val_number(0); }
static Value native_ketiwe_input_text(int argc, Value *args) { (void)argc;(void)args; return val_string(""); }
#endif

static Value native_type_of(int argc, Value *args) {
    if (argc < 1) return val_string("nothing");
    return val_string(val_type_name(&args[0]));
}

static Value native_to_number(int argc, Value *args) {
    if (argc < 1) return val_number(0);
    if (args[0].type == VAL_NUMBER) return val_copy(args[0]);
    if (args[0].type == VAL_BOOL) return val_number(args[0].as.boolean ? 1 : 0);
    if (args[0].type != VAL_STRING) return val_number(0);
    char *end;
    double d = strtod(args[0].as.string, &end);
    if (end == args[0].as.string) return val_number(0);
    return val_number(d);
}

static Value native_to_string(int argc, Value *args) {
    if (argc < 1) return val_string("");
    char tmp[64]; return val_string(value_to_cstr(args[0], tmp));
}

static Value native_input(int argc, Value *args) {
    if (argc > 0) val_print(&args[0]);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return val_nil();
    buf[strcspn(buf, "\r\n")] = '\0';
    return val_string(buf);
}

static Value native_make_map(int argc, Value *args) {
    Value m = val_map();
    for (int i = 0; i + 1 < argc; i += 2) {
        if (args[i].type == VAL_STRING)
            val_map_set(&m, args[i].as.string, val_copy(args[i+1]));
    }
    return m;
}

static Value native_keys(int argc, Value *args) {
    if (argc < 1 || args[0].type != VAL_MAP) return val_array();
    Value a = val_array();
    for (int i = 0; i < args[0].as.map.count; i++)
        val_array_push(&a.as.array, val_string(args[0].as.map.entries[i].key));
    return a;
}

static Value native_has_key(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_MAP || args[1].type != VAL_STRING) return val_bool(0);
    return val_bool(val_map_has(&args[0], args[1].as.string));
}

static Value native_remove_key(int argc, Value *args) {
    if (argc < 2 || args[0].type != VAL_MAP || args[1].type != VAL_STRING) return val_nil();
    // return value without mutating original to avoid shared-heap double-free;
    // mutation via index assign is preferred: use separate native or set m["k"] to nothing
    return val_copy(val_map_get(&args[0], args[1].as.string));
}

// ── HTTP server (raw sockets) ────────────────────────────────────────
static Interpreter *g_interp_for_http = NULL;

static Value native_serve(int argc, Value *args) {
#ifdef _WIN32
    static int wsa_done = 0;
    if (!wsa_done) { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); wsa_done = 1; }
#define sb_close closesocket
#else
#define sb_close close
#endif
    if (argc < 2) return val_error("serve needs a port and a handler function");
    if (args[0].type != VAL_NUMBER) return val_error("port must be a number");
    if (args[1].type != VAL_FUNCTION && args[1].type != VAL_NATIVE)
        return val_error("handler must be a function");
    int port = (int)args[0].as.number;
    Value handler = val_copy(args[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return val_error("cannot create socket");
#ifdef _WIN32
    BOOL opt = TRUE;
#else
    int opt = 1;
#endif
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        sb_close(server_fd);
        return val_error("cannot bind port");
    }
    if (listen(server_fd, 16) < 0) { sb_close(server_fd); return val_error("listen failed"); }
    printf("serving on http://localhost:%d\n", port);

    while (1) {
        int client = accept(server_fd, NULL, NULL);
        if (client < 0) continue;
        char req[8192];
        ssize_t n = recv(client, req, sizeof(req)-1, 0);
        if (n <= 0) { sb_close(client); continue; }
        req[n] = '\0';

        // parse request line + body
        char method[16] = "GET", path[2048] = "/", body[4096] = "";
        sscanf(req, "%15s %2047s", method, path);
        char *body_start = strstr(req, "\r\n\r\n");
        if (body_start) snprintf(body, sizeof(body), "%s", body_start + 4);

        Value req_map = val_map();
        val_map_set(&req_map, "method", val_string(method));
        val_map_set(&req_map, "path", val_string(path));
        val_map_set(&req_map, "body", val_string(body));

        // call handler(req_map)
        Value resp_val;
        if (handler.type == VAL_NATIVE) {
            Value argv[1] = { val_copy(req_map) };
            resp_val = handler.as.native.fn(1, argv);
        } else {
            Environment *fn_env = env_create(handler.as.function.closure);
            if (handler.as.function.param_count > 0)
                env_set(fn_env, handler.as.function.params[0], val_copy(req_map));
            resp_val = interp_block(g_interp_for_http, fn_env, handler.as.function.body);
            env_free(fn_env);
            if (g_interp_for_http->returning) {
                g_interp_for_http->returning = 0;
                resp_val = val_copy(g_interp_for_http->return_value);
                val_free(&g_interp_for_http->return_value);
                g_interp_for_http->return_value = val_nil();
            }
        }

        char tmp[64];
        const char *resp = value_to_cstr(resp_val, tmp);
        char header[256];
        size_t body_len = strlen(resp);
        snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", body_len);
        send(client, header, strlen(header), 0);
        send(client, resp, body_len, 0);
        sb_close(client);
        val_free(&req_map);
        val_free(&resp_val);
    }
    sb_close(server_fd);
    return val_nil();
}
#undef sb_close

// ── JSON / CSV ───────────────────────────────────────────────────────

static void json_skip(const char *s, int *i) { while (s[*i]==' '||s[*i]=='\t'||s[*i]=='\n'||s[*i]=='\r') (*i)++; }
static Value json_parse_value(const char *s, int *i);
static Value json_parse_string(const char *s, int *i) {
    (*i)++; int start = *i;
    while (s[*i] && s[*i]!='"') { if (s[*i]=='\\' && s[*i+1]) (*i)+=2; else (*i)++; }
    int len = *i - start;
    char *buf = malloc(len+1); int b = 0;
    for (int j = start; j < *i; j++) {
        if (s[j]=='\\' && s[j+1]) {
            j++;
            switch (s[j]) {
                case 'n': buf[b++]='\n'; break;
                case 't': buf[b++]='\t'; break;
                case 'r': buf[b++]='\r'; break;
                case 'b': buf[b++]='\b'; break;
                case 'f': buf[b++]='\f'; break;
                case '/': buf[b++]='/'; break;
                case '"': buf[b++]='"'; break;
                case '\\': buf[b++]='\\'; break;
                case 'u': {
                    if (s[j+1]&&s[j+2]&&s[j+3]&&s[j+4]) {
                        char hex[5]={s[j+1],s[j+2],s[j+3],s[j+4],0};
                        unsigned cp=(unsigned)strtoul(hex,NULL,16); j+=4;
                        if (cp<0x80) buf[b++]=(char)cp;
                        else if (cp<0x800){buf[b++]=(char)(0xC0|(cp>>6));buf[b++]=(char)(0x80|(cp&0x3F));}
                        else {buf[b++]=(char)(0xE0|(cp>>12));buf[b++]=(char)(0x80|((cp>>6)&0x3F));buf[b++]=(char)(0x80|(cp&0x3F));}
                    }
                    break;
                }
                default: buf[b++]=s[j]; break;
            }
        } else buf[b++]=s[j];
    }
    buf[b]='\0';
    Value v = val_string(buf); free(buf);
    if (s[*i]=='"') (*i)++;
    return v;
}
static Value json_parse_array(const char *s, int *i) {
    (*i)++; Value a = val_array();
    json_skip(s,i);
    if (s[*i]==']') { (*i)++; return a; }
    while (1) {
        json_skip(s,i);
        Value v = json_parse_value(s,i);
        val_array_push(&a.as.array, v);
        json_skip(s,i);
        if (s[*i]==',') { (*i)++; continue; }
        if (s[*i]==']') { (*i)++; break; }
        break;
    }
    return a;
}
static Value json_parse_object(const char *s, int *i) {
    (*i)++; Value m = val_map();
    json_skip(s,i);
    if (s[*i]=='}') { (*i)++; return m; }
    while (1) {
        json_skip(s,i);
        if (s[*i]!='"') break;
        Value k = json_parse_string(s,i);
        json_skip(s,i);
        if (s[*i]==':') (*i)++;
        json_skip(s,i);
        Value v = json_parse_value(s,i);
        val_map_set(&m, k.as.string, v);
        val_free(&k);
        json_skip(s,i);
        if (s[*i]==',') { (*i)++; continue; }
        if (s[*i]=='}') { (*i)++; break; }
        break;
    }
    return m;
}
static Value json_parse_value(const char *s, int *i) {
    json_skip(s,i);
    if (s[*i]=='"') return json_parse_string(s,i);
    if (s[*i]=='{') return json_parse_object(s,i);
    if (s[*i]=='[') return json_parse_array(s,i);
    if (strncmp(s+*i,"true",4)==0) { *i+=4; return val_bool(1); }
    if (strncmp(s+*i,"false",5)==0) { *i+=5; return val_bool(0); }
    if (strncmp(s+*i,"null",4)==0) { *i+=4; return val_nil(); }
    char *end; double n = strtod(s+*i,&end);
    if (end!=s+*i) { *i = end - s; return val_number(n); }
    return val_nil();
}
static Value native_parse_json(int argc, Value *args) {
    if (argc<1 || args[0].type!=VAL_STRING) return val_error("parse_json needs a string");
    int pos=0; Value v = json_parse_value(args[0].as.string,&pos);
    return v;
}
static void json_append(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t sl = strlen(s);
    while (*len + sl + 1 > *cap) { *cap = *cap==0?256:*cap*2; *buf = realloc(*buf,*cap); }
    memcpy(*buf+*len,s,sl); *len+=sl; (*buf)[*len]='\0';
}
static void value_to_json_rec(Value *v, char **buf, size_t *len, size_t *cap) {
    char tmp[64];
    switch(v->type) {
        case VAL_NIL: json_append(buf,len,cap,"null"); break;
        case VAL_BOOL: json_append(buf,len,cap,v->as.boolean?"true":"false"); break;
        case VAL_NUMBER: snprintf(tmp,sizeof(tmp),"%g",v->as.number); json_append(buf,len,cap,tmp); break;
        case VAL_STRING: {
            json_append(buf,len,cap,"\"");
            for (char *p=v->as.string;*p;p++) {
                if (*p=='"'||*p=='\\') { char esc[3]={'\\',*p,'\0'}; json_append(buf,len,cap,esc); }
                else if (*p=='\n') json_append(buf,len,cap,"\\n");
                else { char ch[2]={*p,'\0'}; json_append(buf,len,cap,ch); }
            }
            json_append(buf,len,cap,"\""); break;
        }
        case VAL_ARRAY: {
            json_append(buf,len,cap,"[");
            for(int i=0;i<v->as.array.count;i++) {
                if(i) json_append(buf,len,cap,",");
                value_to_json_rec(&v->as.array.items[i],buf,len,cap);
            }
            json_append(buf,len,cap,"]"); break;
        }
        case VAL_MAP: {
            json_append(buf,len,cap,"{");
            for(int i=0;i<v->as.map.count;i++) {
                if(i) json_append(buf,len,cap,",");
                json_append(buf,len,cap,"\""); json_append(buf,len,cap,v->as.map.entries[i].key); json_append(buf,len,cap,"\":");
                value_to_json_rec(v->as.map.entries[i].value,buf,len,cap);
            }
            json_append(buf,len,cap,"}"); break;
        }
        default: json_append(buf,len,cap,"null"); break;
    }
}
static Value native_to_json(int argc, Value *args) {
    if (argc<1) return val_string("null");
    char *buf=NULL; size_t len=0, cap=0;
    value_to_json_rec(&args[0],&buf,&len,&cap);
    if(!buf) return val_string("null");
    Value v=val_string(buf); free(buf); return v;
}
static Value native_read_csv(int argc, Value *args) {
    if (argc<1 || args[0].type!=VAL_STRING) return val_error("read_csv needs a filename");
    char *content = read_file_cstr(args[0].as.string);
    if (!content) return val_error("cannot read csv");
    Value rows = val_array();
    char *line = strtok(content,"\n");
    while(line) {
        // skip empty
        if (*line=='\0' || *line=='\r') { line=strtok(NULL,"\n"); continue; }
        Value row = val_array();
        // simple split by comma, no quote handling for brevity
        char *p=line;
        while(p) {
            char *comma=strchr(p,',');
            if(comma) *comma='\0';
            // trim \r
            size_t l=strlen(p); while(l>0 && (p[l-1]=='\r'||p[l-1]==' ')) p[--l]='\0';
            while(*p==' ') p++;
            val_array_push(&row.as.array,val_string(p));
            if(comma) p=comma+1; else break;
        }
        val_array_push(&rows.as.array,row);
        line=strtok(NULL,"\n");
    }
    free(content);
    return rows;
}

// ── Class support ────────────────────────────────────────────────────

static Value interp_eval_class(Interpreter *interp, Environment *env, AstNode *node) {
    (void)interp;
    int fc = node->as.class_def.field_count;
    ClassDef *cd = malloc(sizeof(ClassDef));
    cd->fields = malloc(sizeof(char*) * (fc > 0 ? fc : 1));
    for (int i = 0; i < fc; i++) cd->fields[i] = strdup(node->as.class_def.fields[i]);
    cd->field_count = fc;
    cd->methods = NULL;
    cd->method_count = 0;

    for (int i = 0; i < node->as.class_def.methods.count; i++) {
        AstNode *m = node->as.class_def.methods.items[i];
        if (m->type != NODE_FUNCTION_DEF) continue;
        cd->methods = realloc(cd->methods, sizeof(Method) * (cd->method_count + 1));
        Method *met = &cd->methods[cd->method_count++];
        met->name = strdup(m->as.func_def.name);
        met->params = m->as.func_def.params;
        met->param_count = m->as.func_def.param_count;
        met->body = m->as.func_def.body;
        met->closure = env;
    }

    Value cls = val_class(cd);
    env_set(env, node->as.class_def.name, cls);
    return val_nil();
}

// ── Eval ─────────────────────────────────────────────────────────────

static Value interp_eval(Interpreter *interp, Environment *env, AstNode *node) {
    if (interp->had_error) return val_nil();

    switch (node->type) {
        case NODE_NUMBER_LIT: return val_number(node->as.number);
        case NODE_STRING_LIT: return val_string(node->as.string);
        case NODE_BOOL_LIT:   return val_bool(node->as.boolean);
        case NODE_IDENTIFIER: return env_get(env, node->as.identifier);

        case NODE_ASSIGN: {
            Value val = interp_eval(interp, env, node->as.assign.value);
            if (interp->had_error) return val_nil();
            if (node->as.assign.type_name) {
                const char *want = node->as.assign.type_name;
                const char *got = val_type_name(&val);
                int ok = strcmp(want, got)==0
                    || (strcmp(want,"text")==0 && strcmp(got,"text")==0)
                    || (strcmp(want,"number")==0 && strcmp(got,"number")==0)
                    || (strcmp(want,"truth")==0 && got[0]=='t')
                    || (strcmp(want,"list")==0 && strcmp(got,"list")==0)
                    || (strcmp(want,"map")==0 && strcmp(got,"map")==0);
                if (!ok) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "type check failed: '%s' is a %s, not a %s", node->as.assign.name, got, want);
                    interp_error(interp, node->line, msg);
                    return val_nil();
                }
            }
            env_set(env, node->as.assign.name, val_copy(val));
            return val;
        }

        case NODE_BINARY_OP: {
            Value left = interp_eval(interp, env, node->as.binary.left);
            if (interp->had_error) return val_nil();
            TokenType op = node->as.binary.op;
            if (op == TOKEN_AND) {
                if (!val_is_truthy(&left)) return val_bool(0);
                Value right = interp_eval(interp, env, node->as.binary.right);
                if (interp->had_error) return val_nil();
                return val_bool(val_is_truthy(&right));
            }
            if (op == TOKEN_OR) {
                if (val_is_truthy(&left)) return val_bool(1);
                Value right = interp_eval(interp, env, node->as.binary.right);
                if (interp->had_error) return val_nil();
                return val_bool(val_is_truthy(&right));
            }
            Value right = interp_eval(interp, env, node->as.binary.right);
            if (interp->had_error) return val_nil();

            if (op == TOKEN_PLUS && (left.type==VAL_STRING || right.type==VAL_STRING))
                return value_concat(left, right);

            if (op == TOKEN_EQUAL || op == TOKEN_NOTEQ) {
                int eq = 0;
                if (left.type==VAL_STRING && right.type==VAL_STRING)
                    eq = (strcmp(left.as.string, right.as.string)==0);
                else if (left.type==VAL_NUMBER && right.type==VAL_NUMBER)
                    eq = (left.as.number==right.as.number);
                else if (left.type==VAL_BOOL && right.type==VAL_BOOL)
                    eq = (left.as.boolean==right.as.boolean);
                else if (left.type==VAL_NIL && right.type==VAL_NIL) eq = 1;
                else if (left.type==VAL_ARRAY && right.type==VAL_ARRAY) {
                    if (left.as.array.count != right.as.array.count) eq = 0;
                    else {
                        eq = 1;
                        for (int i=0; i<left.as.array.count; i++) {
                            Value aa = left.as.array.items[i];
                            Value bb = right.as.array.items[i];
                            if (aa.type != bb.type) { eq=0; break; }
                            if (aa.type==VAL_NUMBER) {
                                if (aa.as.number != bb.as.number) { eq=0; break; }
                            } else if (aa.type==VAL_STRING) {
                                if (strcmp(aa.as.string,bb.as.string)!=0) { eq=0; break; }
                            } else if (aa.type==VAL_BOOL) {
                                if (aa.as.boolean != bb.as.boolean) { eq=0; break; }
                            } else if (aa.type==VAL_NIL) {
                                // equal, keep eq=1
                            } else { eq=0; break; }
                        }
                    }
                }
                else eq = 0;
                return val_bool(op == TOKEN_EQUAL ? eq : !eq);
            }
            if (op == TOKEN_EQUAL) {
                if (left.type==VAL_STRING && right.type==VAL_STRING)
                    return val_bool(strcmp(left.as.string, right.as.string)==0);
                if (left.type==VAL_NUMBER && right.type==VAL_NUMBER)
                    return val_bool(left.as.number==right.as.number);
                if (left.type==VAL_BOOL && right.type==VAL_BOOL)
                    return val_bool(left.as.boolean==right.as.boolean);
                if (left.type==VAL_NIL && right.type==VAL_NIL) return val_bool(1);
                return val_bool(0);
            }

            if (left.type!=VAL_NUMBER || right.type!=VAL_NUMBER) {
                char msg[192];
                snprintf(msg, sizeof(msg),
                    "cannot do math on non-numbers (%s %s %s)",
                    val_type_name(&left), op==TOKEN_PLUS?"plus":op==TOKEN_MINUS?"minus":op==TOKEN_STAR?"times":op==TOKEN_SLASH?"divided by":op==TOKEN_LESS?"is less than":op==TOKEN_GREATER?"is greater than":"?", val_type_name(&right));
                interp_error_code(interp, node->line, ERR_TYPE_MISMATCH, msg);
                return val_nil();
            }
            double a = left.as.number, b = right.as.number;
            switch (op) {
                case TOKEN_PLUS:  return val_number(a + b);
                case TOKEN_MINUS: return val_number(a - b);
                case TOKEN_STAR:  return val_number(a * b);
                case TOKEN_SLASH:
                    if (b==0) { interp_error_code(interp, node->line, ERR_DIV_ZERO, "cannot divide by nothing"); return val_nil(); }
                    return val_number(a / b);
                case TOKEN_MOD: {
                    if (b==0) { interp_error_code(interp, node->line, ERR_DIV_ZERO, "cannot mod by nothing"); return val_nil(); }
                    long long ai = (long long)a, bi = (long long)b;
                    long long r = ai % bi;
                    if (r != 0 && ((r < 0) != (bi < 0))) r += bi;
                    return val_number((double)r);
                }
                case TOKEN_INTDIV: {
                    if (b==0) { interp_error_code(interp, node->line, ERR_DIV_ZERO, "cannot divide by nothing"); return val_nil(); }
                    return val_number((double)((long long)a / (long long)b));
                }
                case TOKEN_LESS:    return val_bool(a < b);
                case TOKEN_GREATER: return val_bool(a > b);
                default:
                    interp_error(interp, node->line, "unknown operator");
                    return val_nil();
            }
        }

        case NODE_UNARY_OP: {
            Value operand = interp_eval(interp, env, node->as.unary.operand);
            if (interp->had_error) return val_nil();
            if (node->as.unary.op==TOKEN_NOT) return val_bool(!val_is_truthy(&operand));
            if (node->as.unary.op==TOKEN_MINUS) {
                if (operand.type!=VAL_NUMBER) { interp_error_code(interp, node->line, ERR_TYPE_MISMATCH, "cannot negate non-number"); return val_nil(); }
                return val_number(-operand.as.number);
            }
            return val_nil();
        }

        case NODE_SAY: {
            Value val = interp_eval(interp, env, node->as.say.value);
            if (interp->had_error) return val_nil();
            val_print(&val); printf("\n");
            return val_nil();
        }

        case NODE_RETURN: {
            Value val = interp_eval(interp, env, node->as.ret.value);
            if (interp->had_error) return val_nil();
            val_free(&interp->return_value);
            interp->return_value = val_copy(val);
            interp->returning = 1;
            return val_nil();
        }

        case NODE_IF: {
            Value cond = interp_eval(interp, env, node->as.if_stmt.condition);
            if (interp->had_error || interp->returning) return val_nil();
            if (val_is_truthy(&cond)) return interp_block(interp, env, node->as.if_stmt.then_block);
            else if (node->as.if_stmt.else_block) return interp_block(interp, env, node->as.if_stmt.else_block);
            return val_nil();
        }

        case NODE_COUNT: {
            Value from_val = interp_eval(interp, env, node->as.count.from);
            Value to_val   = interp_eval(interp, env, node->as.count.to);
            if (from_val.type!=VAL_NUMBER || to_val.type!=VAL_NUMBER) {
                interp_error(interp, node->line, "loop bounds must be numbers");
                return val_nil();
            }
            Value last = val_nil();
            for (double i = from_val.as.number; i <= to_val.as.number; i += 1) {
                env_set(env, node->as.count.var_name, val_number(i));
                last = interp_block(interp, env, node->as.count.body);
                if (interp->had_error || interp->returning) break;
                if (interp->breaking) { interp->breaking=0; break; }
                if (interp->continuing) { interp->continuing=0; continue; }
            }
            return last;
        }

        case NODE_LOOP_THROUGH: {
            Value iterable = interp_eval(interp, env, node->as.loop_through.iterable);
            if (iterable.type != VAL_ARRAY) {
                interp_error(interp, node->line, "can only loop through lists");
                return val_nil();
            }
            Value last = val_nil();
            for (int i = 0; i < iterable.as.array.count; i++) {
                env_set(env, node->as.loop_through.var_name, val_copy(iterable.as.array.items[i]));
                last = interp_block(interp, env, node->as.loop_through.body);
                if (interp->had_error || interp->returning) break;
                if (interp->breaking) { interp->breaking=0; break; }
                if (interp->continuing) { interp->continuing=0; continue; }
            }
            return last;
        }

        case NODE_WHILE: {
            Value last = val_nil();
            while (1) {
                Value cond = interp_eval(interp, env, node->as.while_stmt.condition);
                if (interp->had_error || interp->returning) break;
                if (!val_is_truthy(&cond)) break;
                last = interp_block(interp, env, node->as.while_stmt.body);
                if (interp->had_error || interp->returning) break;
                if (interp->breaking) { interp->breaking=0; break; }
                if (interp->continuing) { interp->continuing=0; continue; }
            }
            return last;
        }

        case NODE_BREAK: {
            interp->breaking = 1;
            return val_nil();
        }

        case NODE_CONTINUE: {
            interp->continuing = 1;
            return val_nil();
        }

        case NODE_ASSERT: {
            Value v = interp_eval(interp, env, node->as.assert_stmt.value);
            if (interp->had_error) return val_nil();
            int ok;
            char detail[256] = "";
            if (node->as.assert_stmt.expected) {
                Value e = interp_eval(interp, env, node->as.assert_stmt.expected);
                if (interp->had_error) { val_free(&v); return val_nil(); }
                if (v.type==VAL_NUMBER && e.type==VAL_NUMBER) ok = (v.as.number==e.as.number);
                else if (v.type==VAL_STRING && e.type==VAL_STRING) ok = (strcmp(v.as.string,e.as.string)==0);
                else if (v.type==VAL_BOOL && e.type==VAL_BOOL) ok = (v.as.boolean==e.as.boolean);
                else ok = 0;
                if (!ok) {
                    char tv[64], te[64];
                    snprintf(detail, sizeof(detail), "expected %s, got %s",
                        value_to_cstr(e, te), value_to_cstr(v, tv));
                }
                val_free(&e);
            } else {
                ok = val_is_truthy(&v);
                if (!ok) snprintf(detail, sizeof(detail), "value is not truthy");
            }
            val_free(&v);
            if (ok) {
                interp->asserts_passed++;
                printf("ok\n");
            } else {
                interp->asserts_failed++;
                char msg[512];
                snprintf(msg, sizeof(msg), "assert failed on line %d: %s", node->line, detail);
                interp_error(interp, node->line, msg);
                fprintf(stderr, "FAIL: %s\n", msg);
                interp->had_error = 0; // don't halt the test file
            }
            return val_nil();
        }

        case NODE_MATCH: {
            // NOTE: v/pv may alias env-owned values (env_get returns by value,
            // no copy) — never val_free them here.
            Value v = interp_eval(interp, env, node->as.match.value);
            if (interp->had_error || interp->returning) return val_nil();
            for (int i=0;i<node->as.match.case_count;i++) {
                AstNode *pat = node->as.match.cases[i].pattern;
                int is_wild = (pat->type==NODE_IDENTIFIER && strcmp(pat->as.identifier,"_")==0);
                int matched = 0;
                if (is_wild) matched = 1;
                else {
                    Value pv = interp_eval(interp, env, pat);
                    if (interp->had_error) return val_nil();
                    if (v.type==VAL_NUMBER && pv.type==VAL_NUMBER) matched = (v.as.number==pv.as.number);
                    else if (v.type==VAL_STRING && pv.type==VAL_STRING) matched = (strcmp(v.as.string,pv.as.string)==0);
                    else if (v.type==VAL_BOOL && pv.type==VAL_BOOL) matched = (v.as.boolean==pv.as.boolean);
                    else if (v.type==VAL_NIL && pv.type==VAL_NIL) matched = 1;
                    else matched = 0;
                }
                if (matched) {
                    Value res = interp_block(interp, env, node->as.match.cases[i].body);
                    return res;
                }
            }
            return val_nil();
        }

        case NODE_LAMBDA:
            return val_function(
                node->as.func_def.params,
                node->as.func_def.param_count,
                node->as.func_def.body, env);

        case NODE_FUNCTION_DEF: {
            Value fn = val_function(
                node->as.func_def.params,
                node->as.func_def.param_count,
                node->as.func_def.body, env);
            env_set(env, node->as.func_def.name, fn);
            return val_nil();
        }

        case NODE_FUNCTION_CALL: {
            // method call via receiver
            if (node->as.func_call.receiver) {
                Value obj = interp_eval(interp, env, node->as.func_call.receiver);
                if (interp->had_error) return val_nil();

                if (obj.type == VAL_MAP) {
                    // map.method(args) — lookup function in map, call it
                    const char *method = node->as.func_call.name;
                    Value fn = val_nil();
                    for (int i = 0; i < obj.as.map.count; i++)
                        if (strcmp(obj.as.map.entries[i].key, method) == 0)
                            fn = *obj.as.map.entries[i].value;
                    if (fn.type != VAL_FUNCTION && fn.type != VAL_NATIVE) {
                        char msg[256]; snprintf(msg, sizeof(msg), "'%s' has no method '%s'", method, method);
                        interp_error(interp, node->line, msg); return val_nil();
                    }
                    int argc = node->as.func_call.args.count;
                    Value *args = malloc(sizeof(Value) * (argc > 0 ? argc : 1));
                    for (int i = 0; i < argc; i++) {
                        args[i] = interp_eval(interp, env, node->as.func_call.args.items[i]);
                        if (interp->had_error) { free(args); return val_nil(); }
                    }
                    Value result;
                    if (fn.type == VAL_NATIVE) {
                        result = fn.as.native.fn(argc, args);
                    } else {
                        if (interp->recursion_depth >= SB_MAX_RECURSION) {
                            char msg[256]; snprintf(msg, sizeof(msg), "too much recursion in '%s'", method);
                            interp_error(interp, node->line, msg);
                            free(args); return val_nil();
                        }
                        Environment *fn_env = env_create(fn.as.function.closure);
                        interp->recursion_depth++;
                        for (int i = 0; i < fn.as.function.param_count && i < argc; i++)
                            env_set(fn_env, fn.as.function.params[i], val_copy(args[i]));
                        result = interp_block(interp, fn_env, fn.as.function.body);
                        interp->recursion_depth--;
                        env_free(fn_env);
                        if (interp->returning) {
                            interp->returning = 0;
                            result = val_copy(interp->return_value);
                            val_free(&interp->return_value);
                            interp->return_value = val_nil();
                        }
                    }
                    free(args);
                    return result;

                } else if (obj.type == VAL_INSTANCE) {
                    Method *met = NULL;
                    for (int i = 0; i < obj.as.instance.class_def->method_count; i++)
                        if (strcmp(obj.as.instance.class_def->methods[i].name, node->as.func_call.name)==0)
                            { met = &obj.as.instance.class_def->methods[i]; break; }
                    if (!met) {
                        char msg[256]; snprintf(msg, sizeof(msg), "object has no method '%s'", node->as.func_call.name);
                        interp_error(interp, node->line, msg); return val_nil();
                    }
                    int argc = node->as.func_call.args.count;
                    Value *args = malloc(sizeof(Value) * (argc > 0 ? argc : 1));
                    for (int i = 0; i < argc; i++) {
                        args[i] = interp_eval(interp, env, node->as.func_call.args.items[i]);
                        if (interp->had_error) { free(args); return val_nil(); }
                    }
                    if (interp->recursion_depth >= SB_MAX_RECURSION) {
                        char msg[256]; snprintf(msg, sizeof(msg), "too much recursion in '%s'", node->as.func_call.name);
                        interp_error(interp, node->line, msg);
                        free(args); return val_nil();
                    }
                    Environment *fn_env = env_create(met->closure);
                    interp->recursion_depth++;
                    int _depth_m = 0;
                    if (interp->call_depth < SB_MAX_CALLS) {
                        snprintf(interp->call_stack[interp->call_depth], 64, "%s", node->as.func_call.name);
                        interp->call_lines[interp->call_depth] = node->line;
                        interp->call_depth++;
                        _depth_m = 1;
                    }
                    env_set(fn_env, "self", val_copy(obj));
                    for (int i=0; i<met->param_count && i<argc; i++)
                        env_set(fn_env, met->params[i], val_copy(args[i]));
                    Value result = interp_block(interp, fn_env, met->body);
                    interp->recursion_depth--;
                    if (_depth_m) interp->call_depth--;
                    env_free(fn_env);
                    free(args);
                    if (interp->returning) {
                        interp->returning = 0;
                        result = val_copy(interp->return_value);
                        val_free(&interp->return_value);
                        interp->return_value = val_nil();
                    } else {
                        result = val_nil();
                    }
                    return result;
                } else {
                    interp_error(interp, node->line, "can only call methods on objects or maps");
                    return val_nil();
                }
            }

            Value callee = env_get(env, node->as.func_call.name);
            if (callee.type!=VAL_FUNCTION && callee.type!=VAL_NATIVE) {
                char msg[256]; snprintf(msg,sizeof(msg),"cannot call '%s', it is not a function", node->as.func_call.name);
                interp_error(interp, node->line, msg); return val_nil();
            }
            int argc = node->as.func_call.args.count;
            Value *args = malloc(sizeof(Value) * (argc>0 ? argc : 1));
            for (int i=0;i<argc;i++) {
                args[i]=interp_eval(interp, env, node->as.func_call.args.items[i]);
                if (interp->had_error){ free(args); return val_nil(); }
            }
            // special mutating handling for list/map natives called with var name
            if (callee.type==VAL_NATIVE && argc>=1 &&
                node->as.func_call.args.items[0]->type == NODE_IDENTIFIER) {
                Value *var_ptr = env_get_ptr(env, node->as.func_call.args.items[0]->as.identifier);
                if (var_ptr && var_ptr->type == VAL_ARRAY &&
                    strcmp(callee.as.native.name, "add_to")==0 && argc==2) {
                    val_array_push(&var_ptr->as.array, val_copy(args[1]));
                    free(args);
                    return val_copy(*var_ptr);
                }
                if (var_ptr && var_ptr->type == VAL_ARRAY &&
                    strcmp(callee.as.native.name, "remove_from")==0 && argc==2 && args[1].type==VAL_NUMBER) {
                    Value removed = val_array_remove(&var_ptr->as.array, (int)args[1].as.number);
                    free(args);
                    return removed;
                }
            }
            // special mutating handling for remove_key with var name
            if (callee.type==VAL_NATIVE && strcmp(callee.as.native.name, "remove_key")==0 && argc==2) {
                AstNode *first_ast = node->as.func_call.args.items[0];
                if (first_ast->type == NODE_IDENTIFIER) {
                    Value *map_ptr = env_get_ptr(env, first_ast->as.identifier);
                    if (map_ptr && map_ptr->type==VAL_MAP && args[1].type==VAL_STRING) {
                        Value removed = val_map_remove(map_ptr, args[1].as.string);
                        free(args);
                        return removed;
                    }
                }
            }
            Value result;
            if (callee.type==VAL_NATIVE) {
                result = callee.as.native.fn(argc, args);
            } else {
                if (interp->recursion_depth >= SB_MAX_RECURSION) {
                    char msg[256]; snprintf(msg, sizeof(msg), "too much recursion in '%s'", node->as.func_call.name);
                    interp_error(interp, node->line, msg);
                    free(args); return val_nil();
                }
                Environment *fn_env = env_create(callee.as.function.closure);
                interp->recursion_depth++;
                int _depth_f = 0;
                if (interp->call_depth < SB_MAX_CALLS) {
                    snprintf(interp->call_stack[interp->call_depth], 64, "%s", node->as.func_call.name);
                    interp->call_lines[interp->call_depth] = node->line;
                    interp->call_depth++;
                    _depth_f = 1;
                }
                for (int i=0;i<callee.as.function.param_count && i<argc; i++)
                    env_set(fn_env, callee.as.function.params[i], val_copy(args[i]));
                result = interp_block(interp, fn_env, callee.as.function.body);
                interp->recursion_depth--;
                if (_depth_f) interp->call_depth--;
                env_free(fn_env);
                if (interp->returning) {
                    interp->returning=0;
                    result = val_copy(interp->return_value);
                    val_free(&interp->return_value);
                    interp->return_value = val_nil();
                } else {
                    result = val_nil();
                }
            }
            free(args);
            return result;
        }

        case NODE_POSSESSIVE: {
            Value obj = interp_eval(interp, env, node->as.possessive.object);
            if (obj.type == VAL_MAP)
                return val_copy(val_map_get(&obj, node->as.possessive.field));
            if (obj.type != VAL_INSTANCE) {
                interp_error(interp, node->line, "cannot access property of non-object");
                return val_nil();
            }
            for (int i=0;i<obj.as.instance.field_count;i++)
                if (strcmp(obj.as.instance.class_def->fields[i], node->as.possessive.field)==0)
                    return val_copy(obj.as.instance.fields[i]);
            // zero-arg method auto-call
            for (int i=0;i<obj.as.instance.class_def->method_count;i++)
                if (strcmp(obj.as.instance.class_def->methods[i].name, node->as.possessive.field)==0) {
                    Method *met = &obj.as.instance.class_def->methods[i];
                    if (interp->recursion_depth >= SB_MAX_RECURSION) {
                        interp_error(interp, node->line, "too much recursion");
                        return val_nil();
                    }
                    Environment *fn_env = env_create(met->closure);
                    interp->recursion_depth++;
                    env_set(fn_env, "self", val_copy(obj));
                    Value result = interp_block(interp, fn_env, met->body);
                    interp->recursion_depth--;
                    env_free(fn_env);
                    if (interp->returning) {
                        interp->returning=0;
                        result = val_copy(interp->return_value);
                        val_free(&interp->return_value);
                        interp->return_value = val_nil();
                    } else {
                        result = val_nil();
                    }
                    return result;
                }
            char msg[256]; snprintf(msg,sizeof(msg),"object has no property '%s'", node->as.possessive.field);
            interp_error(interp, node->line, msg);
            return val_nil();
        }

        case NODE_INDEX: {
            Value obj = interp_eval(interp, env, node->as.index.object);
            Value idx = interp_eval(interp, env, node->as.index.index);
            if (obj.type==VAL_ARRAY && idx.type==VAL_NUMBER)
                return val_copy(val_array_get(&obj.as.array, (int)idx.as.number));
            if (obj.type==VAL_STRING && idx.type==VAL_NUMBER) {
                int i = (int)idx.as.number;
                if (i<0 || i>=(int)strlen(obj.as.string)) return val_string("");
                char buf[2]={obj.as.string[i], '\0'};
                return val_string(buf);
            }
            if (obj.type==VAL_MAP && idx.type==VAL_STRING)
                return val_copy(val_map_get(&obj, idx.as.string));
            interp_error(interp, node->line, "cannot index this value");
            return val_nil();
        }

        case NODE_TRY: {
            interp->had_error = 0;
            Value result = interp_block(interp, env, node->as.try_stmt.body);
            if (interp->had_error) {
                // for backward compat: err var stays a string (old tests do `caught is "..."`)
                char *errmsg = strdup(interp->error_msg);
                int errline = interp->error_line;
                ErrorCode errcode = interp->error_code;
                char errtrace[1024];
                snprintf(errtrace, sizeof(errtrace), "%s", interp->error_trace);
                interp->had_error = 0;
                // primary: string (compat)
                env_set(env, node->as.try_stmt.error_var, val_string(errmsg));
                // structured: <var>_info map with code/message/line/trace for new code
                char info_key[256];
                snprintf(info_key, sizeof(info_key), "%s_info", node->as.try_stmt.error_var);
                Value errmap = val_map();
                val_map_set(&errmap, "code", val_number((double)errcode));
                val_map_set(&errmap, "message", val_string(errmsg));
                val_map_set(&errmap, "line", val_number((double)errline));
                val_map_set(&errmap, "trace", val_string(errtrace));
                env_set(env, info_key, errmap);
                free(errmsg);
                result = interp_block(interp, env, node->as.try_stmt.catch_body);
            }
            return result;
        }

        case NODE_RAISE: {
            Value msg = interp_eval(interp, env, node->as.raise.message);
            interp_error(interp, node->line, msg.type==VAL_STRING ? msg.as.string : "something went wrong");
            return val_nil();
        }

        case NODE_INDEX_ASSIGN: {
            Value val = interp_eval(interp, env, node->as.index_assign.value);
            Value idx = interp_eval(interp, env, node->as.index_assign.index);
            // write through to the real binding (nearest scope), like add_to
            Value *slot = env_get_ptr(env, node->as.index_assign.name);
            if (!slot) {
                Value fresh = val_map();
                if (idx.type == VAL_STRING) val_map_set(&fresh, idx.as.string, val_copy(val));
                env_set(env, node->as.index_assign.name, fresh);
                return val_nil();
            }
            if (slot->type == VAL_MAP && idx.type == VAL_STRING) {
                val_map_set(slot, idx.as.string, val_copy(val));
            } else if (slot->type == VAL_ARRAY && idx.type == VAL_NUMBER) {
                int i = (int)idx.as.number;
                if (i >= 0 && i < slot->as.array.count) {
                    val_free(&slot->as.array.items[i]);
                    slot->as.array.items[i] = val_copy(val);
                }
            } else {
                interp_error(interp, node->line, "cannot assign to this index");
            }
            return val_nil();
        }

        case NODE_START_TASK: {
            AstNode *call_node = node->as.start_task.call;
            if (call_node->type != NODE_FUNCTION_CALL) {
                interp_error(interp, node->line, "can only start tasks from function calls");
                return val_nil();
            }
            Value fn = env_get(env, call_node->as.func_call.name);
            if (fn.type != VAL_FUNCTION && fn.type != VAL_NATIVE) {
                char msg[256]; snprintf(msg, sizeof(msg), "'%s' is not a function", call_node->as.func_call.name);
                interp_error(interp, node->line, msg);
                return val_nil();
            }
#ifdef _WIN32
            /* no fork on Windows: run the task synchronously */
            {
                int argc2 = call_node->as.func_call.args.count;
                Value fnv = fn;
                if (fnv.type == VAL_NATIVE) {
                    Value *args = malloc(sizeof(Value)*(argc2>0?argc2:1));
                    for (int i=0;i<argc2;i++)
                        args[i] = interp_eval(interp, env, call_node->as.func_call.args.items[i]);
                    fnv.as.native.fn(argc2, args);
                    free(args);
                } else {
                    Environment *fe = env_create(fnv.as.function.closure);
                    for (int i=0;i<fnv.as.function.param_count && i<argc2;i++) {
                        Value av = interp_eval(interp, env, call_node->as.func_call.args.items[i]);
                        env_set(fe, fnv.as.function.params[i], val_copy(av));
                    }
                    interp_block(interp, fe, fnv.as.function.body);
                    env_free(fe);
                }
                if (interp->task_count < SB_MAX_TASKS) {
                    interp->task_names[interp->task_count] = strdup(node->as.start_task.name);
                    interp->task_pids[interp->task_count] = 0;
                    interp->task_count++;
                }
            }
            return val_nil();
#else
            pid_t pid = fork();
            if (pid == 0) {
                int argc = call_node->as.func_call.args.count;
                if (fn.type == VAL_NATIVE) {
                    Value *args = malloc(sizeof(Value) * (argc > 0 ? argc : 1));
                    for (int i = 0; i < argc; i++)
                        args[i] = interp_eval(interp, env, call_node->as.func_call.args.items[i]);
                    fn.as.native.fn(argc, args);
                    free(args);
                } else {
                    Environment *fn_env = env_create(fn.as.function.closure);
                    for (int i = 0; i < fn.as.function.param_count && i < argc; i++) {
                        Value arg = interp_eval(interp, env, call_node->as.func_call.args.items[i]);
                        env_set(fn_env, fn.as.function.params[i], val_copy(arg));
                    }
                    interp_block(interp, fn_env, fn.as.function.body);
                    env_free(fn_env);
                }
                _exit(0);
            } else if (pid > 0) {
                if (interp->task_count < SB_MAX_TASKS) {
                    interp->task_names[interp->task_count] = strdup(node->as.start_task.name);
                    interp->task_pids[interp->task_count] = pid;
                    interp->task_count++;
                }
            }
#endif /* _WIN32 */
            return val_nil();
        }

        case NODE_WAIT_TASK: {
            const char *name = node->as.wait_task.name;
            for (int i = 0; i < interp->task_count; i++) {
                if (strcmp(interp->task_names[i], name) == 0) {
#ifdef _WIN32
                    /* tasks ran synchronously; result is the completion marker */
                    free(interp->task_names[i]);
                    for (int j = i; j < interp->task_count - 1; j++) {
                        interp->task_names[j] = interp->task_names[j+1];
                        interp->task_pids[j] = interp->task_pids[j+1];
                    }
                    interp->task_count--;
                    return val_number(0);
#else
                    int status;
                    waitpid(interp->task_pids[i], &status, 0);
                    free(interp->task_names[i]);
                    for (int j = i; j < interp->task_count - 1; j++) {
                        interp->task_names[j] = interp->task_names[j+1];
                        interp->task_pids[j] = interp->task_pids[j+1];
                    }
                    interp->task_count--;
                    return val_number(WEXITSTATUS(status));
#endif /* _WIN32 */
                }
            }
            interp_error(interp, node->line, "unknown task");
            return val_nil();
        }

        case NODE_CLASS_DEF:
            return interp_eval_class(interp, env, node);

        case NODE_NEW_INSTANCE: {
            Value cls = env_get(env, node->as.new_instance.class_name);
            if (cls.type != VAL_CLASS) {
                char msg[256]; snprintf(msg,sizeof(msg),"'%s' is not a class", node->as.new_instance.class_name);
                interp_error(interp, node->line, msg); return val_nil();
            }
            Value inst = val_instance(cls.as.class_def);
            int argc = node->as.new_instance.args.count;
            for (int i=0; i<argc && i<inst.as.instance.field_count; i++) {
                Value arg = interp_eval(interp, env, node->as.new_instance.args.items[i]);
                val_free(&inst.as.instance.fields[i]);
                inst.as.instance.fields[i] = val_copy(arg);
            }
            return inst;
        }

        case NODE_IMPORT:
            run_import(interp, node);
            return val_nil();

        case NODE_BLOCK:
            return interp_block(interp, env, node);

        case NODE_PROGRAM: {
            Value result = val_nil();
            for (int i=0; i<node->as.program.count; i++) {
                if (interp->debug) {
                    printf("\n[line %d] (dbg) ", node->as.program.items[i]->line);
                    fflush(stdout);
                    char cmd[256];
                    int stepping = 1;
                    while (stepping && fgets(cmd, sizeof(cmd), stdin)) {
                        cmd[strcspn(cmd, "\r\n")] = '\0';
                        if (cmd[0]=='\0' || strcmp(cmd,"n")==0 || strcmp(cmd,"next")==0) stepping = 0;
                        else if (strcmp(cmd,"c")==0 || strcmp(cmd,"continue")==0) { interp->debug = 0; stepping = 0; }
                        else if (strncmp(cmd,"p ",2)==0) {
                            Value v = env_get(interp->global, cmd+2);
                            val_print(&v); printf("\n");
                            val_free(&v);
                        } else if (strcmp(cmd,"where")==0) {
                            printf("line %d, statement %d/%d\n", node->as.program.items[i]->line, i+1, node->as.program.count);
                        } else if (strcmp(cmd,"q")==0 || strcmp(cmd,"quit")==0) {
                            exit(0);
                        } else if (strcmp(cmd,"help")==0) {
                            printf("n/Enter=next  c=continue  p <var>=print  where=q\n");
                        } else {
                            printf("(dbg) unknown command, try help\n");
                        }
                        if (stepping) { printf("(dbg) "); fflush(stdout); }
                    }
                }
                result = interp_eval(interp, env, node->as.program.items[i]);
                if (interp->had_error || interp->returning) break;
            }
            return result;
        }
    }
    return val_nil();
}

void interp_init(Interpreter *interp) {
    memset(interp, 0, sizeof(*interp));
    interp->global = env_create(NULL);
    interp->return_value = val_nil();
    g_interp_for_http = interp;

    env_set(interp->global, "type_of",    val_native(native_type_of,    "type_of"));
    env_set(interp->global, "to_number",  val_native(native_to_number,  "to_number"));
    env_set(interp->global, "to_string",  val_native(native_to_string,  "to_string"));
    env_set(interp->global, "input",      val_native(native_input,      "input"));
    env_set(interp->global, "list",       val_native(native_make_list,  "list"));
    env_set(interp->global, "length",     val_native(native_length,     "length"));
    env_set(interp->global, "item",       val_native(native_item,       "item"));
    env_set(interp->global, "add_to",     val_native(native_add_to,     "add_to"));
    env_set(interp->global, "remove_from",val_native(native_remove_from,"remove_from"));
    env_set(interp->global, "upper",      val_native(native_upper,      "upper"));
    env_set(interp->global, "lower",      val_native(native_lower,      "lower"));
    env_set(interp->global, "trim",       val_native(native_trim,       "trim"));
    env_set(interp->global, "split",      val_native(native_split_str,  "split"));
    env_set(interp->global, "join",       val_native(native_join,       "join"));
    env_set(interp->global, "contains",   val_native(native_contains_str,"contains"));
    env_set(interp->global, "replace",    val_native(native_replace_str,"replace"));
    env_set(interp->global, "find",       val_native(native_find_str,   "find"));
    env_set(interp->global, "reverse",    val_native(native_reverse_str,"reverse"));
    env_set(interp->global, "sort",       val_native(native_sort_array, "sort"));
    env_set(interp->global, "has",        val_native(native_has,        "has"));
    env_set(interp->global, "fetch",      val_native(native_fetch,      "fetch"));
    env_set(interp->global, "window",     val_native(native_window,     "window"));
    env_set(interp->global, "shimgui_window", val_native(native_shimgui_window, "shimgui_window"));
    env_set(interp->global, "shimgui_label",  val_native(native_shimgui_label,  "shimgui_label"));
    env_set(interp->global, "shimgui_button", val_native(native_shimgui_button, "shimgui_button"));
    env_set(interp->global, "shimgui_entry",  val_native(native_shimgui_entry,  "shimgui_entry"));
    env_set(interp->global, "shimgui_run",    val_native(native_shimgui_run,    "shimgui_run"));
    env_set(interp->global, "ketiwe_window",     val_native(native_ketiwe_window,     "ketiwe_window"));
    env_set(interp->global, "ketiwe_rect",       val_native(native_ketiwe_rect,       "ketiwe_rect"));
    env_set(interp->global, "ketiwe_text",       val_native(native_ketiwe_text,       "ketiwe_text"));
    env_set(interp->global, "ketiwe_button",     val_native(native_ketiwe_button,     "ketiwe_button"));
    env_set(interp->global, "ketiwe_poll",       val_native(native_ketiwe_poll,       "ketiwe_poll"));
    env_set(interp->global, "ketiwe_flip",       val_native(native_ketiwe_flip,       "ketiwe_flip"));
    env_set(interp->global, "ketiwe_mouse_x",    val_native(native_ketiwe_mouse_x,    "ketiwe_mouse_x"));
    env_set(interp->global, "ketiwe_mouse_y",    val_native(native_ketiwe_mouse_y,    "ketiwe_mouse_y"));
    env_set(interp->global, "ketiwe_mouse_down", val_native(native_ketiwe_mouse_down, "ketiwe_mouse_down"));
    env_set(interp->global, "ketiwe_circle",     val_native(native_ketiwe_circle,     "ketiwe_circle"));
    env_set(interp->global, "ketiwe_input",      val_native(native_ketiwe_input,      "ketiwe_input"));
    env_set(interp->global, "ketiwe_key_press",  val_native(native_ketiwe_key_press,  "ketiwe_key_press"));
    env_set(interp->global, "ketiwe_input_text", val_native(native_ketiwe_input_text, "ketiwe_input_text"));
    env_set(interp->global, "read_file",  val_native(native_read_file,  "read_file"));
    env_set(interp->global, "write_file", val_native(native_write_file, "write_file"));
    env_set(interp->global, "append_file",val_native(native_append_file,"append_file"));
    env_set(interp->global, "file_exists",val_native(native_file_exists,"file_exists"));
    env_set(interp->global, "play",       val_native(native_play,       "play"));
    env_set(interp->global, "run",        val_native(native_run_cmd,    "run"));
    env_set(interp->global, "map",        val_native(native_make_map,   "map"));
    env_set(interp->global, "keys",       val_native(native_keys,       "keys"));
    env_set(interp->global, "has_key",    val_native(native_has_key,    "has_key"));
    env_set(interp->global, "remove_key", val_native(native_remove_key, "remove_key"));
    env_set(interp->global, "parse_json", val_native(native_parse_json, "parse_json"));
    env_set(interp->global, "to_json",    val_native(native_to_json,    "to_json"));
    env_set(interp->global, "read_csv",   val_native(native_read_csv,   "read_csv"));
    env_set(interp->global, "serve",      val_native(native_serve,      "serve"));
}

void interp_free(Interpreter *interp) {
    env_free(interp->global);
    val_free(&interp->return_value);
    for (int i=0;i<interp->search_path_count;i++) free(interp->search_paths[i]);
    for (int i=0;i<interp->loaded_count;i++) free(interp->loaded[i]);
}

Value interp_run(Interpreter *interp, AstNode *program) {
    Value result = interp_eval(interp, interp->global, program);
    interp->returning = 0;
    return result;
}
