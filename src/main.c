#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#ifndef _WIN32
#include <sys/utsname.h>
#include <sys/prctl.h>
#endif
#ifdef _WIN32
#define TokenType SB_WIN_TOKEN_TYPE
#include <windows.h>
#undef TokenType
static char *win_readline(const char *prompt) {
    printf("%s", prompt);
    fflush(stdout);
    char *buf = malloc(4096);
    if (!fgets(buf, 4096, stdin)) { free(buf); return NULL; }
    buf[strcspn(buf, "\r\n")] = '\0';
    return buf;
}
#ifndef WIFEXITED
#define WIFEXITED(s) 1
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(s) ((s) & 0xff)
#endif
#define readline win_readline
static void add_history(const char *h) { (void)h; }
static void read_history(const char *h) { (void)h; }
static void write_history(const char *h) { (void)h; }
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "lexer.h"
#include "parser.h"
#include "interpreter.h"

#ifndef SB_SRC_DIR
#define SB_SRC_DIR "."
#endif

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "sb: cannot read '%s'\n", path); exit(1); }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (fread(buf, 1, size, f) != (size_t)size) { fclose(f); free(buf); fprintf(stderr, "sb: cannot read '%s'\n", path); exit(1); }
    buf[size] = '\0'; fclose(f); return buf;
}

static void dirname_of(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    if (!slash) snprintf(out, cap, ".");
    else if (slash == path) snprintf(out, cap, "/");
    else { size_t len = (size_t)(slash - path); if (len >= cap) len = cap - 1; memcpy(out, path, len); out[len] = '\0'; }
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb"); if (f) { fclose(f); return 1; } return 0;
}

#ifdef _WIN32
#include <direct.h>
#define sb_mkdir(p) _mkdir(p)
#else
#define sb_mkdir(p) mkdir((p), 0755)
#endif

static void ensure_dir(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == 0) return;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return;
    // strip trailing slash
    if (tmp[len-1] == '/') tmp[len-1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            sb_mkdir(tmp);
            *p = '/';
        }
    }
    sb_mkdir(tmp);
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb"); if (!in) return 0;
    FILE *out = fopen(dst, "wb"); if (!out) { fclose(in); return 0; }
    char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out); return 1;
}

static void generate_c(const char *src_path, const char *c_path) {
    char *src = read_file(src_path);
    size_t len = strlen(src);
    char *esc = malloc(len*4 + 1); char *p = esc;
    for (size_t i=0;i<len;i++) {
        if (src[i]=='\\' || src[i]=='"') { *p++='\\'; *p++=src[i]; }
        else if (src[i]=='\n') { *p++='\\'; *p++='n'; }
        else if (src[i]=='\r') { *p++='\\'; *p++='r'; }
        else if (src[i]=='\t') { *p++='\\'; *p++='t'; }
        else *p++=src[i];
    } *p='\0';
    FILE *f = fopen(c_path, "wb");
    if (!f) { perror("tmp"); free(src); free(esc); return; }
    fprintf(f, "#ifndef SB_STD_DIR\n#define SB_STD_DIR %s\n#endif\n", SB_STD_DIR);
    fprintf(f, "#ifndef SB_SRC_DIR\n#define SB_SRC_DIR %s\n#endif\n", SB_SRC_DIR);
    fprintf(f, "#include \"lexer.h\"\n#include \"parser.h\"\n#include \"interpreter.h\"\n");
    fprintf(f, "#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n");
    fprintf(f, "int main(){const char *src=\"%s\";\n", esc);
    fprintf(f, "Interpreter ip; interp_init(&ip);\n");
    fprintf(f, "interp_add_search_path(&ip,\".\");\n");
    fprintf(f, "interp_add_search_path(&ip,\"./sb_modules\");\n");
    fprintf(f, "interp_add_search_path(&ip,\"%s\");\n", SB_STD_DIR);
    fprintf(f, "char base[1024]; const char *s=strrchr(\"%s\",'/'); if(s){size_t n=s-\"%s\"; strncpy(base,\"%s\",n); base[n]='\\0';} else strcpy(base,\".\");\n", src_path, src_path, src_path);
    fprintf(f, "interp_add_search_path(&ip,base);\n");
    // embed stdlib modules so built binaries need no std/ directory
    {
        DIR *sd = opendir(SB_STD_DIR);
        if (sd) {
            struct dirent *sde;
            int emb_idx = 0;
            while ((sde = readdir(sd)) != NULL && emb_idx < 60) {
                size_t sl = strlen(sde->d_name);
                if (sl > 3 && strcmp(sde->d_name + sl - 3, ".sb") == 0) {
                    char fpath[1600];
                    snprintf(fpath, sizeof(fpath), "%s/%s", SB_STD_DIR, sde->d_name);
                    char *fsrc = read_file(fpath);
                    size_t flen = strlen(fsrc);
                    char *fesc = malloc(flen*4 + 1); char *fp2 = fesc;
                    for (size_t i=0;i<flen;i++) {
                        if (fsrc[i]=='\\' || fsrc[i]=='"') { *fp2++='\\'; *fp2++=fsrc[i]; }
                        else if (fsrc[i]=='\n') { *fp2++='\\'; *fp2++='n'; }
                        else if (fsrc[i]=='\r') { *fp2++='\\'; *fp2++='r'; }
                        else if (fsrc[i]=='\t') { *fp2++='\\'; *fp2++='t'; }
                        else *fp2++=fsrc[i];
                    }
                    *fp2='\0';
                    char modname[256];
                    snprintf(modname, sizeof(modname), "%.*s", (int)(sl-3), sde->d_name);
                    fprintf(f, "interp_embed_module(&ip,\"%s\",\"%s\");\n", modname, fesc);
                    free(fesc); free(fsrc);
                    emb_idx++;
                }
            }
            closedir(sd);
        }
    }
    fprintf(f, "Parser p; parser_init(&p,src);\n");
    fprintf(f, "AstNode *prog=parser_parse(&p);\n");
    fprintf(f, "if(p.had_error){fprintf(stderr,\"compile error: %%s\\n\",p.error_msg); return 1;}\n");
    fprintf(f, "interp_run(&ip,prog); if(ip.had_error) fprintf(stderr,\"runtime: %%s\\n\",ip.error_msg);\n");
    fprintf(f, "return 0;}\n");
    fclose(f); free(src); free(esc);
}

static int build_to(const char *src_path, const char *out_path) {
    char c_path[1024]; snprintf(c_path, sizeof(c_path), "/tmp/sb_build_%d.c", getpid());
    generate_c(src_path, c_path);
    char cflags[4096]="", libs[4096]="";
    FILE *pp = popen("pkg-config --cflags gtk+-3.0 webkit2gtk-4.1 libcurl x11 2>/dev/null | tr '\n' ' '", "r");
    if (pp) { fread(cflags,1,sizeof(cflags)-1,pp); pclose(pp); cflags[strcspn(cflags,"\r\n")]='\0'; }
    pp = popen("pkg-config --libs gtk+-3.0 webkit2gtk-4.1 libcurl x11 2>/dev/null | tr '\n' ' '; echo -n ' -lreadline -lm'", "r");
    if (pp) { fread(libs,1,sizeof(libs)-1,pp); pclose(pp); libs[strcspn(libs,"\r\n")]='\0'; }
    for (char *q=cflags;*q;q++) if(*q=='\n'||*q=='\r') *q=' ';
    for (char *q=libs;*q;q++) if(*q=='\n'||*q=='\r') *q=' ';
    char simple[4096];
    snprintf(simple, sizeof(simple),
        "gcc -std=c11 -Wall -I%s/src %s -o %s %s %s/src/lexer.c %s/src/ast.c %s/src/value.c %s/src/parser.c %s/src/interpreter.c %s/src/ketiwe.c %s 2>&1",
        SB_SRC_DIR, cflags, out_path, c_path,
        SB_SRC_DIR, SB_SRC_DIR, SB_SRC_DIR, SB_SRC_DIR, SB_SRC_DIR, SB_SRC_DIR,
        libs);
    int rc = system(simple);
    unlink(c_path);
    if (rc==0) printf("Built %s (from %s)\n", out_path, src_path);
    return rc==0 ? 0 : 1;
}

