-- ============================================================================
-- CHIRP Droid Control — EdgeTX Telemetry & Direct CRSF Command Dashboard
-- Target Transmitters:
--   • Primary: Radiomaster Zorro / Pocket / Boxer (128x64 Monochrome LCD)
--   • Extended: Radiomaster TX16S / ColorLCD (480x272 Color Display)
--
-- Installation on EdgeTX SD Card:
--   Copy this file to: /SCRIPTS/TELEMETRY/CHIRP.lua
--   In Model Setup -> Telemetry -> Screen 1 -> Select "Script" -> "CHIRP"
-- ============================================================================

local CACHE_FILE = "/SCRIPTS/TELEMETRY/chirp_cache.txt"

-- CRSF 0x32 Direct Command Constants
local CRSF_FRAME_CMD       = 0x32
local CRSF_ADDR_FC         = 0xC8
local CRSF_ADDR_HANDSET    = 0xEA
local CHIRP_CMD_PLAY_SOUND = 0xA1
local CHIRP_CMD_STOP_AUDIO = 0xA2
local CHIRP_CMD_SET_PAGE   = 0xA4
local CHIRP_CMD_SYNC_MAN   = 0xA5

local ALL_PAGES = { 'A', 'B', 'C', 'D', 'E', 'F' }

-- Persistent Manifest & Cache State
local soundNames     = {}
local pageNames      = {}
local soundStructure = {}
local currentPage    = 'A'
local cacheDirty     = false
local lastCacheSave  = 0

for b = 1, 4 do
    soundStructure[b] = { A = 0, B = 0, C = 0, D = 0, E = 0, F = 0 }
end

-- Persistent Telemetry State (Retained across multiplexed CRSF sensor cycles)
local telem = {
    batt1       = 0.0,
    batt2       = 0.0,
    batt3       = 0.0,
    current     = 0.0,
    peakCurrent = 0.0,
    volume      = 50,
    speedMode   = 0,    -- 0=DIS, 1=SLO, 2=MED, 3=FST
    autoDome    = 0,    -- 0=off, 1=less, 2=more
    autoChirp   = false,
    domeHeading = 0,
    linkQuality = 0
}

-- UI Navigation & Visual Feedback State
local activeScreen    = 1 -- 1: Sound Dashboard, 2: Diagnostics, 3: Settings
local lastTrigBank    = 0
local lastTrigTime    = 0
local lastStopTime    = 0
local lastSyncReqTime = 0

-- Momentary Button Edge & Combo State (SD=Bank1, SH=Bank2, SA=Bank3, SG=Bank4, SA+SD=Stop)
local btnState = {
    sdPrev      = false,
    shPrev      = false,
    saPrev      = false,
    sgPrev      = false,
    comboActive = false
}

-- Page 3 Menu Items (Includes [back to pg1] at top & bottom, NVS Settings IDs 1..9, and Re-Sync Audio)
local menuItems = {
    { type = "nav_pg1",   name = "[back to pg1]" },
    { id = 1, name = "Bank 1 Page",   type = "char", val = 65,  min = 65,   max = 70  },
    { id = 2, name = "Dome Offset",   type = "int",  val = 0,   min = -127, max = 127 },
    { id = 3, name = "Dome Invert",   type = "bool", val = 0,   min = 0,    max = 1   },
    { id = 4, name = "Voice Debug",   type = "bool", val = 1,   min = 0,    max = 1   },
    { id = 5, name = "L Motor Inv",   type = "bool", val = 0,   min = 0,    max = 1   },
    { id = 6, name = "R Motor Inv",   type = "bool", val = 0,   min = 0,    max = 1   },
    { id = 7, name = "Speed Slow",    type = "int",  val = 30,  min = 0,    max = 100 },
    { id = 8, name = "Speed Med",     type = "int",  val = 70,  min = 0,    max = 100 },
    { id = 9, name = "Speed Fast",    type = "int",  val = 100, min = 0,    max = 100 },
    { type = "sync_audio", name = "Re-Sync Audio" },
    { type = "nav_pg1",    name = "[back to pg1]" }
}
local menuIdx      = 1
local settingsEdit = false

-- ============================================================================
-- Helper Functions
-- ============================================================================

local function sanitize(s)
    if not s then return "" end
    return string.gsub(s, "[%s%(%)%'%-%._]", "")
end

