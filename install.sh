#!/bin/sh
# ============================================================
# ShimbaBomb installer
#   works on: Linux (apt/dnf/pacman/zypper), macOS (brew), WSL
#
#   ./install.sh                 install deps + build + install
#   ./install.sh --no-deps       skip package-manager step
#   ./install.sh --prefix DIR    default: $HOME/.local
#   ./install.sh --uninstall     remove installed files
# ============================================================
set -e

PREFIX="$HOME/.local"
INSTALL_DEPS=1
UNINSTALL=0

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2;;
        --no-deps) INSTALL_DEPS=0; shift;;
        --uninstall) UNINSTALL=1; shift;;
        --help|-h) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
        *) echo "install.sh: unknown option $1"; exit 1;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src"
STD_DIR="$SCRIPT_DIR/std"

if [ ! -f "$SRC_DIR/main.c" ]; then
    # piped from web — clone source to temp dir, then re-run from there
    if command -v git >/dev/null 2>&1; then
        TMPDIR="$(mktemp -d)"
        echo "Downloading ShimbaBomb source..."
        git clone --depth 1 https://github.com/Shimba-crypto/shimbabomb.git "$TMPDIR/shimbabomb" 2>/dev/null
        if [ -f "$TMPDIR/shimbabomb/src/main.c" ]; then
            exec sh "$TMPDIR/shimbabomb/install.sh" "$@"
        fi
    fi
    echo "install.sh: run this from the shimbabomb repo root (src/ not found)"
    echo "Or install git first, then: curl -sL https://shimbabomb.pages.dev/install.sh | bash"
    exit 1
fi

BIN_DIR="$PREFIX/bin"
DATA_DIR="$PREFIX/share/shimbabomb"

if [ "$UNINSTALL" = "1" ]; then
    echo "Removing $BIN_DIR/sb and $DATA_DIR ..."
    rm -f "$BIN_DIR/sb"
    rm -rf "$DATA_DIR"
    echo "Done. (libraries in ~/.shimbabomb were left alone)"
    exit 0
fi

# ---------- detect OS ----------
OS="unknown"
case "$(uname -s)" in
    Linux*) OS="linux";;
    Darwin*) OS="macos";;
    *) echo "install.sh: unsupported OS $(uname -s) — try --no-deps if deps exist";;
esac
ARCH="$(uname -m)"

echo "== ShimbaBomb installer =="
echo "   OS: $OS ($ARCH)   prefix: $PREFIX"

# ---------- deps ----------
have() { command -v "$1" >/dev/null 2>&1; }

if [ "$INSTALL_DEPS" = "1" ] && [ "$OS" = "linux" ]; then
    MISSING=""
    have gcc       || MISSING="$MISSING gcc"
    have pkg-config || MISSING="$MISSING pkg-config"
    pkg-config --exists gtk+-3.0 2>/dev/null || MISSING="$MISSING libgtk-3-dev"
    pkg-config --exists libcurl 2>/dev/null || MISSING="$MISSING libcurl-devel"
    have curl      || true
    ldconfig -p 2>/dev/null | grep -q libreadline || MISSING="$MISSING libreadline-dev"
    WEBKIT_OK=0
    pkg-config --exists webkit2gtk-4.1 2>/dev/null && WEBKIT_OK=1
    pkg-config --exists webkit2gtk-4.0 2>/dev/null && WEBKIT_OK=1
    [ "$WEBKIT_OK" = "0" ] && MISSING="$MISSING libwebkit2gtk-4.1-dev"

    if [ -n "$MISSING" ]; then
        echo "Installing missing packages:$MISSING"
        if have apt-get; then
            sudo apt-get update -qq
            PKGS=$(echo "$MISSING" | tr ' ' '\n' | sed 's/libcurl-devel/libcurl4-openssl-dev/' | tr '\n' ' ')
            sudo apt-get install -y -qq gcc pkg-config libgtk-3-dev libcurl4-openssl-dev libreadline-dev $PKGS
        elif have dnf; then
            sudo dnf install -y gcc pkg-config gtk3-devel webkit2gtk4.1-devel libcurl-devel readline-devel
        elif have pacman; then
            sudo pacman -S --needed --noconfirm base-devel gtk3 webkit2gtk-4.1 curl readline
        elif have zypper; then
            sudo zypper install -y gcc pkg-config gtk3-devel webkit2gtk4.1-devel libcurl-devel readline-devel
        else
            echo "WARNING: unknown package manager; continuing and hoping deps exist"
        fi
    else
        echo "All dependencies present."
    fi
elif [ "$INSTALL_DEPS" = "1" ] && [ "$OS" = "macos" ]; then
    if ! have brew; then
        echo "ERROR: Homebrew not found. Install from https://brew.sh then rerun."
        exit 1
    fi
    echo "Installing deps via brew (this can take a while — webkit2gtk is big)..."
    brew list pkg-config >/dev/null 2>&1 || brew install pkg-config
    brew list gtk+3     >/dev/null 2>&1 || brew install gtk+3
    brew list webkit2gtk >/dev/null 2>&1 || brew install webkit2gtk
    brew list curl      >/dev/null 2>&1 || brew install curl
    brew list readline  >/dev/null 2>&1 || brew install readline
