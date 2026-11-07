-- Retail grid measurement harness for the six remaining families.
-- Install as mods/griddump/main.lua (and a metadata.xml), enable it, then
-- enter each room in docs/GRID_SIX_FAMILIES.md. Copy log.txt back.
local mod = RegisterMod("griddump", 1)

local WATCH = {[14] = true, [21] = true, [24] = true}
local last = {}

local function desc_line(tag, index, grid)
    local room = Game():GetRoom()
    local level = Game():GetLevel()
    local descriptor = level:GetCurrentRoomDesc()
    local data = descriptor.Data
    local state = grid:GetSaveState()
    local sprite = grid:GetSprite()
    local filename = sprite and sprite:GetFilename() or ""
    local animation = sprite and sprite:GetAnimation() or ""
    local frame = sprite and sprite:GetFrame() or -1
    return string.format(
        "GRIDDUMP %s f=%d safe=%d grid=%d T=%d V=%d S=%d SC=%d SS=%u VD=%d I=%s VS=%u CC=%d PATH=%d RT=%d RV=%d anm=%s anim=%s fr=%d",
        tag, Game():GetFrameCount(), descriptor.SafeGridIndex, index,
        state.Type, state.Variant, state.State, state.SpawnCount,
        state.SpawnSeed, state.VarData, tostring(state.Initialized),
        state.VariableSeed, grid.CollisionClass, room:GetGridPath(index),
        data.Type, data.Variant, filename, animation, frame)
end

local function scan(tag)
    local room = Game():GetRoom()
    local descriptor = Game():GetLevel():GetCurrentRoomDesc()
    for index = 0, room:GetGridSize() - 1 do
        local grid = room:GetGridEntity(index)
        if grid and WATCH[grid:GetType()] then
            local line = desc_line(tag, index, grid)
            local key = descriptor.SafeGridIndex .. ":" .. index
            local body = line:gsub("f=%d+ ", "")
            if tag ~= "UPD" or last[key] ~= body then
                Isaac.DebugString(line)
                last[key] = body
            end
        end
    end
end

mod:AddCallback(ModCallbacks.MC_POST_NEW_ROOM, function()
    scan("NEW")
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
    scan("UPD")
end)

mod:AddCallback(ModCallbacks.MC_POST_NPC_INIT, function(_, npc)
    Isaac.DebugString(string.format(
        "GRIDDUMP SPAWN f=%d T=%d V=%d ST=%d seed=%u pos=%.3f,%.3f",
        Game():GetFrameCount(), npc.Type, npc.Variant, npc.SubType,
        npc.InitSeed, npc.Position.X, npc.Position.Y))
end)

mod:AddCallback(ModCallbacks.MC_POST_PICKUP_INIT, function(_, pickup)
    Isaac.DebugString(string.format(
        "GRIDDUMP PICKUP f=%d V=%d ST=%d seed=%u pos=%.3f,%.3f",
        Game():GetFrameCount(), pickup.Variant, pickup.SubType,
        pickup.InitSeed, pickup.Position.X, pickup.Position.Y))
end)