static int handle_install(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: sb install <package>\n       sb install .\n");
        return 1;
    }
    const char *target = argv[2];
    if (strcmp(target, ".") == 0) {
        const char *manifest = NULL;
        if (file_exists("shimba.toml")) manifest = "shimba.toml";
        else if (file_exists("sb.toml")) manifest = "sb.toml";
        else if (file_exists("package.toml")) manifest = "package.toml";
        else { fprintf(stderr, "sb: no shimba.toml/sb.toml found in current directory\n"); return 1; }
        printf("Installing from %s...\n", manifest);
        char *content = read_file(manifest);
        int in_deps = 0, count = 0;
        char *line = strtok(content, "\n");
        while (line) {
            while (*line==' '||*line=='\t') line++;
            if (line[0]=='\0' || line[0]=='#') { line = strtok(NULL, "\n"); continue; }
            if (line[0]=='[') { in_deps = (strstr(line, "dependencies") != NULL); line = strtok(NULL, "\n"); continue; }
            if (in_deps) {
                char pkg[256] = {0};
                if (sscanf(line, " %255[^ =] =", pkg) == 1) {
                    char *eq = strchr(pkg, '"'); if (eq) *eq='\0';
                    // trim
                    char *p = pkg; while (*p==' '||*p=='\t') p++;
                    char *end = p + strlen(p) -1; while (end>p && (*end==' '||*end=='\t'||*end=='"')) *end--='\0';
                    if (*p) {
                        printf("  -> %s\n", p);
                        char cmd[512]; snprintf(cmd, sizeof(cmd), "sb install %s", p);
                        system(cmd); count++;
                    }
                }
            }
            line = strtok(NULL, "\n");
        }
        free(content);
        if (count==0) printf("No dependencies found.\n");
        else printf("Installed %d packages.\n", count);
        return 0;
    }

    char pkg[256]; strncpy(pkg, target, sizeof(pkg)-1); pkg[sizeof(pkg)-1]='\0';
    char *at = strchr(pkg, '@'); if (at) *at='\0';
    char *ver = at ? at+1 : NULL;

    // git URL form: https://... or user/repo or local git path
    if (strstr(target, "://") || target[0]=='/' || strncmp(target,"./",2)==0 ||
        (strchr(pkg,'/') && !strchr(pkg, ' '))) {
        char url[512];
        if (strstr(pkg, "://") || target[0]=='/' || strncmp(target,"./",2)==0)
            snprintf(url, sizeof(url), "%s", pkg);
        else
            snprintf(url, sizeof(url), "https://github.com/%s", pkg);
        printf("Installing from git %s...\n", url);
        ensure_dir("sb_modules");
        char tmpdir[256];
        snprintf(tmpdir, sizeof(tmpdir), "/tmp/sb_pkg_%d", getpid());
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "git clone --depth 1 '%s' '%s' 2>&1 | tail -1", url, tmpdir);
        if (system(cmd) != 0) { fprintf(stderr, "sb: git clone failed\n"); return 1; }
        int copied = 0;
        DIR *d = opendir(tmpdir);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                size_t l = strlen(de->d_name);
                if (l>3 && strcmp(de->d_name+l-3,".sb")==0) {
                    char srcf[1024], dstf[1024];
                    snprintf(srcf, sizeof(srcf), "%s/%s", tmpdir, de->d_name);
                    snprintf(dstf, sizeof(dstf), "sb_modules/%s", de->d_name);
                    copy_file(srcf, dstf);
                    printf("  + %s\n", de->d_name);
                    copied++;
                }
            }
            closedir(d);
        }
        char rmc[512]; snprintf(rmc, sizeof(rmc), "rm -rf '%s'", tmpdir); system(rmc);
        if (copied == 0) { fprintf(stderr, "sb: no .sb files in package\n"); return 1; }
        printf("Installed %d file(s) into sb_modules/\n", copied);
        return 0;
    }

    printf("Installing %s%s%s...\n", pkg, ver?"@":"", ver?ver:"");

    char src[1024], dst[1024];
    snprintf(src, sizeof(src), "%s/%s.sb", SB_STD_DIR, pkg);
    ensure_dir("sb_modules");
    snprintf(dst, sizeof(dst), "sb_modules/%s.sb", pkg);

    if (file_exists(src)) {
        if (copy_file(src, dst)) {
            printf("  %s -> %s (from std)\n", pkg, dst);
            return 0;
        }
    }
    snprintf(src, sizeof(src), "std/%s.sb", pkg);
    if (file_exists(src) && copy_file(src, dst)) {
        printf("  %s -> %s (from ./std)\n", pkg, dst);
        return 0;
    }
    // fallback: try remote via curl (if network)
    char url[1024];
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/shimbabomb/std/main/%s.sb", pkg);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "curl -sL '%s' -o '%s' 2>/dev/null", url, dst);
    if (system(cmd)==0 && file_exists(dst)) {
        // check if file is not empty and not 404 html
        char *c = read_file(dst);
        if (c && strstr(c, "404") && strlen(c) < 1000) { unlink(dst); free(c); }
        else { free(c); printf("  %s -> %s (from remote)\n", pkg, dst); return 0; }
    }
    fprintf(stderr, "sb: package '%s' not found in std nor remote\n", pkg);
    return 1;
}

static int handle_update(void) {
    // read current VERSION (try cwd, then installed dir, then exe dir)
    char cur_ver[32] = "v0.0.0";
    char vpath[1024];
    FILE *vf = fopen("VERSION", "rb");
    if (!vf) {
        snprintf(vpath, sizeof(vpath), "%s/VERSION", SB_SRC_DIR);
        vf = fopen(vpath, "rb");
    }
    if (!vf) {
        ssize_t len = readlink("/proc/self/exe", vpath, sizeof(vpath)-1);
        if (len > 0) {
            vpath[len] = '\0';
            char *slash = strrchr(vpath, '/');
            if (slash) { *slash = '\0'; char vp2[1024]; snprintf(vp2, sizeof(vp2), "%s/../share/shimbabomb/VERSION", vpath); vf = fopen(vp2, "rb");
                if (!vf) { snprintf(vp2, sizeof(vp2), "%s/../../VERSION", vpath); vf = fopen(vp2, "rb"); }
            }
        }
    }
    if (vf) { if (fgets(cur_ver, sizeof(cur_ver), vf)) cur_ver[strcspn(cur_ver, "\r\n")] = '\0'; fclose(vf); }
    printf("Current version: %s\n", cur_ver);

    // fetch latest tag from GitHub tags API
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "curl -sL https://api.github.com/repos/Shimba-crypto/shimbabomb/tags");
    FILE *p = popen(cmd, "r");
    if (!p) { fprintf(stderr, "sb update: cannot reach GitHub\n"); return 1; }
    char buf[8192] = {0};
    fread(buf, 1, sizeof(buf)-1, p);
    pclose(p);

    // extract first "name": "vX.Y.Z" from JSON array
    char *tag = strstr(buf, "\"name\"");
    if (!tag) { fprintf(stderr, "sb update: could not find latest version\n"); return 1; }
    tag = strchr(tag, ':');
    if (!tag) { fprintf(stderr, "sb update: bad response\n"); return 1; }
    tag++;
    while (*tag == ' ' || *tag == '"') tag++;
    char latest[32] = {0};
    int i = 0;
    while (*tag && *tag != '"' && i < 31) { latest[i++] = *tag; tag++; }
    latest[i] = '\0';

    printf("Latest version:  %s\n", latest);

    if (strcmp(cur_ver, latest) == 0) {
        printf("Already up to date!\n");
        return 0;
    }
    // don't downgrade if local is ahead of release
    if (strcmp(cur_ver, latest) > 0) {
        printf("Already up to date! (local %s ahead of release %s)\n", cur_ver, latest);
        return 0;
    }

    printf("Updating %s -> %s ...\n", cur_ver, latest);

    // try tag-specific install.sh, fallback to main
    char url[1024], url2[1024];
    snprintf(url, sizeof(url), "https://raw.githubusercontent.com/Shimba-crypto/shimbabomb/%s/install.sh", latest);
    snprintf(url2, sizeof(url2), "https://raw.githubusercontent.com/Shimba-crypto/shimbabomb/main/install.sh");
    char test[2048];
    snprintf(test, sizeof(test), "curl -fsSL '%s' -o /tmp/sb_install_test.sh 2>/dev/null && head -1 /tmp/sb_install_test.sh | grep -q '^#!/bin/sh'", url);
    const char *use_url = (system(test) == 0) ? url : url2;
    snprintf(cmd, sizeof(cmd), "curl -fsSL '%s' | bash", use_url);
    printf("Running: %s\n", cmd);
    return system(cmd);
}

static void print_help(void) {
    char ver[32] = "v1.11.0";
    char vpath[1024];
    FILE *vf = fopen("VERSION", "rb");
    if (!vf) {
        snprintf(vpath, sizeof(vpath), "%s/VERSION", SB_SRC_DIR);
        vf = fopen(vpath, "rb");
    }
    if (!vf) {
#ifndef _WIN32
        char exe_path[1024];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
        if (len > 0) {
            exe_path[len] = '\0';
            char *slash = strrchr(exe_path, '/');
            if (slash) { *slash = '\0'; snprintf(vpath, sizeof(vpath), "%s/../../VERSION", exe_path); vf = fopen(vpath, "rb"); }
        }
#endif
    }
    if (vf) { if (fgets(ver, sizeof(ver), vf)) ver[strcspn(ver, "\r\n")] = '\0'; fclose(vf); }
    printf("ShimbaBomb %s — English-like scripting\n", ver);
    printf("Usage:\n");
    printf("  sb run <file.sb>         Run file (alias for sb <file.sb>)\n");
    printf("  sb [file.sb] [-i] [--noconsole]  Run file, -i stays in REPL, --noconsole hides console (window only)\n");
    printf("  sb                       REPL with history, try 'help'\n");
    printf("  sb update                Auto-update to latest release from GitHub\n");
    printf("  sb install <pkg>         Install package to sb_modules/\n");
    printf("  sb install .             Install from shimba.toml/sb.toml [dependencies]\n");
    printf("  sb build <file>          Compile to binary (see sb-build)\n");
    printf("  sb init                  Scaffold a new project\n");
    printf("  sb watch                 Watch .sb files and re-run on change\n");
    printf("  sb fmt <file>            Format an .sb file\n");
    printf("  sb web                   Serve current dir on http://localhost:8080\n");
    printf("  sb publish               Publish package (info)\n");
    printf("  sb link                  Install THIS project as a local library\n");
    printf("  sb pack deb|rpm [dir]    Package project into .deb or .rpm\n");
    printf("  sb pack exe <file.sb>    Windows x64 console .exe (mingw)\n");
    printf("  sb test                  Run tests/*.sb with assert\n");
    printf("  sb doc <file>            Generate HTML docs from ## comments\n");
    printf("  sb --debug file.sb       Step debugger (n/c/p var/where/q)\n");
    printf("  sb lsp                   Run LSP server on stdio\n");
    printf("  sb --help                This help\n");
}

