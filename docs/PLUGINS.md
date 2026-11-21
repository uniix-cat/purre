# 🐾 Purre Lua Plugin & Extensibility Guide

[English](PLUGINS.md) | [Русский](PLUGINS_RU.md)

Purre is designed from the ground up to be lightweight, modular, and hackable. It embeds **LuaJIT 2.1**, giving you near-native execution speed for scripting, plugins, and custom tooling without bloating the core C codebase.

The editor's built-in LSP client, static analyzer engine, JSON parser, and commands are all written in pure Lua on top of the Purre C API.

---

## 🏗️ Architecture Overview

Purre combines a minimal C event loop with LuaJIT:

```
┌────────────────────────────────────────────────────────┐
│                      Purre Core (C)                    │
│  - Terminal Raw Mode (termios, UTF-8, ANSI escape)     │
│  - Text Buffer & Gutter (dynamic line arrays)          │
│  - Non-blocking I/O poll (stdin + subprocess pipes)    │
│  - TrueColor Dracula rendering                         │
└──────────────────────────┬─────────────────────────────┘
                           │ C-API (lua_bridge)
┌──────────────────────────▼─────────────────────────────┐
│                     LuaJIT Runtime                     │
│  - runtime/init.lua     (Lifecycle hooks & commands)   │
│  - runtime/lsp.lua      (LSP JSON-RPC client)          │
│  - runtime/analyzer.lua (Static analyzers: clang-tidy) │
│  - runtime/json.lua     (Pure Lua JSON encoder/decoder)│
└────────────────────────────────────────────────────────┘
```

The runtime searches for configuration and scripts in:
1. `runtime/init.lua` (relative to current working directory)
2. `~/.local/share/purre/runtime/init.lua` (user installation)
3. `/usr/local/share/purre/runtime/init.lua` (local system installation)
4. `/usr/share/purre/runtime/init.lua` (system-wide installation)

---

## 🛠️ The `purr` Global API

When Purre initializes, it exposes a global `purr` table to the Lua state containing the following functions:

### 1. Buffer & Text Inspection

* **`purr.get_buffer_text()` -> `string`**
  Returns the entire current buffer contents as a single string.

* **`purr.insert_text(text)`**
  Inserts `text` (UTF-8 string) at the current cursor location.

* **`purr.get_filename()` -> `string | nil`**
  Returns the filename of the active buffer, or `nil` if unsaved `[No Name]`.

* **`purr.get_filetype()` -> `string`**
  Returns the detected filetype (e.g., `"c"`, `"cpp"`, `"python"`, `"rust"`, `"lua"`, `"text"`).

### 2. Cursor Positioning

* **`purr.get_cursor()` -> `row, col`**
  Returns the 1-based line number (`row`) and 1-based visual byte column (`col`).

* **`purr.set_cursor(row, col)`**
  Moves the cursor to line `row` and column `col` (1-based). Clamped automatically to buffer bounds.

### 3. File Operations

* **`purr.open_file(filepath)` -> `boolean`**
  Loads the file at `filepath` into the active buffer.

* **`purr.save_file(filepath)` -> `boolean`**
  Saves the current buffer to disk. If `filepath` is omitted or nil, saves to the current filename.

### 4. Status Bar & UI

* **`purr.set_status(message)`**
  Displays a message in the bottom status message bar.

* **`purr.set_lsp_status(status_str)`**
  Updates the LSP badge in the status bar (e.g. `"✓ LSP: clangd"` or `"LSP: idle"`).

* **`purr.set_diagnostics(diagnostics_table)`**
  Populates line diagnostics in the gutter and status bar.
  Each entry in `diagnostics_table` should be a table:
  ```lua
  {
      line = 42,       -- 1-based line number
      col = 5,         -- 1-based column
      severity = 1,    -- 1: Error (󰅚), 2: Warning (󰀪), 3: Info (󰋽), 4: Hint
      message = "Unused variable 'counter'"
  }
  ```

### 5. Completion Engine

* **`purr.set_completions(items_table)`**
  Displays the floating completion popup menu at the cursor.
  Each entry in `items_table`:
  ```lua
  {
      label = "printf",            -- Text shown in menu
      insert_text = "printf()",    -- Text inserted on completion
      kind = 2                     -- Kind icon: 2/3: Function (󰊕), 6: Variable (󰆧), 14: Keyword (󰌋)
  }
  ```

