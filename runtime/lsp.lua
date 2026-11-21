-- purr built-in LSP client
local json = dofile("runtime/json.lua")

local lsp = {
    servers = {},
    configs = {
        c = { cmd = "clangd", args = {"--background-index", "--clang-tidy"} },
        cpp = { cmd = "clangd", args = {"--background-index", "--clang-tidy"} },
        python = { cmd = "pyright-langserver", args = {"--stdio"} },
        rust = { cmd = "rust-analyzer", args = {} },
        go = { cmd = "gopls", args = {} },
        lua = { cmd = "lua-language-server", args = {} }
    }
}

local function path_to_uri(path)
    if not path:match("^/") then
        local cwd = io.popen("pwd"):read("*l") or ""
        path = cwd .. "/" .. path
    end
    return "file://" .. path
end

function lsp.send_rpc(server, msg)
    local body = json.encode(msg)
    local packet = string.format("Content-Length: %d\r\n\r\n%s", #body, body)
    purr.send_stdin(server.proc_id, packet)
end

function lsp.handle_message(server, msg)
    if not msg then return end

    -- Diagnostics notification
    if msg.method == "textDocument/publishDiagnostics" and msg.params then
        local diags = msg.params.diagnostics or {}
        if #diags > 0 then
            purr.set_lsp_status(string.format("⚠ %d issues", #diags))
            local first = diags[1]
            local line = (first.range and first.range.start and first.range.start.line or 0) + 1
            purr.set_status(string.format("L%d: %s", line, first.message or ""))
        else
            purr.set_lsp_status("✓ LSP: 0 errors")
        end
    end

    -- Server capabilities / initialize response
    if msg.id and server.pending_requests[msg.id] then
        local cb = server.pending_requests[msg.id]
        server.pending_requests[msg.id] = nil
        cb(msg.result, msg.error)
    end
end

function lsp.process_buffer(server)
    while true do
        local cl_start, cl_end, cl_val = server.raw_buf:find("Content%-Length:%s*(%d+)\r\n\r\n")
        if not cl_start then
            break
        end

        local content_len = tonumber(cl_val)
        local body_start = cl_end + 1
        local body_end = body_start + content_len - 1

        if #server.raw_buf < body_end then
            -- Need more data
            break
        end

        local body = server.raw_buf:sub(body_start, body_end)
        server.raw_buf = server.raw_buf:sub(body_end + 1)

        local ok, msg = pcall(json.decode, body)
        if ok and msg then
            lsp.handle_message(server, msg)
        end
    end
end

function lsp.start(filetype)
    local cfg = lsp.configs[filetype]
    if not cfg then return nil end

    if lsp.servers[filetype] and lsp.servers[filetype].proc_id then
        return lsp.servers[filetype]
    end

    purr.set_lsp_status("LSP: connecting " .. cfg.cmd .. "...")

    local server = {
        filetype = filetype,
        raw_buf = "",
        request_id = 1,
        pending_requests = {},
        is_ready = false
    }

    local proc_id = purr.spawn_async(cfg.cmd, cfg.args, function(data)
        server.raw_buf = server.raw_buf .. data
        lsp.process_buffer(server)
    end)

    if not proc_id then
        purr.set_lsp_status("LSP: " .. cfg.cmd .. " not found")
        return nil
    end

    server.proc_id = proc_id
    lsp.servers[filetype] = server

    -- Send initialize request
    local cwd = io.popen("pwd"):read("*l") or ""
    local root_uri = "file://" .. cwd

    local req_id = server.request_id
    server.request_id = server.request_id + 1

    server.pending_requests[req_id] = function(result, err)
        if not err then
            server.is_ready = true
            purr.set_lsp_status("✓ LSP: " .. cfg.cmd)
            lsp.send_rpc(server, {
                jsonrpc = "2.0",
                method = "initialized",
                params = {}
            })
            -- Open current file if exists
            local fn = purr.get_filename()
            if fn then
                lsp.did_open(fn, filetype)
            end
        else
            purr.set_lsp_status("LSP init error")
        end
    end

    lsp.send_rpc(server, {
        jsonrpc = "2.0",
        id = req_id,
        method = "initialize",
        params = {
            processId = nil,
            rootUri = root_uri,
            capabilities = {
                textDocument = {
                    publishDiagnostics = { relatedInformation = true }
                }
            }
        }
    })

    return server
end

function lsp.did_open(filename, filetype)
    local server = lsp.servers[filetype]
    if not server or not server.is_ready then
        server = lsp.start(filetype)
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
            textDocument = {
                uri = path_to_uri(filename)
            },
            text = text
        }
    })
end

return lsp