static void repl_with(Interpreter *interp) {
    const char *home = getenv("HOME");
    char hist_path[1024] = {0};
    if (home) snprintf(hist_path, sizeof(hist_path), "%s/.shimbabomb_history", home);
    if (hist_path[0]) read_history(hist_path);

    while (1) {
        char *line = readline(">>> ");
        if (!line) break;
        if (*line) add_history(line);
        if (hist_path[0]) write_history(hist_path);
        if (strcmp(line, "exit")==0 || strcmp(line, "quit")==0) { free(line); break; }
        if (strcmp(line, "help")==0) { print_help(); free(line); continue; }
        if (strcmp(line, "clear")==0) { printf("\033[2J\033[H"); free(line); continue; }
        if (strlen(line)==0) { free(line); continue; }

        Parser parser;
        parser_init(&parser, line);
        AstNode *prog = parser_parse(&parser);
        if (parser.had_error) {
            fprintf(stderr, "error: %s\n", parser.error_msg);
            node_free(prog); free(line); continue;
        }
        Value result = interp_run(interp, prog);
        if (interp->had_error) {
            fprintf(stderr, "error: %s\n", interp->error_msg);
            interp->had_error = 0;
        }
        val_free(&result);
        node_free(prog);
        free(line);
    }
    printf("\n");
}

static int handle_init(void) {
    if (file_exists("shimba.toml") || file_exists("sb.toml")) {
        fprintf(stderr, "sb: project already initialized (shimba.toml exists)\n");
        return 1;
    }
    FILE *f = fopen("shimba.toml", "w");
    if (!f) { perror("shimba.toml"); return 1; }
    fprintf(f, "[project]\nname = \"my-project\"\nversion = \"0.1.0\"\n\n[dependencies]\n");
    fclose(f);
    f = fopen("main.sb", "w");
    if (!f) { perror("main.sb"); return 1; }
    fprintf(f, "SB\nsay \"hello from my project\".\n");
    fclose(f);
    ensure_dir("sb_modules");
    printf("Created shimba.toml, main.sb, sb_modules/\n");
    return 0;
}

#ifdef __linux__
#include <sys/inotify.h>
#endif

static int handle_watch(void) {
    printf("Watching .sb files... (Ctrl+C to stop)\n");
#ifdef __linux__
    int ifd = inotify_init();
    int use_inotify = (ifd >= 0);
    if (use_inotify) {
        // add watches recursively on dirs containing .sb files
        char wd_names[64][1024];
        int wd_map[64], wd_count = 0;
        // helper: add dir + recurse
        void add_dir(const char *path, int depth) {
            if (depth > 4 || wd_count >= 60) return;
            int wd = inotify_add_watch(ifd, path, IN_MODIFY | IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE);
            if (wd >= 0 && wd < 64) {
                snprintf(wd_names[wd], sizeof(wd_names[0]), "%s", path);
                wd_map[wd] = 1; if (wd >= wd_count) wd_count = wd + 1;
            }
            DIR *sub = opendir(path);
            if (!sub) return;
            struct dirent *de;
            while ((de = readdir(sub)) != NULL) {
                if (de->d_name[0]=='.') continue;
                char subpath[1024];
                snprintf(subpath, sizeof(subpath), "%s/%s", path, de->d_name);
                struct stat st2;
                if (stat(subpath, &st2)==0 && S_ISDIR(st2.st_mode))
                    add_dir(subpath, depth+1);
            }
            closedir(sub);
        };
        add_dir(".", 0);

        char buf[16384];
        time_t last_run = 0;
        while (1) {
            ssize_t len = read(ifd, buf, sizeof(buf));
            if (len <= 0) { struct timespec ts={0,100000000}; nanosleep(&ts, NULL); continue; }
            int has_sb = 0;
            for (ssize_t i = 0; i < len; ) {
                struct inotify_event *ev = (struct inotify_event*)&buf[i];
                if ((ev->mask & (IN_MODIFY|IN_CREATE|IN_MOVED_TO|IN_CLOSE_WRITE)) && ev->len > 0) {
                    size_t l = strlen(ev->name);
                    if (l>3 && strcmp(ev->name+l-3,".sb")==0 && ev->name[0] != '.')
                        has_sb = 1;
                }
                i += sizeof(struct inotify_event) + ev->len;
            }
            if (has_sb) {
                time_t now = time(NULL);
                if (now != last_run) {
                    last_run = now;
                    sleep(1); // let editor finish writing
                    printf("\n--- Change detected ---\n");
                    if (file_exists("main.sb")) {
                        printf("Running main.sb:\n");
                        system("sb main.sb");
                    } else {
                        system("ls -t *.sb 2>/dev/null | head -1 | xargs -I{} sh -c 'echo \"Running {}\"; sb \"{}\" 2>&1 | head -30'");
                    }
                    // re-add watches for any new dirs
                    add_dir(".", 0);
                }
            }
        }
    } else
#endif
    {
    struct stat st;
    time_t last_mtime = 0;
    DIR *d0 = opendir(".");
    if (d0) {
        struct dirent *de;
        while ((de = readdir(d0)) != NULL) {
            size_t l = strlen(de->d_name);
            if (l > 3 && strcmp(de->d_name + l - 3, ".sb") == 0) {
                if (stat(de->d_name, &st) == 0 && st.st_mtime > last_mtime) last_mtime = st.st_mtime;
            }
        }
        closedir(d0);
    }
    while (1) {
        int changed = 0;
        time_t max_mtime = last_mtime;
        DIR *d = opendir(".");
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                size_t l = strlen(de->d_name);
                if (l > 3 && strcmp(de->d_name + l - 3, ".sb") == 0) {
                    if (stat(de->d_name, &st) == 0) {
                        if (st.st_mtime > last_mtime) changed = 1;
                        if (st.st_mtime > max_mtime) max_mtime = st.st_mtime;
                    }
                }
            }
            closedir(d);
        }
        if (changed) {
            last_mtime = max_mtime;
            printf("\n--- Change detected ---\n");
            if (file_exists("main.sb")) {
                printf("Running main.sb:\n");
                system("sb main.sb");
            } else {
                char cmd[512];
                snprintf(cmd, sizeof(cmd), "ls -t *.sb 2>/dev/null | head -1 | xargs -I{} sh -c 'echo \"Running {}\"; sb \"{}\" 2>&1 | head -30'");
                system(cmd);
            }
        }
        sleep(1);
    }
    }
    return 0;
}

static int handle_fmt(const char *path) {
    if (!path) { fprintf(stderr, "Usage: sb fmt <file.sb>\n"); return 1; }
    char *src = read_file(path);
    FILE *f = fopen(path, "w");
    if (!f) { free(src); return 1; }
    int indent = 0;
    const char *p = src;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        while (len > 0 && (p[len-1]==' '||p[len-1]=='\t'||p[len-1]=='\r')) len--;
        if (len == 0) { fputc('\n', f); if (nl) p = nl + 1; else break; continue; }
        char line[4096]; if (len >= sizeof(line)) len = sizeof(line)-1;
        memcpy(line, p, len); line[len] = '\0';
        // trim leading whitespace for analysis
        char *trim = line;
        while (*trim==' ' || *trim=='\t') trim++;
        int is_end = (strncmp(trim, "end", 3)==0 && (trim[3]=='\0' || trim[3]=='.' || trim[3]==' '));
        int is_otherwise = (strncmp(trim, "otherwise", 9)==0);
        int is_catch = (strncmp(trim, "catch", 5)==0);
        if (is_end || is_otherwise || is_catch) {
            indent--; if (indent<0) indent=0;
        }
        for (int i = 0; i < indent; i++) fprintf(f, "    ");
        fputs(trim, f);
        fputc('\n', f);
        // check if this line opens a block
        int opens = 0;
        if (strncmp(trim, "define ", 7)==0) opens = 1;
        else if (strncmp(trim, "class ", 6)==0) opens = 1;
        else if (strncmp(trim, "method ", 7)==0) opens = 1;
        else if (strncmp(trim, "if ", 3)==0) opens = 1;
        else if (strncmp(trim, "count ", 6)==0) opens = 1;
        else if (strncmp(trim, "loop ", 5)==0) opens = 1;
        else if (strncmp(trim, "try", 3)==0) opens = 1;
        if (opens) indent++;
        else if (is_otherwise || is_catch) indent++;
        if (nl) p = nl + 1; else break;
    }
    fclose(f); free(src);
    printf("Formatted %s\n", path);
    return 0;
}