fi

# ---------- build ----------
echo "== building compiler =="
GTK_CFLAGS=$(pkg-config --cflags gtk+-3.0 2>/dev/null || echo "")
GTK_LIBS=$(pkg-config --libs gtk+-3.0 2>/dev/null || echo "")
WEBKIT_CFLAGS=$(pkg-config --cflags webkit2gtk-4.1 2>/dev/null || pkg-config --cflags webkit2gtk-4.0 2>/dev/null || echo "")
WEBKIT_LIBS=$(pkg-config --libs webkit2gtk-4.1 2>/dev/null || pkg-config --libs webkit2gtk-4.0 2>/dev/null || echo "")
CURL_CFLAGS=$(pkg-config --cflags libcurl 2>/dev/null || echo "")
CURL_LIBS=$(pkg-config --libs libcurl 2>/dev/null || echo "-lcurl")
X11_CFLAGS=$(pkg-config --cflags x11 2>/dev/null || echo "")
X11_LIBS=$(pkg-config --libs x11 2>/dev/null || echo "-lX11")

TMPBIN="$(mktemp -d)/sb"
gcc -std=c11 -Wall -O2 \
    $GTK_CFLAGS $WEBKIT_CFLAGS $CURL_CFLAGS $X11_CFLAGS \
    -DSB_STD_DIR="\"$DATA_DIR/std\"" \
    -DSB_SRC_DIR="\"$DATA_DIR\"" \
    -o "$TMPBIN" \
    "$SRC_DIR/lexer.c" "$SRC_DIR/ast.c" "$SRC_DIR/value.c" \
    "$SRC_DIR/parser.c" "$SRC_DIR/interpreter.c" "$SRC_DIR/main.c" \
    "$SRC_DIR/ketiwe.c" \
    -Wl,--as-needed $GTK_LIBS $WEBKIT_LIBS $CURL_LIBS $X11_LIBS -lreadline -lm -ldl

# ---------- install ----------
mkdir -p "$BIN_DIR" "$DATA_DIR/src" "$DATA_DIR/std"
cp "$TMPBIN" "$BIN_DIR/sb"
chmod +x "$BIN_DIR/sb"
cp "$SRC_DIR"/*.c "$SRC_DIR"/*.h "$DATA_DIR/src/"
cp "$STD_DIR"/*.sb "$DATA_DIR/std/" 2>/dev/null || true
cp "$SCRIPT_DIR/VERSION" "$DATA_DIR/VERSION" 2>/dev/null || echo "v1.13.0" > "$DATA_DIR/VERSION"

# bundle vendor libs for self-contained install (makes sb a real self-contained lang)
VENDOR_LIB="$DATA_DIR/lib"
if command -v ldd >/dev/null 2>&1 && [ -f "$BIN_DIR/sb" ]; then
    mkdir -p "$VENDOR_LIB"
    for lib in $(ldd "$BIN_DIR/sb" 2>&1 | awk '{print $3}' | grep '^/usr/lib' | sort -u); do
        base=$(basename "$lib")
        case "$base" in libc.so*|libm.so*|libpthread.so*|ld-linux*|libdl.so*|librt.so*|libresolv.so*) continue ;; esac
        cp -L -n "$lib" "$VENDOR_LIB/" 2>/dev/null || true
    done
    if [ -f "$VENDOR_LIB/libgtk-3.so.0" ]; then
        mv "$BIN_DIR/sb" "$BIN_DIR/sb.${VER}"
        cat > "$BIN_DIR/sb" <<EOSBWRAP
#!/bin/bash
VENDOR="\$HOME/.local/share/shimbabomb/lib"
if [ -d "\$VENDOR" ]; then export LD_LIBRARY_PATH="\$VENDOR:\$LD_LIBRARY_PATH"; fi
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
exec -a sb "\$DIR/sb.${VER}" "\$@"
EOSBWRAP
        chmod +x "$BIN_DIR/sb"
    fi
fi

# shellcheck disable=SC2016
echo 'export PATH="$HOME/.local/bin:$PATH"' > /dev/null # hint below

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *)
        echo ""
        echo "NOTE: $BIN_DIR is not on your PATH."
        echo "Add this to ~/.bashrc or ~/.zshrc:"
        echo "  export PATH=\"$BIN_DIR:\$PATH\""
        ;;
esac

VER=$(cat "$SCRIPT_DIR/VERSION" 2>/dev/null || echo "v1.10.0")
echo ""
echo "== ShimbaBomb $VER installed =="
echo "   binary : $BIN_DIR/sb"
echo "   stdlib : $DATA_DIR/std"
echo "   sources: $DATA_DIR/src (for 'sb build')"
echo ""
echo "Try it:"
echo "   sb                       # REPL"
echo "   sb examples/hello.sb     # run a file"
echo "   sb init                  # new project"
