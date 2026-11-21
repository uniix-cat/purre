# Purre 🐾

> Lightweight, modern modal terminal text editor written in clean C (<2500 lines) with built-in LSP, hybrid Tab autocomplete, static analysis, Visual mode, syntax highlighting, and LuaJIT extensibility — ready out of the box with zero configuration.

[English](README.md) | [Русский](README_RU.md) | [Lua Plugins](docs/PLUGINS.md)

---

### Why Purre?
Because honestly, none of the current editors work for me — they all swing into two extremes:
- **neovim**: requires way too much tweaking even when using pre-made distributions
- **nano, micro**: don't even have proper LSP, what are we even talking about?

My tokens are free anyway, so ~~Gemini~~ I wrote the Purre editor, combining the ~~worst~~ best of both worlds.

### Key Highlights

* **Zero-Config LSP**: Automatic language server discovery for ~30 programming languages (`clangd`, `pyright`, `rust-analyzer`, `gopls`, `zls`, etc.). No plugin managers or manual config files needed.
* **Compact & Ultra-Fast**: Under **2,500 lines** of modular C99 and a **~55 KB** binary. Blazing fast startup (< 2 milliseconds).
* **Non-blocking Asynchronous I/O**: Language servers and linters run asynchronously via subprocess pipes (`poll`) without freezing the terminal UI.
* **Ergonomic Home-Row Navigation (`jkl;`)**:
  - The obsolete `hjkl` layout (created in the 1970s for Lear Siegler terminals) is replaced by **`jkl;`** (`j`: left, `k`: down, `l`: up, `;`: right). Your fingers stay naturally centered on the home row without shifting your hand left.
  - Insert mode toggle is **`a`** directly at the cursor position.
  - Visual mode selection is toggled with **`v`**.
  - Fail-safe universal hotkeys: `Ctrl+S` (save), `Ctrl+Q` (quit), `Ctrl+C` (escape to Normal Mode).
* **Full Visual Mode Selection**:
  - Highlight text character by character or across multiple lines.
  - Quick yank (`y`), delete/cut (`d` / `x`), change (`c`), and paste (`p` / `P`).
* **Hybrid Tab Autocomplete**:
  - Semantic completion from running LSP servers.
  - Instant automatic fallback: buffer word scanning + language keywords if an LSP is absent or still initializing.
  - Cycle candidates with `Tab` or `↓`/`Ctrl+N`, cycle back with `↑`/`Ctrl+P`, confirm with `Enter`.
* **Built-in Static Analysis**: Real-time linting with `clang-tidy`, `cppcheck`, `shellcheck`, and `ruff` on save or via `:check`.
* **Nerd Fonts Out of the Box**: Status bar cat badge (`󰄛`), filetype icons (``, ``, ``), and gutter diagnostic marks (`󰅚` error, `󰀪` warning, `󰋽` info).
* **Smart Auto-Pairs & Indentation**:
  - Auto-closing pairs: `()`, `[]`, `{}`, `""`, `''`.
  - Smart overtyping (skips duplicate closing characters).
  - Pair deletion on Backspace.
  - Smart auto-indent on `Enter` and auto-unindent when typing `}`.
* **Full UTF-8 & Cyrillic Support**: Native multi-byte UTF-8 handling. Russian text edits seamlessly without cursor displacement.
* **LuaJIT Scripting**: Embedded LuaJIT 2.1 allows writing custom commands, hooks, and extensions with near-native performance ([Lua Plugin Guide](docs/PLUGINS.md)).

---

## How to Install

