-- coverage_mod/effects.lua (coverage.py --mod effects): the screen effects one after another in the run's first
-- room, each logged ("cov: effect ...") a second before the next, so the screenshots and the shader samples
-- (ISAAC360_SHADER_SAMPLES) catch each one: a pill (bloom), Retro Vision (pixelation), Wavy Cap (the dizzy wobble),
-- the Death card (smoke over the room) and the Tower card (explosions: the shockwave). Two minutes.
local mod = RegisterMod("isaac360 effects check", 1)
local started, updates = false, 0

local function log(s) Isaac.DebugString("cov: " .. s) end

local steps = {
    {60, "pill (bloom)", function(p) p:UsePill(PillEffect.PILLEFFECT_BAD_TRIP, PillColor.PILL_BLUE_BLUE) end},
    {300, "Retro Vision (pixelation)", function(p) p:UsePill(PillEffect.PILLEFFECT_RETRO_VISION, PillColor.PILL_BLUE_BLUE) end},
    {720, "Wavy Cap (dizzy)", function(p) p:UseActiveItem(CollectibleType.COLLECTIBLE_WAVY_CAP) end},
    {1140, "Death card (smoke)", function(p) p:UseCard(Card.CARD_DEATH) end},
    {1440, "Tower card (shockwave)", function(p) p:UseCard(Card.CARD_TOWER) end},
}

mod:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
    started = true
    p = Isaac.GetPlayer(0)
    p:AddMaxHearts(12)
    p:AddHearts(12)
    log("run started")
end)

mod:AddCallback(ModCallbacks.MC_POST_UPDATE, function()
    if not started or updates < 0 then return end
    updates = updates + 1
    local p = Isaac.GetPlayer(0)
    for _, s in ipairs(steps) do
        if updates == s[1] then
            log("effect " .. s[2] .. " at update " .. updates)
            local ok, err = pcall(s[3], p)
            if not ok then log("  -> " .. tostring(err)) end
        end
    end
    if updates >= 1800 then
        log("done")
        updates = -1
    end
end)

mod:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, function(_, e)
    if e.Type == EntityType.ENTITY_PLAYER then return false end
end)