static int handle_doc(const char *path) {
    if (!path) { fprintf(stderr, "Usage: sb doc <file.sb>\n"); return 1; }
    char *src = read_file(path);
    char out_path[1200];
    snprintf(out_path, sizeof(out_path), "%s.html", path);
    FILE *f = fopen(out_path, "w");
    if (!f) { free(src); return 1; }
    fprintf(f, "<!DOCTYPE html><html><head><meta charset='utf-8'><title>%s docs</title>", path);
    fprintf(f, "<style>body{font-family:sans-serif;max-width:800px;margin:2rem auto;padding:0 1rem;background:#14141e;color:#e0e0ef}");
    fprintf(f, "h2{color:#8be9fd;border-bottom:1px solid #333}pre{background:#1e1e2e;padding:.8em;border-radius:6px;overflow-x:auto}");
    fprintf(f, ".doc{color:#a6e3a1}</style></head><body>\n");
    char pending_docs[64][512];
    int doc_count = 0;
    const char *p = src;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[1024]; if (len >= sizeof(line)) len = sizeof(line)-1;
        memcpy(line, p, len); line[len]='\0';
        char *trim = line; while (*trim==' '||*trim=='\t') trim++;
        if (strncmp(trim, "##", 2)==0) {
            if (doc_count < 64) snprintf(pending_docs[doc_count++], sizeof(pending_docs[0]), "%s", trim+2);
        } else if ((strncmp(trim,"define ",7)==0 || strncmp(trim,"class ",6)==0) && doc_count > 0) {
            // heading + docs + signature
            fprintf(f, "<section><h2>");
            for (char *w = trim + (strncmp(trim,"define ",7)==0?7:6); *w && *w!=' '; w++) fputc(*w, f);
            fprintf(f, "</h2>\n");
            for (int i=0;i<doc_count;i++) fprintf(f, "<p class='doc'>%s</p>\n", pending_docs[i]);
            fprintf(f, "<pre>"); for(char*c=trim;*c;c++) { if(*c=='<') fprintf(f,"&lt;"); else fputc(*c,f);} fprintf(f, "</pre></section>\n");
            doc_count = 0;
        }
        if (nl) p = nl + 1; else break;
    }
    fprintf(f, "</body></html>\n");
    fclose(f); free(src);
    printf("Docs written to %s\n", out_path);
    return 0;
}

// ── Minimal LSP (stdio JSON-RPC) ─────────────────────────────────────

static void lsp_send(const char *method, const char *params_json) {
    char body[4096];
    snprintf(body, sizeof(body), "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":%s}", method, params_json);
    printf("Content-Length: %zu\r\n\r\n%s", strlen(body), body);
    fflush(stdout);
}

static void lsp_notify(const char *method, const char *params_json) {
    char body[8192];
    snprintf(body, sizeof(body), "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}", method, params_json);
    printf("Content-Length: %zu\r\n\r\n%s", strlen(body), body);
    fflush(stdout);
}

static int handle_lsp(void) {
    // keywords for completion
    static const char *kws[] = {"set","to","say","define","with","as","end",
        "if","then","otherwise","count","from","loop","through","while",
        "break","continue","match","try","catch","raise","class","new",
        "method","give back","pls bring","start","task","wait for","async","await","assert",NULL};

    char line[256];
    char content[65536];
    size_t clen = 0;
    // read headers
    while (fgets(line, sizeof(line), stdin)) {
        if (strcmp(line, "\r\n")==0 || strcmp(line,"\n")==0) break;
    }
    (void)content; (void)clen;

    lsp_send("initialize", "{\"capabilities\":{\"textDocumentSync\":1,\"completionProvider\":{\"triggerCharacters\":[\" \"]}}}");

    while (1) {
        // read Content-Length
        int len = 0;
        if (!fgets(line, sizeof(line), stdin)) break;
        if (strncmp(line, "Content-Length:", 15)==0)
            len = atoi(line+15);
        // skip to body
        while (fgets(line, sizeof(line), stdin)) {
            if (strcmp(line, "\r\n")==0 || strcmp(line,"\n")==0) break;
        }
        if (len <= 0 || len >= (int)sizeof(content)) continue;
        size_t got = fread(content, 1, len, stdin);
        content[got] = '\0';

        if (strstr(content, "\"shutdown\"")) {
            lsp_send(NULL, "null");
            return 0;
        }
        if (strstr(content, "\"textDocument/didOpen\"") || strstr(content, "\"textDocument/didChange\"")) {
            // extract text between first \"text\":\" and closing unescaped quote (best effort)
            char *t = strstr(content, "\\\"text\\\"");
            if (!t) t = strstr(content, "\"text\"");
            if (t) {
                t = strchr(t+6, ':');
                if (t) t++;
                while (*t==' ') t++;
                if (*t=='\"') t++;
                char src[16384]; int si=0;
                for (; *t && si < (int)sizeof(src)-1; t++) {
                    if (*t=='\\' && t[1]=='\"') { src[si++]='\"'; t++; }
                    else if (*t=='\\' && t[1]=='n') { src[si++]='\n'; t++; }
                    else if (*t=='\\' && t[1]=='\\') { src[si++]='\\'; t++; }
                    else if (*t=='\"') break;
                    else if (*t!='\\') src[si++]=*t;
                }
                src[si]='\0';
                // diagnostics: try parse
                Parser p;
                parser_init(&p, src);
                AstNode *prog = parser_parse(&p);
                char diag[1024];
                if (p.had_error) {
                    snprintf(diag, sizeof(diag),
                        "{\"uri\":\"file:///current.sb\",\"diagnostics\":[{\"severity\":1,\"message\":\"%s\",\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":10}}}]}",
                        p.error_msg);
                } else {
                    snprintf(diag, sizeof(diag), "{\"uri\":\"file:///current.sb\",\"diagnostics\":[]}");
                }
                node_free(prog);
                lsp_notify("textDocument/publishDiagnostics", diag);
            }
        }
        else if (strstr(content, "\"textDocument/completion\"")) {
            char items[4096] = "";
            for (int i=0; kws[i]; i++) {
                char it[128];
                snprintf(it, sizeof(it), "%s{\"label\":\"%s\"}", i?",":"", kws[i]);
                strncat(items, it, sizeof(items)-strlen(items)-1);
            }
            char params[4200];
            snprintf(params, sizeof(params), "{\"isIncomplete\":false,\"items\":[%s]}", items);
            // completion response needs the request id — best effort with id:1
            char body[4300];
            snprintf(body, sizeof(body), "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":%s}", params);
            printf("Content-Length: %zu\r\n\r\n%s", strlen(body), body);
            fflush(stdout);
        }
    }
    return 0;
}

// ── sb pack deb / rpm ───────────────────────────────────────────────

static void pack_read_manifest(const char *dir, char *name, size_t ncap,
                               char *version, size_t vcap,
                               char *desc, size_t dcap,
                               char *maint, size_t mcap) {
    char mp[1200];
    snprintf(mp, sizeof(mp), "%s/shimba.toml", dir);
    if (!file_exists(mp)) snprintf(mp, sizeof(mp), "%s/sb.toml", dir);
    if (!file_exists(mp)) return;
    char *content = read_file(mp);
    char *line = strtok(content, "\n");
    int in_project = 0;
    while (line) {
        char *t = line + strspn(line, " \t");
        char tmp[512];
        if (sscanf(t, "name = \"%511[^\"]\"", tmp)==1 && name[0]=='\0') snprintf(name,ncap,"%s",tmp);
        else if (sscanf(t, "version = \"%63[^\"]\"", tmp)==1 && version[0]=='\0') snprintf(version,vcap,"%s",tmp);
        else if (sscanf(t, "description = \"%511[^\"]\"", tmp)==1 && desc[0]=='\0') snprintf(desc,dcap,"%s",tmp);
        else if (sscanf(t, "maintainer = \"%255[^\"]\"", tmp)==1 && maint[0]=='\0') snprintf(maint,mcap,"%s",tmp);
        (void)in_project;
        line = strtok(NULL,"\n");
    }
    free(content);
}

