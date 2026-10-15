-- Recorded from mistral-large-latest (golden prompt flee_script, before the prompt
-- documented get_health/get_field precisely). Kept as a fixture: it is schema-valid
-- but fails at runtime, which the dry run must catch.
-- skeleton_flee.lua: a melee enemy that flees when hurt.
-- Chasing is handled by the entity's Follow component; this script
-- decides when to swing and when to flee.

properties = {
    swing_range = 1.1, -- cells; a bit more than MeleeAttack.reach
    flee_health = 3,   -- flee when health drops below this
    taunt = "",       -- a sound to play when it first spots the player
}

local spotted = false
local fleeing = false

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
    local current_health = get_health()[1]
    if current_health < properties.flee_health then
        fleeing = true
    end
    if fleeing then
        -- Pause the Follow component (it would keep walking toward the player)
        set_field(self_id(), "Follow", "speed", 0)
        -- Run away from the player
        local tx, ty = position_of(target)
        local x, y = get_position()
        local dx, dy = x - tx, y - ty
        local len = math.sqrt(dx * dx + dy * dy)
        if len > 0 then
            dx, dy = dx / len, dy / len
        end
        move_toward(x + dx, y + dy, get_field(self_id(), "Stats", "moveSpeed"))
    else
        -- Restore the Follow component's speed
        set_field(self_id(), "Follow", "speed", get_field(nil, "prefabs/skeleton.prefab", "Follow", "speed"))
        if distance < properties.swing_range then
            attack_toward(target)
        end
    end
end