local function pushCrsfCommand(payload)
    if crossfireTelemetryPush ~= nil then
        crossfireTelemetryPush(CRSF_FRAME_CMD, payload)
    end
end

local function findSettingById(paramId)
    for _, item in ipairs(menuItems) do
        if item.id == paramId then
            return item
        end
    end
    return nil
end

-- Determine highest populated page letter across all banks (minimum 'C', up to 'F')
local function getMaxPageIdx()
    local maxIdx = 3 -- Default to at least A, B, C (indices 1..3)
    for pIdx = 4, #ALL_PAGES do
        local pChar = ALL_PAGES[pIdx]
        for b = 1, 4 do
            if (soundStructure[b][pChar] or 0) > 0 then
                maxIdx = pIdx
            end
        end
    end
    return maxIdx
end

local function getEffectivePage(bankNum)
    if bankNum == 1 then
        local b1Setting = findSettingById(1)
        local b1Char = string.char(b1Setting and b1Setting.val or 65)
        if b1Char >= 'A' and b1Char <= 'F' then
            return b1Char
        end
        return 'A'
    end
    local count = soundStructure[bankNum][currentPage]
    return (count and count > 0) and currentPage or 'A'
end

local function getSelectedTrack(bankNum, sliderNorm)
    local effPage = getEffectivePage(bankNum)
    local count   = soundStructure[bankNum][effPage]
    if not count or count <= 0 then count = 1 end
    local idx = math.floor(sliderNorm * count) + 1
    if idx < 1 then idx = 1 end
    if idx > count then idx = count end
    return idx, effPage, count
end

local function triggerBankSound(bankNum, sliderNorm)
    local trackIdx, effPage = getSelectedTrack(bankNum, sliderNorm)
    pushCrsfCommand({
        CRSF_ADDR_FC,
        CRSF_ADDR_HANDSET,
        CHIRP_CMD_PLAY_SOUND,
        bankNum,
        string.byte(effPage),
        trackIdx
    })
    lastTrigBank = bankNum
    lastTrigTime = getTime()
end

local function triggerStopAudio()
    pushCrsfCommand({
        CRSF_ADDR_FC,
        CRSF_ADDR_HANDSET,
        CHIRP_CMD_STOP_AUDIO
    })
    lastStopTime = getTime()
end

-- ============================================================================
-- SD Card Manifest Cache Persistence
-- ============================================================================

local function loadCache()
    local f = io.open(CACHE_FILE, "r")
    if f == nil then return end
    local content = io.read(f, 4096)
    io.close(f)
    if not content then return end

    for k, v in string.gmatch(content, "(%w+)=([^;\r\n]+);") do
        local prefix = string.sub(k, 1, 1)
        if prefix == "C" then
            local b = tonumber(string.sub(k, 2, 2))
            local p = string.sub(k, 3, 3)
            local c = tonumber(v)
            if b and p and c and soundStructure[b] then
                soundStructure[b][p] = c
            end
        elseif prefix == "P" then
            local bp = string.sub(k, 2, 3)
            pageNames[bp] = v
        elseif prefix == "S" then
            local paramId = tonumber(string.sub(k, 2))
            local item = findSettingById(paramId)
            if item then
                item.val = tonumber(v) or item.val
            end
        else
            soundNames[k] = v
        end
    end
end

local function saveCache()
    if not cacheDirty then return end
    local f = io.open(CACHE_FILE, "w")
    if f == nil then return end

    for k, v in pairs(soundNames) do
        io.write(f, k .. "=" .. v .. ";\n")
    end
    for k, v in pairs(pageNames) do
        io.write(f, "P" .. k .. "=" .. v .. ";\n")
    end
    for _, item in ipairs(menuItems) do
        if item.id ~= nil then
            io.write(f, "S" .. item.id .. "=" .. item.val .. ";\n")
        end
    end
    for b = 1, 4 do
        for _, p in ipairs(ALL_PAGES) do
            io.write(f, "C" .. b .. p .. "=" .. (soundStructure[b][p] or 0) .. ";\n")
        end
    end
    io.close(f)
    cacheDirty = false
end

-- ============================================================================
-- Telemetry & Switch Input Polling (Runs in both Background & Foreground)
-- ============================================================================

