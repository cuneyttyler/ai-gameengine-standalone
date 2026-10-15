-- Milestone 0.5 hot-reload demo: bobs the NPC up and down in place.
-- Edit amplitude/speed below and save while the app is running — the
-- change takes effect within a fraction of a second, no restart needed.

local base_x, base_y = get_position()
local t = 0
local amplitude = 1.6
local speed = 2.0

function update(dt)
    t = t + dt
    set_position(base_x, base_y + math.sin(t * speed) * amplitude)
end
