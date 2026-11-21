# 🐾 Руководство по разработке плагинов и расширений на Lua для Purr

[English](PLUGINS.md) | [Русский](PLUGINS_RU.md)

Purr спроектирован как легковесный, модульный и расширяемый редактор. В него встроен **LuaJIT 2.1**, обеспечивающий околонативную скорость выполнения скриптов, плагинов и утилит без усложнения и раздувания ядра на Си.

Все встроенные механизмы: LSP-клиент, анализаторы кода (`clang-tidy`), JSON-парсер и пользовательские команды — написаны на чистом Lua поверх Си API Purr.

---

## 🏗️ Архитектура

Purr объединяет минималистичный цикл событий на Си с виртуальной машиной LuaJIT:

```
┌────────────────────────────────────────────────────────┐
│                      Ядро Purr (C)                     │
│  - Raw-режим терминала (termios, UTF-8, ANSI escape)   │
│  - Текстовый буфер и гаттер (динамические массивы)     │
│  - Неблокирующий опрос I/O poll (терминал + пайпы)     │
│  - Отрисовка TrueColor Dracula                         │
└──────────────────────────┬─────────────────────────────┘
                           │ Си API (lua_bridge)
┌──────────────────────────▼─────────────────────────────┐
│                    Среда LuaJIT                        │
│  - runtime/init.lua     (Хуки жизненного цикла, команды│
│  - runtime/lsp.lua      (JSON-RPC клиент LSP)          │
│  - runtime/analyzer.lua (Анализаторы: clang-tidy и др.)│
│  - runtime/json.lua     (Парсер/энкодер JSON на Lua)   │
└────────────────────────────────────────────────────────┘
```

Файлы конфигурации и скрипты загружаются в следующем порядке:
1. `runtime/init.lua` (относительно текущей рабочей директории)
2. `/usr/share/purr/runtime/init.lua` (общесистемная установка)

---

## 🛠️ Глобальный API таблицы `purr`

При запуске Purr регистрирует в глобальной области видимости Lua таблицу `purr` со следующими функциями:

### 1. Работа с буфером и текстом

* **`purr.get_buffer_text()` -> `string`**
  Возвращает весь текст текущего активного буфера одной строкой.

* **`purr.insert_text(text)`**
  Вставляет строку `text` (в кодировке UTF-8) в текущую позицию курсора.

* **`purr.get_filename()` -> `string | nil`**
  Возвращает имя текущего открытого файла или `nil`, если файл ещё не сохранён (`[No Name]`).

* **`purr.get_filetype()` -> `string`**
  Возвращает определённый тип файла (например, `"c"`, `"cpp"`, `"python"`, `"rust"`, `"lua"`, `"text"`).

### 2. Управление курсором

* **`purr.get_cursor()` -> `row, col`**
  Возвращает 1-based номер строки (`row`) и 1-based визуальную колонку курсора (`col`).

* **`purr.set_cursor(row, col)`**
  Перемещает курсор на строку `row` и колонку `col` (отсчёт с 1). Координаты автоматически ограничиваются размером буфера.

### 3. Операции с файлами

* **`purr.open_file(filepath)` -> `boolean`**
  Загружает файл по пути `filepath` в текущий буфер.

* **`purr.save_file(filepath)` -> `boolean`**
  Сохраняет буфер на диск. Если `filepath` не указан или `nil`, сохраняет по текущему имени файла.

### 4. Статусная строка и UI

* **`purr.set_status(message)`**
  Выводит сообщение в нижнюю статусную строку редактора.

* **`purr.set_lsp_status(status_str)`**
  Обновляет индикатор LSP в строке состояния (например, `"✓ LSP: clangd"` или `"LSP: idle"`).

* **`purr.set_diagnostics(diagnostics_table)`**
  Передаёт список ошибок и предупреждений для отображения в гаттере (номера строк) и статусной строке.
  Формат каждого элемента таблицы:
  ```lua
  {
      line = 42,       -- номер строки (с 1)
      col = 5,         -- колонка (с 1)
      severity = 1,    -- 1: Ошибка (󰅚), 2: Предупреждение (󰀪), 3: Инфо (󰋽), 4: Подсказка
      message = "Неиспользуемая переменная 'counter'"
  }
  ```

### 5. Движок автодополнения (Completion)