* **`purr.clear_completions()`**
  Closes the completion popup menu and resets active completion candidates.

### 6. Non-blocking Asynchronous Subprocesses

Purr allows running CLI tools and background daemons without blocking the terminal event loop:

* **`purr.spawn_async(command, args_table, on_stdout_callback) -> proc_id`**
  Spawns a child process with piped stdin/stdout/stderr.
  `on_stdout_callback` is called whenever data arrives on stdout:
  ```lua
  local proc_id = purr.spawn_async("git", {"status", "--porcelain"}, function(output_chunk)
      print("Git status:", output_chunk)
  end)
  ```

* **`purr.send_stdin(proc_id, data_string)`**
  Sends raw data to the child process's standard input (used for LSP JSON-RPC packets).

* **`purr.close_proc(proc_id)`**
  Sends `SIGTERM` and closes the pipes for the subprocess.

---

## ⚡ Registering Custom Commands

Custom commands accessible via `:` in Command Mode can be added by registering them in `purr.commands`:

```lua
-- In runtime/init.lua or your custom plugin:
purr.commands["count"] = function(args)
    local text = purr.get_buffer_text() or ""
    local lines = 0
    for _ in text:gmatch("\n") do lines = lines + 1 end
    local words = 0
    for _ in text:gmatch("%S+") do words = words + 1 end
    local bytes = #text

    purr.set_status(string.format("Lines: %d | Words: %d | Bytes: %d", lines, words, bytes))
end
```
Typing `:count` in Purr will now show the statistics in the status bar!

---

## 🪝 Lifecycle Hooks

Purr calls predefined Lua callbacks on key editor events:

| Hook | Triggered When | Arguments |
| :--- | :--- | :--- |
| `purr.on_file_opened(filename, filetype)` | A file is loaded into the buffer | `filename: string`, `filetype: string` |
| `purr.on_file_saved(filename)` | A file is saved to disk (`Ctrl+S`, `:w`) | `filename: string` |
| `purr.on_completion_request()` | User presses `Tab` after an identifier | *(none)* |

---

## 💡 Practical Plugin Examples

### Example 1: Asynchronous Auto-Formatter (`:fmt`)

Create a plugin that formats the current buffer using `clang-format` (for C/C++) or `black` (for Python):

```lua
purr.commands["fmt"] = function()
    local fn = purr.get_filename()
    local ft = purr.get_filetype()
    if not fn then
        purr.set_status("Cannot format unsaved buffer")
        return
    end

    -- Save first
    purr.save_file()

    local cmd, args
    if ft == "c" or ft == "cpp" then
        cmd = "clang-format"
        args = { "-i", fn }
    elseif ft == "python" then
        cmd = "black"
        args = { "-q", fn }
    else
        purr.set_status("No formatter configured for " .. ft)
        return
    end

    purr.set_status("Formatting with " .. cmd .. "...")
    purr.spawn_async(cmd, args, function(output)
        -- Process finished, reload buffer
        purr.open_file(fn)
        purr.set_status("Formatted successfully with " .. cmd)
    end)
end
```

### Example 2: Git Branch Indicator in Status Bar

```lua
function update_git_badge()
    purr.spawn_async("git", {"rev-parse", "--abbrev-ref", "HEAD"}, function(branch)
        branch = branch:gsub("%s+", "")
        if #branch > 0 then
            purr.set_status(" " .. branch)
        end
    end)
end

-- Update when opening or saving files
local orig_on_opened = purr.on_file_opened
function purr.on_file_opened(fn, ft)
    if orig_on_opened then orig_on_opened(fn, ft) end
    update_git_badge()
end
```

### Example 3: Adding a Custom Language Server

Register any custom language server in `runtime/lsp.lua` or directly from `runtime/init.lua`:

```lua
purr.lsp.register("zig", {
    cmd = "zls",
    args = {}
})

purr.lsp.register("nim", {
    cmd = "nimlsp",
    args = {}
})
```
Or interactively from the command line inside Purr:
```
:lsp set zig zls
```