### Prerequisites
- C compiler (`gcc` or `clang`)
- `make`
- `luajit` (`libluajit-5.1-dev` on Debian and Ubuntu, `luajit` on Arch Linux)
- `tree-sitter` library
- A [Nerd Font](https://www.nerdfonts.com/) enabled terminal (e.g. JetBrains Mono Nerd Font, FiraCode Nerd Font, Alacritty, Kitty, WezTerm).

### Installation

```bash
git clone https://github.com/uniix-cat/purre.git
cd purre
make

# System-wide install (in root)
sudo make install

# User-only install (no root/sudo needed)
make install PREFIX=$HOME/.local
```

Launch is vim-like, no surprises here:
```bash
purre [filename]
```

---

## Hotkeys

### Universal (Work in ALL modes)
| Shortcut | Action |
| :--- | :--- |
| `Ctrl + S` | Save file immediately |
| `Ctrl + Q` | Quit Purre (prompts if there are unsaved changes) |
| `Ctrl + C` / `Esc` | Return to **Normal Mode** / Close completion popup |

### Normal Mode (Default Command Mode)
| Key | Action |
| :--- | :--- |
| **`j`** | Move cursor **left** (index finger) |
| **`k`** | Move cursor **down** (middle finger) |
| **`l`** | Move cursor **up** (ring finger) |
| **`;`** | Move cursor **right** (pinky finger) |
| **`a`** | Enter **Insert Mode** directly at cursor |
| **`v`** | Enter **Visual Mode** (selection mode) |
| `w` / `b` | Move word forward / backward |
| `0` / `$` | Move to beginning / end of line |
| `g` / `G` | Jump to top / bottom of buffer |
| `o` / `O` | Insert newline below / above and switch to Insert Mode |
| `x` | Delete character under cursor |
| `d d` | Delete current line entirely |
| `p` / `P` | Paste yanked text after / before cursor |
| `K` | LSP Hover (show documentation and types under cursor) |
| `:` | Open Command Mode |

### Visual Mode (`v`)
| Key | Action |
| :--- | :--- |
| `j` / `k` / `l` / `;` | Expand / shrink selection boundary (left, down, up, right) |
| `w` / `b` / `0` / `$` | Jump word forward / backward / line start / line end |
| `y` | **Yank (copy)** selected text to buffer |
| `d` / `x` | **Delete / cut** selected text |
| `c` | **Change** — delete selection and switch to Insert Mode |
| `Esc` / `Ctrl + C` / `v` | Cancel selection and return to Normal Mode |

### Insert Mode (`a`)
| Key | Action |
| :--- | :--- |
| `Tab` | Open autocomplete / Cycle through options forward |
| `↓` / `Ctrl + N` | Select next completion candidate |
| `↑` / `Ctrl + P` | Select previous completion candidate |
| `Enter` | Accept selected completion / Insert smart newline with indentation |
| `Backspace` | Delete character (deletes pair if cursor is inside `()`) |
| `Esc` / `Ctrl + C` | Close autocomplete or return to Normal Mode |

### Command Mode (`:`)
| Command | Action |
| :--- | :--- |
| `:w [filename]` | Save buffer |
| `:q` | Quit |
| `:q!` | Force quit without saving |
| `:wq` | Save and quit |
| `:e <path>` | Open file in buffer |
| `:check` | Run static analyzer on current file (`clang-tidy`, `ruff`, etc.) |
| `:lsp start` | Start language server for current filetype |
| `:lsp status` | Display active LSP server status |
| `:lsp hover` | Show symbol documentation (same as `K` key) |
| `:lsp def` | Go to definition under cursor |
| `:lsp list` | List supported LSP languages |
| `:lsp set <lang> <cmd>` | Register or override a custom LSP command on the fly |
| `:lua <code>` | Evaluate Lua code directly inside the editor |
| `:help` | Show quick cheat sheet in the status bar |

---

## 🔌 Supported Languages & LSPs

Purre automatically detects and connects to installed servers in your `$PATH`:

| Language | Primary LSP | Fallback / Alternatives | Linters & Analyzers |
| :--- | :--- | :--- | :--- |
| **C / C++** | `clangd` | `ccls` | `clang-tidy`, `cppcheck` |
| **Python** | `pyright` | `pyright-langserver`, `pylsp`, `ruff` | `ruff`, `flake8` |
| **Rust** | `rust-analyzer` | — | `clippy` |
| **Go** | `gopls` | — | `golangci-lint` |
| **TypeScript / JS**| `typescript-language-server` | `vtsls`, `deno` | `eslint` |
| **Lua** | `lua-language-server` | — | `luacheck` |
| **HTML / CSS / JSON**| `vscode-html-language-server` | `vscode-css-language-server` | — |
| **Shell** | `bash-language-server` | — | `shellcheck` |
| **Zig** | `zls` | — | — |
| **PHP** | `intelephense` | `phpactor` | `phpstan` |

To install an LSP on Arch Linux:
```bash
sudo pacman -S clang          # C/C++ (clangd)
sudo pacman -S pyright        # Python
sudo pacman -S rust-analyzer  # Rust
sudo pacman -S gopls          # Go
```

---

## 🧩 Plugin Development (if anyone actually ever needs this editor)

Purre embeds LuaJIT 2.1. You can add custom commands, formatters, and status badges directly in `runtime/init.lua`.

See the full [Lua Plugin Documentation](docs/PLUGINS.md) for API details and code examples.

---

## 📜 License

GNU General Public License v3.0 or later (GPL-3.0-or-later). Crafted with ❤️ by [uniix-cat](https://github.com/uniix-cat). (and yeah, Gemini sloped all the code, let's be real)
