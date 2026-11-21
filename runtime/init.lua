-- purr standard runtime initialization
local runtime_dir = purr and purr.runtime_dir or "runtime"
local lsp = dofile(runtime_dir .. "/lsp.lua")
local analyzer = dofile(runtime_dir .. "/analyzer.lua")

purr.lsp = lsp
purr.analyzer = analyzer
purr.commands = {}

-- :help command
purr.commands.help = function()
    purr.set_status("󰄛 purre: Normal [j:left, k:down, l:up, ;:right, a:ins, v:vis, y:copy, p:paste, x:del, dd:cut, K:hover, :cmd] | :check | Ctrl+S (save), Ctrl+Q (quit)")
end

-- :check / :analyze command
purr.commands.check = function()
    local fn = purr.get_filename()
    local ft = purr.get_filetype()
    if fn and ft then
        analyzer.run(fn, ft)
    else
        purr.set_status("No active file to analyze")
    end
end
purr.commands.analyze = purr.commands.check

-- :lsp command
purr.commands.lsp = function(args)
    local ft = purr.get_filetype()
    local subcmd, rest = args:match("^(%S+)%s*(.*)$")
    subcmd = subcmd or ""

    if subcmd == "start" or subcmd == "" then
        lsp.start(ft)
    elseif subcmd == "status" then
        local s = lsp.servers[ft]
        if s and s.is_ready then
            purr.set_status(string.format("󰄬 LSP: active (%s) for %s", s.active_cmd or "server", ft))
        else
            purr.set_status("󰅚 LSP: not running for " .. ft)
        end
    elseif subcmd == "hover" then
        lsp.hover()
    elseif subcmd == "def" or subcmd == "definition" then
        lsp.definition()
    elseif subcmd == "stop" then
        local s = lsp.servers[ft]
        if s and s.proc_id then
            purr.close_proc(s.proc_id)
            lsp.servers[ft] = nil
            purr.set_lsp_status("LSP: stopped")
            purr.set_status("LSP server stopped")
        end
    elseif subcmd == "set" then
        local lang, cmd = rest:match("^(%S+)%s+(%S+)$")
        if lang and cmd then
            lsp.register(lang, { cmd = cmd, args = {} })
            purr.set_status(string.format("Registered LSP '%s' for %s", cmd, lang))
        else
            purr.set_status("Usage: :lsp set <language> <command>")
        end
    elseif subcmd == "list" then
        local langs = {}
        for k, _ in pairs(lsp.registry) do
            table.insert(langs, k)
        end
        table.sort(langs)
        purr.set_status("Supported LSP languages: " .. table.concat(langs, ", "))
    else
        purr.set_status("Usage: :lsp [start|status|hover|def|stop|list|set <lang> <cmd>]")
    end
end

-- Automatic lifecycle hooks
function purr.on_file_opened(filename, filetype)
    if filetype and filetype ~= "text" then
        lsp.did_open(filename, filetype)
    end
end

function purr.on_file_saved(filename)
    lsp.did_save(filename)
    local ft = purr.get_filetype()
    if ft then
        analyzer.run(filename, ft)
    end
end

-- Completion lifecycle hook
function purr.on_completion_request()
    lsp.request_completion()
end