local function pollTelemetryAndButtons()
    -- 1. Parse Flight Mode (0x21) Manifest Stream:
    --    "<Bank><Page>#<Count>", "<Bank><Page>0<PageName>", "<Bank><Page><1..o><TrackName>"
    local fmString = getValue('FM')
    if fmString ~= nil and type(fmString) == "string" and string.len(fmString) >= 4 then
        local bStr, p, sChar, payload = string.match(fmString, "(%d)(%u)(.)(.+)")
        if bStr and p and sChar and payload then
            local b = tonumber(bStr)
            local byteVal = string.byte(sChar)
            if b and b >= 1 and b <= 4 and p >= 'A' and p <= 'F' then
                if byteVal == 35 then -- '#' -> Sound Count for (Bank, Page)
                    local cnt = tonumber(payload)
                    if cnt and soundStructure[b][p] ~= cnt then
                        soundStructure[b][p] = cnt
                        cacheDirty = true
                    end
                elseif byteVal == 48 then -- '0' -> Page Name
                    local key = b .. p
                    local clean = sanitize(payload)
                    if clean ~= "" and pageNames[key] ~= clean then
                        pageNames[key] = clean
                        cacheDirty = true
                    end
                else
                    local trackIdx = 0
                    if byteVal >= 49 and byteVal <= 57 then
                        trackIdx = byteVal - 48
                    elseif byteVal >= 97 and byteVal <= 122 then
                        trackIdx = byteVal - 87
                    end
                    if trackIdx >= 1 and trackIdx <= 24 then
                        local key = b .. p .. trackIdx
                        local clean = sanitize(payload)
                        if clean ~= "" and soundNames[key] ~= clean then
                            soundNames[key] = clean
                            cacheDirty = true
                        end
                    end
                end
            end
        end
    end

    -- 2. Parse Multiplexed 3-Battery Telemetry on RxBt (0x08)
    local rawVoltage = getValue('RxBt')
    if rawVoltage ~= nil and rawVoltage > 0 then
        if rawVoltage < 100.0 then
            telem.batt1 = rawVoltage
        elseif rawVoltage >= 100.0 and rawVoltage < 200.0 then
            telem.batt2 = rawVoltage - 100.0
        elseif rawVoltage >= 200.0 and rawVoltage < 300.0 then
            telem.batt3 = rawVoltage - 200.0
        elseif rawVoltage >= 1000 and rawVoltage < 2000 then
            telem.batt2 = (rawVoltage - 1000) / 10.0
        elseif rawVoltage >= 2000 then
            telem.batt3 = (rawVoltage - 2000) / 10.0
        end
    end

    telem.current     = getValue('Curr') or telem.current
    telem.peakCurrent = getValue('Capa') or telem.peakCurrent
    telem.volume      = math.floor(getValue('GSpd') or telem.volume)
    telem.linkQuality = getValue('TQly') or getValue('RQly') or 0

    -- 3. Parse Packed Robot Status in GPS Satellites (Sats)
    local rawSats = getValue('Sats')
    if rawSats ~= nil then
        telem.speedMode = bit32.band(rawSats, 0x03)
        telem.autoChirp = (bit32.band(rawSats, 0x08) ~= 0)
        local adMode    = bit32.band(bit32.rshift(rawSats, 4), 0x03)
        if adMode > 0 then
            telem.autoDome = adMode
        else
            telem.autoDome = (bit32.band(rawSats, 0x04) ~= 0) and 1 or 0
        end
    end

    -- 4. Parse Active Page Sound Counts in Hdg (Banks 1-2) & GAlt (Banks 3-4)
    local rawHdg = math.floor((getValue('Hdg') or 0) * 100 + 0.5)
    if rawHdg > 0 then
        local c1 = bit32.band(rawHdg, 0x1F)
        local c2 = bit32.band(bit32.rshift(rawHdg, 5), 0x1F)
        local b1Page = getEffectivePage(1)
        if c1 > 0 and soundStructure[1][b1Page] ~= c1 then
            soundStructure[1][b1Page] = c1
            cacheDirty = true
        end
        if c2 > 0 and soundStructure[2][currentPage] ~= c2 then
            soundStructure[2][currentPage] = c2
            cacheDirty = true
        end
    end

    local rawAlt = getValue('GAlt')
    if rawAlt ~= nil then
        local altPayload = math.floor(rawAlt + 0.5)
        if altPayload >= 0 then
            local c3 = bit32.band(altPayload, 0x1F)
            local c4 = bit32.band(bit32.rshift(altPayload, 5), 0x1F)
            if c3 > 0 and soundStructure[3][currentPage] ~= c3 then
                soundStructure[3][currentPage] = c3
                cacheDirty = true
            end
            if c4 > 0 and soundStructure[4][currentPage] ~= c4 then
                soundStructure[4][currentPage] = c4
                cacheDirty = true
            end
        end
    end

    -- 5. Read Dial S1 for Sound Selection (Normalized 0.0 .. 1.0)
    local s1Raw = getValue('s1') or getValue('ch7') or -1024
    if s1Raw < -1024 then s1Raw = -1024 end
    if s1Raw > 1024  then s1Raw = 1024  end
    local sliderNorm = (s1Raw + 1024) / 2048.0
    if sliderNorm >= 1.0 then sliderNorm = 0.9999 end

    -- 6. Poll Physical Momentary Buttons (SD=Bank1, SH=Bank2, SA=Bank3, SG=Bank4, SA+SD=Stop)
    local sdNow = (getValue('sd') or -1024) > 200
    local shNow = (getValue('sh') or -1024) > 200
    local saNow = (getValue('sa') or -1024) > 200
    local sgNow = (getValue('sg') or -1024) > 200

    -- Check SA + SD simultaneous Stop-Audio combo
    if saNow and sdNow then
        if not btnState.comboActive then
            btnState.comboActive = true
            triggerStopAudio()
        end
    elseif not saNow and not sdNow then
        -- Cleanly trigger Bank 1 (SD) or Bank 3 (SA) on release if not part of a Stop combo
        if not btnState.comboActive then
            if btnState.sdPrev then
                triggerBankSound(1, sliderNorm)
            elseif btnState.saPrev then
                triggerBankSound(3, sliderNorm)
            end
        end
        btnState.comboActive = false
    end

    -- Bank 2 (SH) and Bank 4 (SG) trigger immediately on rising press edge
    if shNow and not btnState.shPrev then
        triggerBankSound(2, sliderNorm)
    end
    if sgNow and not btnState.sgPrev then
        triggerBankSound(4, sliderNorm)
    end

    btnState.sdPrev = sdNow
    btnState.shPrev = shNow
    btnState.saPrev = saNow
    btnState.sgPrev = sgNow

    return sliderNorm
