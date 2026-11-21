-- purr static analyzer engine (clang-tidy, cppcheck, shellcheck, ruff, etc.)
local analyzer = {
    configs = {
        c = {
            cmd = "clang-tidy",
            args = function(fn) return { fn, "--", "-Wall" } end,
            pattern = "[^:]+:(%d+):(%d+):%s*(%w+):%s*(.*)"
        },
        cpp = {
            cmd = "clang-tidy",
            args = function(fn) return { fn, "--", "-Wall", "-std=c++17" } end,
            pattern = "[^:]+:(%d+):(%d+):%s*(%w+):%s*(.*)"
        },
        bash = {
            cmd = "shellcheck",
            args = function(fn) return { "-f", "gcc", fn } end,
            pattern = "[^:]+:(%d+):(%d+):%s*(%w+):%s*(.*)"
        },
        python = {
            cmd = "ruff",
            args = function(fn) return { "check", "--output-format=concise", fn } end,
            pattern = "[^:]+:(%d+):(%d+):%s*(%w+):%s*(.*)"
        }
    }
}

local function command_exists(cmd)
    local f = io.popen(string.format("command -v %s 2>/dev/null", cmd))
    if f then
        local res = f:read("*l")
        f:close()
        return (res ~= nil and res ~= "")
    end
    return false
end

function analyzer.run(filename, filetype)
    if not filename or filename == "" or not filetype then return end

    local cfg = analyzer.configs[filetype]
    if not cfg or not command_exists(cfg.cmd) then return end

    purr.set_status(string.format("󱉶 Running %s...", cfg.cmd))

    local args = cfg.args(filename)
    local output = ""

    purr.spawn_async(cfg.cmd, args, function(data)
        output = output .. data

        local diags = {}
        for line in output:gmatch("[^\r\n]+") do
            local l, c, sev_str, msg = line:match(cfg.pattern)
            if l and msg then
                local sev = 2 -- default warning
                sev_str = sev_str:lower()
                if sev_str:match("error") then sev = 1
                elseif sev_str:match("note") or sev_str:match("info") then sev = 3
                end

                table.insert(diags, {
                    line = tonumber(l),
                    col = tonumber(c) or 1,
                    severity = sev,
                    message = string.format("[%s] %s", cfg.cmd, msg)
                })
            end
        end

        if #diags > 0 then
            purr.set_diagnostics(diags)
            purr.set_status(string.format("󰅚 %s found %d issues", cfg.cmd, #diags))
        else
            purr.set_status(string.format("󰄬 %s: clean", cfg.cmd))
        end
    end)
end

return analyzer
