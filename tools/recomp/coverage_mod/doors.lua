-- coverage_mod/doors.lua (coverage.py --mod doors): a door walk. On each of the first floors, every room of the floor
-- is entered twice through the game's own room change (Game():ChangeRoom, as a door does), 45 updates apart, so the
-- host log's room lines and slow frame lines give each room's first and repeat visit; with ISAAC360_PROFILE=slow the
-- samples are those of the slow frames, the doors'.
local mod = RegisterMod("isaac360 doors", 1)
local STAGES = {"1", "2", "3", "4", "5", "6"}
local started, wait, stage, pass, rooms, at = false, 0, 0, 1, nil, 1

local function log(s) Isaac.DebugString("cov: " .. s) end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
    started = true
    wait = 30
    Isaac.ExecuteCommand("debug 3")                 -- infinite health: rooms are entered with their enemies alive
    log("run started")
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
    if not started then return end
    if wait > 0 then
        wait = wait - 1
        return
    end
    if not rooms then
        stage = stage + 1
        if stage > #STAGES then
            log("done")
            started = false
            return
        end
        if stage > 1 then
            Isaac.ExecuteCommand("stage " .. STAGES[stage])
        end
        rooms = {}
        local list = Game():GetLevel():GetRooms()
        for i = 0, list.Size - 1 do
            rooms[#rooms + 1] = list:Get(i).SafeGridIndex
        end
        pass, at, wait = 1, 1, 60
        log(string.format("stage %s: %d rooms", STAGES[stage], #rooms))
        return
    end
    if at > #rooms then
        pass, at = pass + 1, 1
        if pass > 2 then
            rooms = nil
            return
        end
    end
    local idx = rooms[at]
    at = at + 1
    Game():ChangeRoom(idx)
    wait = 45
    log(string.format("door %d to %d (pass %d)", at - 1, idx, pass))
end)
