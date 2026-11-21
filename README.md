# Purre 🐾

> Lightweight, modern modal terminal text editor written in clean C (<2500 lines) with built-in LSP, hybrid Tab autocomplete, static analysis, Visual mode, syntax highlighting, and LuaJIT extensibility — ready out of the box with zero configuration.

[English](README.md) | [Русский](README_RU.md) | [Lua Plugins](docs/PLUGINS.md)

---

## 🎯 Why Purre?

Modern text editing often feels polarized:
- **Neovim distributions** (`LazyVim`, `LunarVim`, `AstroNvim`) require dozens of fragile plugins, complex Lua package managers (`lazy.nvim`, `mason.nvim`), and gigabytes of dependencies that break on routine updates.
- **Micro and Nano** are simple, but lack built-in LSP intelligence, semantic autocompletion, and real-time compiler diagnostics.

**Purre bridges this gap.** It delivers the full power of a modern IDE in a lightning-fast, self-contained terminal binary with zero setup.

### Key Highlights

* **Zero-Config LSP**: Automatic language server discovery for ~30 programming languages (`clangd`, `pyright`, `rust-analyzer`, `gopls`, `zls`, etc.). No plugin managers or manual config files needed.
* **Compact & Ultra-Fast**: Under **2,500 lines** of modular C99 and a **~55 KB** binary. Blazing fast startup (< 2 milliseconds).
* **Non-blocking Asynchronous I/O**: Language servers and linters run asynchronously via subprocess pipes without freezing the terminal UI.
* **Ergonomic Home-Row Navigation (`jkl;`)**:
  - The obsolete `hjkl` layout from 1970s Lear Siegler terminals is replaced by **`jkl;`** (`j`: left, `k`: down, `l`: up, `;`: right). Your fingers stay naturally centered on the home row without shifting left.
  - Insert mode toggle is **`a`** directly at the cursor position.
  - Visual mode selection is toggled with **`v`**.
  - Fail-safe universal hotkeys: `Ctrl+S` (save), `Ctrl+Q` (quit), `Ctrl+C` (escape to Normal Mode).
* **Full Visual Mode Selection**:
  - Highlight text character by character or across multiple lines.
  - Quick yank (`y`), delete/cut (`d` / `x`), change (`c`), and paste (`p` / `P`).
* **Hybrid Tab Autocomplete**:
  - Semantic completion from running LSP servers.
  - Instant automatic fallback to buffer word scanning and language keywords if an LSP is absent.
  - Cycle candidates with `Tab` or `↓`/`Ctrl+N`, cycle back with `↑`/`Ctrl+P`, confirm with `Enter`.
* **Built-in Static Analysis**: Real-time linting with `clang-tidy`, `cppcheck`, `shellcheck`, and `ruff`.
* **Nerd Fonts Out of the Box**: Status bar cat badge (`󰄛`), filetype icons (``, ``, ``), and gutter diagnostic marks (`󰅚` error, `󰀪` warning, `󰋽` info).
* **Smart Auto-Pairs & Indentation**:
  - Auto-closing pairs: `()`, `[]`, `{}`, `""`, `''`.
  - Smart overtyping (skips duplicate closing characters).
  - Pair deletion on Backspace.
  - Smart auto-indent on `Enter` and auto-unindent when typing `}`.
* **Full UTF-8 & Cyrillic Support**: Native multi-byte UTF-8 handling. Russian text edits seamlessly without cursor displacement.
* **LuaJIT Scripting**: Embedded LuaJIT allows writing custom commands, hooks, and extensions with near-native performance ([Lua Plugin Guide](docs/PLUGINS.md)).

---

## 🚀 Quick Start

### Prerequisites
- C compiler (`gcc` or `clang`)
- `make`
- `luajit` (`libluajit-5.1-dev` on Debian/Ubuntu, `luajit` on Arch Linux)
- `tree-sitter` library
- A [Nerd Font](https://www.nerdfonts.com/) enabled terminal (e.g. JetBrains Mono Nerd Font, FiraCode Nerd Font, Alacritty, Kitty, WezTerm).

### Build & Install

```bash
# Clone the repository
git clone https://github.com/uniix-cat/purre.git
cd purre

# Compile (takes < 2 seconds)
make

# Option A: System-wide install (executable in /usr/local/bin, runs from anywhere)
sudo make install

# Option B: User-only install (in ~/.local/bin, no sudo required)
make install PREFIX=$HOME/.local

# Launch Purre from anywhere!
purre [filename]
```

---

## ⌨️ Keybindings Reference

### Universal (Work in ALL modes)
| Shortcut | Action |
| :--- | :--- |
| `Ctrl + S` | Save file immediately |
| `Ctrl + Q` | Quit Purre (prompts if there are unsaved changes) |
| `Ctrl + C` / `Esc` | Return to **Normal Mode** / Close completion popup |

### Normal Mode (Default)
| Key | Action |
| :--- | :--- |
| **`j`** | Move cursor **left** (index finger) |
| **`k`** | Move cursor **down** (middle finger) |
| **`l`** | Move cursor **up** (ring finger) |
| **`;`** | Move cursor **right** (pinky finger) |
| **`a`** | Enter **Insert Mode** at cursor |
| **`v`** | Enter **Visual Mode** to select text |
| `w` / `b` | Move word forward / backward |
| `0` / `$` | Move to beginning / end of line |
| `g` / `G` | Jump to top / bottom of buffer |
| `o` / `O` | Insert newline below / above and switch to Insert Mode |
| `x` | Delete character under cursor |
| `d d` | Delete current line |
| `p` / `P` | Paste yanked text after / at cursor |
| `K` | LSP Hover (show documentation and types under cursor) |
| `:` | Open Command Mode |

### Visual Mode (`v`)
| Key | Action |
| :--- | :--- |
| `j` / `k` / `l` / `;` | Expand / shrink selection (left, down, up, right) |
| `w` / `b` / `0` / `$` | Jump word forward / backward / line start / line end |
| `y` | **Yank (copy)** selected text to clipboard buffer |
| `d` / `x` | **Delete (cut)** selected text |
| `c` | **Change** selected text (delete selection and switch to Insert Mode) |
| `Esc` / `Ctrl + C` / `v` | Cancel selection and return to Normal Mode |

### Insert Mode (`a`)
| Key | Action |
| :--- | :--- |
| `Tab` | Trigger autocomplete / Cycle through completion items |
| `↓` / `Ctrl + N` | Select next completion candidate |
| `↑` / `Ctrl + P` | Select previous completion candidate |
| `Enter` | Accept selected completion / Insert smart newline with indentation |
| `Backspace` | Delete character (deletes both characters if between empty pair like `()`) |
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

## 🧩 Extending Purre with Lua

Purre embeds LuaJIT 2.1. You can add custom commands, formatters, and status badges in `runtime/init.lua`.

See the full [Lua Plugin Documentation](docs/PLUGINS.md) for API details and code examples.

---

## 📜 License

MIT License. Designed with ❤️ by [uniix-cat](https://github.com/uniix-cat).
