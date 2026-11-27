-- isaac360 ending test (tools/stage_console.py --ending-test): the player takes no damage, so a run reaches the
-- ending's chest. Appended to an overlay copy of the game's resources/scripts/main.lua in save/overlay, which the
-- port reads before the game's own; staging without --ending-test removes it.
local isaac360EndingTest = RegisterMod("isaac360 ending test", 1)

isaac360EndingTest:AddCallback(ModCallbacks.MC_ENTITY_TAKE_DMG, function(_, entity)
    if entity.Type == EntityType.ENTITY_PLAYER then return false end
end, EntityType.ENTITY_PLAYER)

isaac360EndingTest:AddCallback(ModCallbacks.MC_POST_GAME_STARTED, function()
    Isaac.DebugString("isaac360 ending test: the player takes no damage")
end)