* **`purr.set_completions(items_table)`**
  Открывает всплывающее меню вариантов дополнения под курсором.
  Формат каждого элемента:
  ```lua
  {
      label = "printf",            -- Текст, отображаемый в списке
      insert_text = "printf()",    -- Текст, который вставляется по Enter
      kind = 2                     -- Иконка Nerd Font: 2/3: Функция (󰊕), 6: Переменная (󰆧), 14: Ключевое слово (󰌋)
  }
  ```

* **`purr.clear_completions()`**
  Закрывает всплывающее меню дополнений и сбрасывает текущие кандидаты.

### 6. Неблокирующие асинхронные подпроцессы

Purr позволяет запускать системные утилиты и фоновые демоны без подвисания интерфейса:

* **`purr.spawn_async(command, args_table, on_stdout_callback) -> proc_id`**
  Запускает дочерний процесс с перенаправленными пайпами stdin/stdout/stderr.
  Функция `on_stdout_callback` вызывается всякий раз, когда процесс отдаёт данные в stdout:
  ```lua
  local proc_id = purr.spawn_async("git", {"status", "--porcelain"}, function(output_chunk)
      print("Git status:", output_chunk)
  end)
  ```

* **`purr.send_stdin(proc_id, data_string)`**
  Отправляет сырые данные на вход процесса (используется для LSP JSON-RPC пакетов).

* **`purr.close_proc(proc_id)`**
  Посылает процессу сигнал `SIGTERM` и корректно закрывает файловые дескрипторы пайпов.

---

## ⚡ Регистрация собственных команд

Команды, доступные через `:` в командном режиме, регистрируются в таблице `purr.commands`:

```lua
-- В runtime/init.lua или в вашем файле плагина:
purr.commands["count"] = function(args)
    local text = purr.get_buffer_text() or ""
    local lines = 0
    for _ in text:gmatch("\n") do lines = lines + 1 end
    local words = 0
    for _ in text:gmatch("%S+") do words = words + 1 end
    local bytes = #text

    purr.set_status(string.format("Строк: %d | Слов: %d | Байт: %d", lines, words, bytes))
end
```
Теперь, введя `:count` в Purr, вы мгновенно увидите статистику в статусной строке!

---

## 🪝 Хуки жизненного цикла

Purr вызывает функции обратного вызова в Lua при ключевых событиях:

| Хук | Когда вызывается | Аргументы |
| :--- | :--- | :--- |
| `purr.on_file_opened(filename, filetype)` | Файл загружен в буфер редактора | `filename: string`, `filetype: string` |
| `purr.on_file_saved(filename)` | Файл сохранён на диск (`Ctrl+S`, `:w`) | `filename: string` |
| `purr.on_completion_request()` | Пользователь нажал `Tab` после слова | *(нет)* |

---

## 💡 Практические примеры плагинов

### Пример 1: Асинхронный авто-форматтер кода (`:fmt`)

Форматирование текущего буфера с помощью `clang-format` (C/C++) или `black` (Python):

```lua
purr.commands["fmt"] = function()
    local fn = purr.get_filename()
    local ft = purr.get_filetype()
    if not fn then
        purr.set_status("Нельзя форматировать несохранённый буфер")
        return
    end

    -- Сначала сохраняем
    purr.save_file()

    local cmd, args
    if ft == "c" or ft == "cpp" then
        cmd = "clang-format"
        args = { "-i", fn }
    elseif ft == "python" then
        cmd = "black"
        args = { "-q", fn }
    else
        purr.set_status("Форматтер не настроен для " .. ft)
        return
    end

    purr.set_status("Форматирование через " .. cmd .. "...")
    purr.spawn_async(cmd, args, function(output)
        -- Процесс завершился, перезагружаем буфер
        purr.open_file(fn)
        purr.set_status("Файл успешно отформатирован через " .. cmd)
    end)
end
```

### Пример 2: Индикатор ветки Git в строке состояния

```lua
function update_git_badge()
    purr.spawn_async("git", {"rev-parse", "--abbrev-ref", "HEAD"}, function(branch)
        branch = branch:gsub("%s+", "")
        if #branch > 0 then
            purr.set_status(" " .. branch)
        end
    end)
end

local orig_on_opened = purr.on_file_opened
function purr.on_file_opened(fn, ft)
    if orig_on_opened then orig_on_opened(fn, ft) end
    update_git_badge()
end
```

### Пример 3: Добавление нового языкового сервера

Зарегистрировать новый LSP сервер можно в `runtime/lsp.lua` или прямо в `runtime/init.lua`:

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
Либо на лету прямо из редактора:
```
:lsp set zig zls
```
