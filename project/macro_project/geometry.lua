local M = {}

function M.union(...)
    local rects = { ... }
    if #rects == 0 then
        return nil
    end
    local minX, minY = rects[1].x, rects[1].y
    local maxX, maxY = minX + rects[1].w, minY + rects[1].h
    for i = 2, #rects do
        local r = rects[i]
        minX = math.min(minX, r.x)
        minY = math.min(minY, r.y)
        maxX = math.max(maxX, r.x + r.w)
        maxY = math.max(maxY, r.y + r.h)
    end
    return { x = minX, y = minY, w = maxX - minX, h = maxY - minY }
end

function M.round(num)
    return math.floor(num + 0.5)
end

function M.round_rect(rect)
    return { x = M.round(rect.x), y = M.round(rect.y), w = M.round(rect.w), h = M.round(rect.h) }
end

function M.get_corrected_rect(child, old_parent, new_parent)
    return {
        x = old_parent.x + child.x - new_parent.x,
        y = old_parent.y + child.y - new_parent.y,
        w = child.w,
        h = child.h,
    }
end

return M
