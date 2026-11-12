-- Sound stress for the recompiled game's threads (tools/recomp/coverage.py --mod sound_stress): a test-only script on
-- the same overlay main.lua as the coverage walk. Each update plays four random sound effects and stops two, the
-- music changes every 20 updates (played or crossfaded), a cutscene with its soundtrack plays every 900 updates and
-- the run restarts every 1800, so the main thread adds and removes sound channels and opens music and video streams
-- while the game's sound thread updates them, far more often than play does. Progress lines ("cov:") reach the host log.
local mod = RegisterMod("isaac360 sound stress", 1)

-- the cutscenes that return to the run (8 and the endings go on to the credits and the menus)
local CUTSCENES = {1, 2, 3}
local MINUTES = 10

local started, updates, cutscene = false, 0, 0
local rng = 12345

local function rand(n)
    rng = (rng * 1103515245 + 12345) % 2147483648
    return rng % n + 1
end

local function log(s) Isaac.DebugString("cov: " .. s) end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
    if not started then
        Isaac.ExecuteCommand("debug 3")
        log("stress started")
    end
    started = true
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
    if not started then return end
    updates = updates + 1
    local sfx, music = SFXManager(), MusicManager()
    for _ = 1, 4 do
        sfx:Play(rand(600), 0.05, 0, false, 0.5 + rand(100) / 100, 0)
    end
    for _ = 1, 2 do
        sfx:Stop(rand(600))
    end
    if updates % 20 == 0 then
        local id = rand(110)
        if updates % 40 == 0 then music:Crossfade(id, 0.2) else music:Play(id, 0.1) end
    end
    if updates % 300 == 0 then
        log(string.format("update %d, %d s", updates, updates // 30))
    end
    if updates % 900 == 450 then
        cutscene = cutscene % #CUTSCENES + 1
        log("cutscene " .. CUTSCENES[cutscene])
        Isaac.ExecuteCommand("cutscene " .. CUTSCENES[cutscene])
    elseif updates % 1800 == 0 then
        log("restart")
        Isaac.ExecuteCommand("restart")
    end
    if updates >= MINUTES * 60 * 30 then
        log("done")
        started = false
    end
end)
