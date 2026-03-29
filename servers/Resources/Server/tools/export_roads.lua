-- servers/Resources/Server/tools/export_roads.lua
--
-- NovaMP Road Network Exporter
-- Run this script from the BeamNG.drive in-game Lua console ONCE per map
-- to export the AI waypoint graph that the NovaMP server uses for traffic.
--
-- Usage (BeamNG in-game Lua console — F8):
--   luaConsole:execute('load("novaMP/tools/export_roads.lua")')
--   OR paste the contents directly into the console.
--
-- Output file:
--   <BeamNG userdata>/levels/<mapname>/novaMP_roads.json
--
-- Copy that file to:
--   NovaMP/servers/Resources/Server/maps/<mapname>/roads.json
--
-- The server will automatically load it on next startup.

local function exportRoads()
    local mapName = getMissionFilename()
    if not mapName then
        print("[NovaMP Exporter] ERROR: No map loaded.")
        return
    end
    -- Strip path/extension to get bare map name
    mapName = mapName:match("([^/\\]+)$"):gsub("%..*$","")

    -- ── Method 1: AI Waypoints (preferred) ────────────────────────────────────
    -- Check if the map has an aiWaypoints object in the scene
    local waypoints = scenetree.findClassObjects("AIWaypoint")
    if waypoints and #waypoints > 0 then
        print(("[NovaMP Exporter] Found %d AI waypoints — exporting..."):format(#waypoints))

        local data = { class = "AIWaypoints", waypoints = {} }
        -- Build index lookup for link resolution
        local nameToIdx = {}
        for i, wpName in ipairs(waypoints) do
            nameToIdx[wpName] = i - 1   -- 0-based index
        end

        for i, wpName in ipairs(waypoints) do
            local obj = scenetree.findObject(wpName)
            if obj then
                local pos = obj:getPosition()
                local links = {}
                -- Iterate outgoing links via the waypoint's link list
                local linkCount = obj:getLinkCount and obj:getLinkCount() or 0
                for li = 0, linkCount - 1 do
                    local linkName = obj:getLinkName(li)
                    if nameToIdx[linkName] then
                        table.insert(links, tostring(linkName))
                    end
                end
                data.waypoints[wpName] = {
                    pos   = { pos.x, pos.y, pos.z },
                    links = links,
                    speed = obj:getField("speed", 0) or 14.0,
                    width = obj:getField("width", 0) or 3.5,
                }
            end
        end

        local outPath = ("levels/%s/novaMP_roads.json"):format(mapName)
        local f = io.open(outPath, "w")
        if f then
            f:write(jsonEncode(data))
            f:close()
            print(("[NovaMP Exporter] Written %d waypoints to: %s"):format(
                #waypoints, outPath))
            print("Copy to: servers/Resources/Server/maps/" .. mapName .. "/roads.json")
        else
            print("[NovaMP Exporter] ERROR: Could not write " .. outPath)
        end
        return
    end

    -- ── Method 2: DecalRoads (fallback — generates centerline waypoints) ──────
    local roads = scenetree.findClassObjects("DecalRoad")
    if roads and #roads > 0 then
        print(("[NovaMP Exporter] No AI waypoints found. "
              .."Building from %d DecalRoad objects..."):format(#roads))

        local data = { class = "AIWaypoints", waypoints = {} }
        local nodeIdx = 0
        local prevLastId = nil

        for _, roadName in ipairs(roads) do
            local road = scenetree.findObject(roadName)
            if road then
                local nodeCount = road:getNodeCount and road:getNodeCount() or 0
                local speedLimit = road:getField("speedLimit", 0) or 14.0
                local laneWidth  = road:getField("laneWidth",  0) or 3.5
                local firstId    = nil
                local lastId     = nil

                for ni = 0, nodeCount - 1 do
                    local node = road:getNode(ni)
                    if node then
                        local id = ("wp_%d"):format(nodeIdx)
                        local links = {}
                        -- Link to previous node on this road
                        if lastId then table.insert(links, lastId) end

                        data.waypoints[id] = {
                            pos   = { node.x, node.y, node.z },
                            links = links,
                            speed = tonumber(speedLimit) or 14.0,
                            width = tonumber(laneWidth)  or 3.5,
                        }
                        -- Back-link: previous node points to this one
                        if lastId then
                            table.insert(data.waypoints[lastId].links, id)
                        end

                        if not firstId then firstId = id end
                        lastId  = id
                        nodeIdx = nodeIdx + 1
                    end
                end

                -- Connect end of previous road segment to start of this one
                if prevLastId and firstId then
                    table.insert(data.waypoints[prevLastId].links, firstId)
                end
                prevLastId = lastId
            end
        end

        local outPath = ("levels/%s/novaMP_roads.json"):format(mapName)
        local f = io.open(outPath, "w")
        if f then
            f:write(jsonEncode(data))
            f:close()
            print(("[NovaMP Exporter] Written %d road nodes to: %s"):format(
                nodeIdx, outPath))
            print("Copy to: servers/Resources/Server/maps/" .. mapName .. "/roads.json")
        else
            print("[NovaMP Exporter] ERROR: Could not write " .. outPath)
        end
        return
    end

    print("[NovaMP Exporter] No AI waypoints or DecalRoads found on this map.")
    print("The server will fall back to the procedural grid.")
end

exportRoads()
