-- Commands for the Lantern module (module.json). Each gets its parameters
-- as a table; returning false, "why" fails the command.

function set_all(args)
  local count = 0
  for _, id in ipairs(entities_with("Lantern")) do
    if set_field(id, "Lantern", "lit", args.lit) then
      count = count + 1
    end
  end
  log((args.lit and "lit " or "put out ") .. count .. " lantern(s)")
end

function scale_radius(args)
  local lanterns = entities_with("Lantern")
  if #lanterns == 0 then
    return false, "there are no lanterns in the scene"
  end
  for _, id in ipairs(lanterns) do
    local radius = get_field(id, "Lantern", "radius") * args.factor
    set_field(id, "Lantern", "radius", math.min(20, math.max(0.5, radius)))
  end
end
