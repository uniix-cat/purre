-- purr universal LSP client
local json = dofile("runtime/json.lua")

local lsp = {
    servers = {},
    diagnostics = {},
    -- Broad multi-candidate registry for major languages
    registry = {
        c          = { { cmd = "clangd", args = {"--background-index", "--clang-tidy"} }, { cmd = "ccls", args = {} } },
        cpp        = { { cmd = "clangd", args = {"--background-index", "--clang-tidy"} }, { cmd = "ccls", args = {} } },
        python     = { { cmd = "pyright-langserver", args = {"--stdio"} }, { cmd = "pylsp", args = {} }, { cmd = "basedpyright", args = {"--stdio"} }, { cmd = "ruff", args = {"server"} } },
        rust       = { { cmd = "rust-analyzer", args = {} } },
        go         = { { cmd = "gopls", args = {} } },
        javascript = { { cmd = "typescript-language-server", args = {"--stdio"} }, { cmd = "vtsls", args = {"--stdio"} }, { cmd = "deno", args = {"lsp"} } },
        typescript = { { cmd = "typescript-language-server", args = {"--stdio"} }, { cmd = "vtsls", args = {"--stdio"} }, { cmd = "deno", args = {"lsp"} } },
        lua        = { { cmd = "lua-language-server", args = {} } },
        zig        = { { cmd = "zls", args = {} } },
        bash       = { { cmd = "bash-language-server", args = {"start"} } },
        sh         = { { cmd = "bash-language-server", args = {"start"} } },
        html       = { { cmd = "vscode-html-language-server", args = {"--stdio"} } },
        css        = { { cmd = "vscode-css-language-server", args = {"--stdio"} } },
        json       = { { cmd = "vscode-json-language-server", args = {"--stdio"} } },
        yaml       = { { cmd = "yaml-language-server", args = {"--stdio"} } },
        markdown   = { { cmd = "marksman", args = {"server"} } },
        csharp     = { { cmd = "csharp-ls", args = {} }, { cmd = "OmniSharp", args = {"-lsp"} } },
        java       = { { cmd = "jdtls", args = {} } },
        php        = { { cmd = "intelephense", args = {"--stdio"} }, { cmd = "phpactor", args = {"language-server"} } },
        ruby       = { { cmd = "solargraph", args = {"stdio"} }, { cmd = "ruby-lsp", args = {} } },
        kotlin     = { { cmd = "kotlin-language-server", args = {} } },
        swift      = { { cmd = "sourcekit-lsp", args = {} } },
        dart       = { { cmd = "dart", args = {"language-server", "--client-id=purr"} } },
        haskell    = { { cmd = "haskell-language-server-wrapper", args = {"--lsp"} } },
        ocaml      = { { cmd = "ocamllsp", args = {} } },
        dockerfile = { { cmd = "docker-langserver", args = {"--stdio"} } },
        cmake      = { { cmd = "cmake-language-server", args = {} }, { cmd = "neocmakelsp", args = {"--stdio"} } }
    }
}

-- Cache detected binaries in system PATH
local exec_cache = {}

local function command_exists(cmd)
    if exec_cache[cmd] ~= nil then
        return exec_cache[cmd]
    end
    local f = io.popen(string.format("command -v %s 2>/dev/null", cmd))
    if f then
        local res = f:read("*l")
        f:close()
        exec_cache[cmd] = (res ~= nil and res ~= "")
        return exec_cache[cmd]
    end
    exec_cache[cmd] = false
    return false
end

-- Find project root directory by inspecting ancestor folders
local function find_project_root(file_path)
    local cwd = io.popen("pwd"):read("*l") or ""
    if not file_path or file_path == "" then return cwd end

    local dir = file_path:match("^(.*)/[^/]+$")
    if not dir or dir == "" then dir = cwd end
    if not dir:match("^/") then dir = cwd .. "/" .. dir end

    local markers = {
        ".git", "Cargo.toml", "package.json", "go.mod",
        "compile_commands.json", "CMakeLists.txt", "Makefile",
        "pyproject.toml", "setup.py", "requirements.txt",
        "build.gradle", "pom.xml", ".sln"
    }

    local current = dir
    while current and current ~= "" and current ~= "/" do
        for _, marker in ipairs(markers) do
            local test_path = current .. "/" .. marker
            local f = io.open(test_path, "r")
            if f then
                f:close()
                return current
            end
        end
        current = current:match("^(.*)/[^/]+$")
    end

    return cwd
end

local function path_to_uri(path)
    if not path:match("^/") then
        local cwd = io.popen("pwd"):read("*l") or ""
        path = cwd .. "/" .. path
    end
    return "file://" .. path
