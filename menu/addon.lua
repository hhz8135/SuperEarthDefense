local hd2 = require('mods/skyeshade/hd2runtime')
local mod = hd2.mod()
local mod_id = 'local.super-earth-defense'
local home = os.getenv('LOCALAPPDATA')
if not home or home == '' then
    mod:log('LOCALAPPDATA unavailable; menu settings cannot be saved.')
    return
end
local prefix = home .. '\\CowboyBingus\\Helldivers2\\SuperEarthDefense.'
-- Simplified Chinese labels from the game's cn string tables in build 25480438.
local cities = {
    '鹰都', '行政总都02', '缅城', '棒约克', '馨家泊', '荣都', '仰齐浜',
}
local choices = {'虫族', '机器人', '光能族'}
local defaults = {3, 3, 3, 1, 3, 3, 2}
local selected = {}
local dirty = false
local variant_modes = {'D', 'A', 'C'}
-- Display order is independent of the saved bit positions.
-- Use the game's display names; the war-effect name hashes do not resolve in this build.
local variants = {
    {id = 1243, slot = 2, name = '掠食变种', faction = 1},
    {id = 1244, slot = 3, name = '孢裂变种', faction = 1},
    {id = 1303, slot = 5, name = '爆裂虫族变种', faction = 1},
    {id = 1306, slot = 6, name = '蟑龙', faction = 1},
    {id = 1202, slot = 1, name = '喷气旅', faction = 2},
    {id = 1248, slot = 4, name = '炽灼部队', faction = 2},
    {id = 1360, slot = 8, name = '生化人', faction = 2},
    {id = 1377, slot = 9, name = '无脑群氓', faction = 3},
    {id = 1380, slot = 10, name = '占领者', faction = 3},
    {id = 1402, slot = 11, name = '窃票者', faction = 3},
    {id = 1413, slot = 12, name = '入侵舰队', faction = 3},
}

local function save(suffix, text)
    local file, err = io.open(prefix .. suffix, 'wb')
    if not file then
        mod:log('Cannot open ' .. suffix .. ': ' .. tostring(err))
        return false
    end
    local written, write_err = file:write(text)
    local closed, close_err = file:close()
    if not written or not closed then
        mod:log('Cannot save ' .. suffix .. ': ' .. tostring(write_err or close_err))
        return false
    end
    return true
end

local function register(menu, id, spec, apply)
    spec.mod = '超级地球保卫战'
    spec.mod_id = mod_id
    local ok, accepted, err = pcall(menu.register_option, id, spec)
    if not ok or not accepted then
        mod:log(id .. ': ' .. tostring(err or accepted))
        return false
    end
    local subscribed, result, reason = pcall(menu.on_change, id, apply)
    if not subscribed or result == false then
        mod:log(id .. ': ' .. tostring(reason or result))
        return false
    end
    local got, value = pcall(menu.get, id)
    if not got or not apply(value) then
        mod:log(id .. ': invalid saved option')
        return false
    end
    return true
end

local attempts = 0
hd2.every(0.25, function(timer)
    local menu = rawget(_G, 'ModOptionsMenu')
    attempts = attempts + 1
    if type(menu) ~= 'table' or menu.api ~= 1 or type(menu.register_option) ~= 'function'
        or type(menu.get) ~= 'function' or type(menu.on_change) ~= 'function' then
        if attempts >= 40 then
            timer:cancel()
            mod:log('Mod Options Menu API 1 unavailable; using saved or default settings.')
        end
        return
    end
    timer:cancel()
    register(menu, mod_id .. '.enabled', {
        type = 'toggle', label = '开启保卫战', default = true,
        description = '超级地球暂不支持 N11，请选择 N10 或以下难度。在舰桥应用。联机跟随房主设置，下次银河战争刷新生效。',
    }, function(value)
        if type(value) ~= 'boolean' then return false end
        return save('enabled', value and '1\n' or '0\n')
    end)
    for index, name in ipairs(cities) do
        local slot = index
        local ok = register(menu, mod_id .. '.city_' .. (slot - 1), {
            type = 'choice', choices = choices, default = defaults[slot],
            label = '区域 ' .. (slot - 1) .. ' · ' .. name,
            description = '为这座超级城市选择入侵方，点击应用后在下一次战况刷新时生效。实际任务种族仍在测试中。',
        }, function(value)
            if type(value) ~= 'number' or not choices[value] then return false end
            selected[slot] = tostring(value + 1)
            dirty = true
            return true
        end)
        if not ok then return end
    end
    -- Keep the retired seventh bit zero in the original 12-bit save format.
    local mode, bits = nil, {[7] = '0'}
    local variants_ready, variants_dirty = false, false
    local function flush()
        if dirty then dirty = not save('cities', table.concat(selected)) end
        if variants_ready and variants_dirty then
            variants_dirty = not save('variants', mode .. table.concat(bits))
        end
    end
    flush()
    -- Apply invokes callbacks one by one. Publish each complete group on the next tick.
    hd2.every(0.25, flush, {scope = 'session'})
    local mode_ok = register(menu, mod_id .. '.variant_mode', {
        type = 'choice', label = '敌军变种模式', gap = true,
        choices = {'原版', '全部', '自定义'}, default = 1,
        description = '作用于整个超级地球。全部开放下列 11 种，自定义只启用勾选项。三种模式均排除霸王虫。',
    }, function(value)
        if not variant_modes[value] then return false end
        mode = variant_modes[value]
        variants_dirty = true
        return true
    end)
    if not mode_ok then return end
    local previous_faction
    for _, variant in ipairs(variants) do
        local slot, effect_id = variant.slot, variant.id
        local ok = register(menu, mod_id .. '.variant_' .. effect_id, {
            type = 'toggle', label = choices[variant.faction] .. ' · ' .. variant.name,
            gap = previous_faction ~= variant.faction, default = effect_id == 1413,
            description = '自定义模式下为超级地球启用此敌军效果（' .. effect_id .. '）。联机跟随房主设置。',
        }, function(value)
            if type(value) ~= 'boolean' then return false end
            bits[slot] = value and '1' or '0'
            variants_dirty = true
            return true
        end)
        if not ok then return end
        previous_faction = variant.faction
    end
    variants_ready = true
    flush()
end, {scope = 'session'})
