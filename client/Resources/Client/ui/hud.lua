-- client/Resources/Client/ui/hud.lua
-- In-game HUD: chat overlay, player list, AI traffic indicator

local M = {}

local chat_log    = {}
local players     = {}
local show_players= false
local ai_enabled  = false
local MAX_CHAT    = 20

function M.addChatMessage(msg)
    table.insert(chat_log, { text = msg, age = 0 })
    if #chat_log > MAX_CHAT then table.remove(chat_log, 1) end
end

function M.updatePlayers(list) players = list end
function M.setAIEnabled(v)     ai_enabled = v end

function M.onUpdate(dt)
    for i = #chat_log, 1, -1 do
        chat_log[i].age = chat_log[i].age + dt
        if chat_log[i].age > 10 then table.remove(chat_log, i) end
    end
end

function M.onImguiFrame()
    local screen = im.GetIO().DisplaySize

    -- Chat (bottom-left)
    im.SetNextWindowPos(im.ImVec2(10, screen.y - 150))
    im.SetNextWindowSize(im.ImVec2(420, 140))
    im.SetNextWindowBgAlpha(0.4)
    if im.Begin("##novaMP_chat", nil,
        im.WindowFlags_NoDecoration + im.WindowFlags_NoInputs +
        im.WindowFlags_NoNav + im.WindowFlags_NoMove)
    then
        for _, msg in ipairs(chat_log) do
            local alpha = math.max(0, 1.0 - math.max(0, msg.age - 8) / 2)
            im.PushStyleColor2(im.Col_Text, im.ImVec4(1, 1, 1, alpha))
            im.TextUnformatted(msg.text)
            im.PopStyleColor()
        end
        im.SetScrollHereY(1.0)
    end
    im.End()

    -- Player list (top-right, Tab to toggle)
    if show_players and #players > 0 then
        im.SetNextWindowPos(im.ImVec2(screen.x - 220, 10))
        im.SetNextWindowSize(im.ImVec2(210, 30 + #players * 20))
        im.SetNextWindowBgAlpha(0.65)
        if im.Begin("##novaMP_players", nil,
            im.WindowFlags_NoDecoration + im.WindowFlags_NoInputs +
            im.WindowFlags_NoNav + im.WindowFlags_NoMove)
        then
            im.TextColored(im.ImVec4(0.4, 0.8, 1, 1),
                           "Players (" .. #players .. ")")
            im.Separator()
            for _, p in ipairs(players) do
                im.TextUnformatted(p.name)
            end
        end
        im.End()
    end

    -- AI Traffic indicator (top-left)
    if ai_enabled then
        im.SetNextWindowPos(im.ImVec2(10, 10))
        im.SetNextWindowSize(im.ImVec2(150, 26))
        im.SetNextWindowBgAlpha(0.5)
        if im.Begin("##novaMP_ai", nil,
            im.WindowFlags_NoDecoration + im.WindowFlags_NoInputs +
            im.WindowFlags_NoNav + im.WindowFlags_NoMove)
        then
            im.TextColored(im.ImVec4(0.2, 1, 0.4, 1), "AI Traffic: ON")
        end
        im.End()
    end
end

function M.onKeyDown(key)
    if key == "tab" then show_players = not show_players end
end

return M