end

-- ============================================================================
-- Screen Renderers (128x64 Zorro/Boxer/Pocket)
-- ============================================================================

local function getSpeedLabel()
    if telem.speedMode == 1 then return "SLO"
    elseif telem.speedMode == 2 then return "MED"
    elseif telem.speedMode == 3 then return "FST"
    end
    return "DIS"
end

-- Draws a full-width inverted title bar with page indicator flush against right edge (x=128)
local function drawFullHeader128x64(title, pageTag)
    lcd.drawText(0, 0, title .. "                ", SMLSIZE + INVERS)
    lcd.drawText(128, 0, pageTag, SMLSIZE + RIGHT + INVERS)
    lcd.drawLine(0, 7, 127, 7, SOLID, FORCE)
end

local function drawDashboard128x64(sliderNorm)
    local now = getTime()

    -- Top Status Header (y = 0..6)
    local spdStr = getSpeedLabel()
    lcd.drawText(0, 0, spdStr .. " v" .. telem.volume .. " LQ" .. telem.linkQuality, SMLSIZE)
    lcd.drawText(128, 0, string.format("P:%.1fv", telem.batt1), SMLSIZE + RIGHT)
    lcd.drawLine(0, 7, 127, 7, SOLID, FORCE)

    -- Quadrant Crosshairs & Center Active Page Badge
    lcd.drawLine(64, 8, 64, 63, DOTTED, FORCE)
    lcd.drawLine(0, 35, 127, 35, DOTTED, FORCE)
    lcd.drawText(58, 32, " " .. currentPage .. " ", SMLSIZE + INVERS)

    -- Render each of the 4 Sound Bank Quadrants:
    --   Top-Left:     Bank 2 (SH)     Top-Right:    Bank 1 (SD)
    --   Bottom-Left:  Bank 3 (SA)     Bottom-Right: Bank 4 (SG)
    local function drawBankQuadrant(x, y, bankNum, alignRight)
        local localIdx, effPage, count = getSelectedTrack(bankNum, sliderNorm)
        local isTriggered = (lastTrigBank == bankNum) and ((now - lastTrigTime) < 40)

        local nameCurr = soundNames[bankNum .. effPage .. localIdx] or ("Snd" .. localIdx)
        local namePrev = (localIdx == 1)
            and ("[" .. (pageNames[bankNum .. effPage] or ("B" .. bankNum)) .. "]")
            or  (soundNames[bankNum .. effPage .. (localIdx - 1)] or ("Snd" .. (localIdx - 1)))
        local nameNext = (localIdx == count)
            and ("[" .. (pageNames[bankNum .. effPage] or ("B" .. bankNum)) .. "]")
            or  (soundNames[bankNum .. effPage .. (localIdx + 1)] or ("Snd" .. (localIdx + 1)))

        local smlFlags = alignRight and (SMLSIZE + RIGHT) or SMLSIZE
        local curFlags = alignRight and RIGHT or 0
        if isTriggered then
            curFlags = curFlags + INVERS
        end
        local tx = alignRight and (x + 61) or x

        lcd.drawText(tx, y,      namePrev, smlFlags)
        lcd.drawText(tx, y + 8,  nameCurr, curFlags)
        lcd.drawText(tx, y + 16, nameNext, smlFlags)
    end

    drawBankQuadrant(0,  10, 2, false)
    drawBankQuadrant(66, 10, 1, true)
    drawBankQuadrant(0,  39, 3, false)
    drawBankQuadrant(66, 39, 4, true)

    -- Overlay brief Stop-Audio banner if SA+SD was just pressed
    if (now - lastStopTime) < 50 then
        lcd.drawFilledRectangle(20, 22, 88, 18, ERASE)
        lcd.drawRectangle(20, 22, 88, 18, SOLID)
        lcd.drawText(26, 27, "AUDIO STOPPED", SMLSIZE + INVERS)
    end