end

local function uri_to_path(uri)
    if uri:match("^file://") then
        return uri:sub(8)
    end
    return uri
end

function lsp.send_rpc(server, msg)
    local body = json.encode(msg)
    local packet = string.format("Content-Length: %d\r\n\r\n%s", #body, body)
    purr.send_stdin(server.proc_id, packet)
end

function lsp.handle_message(server, msg)
    if not msg then return end

    -- Diagnostics
    if msg.method == "textDocument/publishDiagnostics" and msg.params then
        local diags = msg.params.diagnostics or {}
        local uri = msg.params.uri or ""
        lsp.diagnostics[uri] = diags

        local list = {}
        for _, d in ipairs(diags) do
            local line = (d.range and d.range.start and d.range.start.line or 0) + 1
            local col = (d.range and d.range.start and d.range.start.character or 0) + 1
            table.insert(list, {
                line = line,
                col = col,
                severity = d.severity or 1,
                message = d.message or ""
            })
        end
        purr.set_diagnostics(list)

        if #diags > 0 then
            purr.set_lsp_status(string.format("⚠ %d issues", #diags))
        else
            purr.set_lsp_status(string.format("✓ LSP: %s", server.active_cmd or "ready"))
        end
    end

    -- Workspace configuration request (must reply with empty table so servers don't stall)
    if msg.id and msg.method == "workspace/configuration" then
        lsp.send_rpc(server, {
            jsonrpc = "2.0",
            id = msg.id,
            result = {}
        })
        return
    end

    -- Handle request response callbacks
    if msg.id and server.pending_requests[msg.id] then
        local cb = server.pending_requests[msg.id]
        server.pending_requests[msg.id] = nil
        cb(msg.result, msg.error)
    end
end

function lsp.process_buffer(server)
    while true do
        local cl_start, cl_end, cl_val = server.raw_buf:find("Content%-Length:%s*(%d+)\r\n\r\n")
        if not cl_start then break end

        local content_len = tonumber(cl_val)
        local body_start = cl_end + 1
        local body_end = body_start + content_len - 1

        if #server.raw_buf < body_end then break end

        local body = server.raw_buf:sub(body_start, body_end)
        server.raw_buf = server.raw_buf:sub(body_end + 1)

        local ok, msg = pcall(json.decode, body)
        if ok and msg then
            lsp.handle_message(server, msg)
        end
    end
end

-- Select best available candidate from registry
function lsp.find_server_candidate(filetype)
    local candidates = lsp.registry[filetype]
    if not candidates then return nil end

    for _, cand in ipairs(candidates) do
        if command_exists(cand.cmd) then
            return cand
        end
    end
    return nil
end

function lsp.register(filetype, config)
    if not lsp.registry[filetype] then
        lsp.registry[filetype] = {}
    end
    table.insert(lsp.registry[filetype], 1, config)
end

function lsp.start(filetype)
    local candidate = lsp.find_server_candidate(filetype)
    if not candidate then
        local known = lsp.registry[filetype]
        if known and #known > 0 then
            purr.set_lsp_status("LSP: " .. known[1].cmd .. " not installed")
            purr.set_status(string.format("Install '%s' for %s LSP support", known[1].cmd, filetype))
        else
            purr.set_lsp_status("LSP: none")
        end
        return nil
    end

    if lsp.servers[filetype] and lsp.servers[filetype].proc_id then
        return lsp.servers[filetype]
    end

    purr.set_lsp_status("LSP: starting " .. candidate.cmd .. "...")

    local server = {
        filetype = filetype,
        active_cmd = candidate.cmd,
        raw_buf = "",
        request_id = 1,
        pending_requests = {},
        is_ready = false
    }

    local proc_id = purr.spawn_async(candidate.cmd, candidate.args or {}, function(data)
        server.raw_buf = server.raw_buf .. data
        lsp.process_buffer(server)
    end)

    if not proc_id then
        purr.set_lsp_status("LSP: failed to launch " .. candidate.cmd)
        return nil
    end

    server.proc_id = proc_id
    lsp.servers[filetype] = server

    local cur_file = purr.get_filename() or ""
    local root_path = find_project_root(cur_file)
    local root_uri = "file://" .. root_path

    local req_id = server.request_id
    server.request_id = server.request_id + 1

    server.pending_requests[req_id] = function(result, err)
        if not err then
            server.is_ready = true
            purr.set_lsp_status("✓ LSP: " .. candidate.cmd)
            lsp.send_rpc(server, {
                jsonrpc = "2.0",
                method = "initialized",
                params = {}
            })
            if cur_file ~= "" then
                lsp.did_open(cur_file, filetype)
            end
        else
            purr.set_lsp_status("LSP init failed")
        end
    end

    lsp.send_rpc(server, {
        jsonrpc = "2.0",
        id = req_id,
        method = "initialize",
        params = {
            processId = nil,
            rootPath = root_path,
            rootUri = root_uri,
            workspaceFolders = {
                { uri = root_uri, name = root_path:match("[^/]+$") or "root" }
            },
            capabilities = {
                textDocument = {
                    synchronization = { openClose = true, change = 1, save = { includeText = true } },
                    publishDiagnostics = { relatedInformation = true },
                    hover = { contentFormat = {"plaintext", "markdown"} },
                    definition = { linkSupport = true }
                }
            }
        }
    })

    return server
end

function lsp.did_open(filename, filetype)
    local server = lsp.servers[filetype]
    if not server or not server.is_ready then
        lsp.start(filetype)
        return
    end

    local text = purr.get_buffer_text()
    lsp.send_rpc(server, {
        jsonrpc = "2.0",
        method = "textDocument/didOpen",
        params = {
            textDocument = {
                uri = path_to_uri(filename),
                languageId = filetype,
                version = 1,
                text = text
            }
        }
    })
end

function lsp.did_save(filename)
    local ft = purr.get_filetype()
    local server = lsp.servers[ft]
    if not server or not server.is_ready then return end

    local text = purr.get_buffer_text()
    lsp.send_rpc(server, {
        jsonrpc = "2.0",
        method = "textDocument/didSave",
        params = {
            textDocument = { uri = path_to_uri(filename) },
            text = text
        }
    })
end

-- Hover documentation (triggered by 'K' or :lsp hover)
function lsp.hover()
    local ft = purr.get_filetype()
    local server = lsp.servers[ft]
    if not server or not server.is_ready then
        purr.set_status("LSP is not active for this file")
        return
    end

    local row, col = purr.get_cursor()
    local fn = purr.get_filename() or "untitled"

    local req_id = server.request_id
    server.request_id = server.request_id + 1

    server.pending_requests[req_id] = function(result, err)
        if err or not result or not result.contents then
            purr.set_status("No hover info available")
            return
        end

        local text = ""
        local contents = result.contents
        if type(contents) == "string" then
            text = contents
        elseif type(contents) == "table" then
            if contents.value then
                text = contents.value
            elseif #contents > 0 then
                local parts = {}
                for _, item in ipairs(contents) do
                    table.insert(parts, type(item) == "table" and (item.value or "") or tostring(item))
                end
                text = table.concat(parts, " ")
            end
        end

        text = text:gsub("\r", ""):gsub("\n", " "):gsub("%s+", " ")
        if #text > 100 then text = text:sub(1, 97) .. "..." end
        purr.set_status(text)
    end

    lsp.send_rpc(server, {
        jsonrpc = "2.0",
        id = req_id,
        method = "textDocument/hover",
        params = {
            textDocument = { uri = path_to_uri(fn) },
            position = { line = row - 1, character = col - 1 }
        }
    })
end

-- Go to definition (triggered by :lsp def)
function lsp.definition()
    local ft = purr.get_filetype()
    local server = lsp.servers[ft]
    if not server or not server.is_ready then
        purr.set_status("LSP is not active for this file")
        return
    end

    local row, col = purr.get_cursor()
    local fn = purr.get_filename() or "untitled"

    local req_id = server.request_id
    server.request_id = server.request_id + 1

    server.pending_requests[req_id] = function(result, err)
        if err or not result then
            purr.set_status("Definition not found")
            return
        end

        local target = result
        if type(result) == "table" and result[1] then
            target = result[1]
        end

        local uri = target.uri or target.targetUri
        local range = target.range or target.targetSelectionRange
        if not uri or not range then
            purr.set_status("Definition not found")
            return
        end

        local path = uri_to_path(uri)
        local cur_fn = purr.get_filename() or ""
        if path ~= cur_fn then
            purr.open_file(path)
        end

        local target_line = (range.start and range.start.line or 0) + 1
        local target_col = (range.start and range.start.character or 0) + 1
        purr.set_cursor(target_line, target_col)
        purr.set_status(string.format("Jumped to %s:%d:%d", path:match("[^/]+$") or path, target_line, target_col))
    end

    lsp.send_rpc(server, {
        jsonrpc = "2.0",
        id = req_id,
        method = "textDocument/definition",
        params = {
            textDocument = { uri = path_to_uri(fn) },
            position = { line = row - 1, character = col - 1 }
        }
    })
end

return lsp
