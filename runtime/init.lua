-- purr standard runtime initialization
local lsp = dofile("runtime/lsp.lua")

purr.commands = {}

-- :help command
purr.commands.help = function()
    purr.set_status("purr: Normal (hjkl, i, a, o, dd, x, :), Insert (Esc), Command (:w, :q, :help, :lsp), Global: Ctrl+S (save), Ctrl+Q (quit)")
end

-- :lsp command
purr.commands.lsp = function(args)
    local ft = purr.get_filetype()
    if args == "start" or args == "" then
        lsp.start(ft)
    elseif args == "status" then
        local s = lsp.servers[ft]
        if s and s.is_ready then
            purr.set_status("LSP server active for " .. ft)
        else
            purr.set_status("LSP server not running for " .. ft)
        end
    elseif args == "stop" then
        local s = lsp.servers[ft]
        if s and s.proc_id then
            purr.close_proc(s.proc_id)
            lsp.servers[ft] = nil
            purr.set_lsp_status("LSP: stopped")
            purr.set_status("LSP server stopped")
        end
    else
        purr.set_status("Usage: :lsp [start|status|stop]")
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
end