end

local function drawDiagnostics128x64()
    drawFullHeader128x64("CHIRP Diagnostics", "[2/3]")

    lcd.drawText(0, 11, string.format("B1:%.1fv B2:%.1fv B3:%.1fv", telem.batt1, telem.batt2, telem.batt3), SMLSIZE)
    lcd.drawText(0, 20, string.format("Curr:%.1fA  Peak:%.1fA", telem.current, telem.peakCurrent), SMLSIZE)
    lcd.drawText(0, 29, string.format("Spd:%s     Dome:%dd", getSpeedLabel(), telem.domeHeading), SMLSIZE)

    local domeModeStr = (telem.autoDome == 2) and "more" or ((telem.autoDome == 1) and "less" or "off")
    local chirpStr    = telem.autoChirp and "on" or "off"
    lcd.drawText(0, 38, string.format("Auto [ dome:%s chirp:%s ]", domeModeStr, chirpStr), SMLSIZE)

    lcd.drawLine(0, 48, 127, 48, DOTTED, FORCE)
    local b1Setting = findSettingById(1)
    local b1Char    = string.char(b1Setting and b1Setting.val or 65)
    lcd.drawText(0, 52, string.format("B1Pg:%s     ActivePg:%s", b1Char, currentPage), SMLSIZE)
end

