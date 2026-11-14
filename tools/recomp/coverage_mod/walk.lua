-- coverage_mod/walk.lua: the player walks when the stick is held (coverage.py --mod walk holds it right, down, left
-- and up in turn once the run has started). The coverage walk itself moves by debug commands only, so a translation
-- error in the player's movement (boot 22: velocity stayed 0) passed every other PC check.
local mod = RegisterMod("isaac360 walk check", 1)
local started, updates, held, moved, last = false, 0, 0, 0, nil

local function log(s) Isaac.DebugString("cov: " .. s) end

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
    started = true
    log("run started")
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
    if not started or updates < 0 then return end
    local p = Isaac.GetPlayer(0)
    local input = p:GetMovementInput()
    if input.X ~= 0 or input.Y ~= 0 then
        held = held + 1
        if last then moved = moved + (p.Position - last):Length() end
    end
    last = p.Position
    updates = updates + 1
    if held >= 240 or updates >= 3600 then
        if held < 240 then
            log(string.format("walk: the stick was held for only %d updates", held))
        elseif moved < 100 then
            log(string.format("walk: STUCK, %.1f moved in %d updates with the stick held", moved, held))
        else
            log(string.format("walk: ok, %.0f moved in %d updates with the stick held", moved, held))
        end
        log("done")
        updates = -1
    end
end)
