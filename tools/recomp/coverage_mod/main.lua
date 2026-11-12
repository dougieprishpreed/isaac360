-- PC coverage runs of the recompiled game (tools/recomp/coverage.py): a test-only script, appended to an overlay
-- copy of resources/scripts/main.lua in a throwaway save folder, that walks a run through every floor and its variants,
-- boss rooms, item rooms and cutscenes with the game's debug commands. Progress lines ("cov:") reach the host log.
local mod = RegisterMod("isaac360 coverage", 1)

local STAGES = {"1", "1a", "1b", "1c", "1d", "2", "2c", "3", "3a", "3b", "3c", "3d", "4c", "5", "5a", "5b", "5c",
                "5d", "6", "7", "7a", "7b", "7c", "8", "9", "10", "10a", "11", "11a", "12", "13"}
local ROOMS = {"s.dice", "s.treasure", "s.shop", "s.secret", "s.curse", "s.devil", "s.angel", "s.sacrifice",
               "s.library", "s.arcade", "s.challenge", "s.miniboss", "s.supersecret", "s.planetarium"}
local CUTSCENES = {1, 2, 3, 8, 13, 17, 20, 25}
local END_FLOORS = {[6] = true, [8] = true, [10] = true, [11] = true, [12] = true, [13] = true}

local steps = {}
local function add(wait, what, arg) steps[#steps + 1] = {wait = wait, what = what, arg = arg} end

add(30, "cmd", "debug 3")      -- infinite health
add(1, "cmd", "debug 4")       -- high damage
add(1, "cmd", "debug 8")       -- infinite charges
for i, stage in ipairs(STAGES) do
    add(60, "cmd", "stage " .. stage)
    add(90, "kill", 90)
    add(1, "cmd", "giveitem c" .. ((i * 37) % 700 + 1))
    for k = 0, 1 do
        local room = ROOMS[(i * 2 + k) % #ROOMS + 1]
        if k == 0 then
            add(60, "boss")                -- the floor's own boss room (goto s.boss / x.boss refuse)
        else
            add(60, "cmd", "goto " .. room)
        end
        add(60, "kill", 240)
    end
end
for _, c in ipairs(CUTSCENES) do
    add(60, "cmd", "cutscene " .. c)
    add(900, "log", "after cutscene " .. c)
end
add(60, "log", "done")

local step, wait, killing, started = 1, 0, 0, false

local function log(s) Isaac.DebugString("cov: " .. s) end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
    started = true
    log("run started")
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
    if not started then return end
    if killing > 0 then
        killing = killing - 1
        -- the bosses whose death ends a run (Mom, Mom's Heart, the final floors) are visited, not killed, so the
        -- walk goes on to the later floors (Mom's ending was run once: it plays the ending and the credits)
        local endsRun = END_FLOORS[Game():GetLevel():GetStage()] and Game():GetRoom():GetType() == RoomType.ROOM_BOSS
        for _, e in ipairs(Isaac.GetRoomEntities()) do
            if not endsRun and e:IsActiveEnemy(false) and e:IsVulnerableEnemy() then e:Kill() end
        end
    end
    if wait > 0 then
        wait = wait - 1
        return
    end
    local s = steps[step]
    if not s then return end
    step = step + 1
    wait = s.wait
    if s.what == "cmd" then
        local level = Game():GetLevel()
        log(string.format("step %d: %s (stage %d type %d)", step - 1, s.arg, level:GetStage(), level:GetStageType()))
        local out = Isaac.ExecuteCommand(s.arg)
        if out and out ~= "" then log("  -> " .. out) end
    elseif s.what == "kill" then
        killing = s.arg
    elseif s.what == "boss" then
        local level = Game():GetLevel()
        local rooms = level:GetRooms()
        local found = false
        for i = 0, rooms.Size - 1 do
            local d = rooms:Get(i)
            if d and d.Data and d.Data.Type == RoomType.ROOM_BOSS then
                log(string.format("step %d: boss room %d (stage %d type %d, %s)", step - 1, d.SafeGridIndex,
                                  level:GetStage(), level:GetStageType(), tostring(d.Data.Name)))
                Game():ChangeRoom(d.SafeGridIndex)
                found = true
                break
            end
        end
        if not found then log(string.format("step %d: no boss room on this floor", step - 1)) end
    else
        log(s.arg)
    end
end)
