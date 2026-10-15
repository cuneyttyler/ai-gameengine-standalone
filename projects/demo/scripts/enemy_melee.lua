-- enemy_melee.lua: a basic melee enemy.
-- Chasing is handled by the entity's Follow component; this script only
-- decides when to swing. Edit and save while playing to hot-reload.

local swing_range = 1.1 -- cells; a bit more than MeleeAttack.reach

function update(dt)
    local target, distance = find_nearest("Player")
    if target and distance < swing_range then
        attack_toward(target)
    end
end
