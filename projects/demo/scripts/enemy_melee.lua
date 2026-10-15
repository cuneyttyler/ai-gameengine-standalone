-- enemy_melee.lua: a basic melee enemy.
-- Chasing is handled by the entity's Follow component; this script only
-- decides when to swing. Edit and save while playing to hot-reload.

-- Per-entity settings: each one shows up in the Inspector under LuaScript,
-- where any skeleton (or the skeleton prefab) can change it.
properties = {
    swing_range = 1.1, -- cells; a bit more than MeleeAttack.reach
    taunt = "",        -- a sound to play when it first spots the player
}

local spotted = false

function update(dt)
    local target, distance = find_nearest("Player")
    if not target then
        return
    end
    if not spotted and distance < get_field(self_id(), "Follow", "range") then
        spotted = true
        if properties.taunt ~= "" then
            play_sound(properties.taunt)
        end
    end
    if distance < properties.swing_range then
        attack_toward(target)
    end
end