local function drawSettings128x64()
    local now = getTime()
    drawFullHeader128x64("CHIRP Settings", "[3/3]")

    -- Compute 5-row scrolling window around menuIdx
    local startIdx = menuIdx - 2
    if startIdx > (#menuItems - 4) then startIdx = #menuItems - 4 end
    if startIdx < 1 then startIdx = 1 end

    local y = 11
    for i = 0, 4 do
        local idx = startIdx + i
        if idx >= 1 and idx <= #menuItems then
            local item  = menuItems[idx]
            local isSel = (idx == menuIdx)
            local prefix = isSel and (settingsEdit and " > " or " * ") or "   "

            if item.type == "nav_pg1" then
                local flags = SMLSIZE
                if isSel then flags = flags + INVERS end
                lcd.drawText(0, y, prefix .. item.name, flags)
            elseif item.type == "sync_audio" then
                local statusStr = ((now - lastSyncReqTime) < 120 and lastSyncReqTime > 0) and "SENT!" or "[run]"
                local flags = SMLSIZE
                if isSel and ((now - lastSyncReqTime) < 120 and lastSyncReqTime > 0) then
                    flags = flags + INVERS
                end
                lcd.drawText(0, y, prefix .. item.name .. ": " .. statusStr, flags)
            else
                local valStr = ""
                if item.type == "char" then
                    valStr = string.char(item.val)
                elseif item.type == "bool" then
                    valStr = (item.val == 1) and "ON" or "OFF"
                else
                    valStr = tostring(item.val)
                end
                local flags = SMLSIZE
                if isSel and settingsEdit then
                    flags = flags + INVERS
                end
                lcd.drawText(0, y, prefix .. item.name .. ": " .. valStr, flags)
            end
        end
        y = y + 10
    end
end

-- ============================================================================
-- EdgeTX Lifecycle Entrypoints
-- ============================================================================

local function cycleActivePage(delta)
    local maxIdx = getMaxPageIdx()
    local curIdx = 1
    for i = 1, maxIdx do
        if ALL_PAGES[i] == currentPage then
            curIdx = i
            break
        end
    end
    curIdx = ((curIdx - 1 + delta) % maxIdx) + 1
    currentPage = ALL_PAGES[curIdx]
    pushCrsfCommand({ CRSF_ADDR_FC, CRSF_ADDR_HANDSET, CHIRP_CMD_SET_PAGE, string.byte(currentPage) })
end

local function init()
    loadCache()
end

local function background()
    pollTelemetryAndButtons()
    local now = getTime()
    if cacheDirty and (now - lastCacheSave > 500) then
        saveCache()
        lastCacheSave = now
    end
end

local function run(event)
    local sliderNorm = pollTelemetryAndButtons()

    -- Dynamically update Bank 1 Page max letter based on populated pages (at least 'C', up to 'F')
    local b1Item = findSettingById(1)
    if b1Item then
        b1Item.max = string.byte(ALL_PAGES[getMaxPageIdx()])
    end

    -- Handle User Input Events
    if activeScreen == 3 and settingsEdit then
        local item = menuItems[menuIdx]
        if event == EVT_ENTER_BREAK then
            settingsEdit = false
            local pVal = item.val
            if pVal < 0 then pVal = 256 + pVal end
            pushCrsfCommand({ CRSF_ADDR_FC, CRSF_ADDR_HANDSET, item.id, pVal })
            cacheDirty = true
        elseif event == EVT_EXIT_BREAK then
            settingsEdit = false
        elseif event == EVT_ROT_LEFT or event == EVT_MINUS_FIRST or event == EVT_MINUS_RPT then
            item.val = math.max(item.min, item.val - 1)
        elseif event == EVT_ROT_RIGHT or event == EVT_PLUS_FIRST or event == EVT_PLUS_RPT then
            item.val = math.min(item.max, item.val + 1)
        end
    else
        if event == EVT_PAGE_BREAK or (event == EVT_ENTER_BREAK and activeScreen ~= 3) then
            activeScreen = (activeScreen % 3) + 1
            if activeScreen == 3 then
                menuIdx = 1
                settingsEdit = false
            end
        elseif event == EVT_EXIT_BREAK then
            activeScreen = 1
            settingsEdit = false
        elseif activeScreen == 1 then
            -- Scroll wheel on Dashboard switches active Sound Page (A..maxPopulatedPage)
            if event == EVT_ROT_RIGHT or event == EVT_PLUS_FIRST then
                cycleActivePage(1)
            elseif event == EVT_ROT_LEFT or event == EVT_MINUS_FIRST then
                cycleActivePage(-1)
            end
        elseif activeScreen == 3 then
            if event == EVT_ENTER_BREAK then
                local item = menuItems[menuIdx]
                if item.type == "nav_pg1" then
                    activeScreen = 1
                    settingsEdit = false
                elseif item.type == "sync_audio" then
                    pushCrsfCommand({ CRSF_ADDR_FC, CRSF_ADDR_HANDSET, CHIRP_CMD_SYNC_MAN })
                    lastSyncReqTime = getTime()
                else
                    settingsEdit = true
                end
            elseif event == EVT_ROT_LEFT or event == EVT_MINUS_FIRST then
                menuIdx = math.max(1, menuIdx - 1)
            elseif event == EVT_ROT_RIGHT or event == EVT_PLUS_FIRST then
                menuIdx = math.min(#menuItems, menuIdx + 1)
            end
        end
    end

    -- Render Active Screen
    lcd.clear()
    if activeScreen == 1 then
        drawDashboard128x64(sliderNorm)
    elseif activeScreen == 2 then
        drawDiagnostics128x64()
    else
        drawSettings128x64()
    end

    local now = getTime()
    if cacheDirty and (now - lastCacheSave > 500) then
        saveCache()
        lastCacheSave = now
    end

    return 0
end

return { init = init, background = background, run = run }