static int build_entry_binary(const char *dir, const char *name, char *binout, size_t bcap) {
    char entry[256] = "main";
    // allow entry override
    {
        char mp[1200];
        snprintf(mp, sizeof(mp), "%s/shimba.toml", dir);
        FILE *tf = fopen(mp, "r");
        if (tf) {
            char lbuf[512];
            while (fgets(lbuf, sizeof(lbuf), tf)) {
                char tmp[256];
                if (sscanf(lbuf, " entry = \"%255[^\"]\"", tmp)==1) snprintf(entry,sizeof(entry),"%s",tmp);
            }
            fclose(tf);
        }
    }
    char src_path[1300];
    snprintf(src_path, sizeof(src_path), "%s/%s.sb", dir, entry);
    if (!file_exists(src_path)) {
        fprintf(stderr, "sb pack: entry file not found: %s\n", src_path);
        return 1;
    }
    char staging_bin[1400];
    snprintf(staging_bin, sizeof(staging_bin), "/tmp/sb_pack_bin_%d_%s", getpid(), name);
    if (build_to(src_path, staging_bin) != 0) return 1;
    snprintf(binout, bcap, "%s", staging_bin);
    return 0;
}

static int handle_pack(int argc, char **argv) {
    const char *fmt = argv[2];
    const char *dir = ".";
    if (argc >= 4) dir = argv[3];
    if (!fmt || (strcmp(fmt,"deb")!=0 && strcmp(fmt,"rpm")!=0 && strcmp(fmt,"msi")!=0)) {
        fprintf(stderr, "Usage: sb pack deb [project-dir]\n       sb pack rpm [project-dir]\n       sb pack msi [project-dir]\n");
        return 1;
    }

    char name[256]="", version[64]="", desc[512]="", maint[256]="";
    pack_read_manifest(dir, name, sizeof(name), version, sizeof(version), desc, sizeof(desc), maint, sizeof(maint));
    if (version[0]=='\0') snprintf(version, sizeof(version), "1.0.0");
    if (desc[0]=='\0') snprintf(desc, sizeof(desc), "ShimbaBomb application");
    if (maint[0]=='\0') snprintf(maint, sizeof(maint), "ShimbaBomb <sb@shimbabomb.dev>");
    if (name[0]=='\0') { fprintf(stderr, "sb pack: no [project] name in %s/shimba.toml\n", dir); return 1; }
    // debian rules: lowercase, [a-z0-9+-.]
    for (char *q=name; *q; q++) { if (*q >= 'A' && *q <= 'Z') *q += 32; }
    if (!(version[0] >= '0' && version[0] <= '9')) { fprintf(stderr, "sb pack: version must start with a digit (got '%s')\n", version); return 1; }

    char binpath[1400];
    if (build_entry_binary(dir, name, binpath, sizeof(binpath)) != 0) return 1;

    if (strcmp(fmt,"deb")==0) {
#ifndef __linux__
        fprintf(stderr, "sb pack deb: only supported on Linux\n");
        return 1;
#else
        if (!file_exists("/usr/bin/dpkg-deb") && system("which dpkg-deb >/dev/null 2>&1") != 0) {
            fprintf(stderr, "sb pack deb: dpkg-deb not found (apt install dpkg-dev)\n");
            return 1;
        }
        char arch[32] = "amd64";
#ifndef _WIN32
        struct utsname u; uname(&u);
        if (strstr(u.machine, "aarch64") || strstr(u.machine, "arm64")) snprintf(arch,sizeof(arch),"arm64");
        else if (strstr(u.machine, "arm")) snprintf(arch,sizeof(arch),"armhf");
#endif

        char stage[1400], ctl_dir[1500], bindir[1500];
        snprintf(stage, sizeof(stage), "/tmp/sb_deb_%d/%s_%s-%s", getpid(), name, version, arch);
        snprintf(ctl_dir, sizeof(ctl_dir), "%s/DEBIAN", stage);
        snprintf(bindir, sizeof(bindir), "%s/usr/local/bin", stage);
        ensure_dir(ctl_dir);
        ensure_dir(bindir);

        char bin_dst[1600];
        snprintf(bin_dst, sizeof(bin_dst), "%s/%s", bindir, name);
        copy_file(binpath, bin_dst);
        chmod(bin_dst, 0755);

        // bundle vendor libs if present (self-contained deb) — selective: only libs actually used by this app
        int bundled = 0;
        {
            const char *home2 = getenv("HOME");
            if (home2) {
                char vendor_src[1200];
                snprintf(vendor_src, sizeof(vendor_src), "%s/.local/share/shimbabomb/lib", home2);
                DIR *vd = opendir(vendor_src);
                if (vd) {
                    // analyze SB source to see what features are used
                    int need_webkit = 0, need_gtk = 0, need_curl = 0, need_audio = 0;
                    {
                        char *src_content = read_file(binpath); // actually binpath is binary, read entry SB file instead
                        // we already have src_path from earlier, use it
                        (void)src_content; // placeholder, will check below via file read
                    }
                    // read the original SB source file for keyword scan
                    char sb_src_check[1300];
                    snprintf(sb_src_check, sizeof(sb_src_check), "%s/%s.sb", dir, name);
                    // also try dir/main.sb
                    if (!file_exists(sb_src_check)) snprintf(sb_src_check, sizeof(sb_src_check), "%s/main.sb", dir);
                    char *check_content = NULL;
                    if (file_exists(sb_src_check)) check_content = read_file(sb_src_check);
                    else {
                        // fallback: check dir/*.sb for any hint
                        DIR *sd = opendir(dir);
                        if (sd) {
                            struct dirent *se;
                            while ((se = readdir(sd)) != NULL) {
                                size_t l = strlen(se->d_name);
                                if (l>3 && strcmp(se->d_name+l-3,".sb")==0) {
                                    char p[1400]; snprintf(p, sizeof(p), "%s/%s", dir, se->d_name);
                                    char *c = read_file(p);
                                    if (c) {
                                        if (!check_content) check_content = strdup(c);
                                        else {
                                            size_t old = strlen(check_content);
                                            check_content = realloc(check_content, old + strlen(c) + 2);
                                            strcat(check_content, "\n"); strcat(check_content, c);
                                        }
                                        free(c);
                                    }
                                }
                            }
                            closedir(sd);
                        }
                    }
                    if (check_content) {
                        if (strstr(check_content, "window") || strstr(check_content, "webkit")) need_webkit = 1;
                        if (strstr(check_content, "shimgui_") || strstr(check_content, "window") || need_webkit) need_gtk = 1;
                        if (strstr(check_content, "fetch")) need_curl = 1;
                        if (strstr(check_content, "play")) need_audio = 1;
                        // if no GUI at all, still need at least readline for REPL, but that's tiny
                        free(check_content);
                    } else {
                        // unknown, bundle all for safety
                        need_webkit = need_gtk = need_curl = 1;
                    }
                    char vendor_dst[1600];
                    snprintf(vendor_dst, sizeof(vendor_dst), "%s/usr/share/shimbabomb/lib", stage);
                    ensure_dir(vendor_dst);
                    struct dirent *ve;
                    int copied = 0;
                    while ((ve = readdir(vd)) != NULL) {
                        if (ve->d_name[0]=='.') continue;
                        // selective filter: skip libs not needed for this app, but keep all (lazy-load) — just don't copy unused category to save size, not removing functionality
                        int skip = 0;
                        if (!need_webkit && (strstr(ve->d_name, "webkit") || strstr(ve->d_name, "javascriptcore") || strstr(ve->d_name, "soup-") || strstr(ve->d_name, "zypp") || strstr(ve->d_name, "aom") || strstr(ve->d_name, "dav1d") || strstr(ve->d_name, "SvtAv1") || strstr(ve->d_name, "rav1e") || strstr(ve->d_name, "gstreamer") || strstr(ve->d_name, "gst"))) skip = 1;
                        if (!need_gtk && (strstr(ve->d_name, "gtk-3") || strstr(ve->d_name, "gdk-3") || strstr(ve->d_name, "cairo") || strstr(ve->d_name, "pango") || strstr(ve->d_name, "harfbuzz") || strstr(ve->d_name, "epoxy") || strstr(ve->d_name, "atspi") || strstr(ve->d_name, "atk-"))) skip = 1;
                        if (!need_curl && (strstr(ve->d_name, "curl") || strstr(ve->d_name, "nghttp") || strstr(ve->d_name, "psl") || strstr(ve->d_name, "brotli") || (strstr(ve->d_name, "ssl") && !need_webkit) || (strstr(ve->d_name, "crypto") && !need_webkit))) skip = 1;
                        if (!need_audio && strstr(ve->d_name, "asound")) skip = 1;
                        if (skip) continue;
                        char srcf[1800], dstf[2000];
                        snprintf(srcf, sizeof(srcf), "%s/%s", vendor_src, ve->d_name);
                        snprintf(dstf, sizeof(dstf), "%s/%s", vendor_dst, ve->d_name);
                        if (copy_file(srcf, dstf)) copied++;
                    }
                    closedir(vd);
                    if (copied > 0) {
                        // wrap binary to set LD_LIBRARY_PATH to bundled libs
                        char real_dst[1600];
                        snprintf(real_dst, sizeof(real_dst), "%s/%s.real", bindir, name);
                        rename(bin_dst, real_dst);
                        FILE *wf = fopen(bin_dst, "w");
                        if (wf) {
                            fprintf(wf, "#!/bin/sh\nVENDOR=\"/usr/share/shimbabomb/lib\"\nif [ -d \"$VENDOR\" ]; then export LD_LIBRARY_PATH=\"$VENDOR:$LD_LIBRARY_PATH\"; fi\nexec \"/usr/local/bin/%s.real\" \"$@\"\n", name);
                            fclose(wf);
                            chmod(bin_dst, 0755);
                        }
                        bundled = 1;
                        printf("Bundled %d libs into /usr/share/shimbabomb/lib (self-contained)\n", copied);
                    }
                }
            }
        }

        char ctl[1600];
        snprintf(ctl, sizeof(ctl), "%s/control", ctl_dir);
        FILE *cf = fopen(ctl, "w");
        if (!cf) { perror("control"); return 1; }
        fprintf(cf, "Package: %s\n", name);
        fprintf(cf, "Version: %s\n", version);
        fprintf(cf, "Section: utils\n");
        fprintf(cf, "Priority: optional\n");
        fprintf(cf, "Architecture: %s\n", arch);
        fprintf(cf, "Maintainer: %s\n", maint);
        if (bundled) {
            fprintf(cf, "Depends: libc6\n");
        } else {
            fprintf(cf, "Depends: libgtk-3-0, libwebkit2gtk-4.1-0 | libwebkit2gtk-4.0-0, libcurl4, libreadline8 | libreadline7\n");
        }
        fprintf(cf, "Description: %s\n Built with ShimbaBomb.\n", desc);
        fclose(cf);

        char outdeb[1600], cmd[3400];
        snprintf(outdeb, sizeof(outdeb), "%s/%s_%s_%s.deb", dir, name, version, arch);
        snprintf(cmd, sizeof(cmd), "dpkg-deb --root-owner-group --build '%s' '%s' 2>&1", stage, outdeb);
        int rc = system(cmd);
        snprintf(cmd, sizeof(cmd), "rm -rf /tmp/sb_deb_%d", getpid());
        system(cmd);
        unlink(binpath);
        if (rc == 0) {
            printf("Packed %s\n", outdeb);
            printf("install with: sudo apt install ./%s_%s_%s.deb\n", name, version, arch);
            return 0;
        }
        return 1;
#endif
    }

#ifdef __linux__
    if (!file_exists("/usr/bin/rpmbuild") && system("which rpmbuild >/dev/null 2>&1") != 0) {
        fprintf(stderr, "sb pack rpm: rpmbuild not found (dnf install rpm-build)\n");
        return 1;
    }
    const char *home = getenv("HOME");
    if (!home) { fprintf(stderr, "sb pack rpm: no HOME\n"); return 1; }
    char top[1200], specs[1300], sources[1300], rpms[1300];
    snprintf(top, sizeof(top), "%s/rpmbuild", home);
    snprintf(specs, sizeof(specs), "%s/SPECS", top);
    snprintf(sources, sizeof(sources), "%s/SOURCES", top);
    snprintf(rpms, sizeof(rpms), "%s/RPMS/x86_64", top);
    char builddir[1300];
    snprintf(builddir, sizeof(builddir), "%s/BUILD", top);
    ensure_dir(specs); ensure_dir(sources); ensure_dir(rpms); ensure_dir(builddir);

    char specp[1400];
    snprintf(specp, sizeof(specp), "%s/%s.spec", specs, name);
    FILE *sf = fopen(specp, "w");
    if (!sf) { perror("spec"); return 1; }
    fprintf(sf, "Name:           %s\n", name);
    fprintf(sf, "Version:        %s\n", version);
    fprintf(sf, "Release:        1%%{?dist}\n");
    fprintf(sf, "Summary:        %s\n", desc);
    fprintf(sf, "BuildArch:      x86_64\n");
    fprintf(sf, "License:        MIT\n");
    fprintf(sf, "Requires:       gtk3, webkit2gtk-4.1, libcurl, readline\n");
    fprintf(sf, "\n");
    fprintf(sf, "%%description\n%s — built with ShimbaBomb.\n\n", desc);
    fprintf(sf, "%%install\nmkdir -p %%{buildroot}/usr/local/bin\ncp %%{_sourcedir}/%s %%{buildroot}/usr/local/bin/%s\n\n", name, name);
    fprintf(sf, "%%files\n/usr/local/bin/%s\n", name);
    fclose(sf);

    char binsrc[1400], cmd[2400];
    snprintf(binsrc, sizeof(binsrc), "%s/%s", sources, name);
    copy_file(binpath, binsrc);
    chmod(binsrc, 0755);
    snprintf(cmd, sizeof(cmd), "rpmbuild -bb --quiet '%s' 2>&1 | tail -3", specp);
    int rc = system(cmd);
    unlink(binpath); unlink(binsrc);
    if (rc == 0) {
        printf("Packed %s/%s-%s-1.x86_64.rpm\n", rpms, name, version);
        printf("install with: sudo dnf install %s/%s-%s-1.x86_64.rpm\n", rpms, name, version);
        return 0;
    }
    return 1;
#else
    fprintf(stderr, "sb pack rpm: only supported on Linux\n");
    return 1;
#endif
}

