-- Minimal pure Lua JSON encoder and decoder for LSP JSON-RPC
local json = {}

local function escape_str(s)
    local in_char  = {'\\', '"', '\b', '\f', '\n', '\r', '\t'}
    local out_char = {'\\\\', '\\"', '\\b', '\\f', '\\n', '\\r', '\\t'}
    for i, c in ipairs(in_char) do
        s = s:gsub(c, out_char[i])
    end
    return s
end

function json.encode(val)
    local t = type(val)
    if t == "nil" then
        return "null"
    elseif t == "boolean" then
        return val and "true" or "false"
    elseif t == "number" then
        return tostring(val)
    elseif t == "string" then
        return '"' .. escape_str(val) .. '"'
    elseif t == "table" then
        if val.__is_array then
            return "[]"
        end
        local is_array = true
        local n = 0
        for k, _ in pairs(val) do
            n = n + 1
            if type(k) ~= "number" or k <= 0 or math.floor(k) ~= k then
                is_array = false
                break
            end
        end
        if n == 0 then
            return "{}"
        end
        if is_array and val[1] == nil then
            is_array = false
        end

        local parts = {}
        if is_array then
            for i = 1, #val do
                table.insert(parts, json.encode(val[i]))
            end
            return "[" .. table.concat(parts, ",") .. "]"
        else
            for k, v in pairs(val) do
                table.insert(parts, json.encode(tostring(k)) .. ":" .. json.encode(v))
            end
            return "{" .. table.concat(parts, ",") .. "}"
        end
    end
    return "null"
end

-- A simple recursive-descent JSON parser
function json.decode(str)
    local pos = 1
    local len = #str

    local function skip_whitespace()
        while pos <= len and str:sub(pos, pos):match("%s") do
            pos = pos + 1
        end
    end

    local parse_value

    local function parse_string()
        pos = pos + 1 -- skip opening quote
        local start = pos
        local res = {}
        while pos <= len do
            local c = str:sub(pos, pos)
            if c == '"' then
                table.insert(res, str:sub(start, pos - 1))
                pos = pos + 1
                return table.concat(res)
            elseif c == '\\' then
                table.insert(res, str:sub(start, pos - 1))
                pos = pos + 1
                local esc = str:sub(pos, pos)
                if esc == '"' or esc == '\\' or esc == '/' then
                    table.insert(res, esc)
                elseif esc == 'n' then table.insert(res, '\n')
                elseif esc == 'r' then table.insert(res, '\r')
                elseif esc == 't' then table.insert(res, '\t')
                end
                pos = pos + 1
                start = pos
            else
                pos = pos + 1
            end
        end
        return table.concat(res)
    end

    local function parse_number()
        local start = pos
        if str:sub(pos, pos) == '-' then pos = pos + 1 end
        while pos <= len and str:sub(pos, pos):match("[%d%.eE%+%-%x]") do
            pos = pos + 1
        end
        return tonumber(str:sub(start, pos - 1))
    end

    local function parse_array()
        pos = pos + 1 -- skip '['
        local arr = {}
        skip_whitespace()
        if pos <= len and str:sub(pos, pos) == ']' then
            pos = pos + 1
            return arr
        end
        while pos <= len do
            table.insert(arr, parse_value())
            skip_whitespace()
            local c = str:sub(pos, pos)
            if c == ']' then
                pos = pos + 1
                break
            elseif c == ',' then
                pos = pos + 1
            else
                break
            end
        end
        return arr
    end

    local function parse_object()
        pos = pos + 1 -- skip '{'
        local obj = {}
        skip_whitespace()
        if pos <= len and str:sub(pos, pos) == '}' then
            pos = pos + 1
            return obj
        end
        while pos <= len do
            skip_whitespace()
            local key = parse_string()
            skip_whitespace()
            if str:sub(pos, pos) == ':' then
                pos = pos + 1
            end
            local val = parse_value()
            obj[key] = val
            skip_whitespace()
            local c = str:sub(pos, pos)
            if c == '}' then
                pos = pos + 1
                break
            elseif c == ',' then
                pos = pos + 1
            else
                break
            end
        end
        return obj
    end

    parse_value = function()
        skip_whitespace()
        if pos > len then return nil end
        local c = str:sub(pos, pos)
        if c == '"' then
            return parse_string()
        elseif c == '{' then
            return parse_object()
        elseif c == '[' then
            return parse_array()
        elseif c == 't' and str:sub(pos, pos + 3) == "true" then
            pos = pos + 4
            return true
        elseif c == 'f' and str:sub(pos, pos + 4) == "false" then
            pos = pos + 5
            return false
        elseif c == 'n' and str:sub(pos, pos + 3) == "null" then
            pos = pos + 4
            return nil
        else
            return parse_number()
        end
    end

    return parse_value()
end

return json
