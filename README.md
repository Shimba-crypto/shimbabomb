<p align="center">
  <img src="assets/logo.svg" width="96" height="96" alt="ShimbaBomb logo">
</p>

<h1 align="center">ShimbaBomb (SB)</h1>
<p align="center">English-like scripting that compiles to native C.</p>

```sb
SB
set x to 10.
say "n=${x}".
```

## Install

```bash
./install.sh                 # deps + build + install to ~/.local/bin/sb
./install.sh --no-deps       # skip deps
```

## Usage

```bash
sb file.sb              # run
sb -i file.sb           # run and stay in REPL
sb init                 # scaffold shimba.toml + main.sb
sb fmt file.sb          # format
sb watch                # re-run on .sb change
sb web                  # serve dir on http://localhost:8080
sb build file.sb        # compile to binary
sb install <pkg>        # install to sb_modules/
sb pack deb|rpm|exe     # package
```

## Syntax

Every statement ends with `.` Files start with `SB`.

See `sb-ai-test/PROMPT_FOR_AI.md` and `~/.config/opencode/skills/shimbabomb/SKILL.md` for the no-cheating spec.

## Std

`std/` — maths, lists, strings, files, crypto, datetime, ui, shimgui, fp

## Docs

Hosted at `https://shimbabomb.pages.dev` (Cloudflare Pages).

## License

MIT