static int handle_pack_msi(int argc, char **argv) {
    (void)argc;
    const char *src = argv[3] ? argv[3] : ".";
    char name[256]="", version[64]="", desc[512]="", maint[256]="";
    pack_read_manifest(src, name, sizeof(name), version, sizeof(version), desc, sizeof(desc), maint, sizeof(maint));
    if (name[0]=='\0') { fprintf(stderr, "sb pack msi: no [project] name in %s/shimba.toml\n", src); return 1; }
    if (version[0]=='\0') snprintf(version, sizeof(version), "1.0.0");
    fprintf(stderr, "sb pack msi: WiX-based MSI packaging\n");
    fprintf(stderr, "  This generates a Windows MSI installer using the WiX toolset.\n");
    fprintf(stderr, "  Requires: wix (https://wixtoolset.org/) on Windows or cross-compile.\n");
    fprintf(stderr, "  For now, use 'sb pack exe' + Inno Setup for Windows installers.\n");
    return 1;
}

static int handle_pack_exe(int argc, char **argv) {
    (void)argc;
    const char *src = argv[2];
    if (!src) { fprintf(stderr, "Usage: sb pack exe <file.sb> [out.exe]\n"); return 1; }
    char out[1200];
    if (argc >= 4) snprintf(out, sizeof(out), "%s", argv[3]);
    else {
        snprintf(out, sizeof(out), "%s", src);
        char *dot = strrchr(out, '.');
        if (dot && strcmp(dot, ".sb")==0) *dot='\0';
        strcat(out, ".exe");
    }
    if (!file_exists("/usr/bin/x86_64-w64-mingw32-gcc") && system("which x86_64-w64-mingw32-gcc >/dev/null 2>&1") != 0) {
        fprintf(stderr, "sb pack exe: x86_64-w64-mingw32-gcc not found\n");
        fprintf(stderr, "  install with: sudo apt install gcc-mingw-w64-x86-64\n");
        return 1;
    }
    char c_path[1100];
    snprintf(c_path, sizeof(c_path), "/tmp/sb_exe_%d.c", getpid());
    generate_c(src, c_path);
    // windows console build: no GUI/curl; winsock + static winpthreads
    char cmd[3600];
    snprintf(cmd, sizeof(cmd),
        "x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall "
        "-DSB_NO_GUI -DSB_NO_CURL "
        "-I%s/src -o %s %s "
        "%s/src/lexer.c %s/src/ast.c %s/src/value.c %s/src/parser.c %s/src/interpreter.c "
        "-lws2_32 -static -lm 2>&1",
        SB_SRC_DIR, out, c_path,
        SB_SRC_DIR, SB_SRC_DIR, SB_SRC_DIR, SB_SRC_DIR, SB_SRC_DIR,
        "");
    int rc = system(cmd);
    unlink(c_path);
    if (rc == 0) {
        printf("Packed %s (Windows x64 console)\n", out);
        printf("note: window/fetch natives are unavailable in console builds\n");
        return 0;
    }
    return 1;
}

