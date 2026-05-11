local bit = require("bit")

local M = {}

local function lerp(a, b, t)
  return a + (b - a) * t
end

function M.lerpRGBA(a, b, t)
  local function ch(x, shift)
    return bit.band(bit.rshift(x, shift), 0xFF)
  end
  local r = math.floor(lerp(ch(a, 24), ch(b, 24), t))
  local g = math.floor(lerp(ch(a, 16), ch(b, 16), t))
  local bl = math.floor(lerp(ch(a, 8), ch(b, 8), t))
  local al = math.floor(lerp(bit.band(a, 0xFF), bit.band(b, 0xFF), t))
  return bit.bor(bit.lshift(r, 24), bit.lshift(g, 16), bit.lshift(bl, 8), al)
end

return M
