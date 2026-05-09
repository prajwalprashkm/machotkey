local bit = require("bit")

local M = {}

local function lerp(a, b, t)
  return a + (b - a) * t
end

function M.lerp_rgba(color1, color2, factor)
  local r1 = bit.band(bit.rshift(color1, 24), 0xFF)
  local g1 = bit.band(bit.rshift(color1, 16), 0xFF)
  local b1 = bit.band(bit.rshift(color1, 8), 0xFF)
  local a1 = bit.band(color1, 0xFF)

  local r2 = bit.band(bit.rshift(color2, 24), 0xFF)
  local g2 = bit.band(bit.rshift(color2, 16), 0xFF)
  local b2 = bit.band(bit.rshift(color2, 8), 0xFF)
  local a2 = bit.band(color2, 0xFF)

  local r = math.floor(lerp(r1, r2, factor))
  local g = math.floor(lerp(g1, g2, factor))
  local b = math.floor(lerp(b1, b2, factor))
  local a = math.floor(lerp(a1, a2, factor))

  return bit.bor(bit.lshift(r, 24), bit.lshift(g, 16), bit.lshift(b, 8), a)
end

return M