static int handle_link(void) {
    const char *manifest = NULL;
    if (file_exists("shimba.toml")) manifest = "shimba.toml";
    else if (file_exists("sb.toml")) manifest = "sb.toml";
    else { fprintf(stderr, "sb link: no shimba.toml/sb.toml found (run 'sb init' first)\n"); return 1; }

    char name[256] = "";
    char entry[256] = "main";
    char *content = read_file(manifest);
    char *line = strtok(content, "\n");
    while (line) {
        char *n = strstr(line, "name");
        if (n) { char tmp[256]; if (sscanf(n, " name = \"%255[^\"]\"", tmp)==1) snprintf(name, sizeof(name), "%s", tmp); }
        char *e = strstr(line, "entry");
        if (e) { char tmp[256]; if (sscanf(e, " entry = \"%255[^\"]\"", tmp)==1) snprintf(entry, sizeof(entry), "%s", tmp); }
        line = strtok(NULL, "\n");
    }
    free(content);
    if (name[0]=='\0') { fprintf(stderr, "sb link: [project] name missing in %s\n", manifest); return 1; }

    const char *home = getenv("HOME");
    if (!home) { fprintf(stderr, "sb link: no HOME\n"); return 1; }
    char libsroot[1024], libdir[1200], entrydst[1300];
    snprintf(libsroot, sizeof(libsroot), "%s/.shimbabomb/libraries", home);
    snprintf(libdir, sizeof(libdir), "%s/%s", libsroot, name);
    ensure_dir(libsroot);
    ensure_dir(libdir);

    int copied = 0;
    DIR *d = opendir(".");
    if (!d) { fprintf(stderr, "sb link: cannot read .\n"); return 1; }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        size_t l = strlen(de->d_name);
        if (l>3 && strcmp(de->d_name+l-3,".sb")==0) {
            char srcf[1200], dstf[1400];
            snprintf(srcf, sizeof(srcf), "%s", de->d_name);
            snprintf(dstf, sizeof(dstf), "%s/%s", libdir, de->d_name);
            copy_file(srcf, dstf);
            printf("  + %s\n", de->d_name);
            copied++;
        }
    }
    closedir(d);

    (void)entry;

    if (copied == 0) { fprintf(stderr, "sb link: no .sb files found in project\n"); return 1; }
    printf("linked library '%s' (%d file(s))\n", name, copied);
    printf("use from any project:\n");
    printf("  pls bring %s.              # whole package (entry: %s.sb)\n", name, entry);
    printf("  pls bring \"%s/api\".       # or any single module\n", name);
    return 0;
}

static int handle_publish(int argc, char **argv) {
    const char *manifest = NULL;
    if (file_exists("shimba.toml")) manifest = "shimba.toml";
    else if (file_exists("sb.toml")) manifest = "sb.toml";
    else { fprintf(stderr, "sb: no shimba.toml/sb.toml found\n"); return 1; }

    // extract name + version from manifest
    char name[256] = "unnamed", version[64] = "0.1.0";
    char *content = read_file(manifest);
    char *line = strtok(content, "\n");
    while (line) {
        char *n = strstr(line, "name");
        if (n) { char tmp[256]; if (sscanf(n, " name = \"%255[^\"]\"", tmp)==1) snprintf(name, sizeof(name), "%s", tmp); }
        char *v = strstr(line, "version");
        if (v) { char tmp[64]; if (sscanf(v, " version = \"%63[^\"]\"", tmp)==1) snprintf(version, sizeof(version), "%s", tmp); }
        line = strtok(NULL, "\n");
    }
    free(content);

    if (argc >= 3 && strcmp(argv[2], "--dry-run") == 0) {
        printf("Would publish %s v%s from %s\n", name, version, manifest);
        return 0;
    }
    if (!file_exists(".git")) {
        fprintf(stderr, "sb publish: not a git repo. Run 'git init' first.\n");
        return 1;
    }
    char tag[128];
    snprintf(tag, sizeof(tag), "v%s", version);
    printf("Publishing %s %s...\n", name, version);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git add -A && git commit -m 'release %s' --allow-empty 2>&1 | tail -1", tag);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "git tag -f %s 2>/dev/null; git push --tags 2>&1 | tail -3", tag);
    int rc = system(cmd);
    if (rc == 0)
        printf("Published! Users can install with:\n  sb install <your-git-url> #%s\n", tag);
    return rc==0 ? 0 : 1;
}

static int run_sb_file(const char *path, int *passed, int *failed) {
    printf("== %s ==\n", path);
    fflush(stdout);
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "SB_TEST_MODE=1 sb '%s' 2>&1", path);
    int rc = system(cmd);
    (void)passed; (void)failed; // per-file counts come via output
    return WIFEXITED(rc) && WEXITSTATUS(rc)==0;
}

static int handle_test(void) {
    DIR *d = opendir(".");
    if (!d) { fprintf(stderr, "sb test: cannot open ./tests\n"); return 1; }
    // look in ./tests first, fall back to current dir
    struct dirent *de;
    char files[256][1024];
    int count = 0;
    const char *dir = file_exists("tests") ? "tests" : ".";
    closedir(d);
    d = opendir(dir);
    if (!d) return 1;
    while ((de = readdir(d)) != NULL && count < 256) {
        size_t l = strlen(de->d_name);
        if (l>3 && strcmp(de->d_name+l-3,".sb")==0 && de->d_name[0] != '.') {
            snprintf(files[count], sizeof(files[0]), "%s/%s", dir, de->d_name);
            count++;
        }
    }
    closedir(d);
    if (count == 0) { printf("No .sb test files found.\n"); return 0; }

    int suites_ok = 0, suites_fail = 0;
    for (int i = 0; i < count; i++) {
        if (run_sb_file(files[i], NULL, NULL)) suites_ok++;
        else suites_fail++;
    }
    printf("\n%d suite(s) passed, %d failed\n", suites_ok, suites_fail);
    return suites_fail ? 1 : 0;
}

static int handle_web(void) {
    printf("Serving on http://localhost:8080\n");
    printf("Press Ctrl+C to stop.\n");
    int rc = system("python3 -m http.server 8080 2>/dev/null || python -m SimpleHTTPServer 8080 2>/dev/null || echo 'No python found'");
    return rc;
}

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>

static const char *REPL_HTML =
"<!DOCTYPE html><html><head><meta charset='utf-8'><title>ShimbaBomb Web REPL</title>"
"<style>body{font-family:monospace;background:#14141e;color:#e0e0ef;max-width:900px;margin:2rem auto;padding:0 1rem}"
"h1{color:#f38ba8}textarea{width:100%;height:180px;background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;border-radius:6px;padding:.8em;font-family:monospace}"
"button{background:#89b4fa;color:#1e1e2e;border:0;padding:.5em 1.2em;border-radius:6px;font-weight:bold;cursor:pointer;margin-top:.5em}"
"#out{white-space:pre-wrap;background:#11111b;border-radius:6px;padding:1em;margin-top:1em;min-height:60px}</style></head><body>"
"<h1>ShimbaBomb Web REPL</h1>"
"<p>Type SB code (statements end with .)</p>"
"<textarea id='code'>SB\nsay \"hello\" plus \" world\".\ncount i from 1 to 3 then\nsay i.\nend</textarea><br>"
"<button onclick='run()'>Run</button>"
"<div id='out'></div>"
"<script>async function run(){const r=await fetch('/eval',{method:'POST',body:document.getElementById('code').value});"
"document.getElementById('out').textContent=await r.text();}</script></body></html>";

static int repl_server_loop(int port) {
    Interpreter interp;
    interp_init(&interp);
    interp_add_search_path(&interp, ".");
    interp_add_search_path(&interp, "./sb_modules");
    interp_add_search_path(&interp, "./std");
    interp_add_search_path(&interp, SB_STD_DIR);
    {
        const char *h_ = getenv("HOME");
        if (h_) {
            char libs_[1024];
            snprintf(libs_, sizeof(libs_), "%s/.shimbabomb/libraries", h_);
            interp_add_search_path(&interp, libs_);
        }
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    listen(sfd, 8);
    printf("Web REPL on http://localhost:%d\n", port);

    while (1) {
        int c = accept(sfd, NULL, NULL);
        if (c < 0) continue;
        char req[16384];
        ssize_t n = recv(c, req, sizeof(req)-1, 0);
        if (n <= 0) { close(c); continue; }
        req[n] = '\0';
        char method[8]="GET", path[256]="/";
        sscanf(req, "%7s %255s", method, path);

        if (strcmp(path, "/")==0 && strcmp(method,"GET")==0) {
            char hdr[128];
            snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", strlen(REPL_HTML));
            send(c, hdr, strlen(hdr), 0);
            send(c, REPL_HTML, strlen(REPL_HTML), 0);
        } else if (strcmp(path, "/eval")==0 && strcmp(method,"POST")==0) {
            char *body = strstr(req, "\r\n\r\n");
            const char *code = body ? body+4 : "";
            // capture stdout via temp file
            FILE *tmpf = tmpfile();
            int saved = dup(STDOUT_FILENO);
            dup2(fileno(tmpf), STDOUT_FILENO);

            Parser parser;
            parser_init(&parser, code);
            AstNode *prog = parser_parse(&parser);
            if (parser.had_error) {
                fprintf(stderr, "parse error: %s\n", parser.error_msg);
                fflush(stderr); fflush(stdout);
            } else {
                Value result = interp_run(&interp, prog);
                val_free(&result);
                if (interp.had_error) {
                    fprintf(stderr, "error: %s\n", interp.error_msg);
                    interp.had_error = 0;
                    fflush(stderr);
                }
                fflush(stdout); fflush(stderr);
            }
            node_free(prog);
            // restore stdout
            dup2(saved, STDOUT_FILENO);
            close(saved);
            // read captured
            char outbuf[16384]; size_t olen = 0;
            rewind(tmpf);
            olen = fread(outbuf, 1, sizeof(outbuf)-1, tmpf);
            outbuf[olen]='\0';
            fclose(tmpf);

            char hdr[128];
            snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", olen);
            send(c, hdr, strlen(hdr), 0);
            send(c, outbuf, olen, 0);
        } else {
            const char *nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nnot found";
            send(c, nf, strlen(nf), 0);
        }
        close(c);
    }
    return 0;
}
#endif

static int handle_web_repl(int port) {
#ifdef __linux__
    return repl_server_loop(port);
#else
    (void)port;
    fprintf(stderr, "web repl not supported here\n");
    return 1;
#endif
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    // set process title so Task Manager shows "sb.v1.11.0" not "sb.real"
#ifndef _WIN32
    {
        char title[64] = "sb";
        FILE *vf = fopen("VERSION", "rb");
        if (!vf) {
            char vpath[1024];
            snprintf(vpath, sizeof(vpath), "%s/VERSION", SB_SRC_DIR);
            vf = fopen(vpath, "rb");
        }
        if (vf) {
            char ver[32];
            if (fgets(ver, sizeof(ver), vf)) {
                ver[strcspn(ver, "\r\n")] = '\0';
                snprintf(title, sizeof(title), "sb.%s", ver);
            }
            fclose(vf);
        }
        prctl(PR_SET_NAME, title);
    }
#endif

    if (argc >= 2 && strcmp(argv[1], "update") == 0) {
        return handle_update();
    }
    if (argc >= 2 && strcmp(argv[1], "install") == 0) {
        return handle_install(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "init") == 0) {
        return handle_init();
    }
    if (argc >= 2 && strcmp(argv[1], "watch") == 0) {
        return handle_watch();
    }
    if (argc >= 2 && strcmp(argv[1], "run") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: sb run <file>\n"); return 1; }
        argv[1] = argv[2];
        if (argc == 3) argc = 2;
        else { argv[2] = argv[3]; argc--; }
    }
    if (argc >= 2 && strcmp(argv[1], "fmt") == 0) {
        return handle_fmt(argc >= 3 ? argv[2] : NULL);
    }
    if (argc >= 2 && strcmp(argv[1], "pack") == 0) {
        if (argc >= 3 && strcmp(argv[2], "exe") == 0) return handle_pack_exe(argc-1, argv+1);
        if (argc >= 3 && strcmp(argv[2], "msi") == 0) return handle_pack_msi(argc-1, argv+1);
        return handle_pack(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "packexe") == 0) {
        return handle_pack_exe(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "link") == 0) {
        return handle_link();
    }
    if (argc >= 2 && strcmp(argv[1], "publish") == 0) {
        return handle_publish(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "test") == 0) {
        return handle_test();
    }
    if (argc >= 2 && strcmp(argv[1], "doc") == 0) {
        return handle_doc(argc >= 3 ? argv[2] : NULL);
    }
    if (argc >= 2 && strcmp(argv[1], "lsp") == 0) {
        return handle_lsp();
    }
    if (argc >= 2 && strcmp(argv[1], "web") == 0) {
        if (argc >= 3 && strcmp(argv[2], "--repl") == 0)
            return handle_web_repl(argc >= 4 ? atoi(argv[3]) : 8080);
        if (argc >= 4 && strcmp(argv[2], "--repl-port") == 0)
            return handle_web_repl(atoi(argv[3]));
        return handle_web();
    }
    if (argc >= 2 && (strcmp(argv[1], "--help")==0 || strcmp(argv[1], "-h")==0 || strcmp(argv[1], "help")==0)) {
        print_help(); return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "build")==0) {
        if (argc < 3) { fprintf(stderr, "Usage: sb build <file.sb>  — compile to binary\n"); return 1; }
        const char *src_path = argv[2];
        char out_path[1024];
        strncpy(out_path, src_path, sizeof(out_path)-1);
        out_path[sizeof(out_path)-1]='\0';
        char *dot = strrchr(out_path, '.'); if (dot && strcmp(dot,".sb")==0) *dot='\0';
        return build_to(src_path, out_path);
    }

    int interactive = 0;
    int debug_mode = 0;
    int noconsole = 0;
    int watch_mode = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) interactive = 1;
        else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) debug_mode = 1;
        else if (strcmp(argv[i], "--watch") == 0) watch_mode = 1;
        else if (strcmp(argv[i], "--noconsole")==0 || strcmp(argv[i], "--no-console")==0 ||
                 strcmp(argv[i], "--nocon")==0 || strcmp(argv[i], "--nocil")==0 ||
                 strcmp(argv[i], "--window")==0 || strcmp(argv[i], "-w")==0) noconsole = 1;
        else if (argv[i][0] != '-' && !path) path = argv[i];
        else if (strcmp(argv[i], "--help")==0) { print_help(); return 0; }
    }
    // --noconsole: fork to background, detach console, window stays (Ketiwe / any GUI)
    // --debug works with --noconsole: child keeps debugger, parent detaches
#ifndef _WIN32
    if (noconsole && path) {
        pid_t pid = fork();
        if (pid < 0) { perror("sb --noconsole fork"); return 1; }
        if (pid > 0) {
            printf("[sb] %s → background pid %d (no console)\n", path, (int)pid);
            return 0;
        }
        setsid();
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            // keep stdout/stderr for debugger output and GUI errors
            if (fd > 2) close(fd);
        }
    }
#endif

    Interpreter interp;
    interp_init(&interp);
    if (path) snprintf(interp.main_path, sizeof(interp.main_path), "%s", path);
    else interp.main_path[0] = '\0';
    if (debug_mode) {
        interp.debug = 1;
        printf("ShimbaBomb debugger — n=next, c=continue, p <var>, where, q\n");
    }
    char base_dir[1024] = ".";
    int has_path = (path != NULL);
    if (has_path) dirname_of(path, base_dir, sizeof(base_dir));

    interp_add_search_path(&interp, base_dir);
    char mod_dir[1024]; snprintf(mod_dir, sizeof(mod_dir), "%s/sb_modules", base_dir);
    interp_add_search_path(&interp, mod_dir);
    interp_add_search_path(&interp, ".");
    interp_add_search_path(&interp, "./sb_modules");
    interp_add_search_path(&interp, "./std");
    interp_add_search_path(&interp, SB_STD_DIR);
    {
        const char *h_ = getenv("HOME");
        if (h_) {
            char libs_[1024];
            snprintf(libs_, sizeof(libs_), "%s/.shimbabomb/libraries", h_);
            interp_add_search_path(&interp, libs_);
        }
    }
    interp_add_search_path(&interp, "sb_modules");

    if (!path) {
        repl_with(&interp);
        interp_free(&interp);
        return 0;
    }

    // --watch: loop, re-run on file change
    if (watch_mode) {
        char *source = NULL;
        AstNode *program = NULL;
        time_t last_mod = 0;
        struct stat st;
        int running = 1;
        while (running) {
            if (stat(path, &st) == 0 && st.st_mtime != last_mod) {
                last_mod = st.st_mtime;
                if (source) { free(source); }
                if (program) { node_free(program); }
                source = read_file(path);
                Parser parser;
                parser_init(&parser, source);
                program = parser_parse(&parser);
                if (parser.had_error) {
                    fprintf(stderr, "sb: parse error in %s: %s\n", path, parser.error_msg);
                    free(source); source = NULL; program = NULL;
                    sleep(1); continue;
                }
                printf("[watch] running %s\n", path);
                Value result = interp_run(&interp, program);
                if (interp.had_error) {
                    fprintf(stderr, "error (sb line %d): %s\n", interp.error_line, interp.error_msg);
                }
                val_free(&result);
                interp_free(&interp);
                interp_init(&interp);
                if (path) snprintf(interp.main_path, sizeof(interp.main_path), "%s", path);
                interp_add_search_path(&interp, base_dir);
                char mod_dir2[1024]; snprintf(mod_dir2, sizeof(mod_dir2), "%s/sb_modules", base_dir);
                interp_add_search_path(&interp, mod_dir2);
                interp_add_search_path(&interp, ".");
                interp_add_search_path(&interp, "./sb_modules");
                interp_add_search_path(&interp, "./std");
                interp_add_search_path(&interp, SB_STD_DIR);
            }
            sleep(1);
        }
        if (source) free(source);
        if (program) node_free(program);
        interp_free(&interp);
        return 0;
    }

    char *source = read_file(path);
    Parser parser;
    parser_init(&parser, source);
    AstNode *program = parser_parse(&parser);
    if (parser.had_error) {
        fprintf(stderr, "sb: parse error in %s: %s\n", path, parser.error_msg);
        node_free(program); free(source);
        interp_free(&interp);
        return 1;
    }

    Value result = interp_run(&interp, program);
    int test_failed = interp.asserts_failed > 0;
    if (interp.had_error) {
        fprintf(stderr, "error (sb line %d): %s\n", interp.error_line, interp.error_msg);
        if (interp.error_trace[0]) fprintf(stderr, "%s\n", interp.error_trace);
    }
    val_free(&result);
    node_free(program);
    free(source);

    if (interactive && getenv("SB_NO_REPL") == NULL) {
        interp.had_error = 0;
        repl_with(&interp);
    }
    interp_free(&interp);
    return test_failed ? 1 : 0;
}
