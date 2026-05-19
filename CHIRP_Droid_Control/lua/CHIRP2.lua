-- CHIRP2 LUA Telemetry Script for Radiomaster Zorro

local CACHE_FILE = "/SCRIPTS/TELEMETRY/chirp2_cache.txt"
local soundNames = {}
local pageNames = {}
local soundStructure = {}
local currentPage = 'A'
local cacheDirty = false
local lastPvSaveTime = 0

-- Strip characters that waste space on the small Zorro LCD
local function sanitize(s)
    return string.gsub(s, "[%s%(%)%'%-%._]", "")
end
local activeScreen = 1 -- 1: Dashboard, 2: Diagnostics

-- Init structure
for b=1,4 do
    soundStructure[b] = {}
    soundStructure[b]['A'] = 0
    soundStructure[b]['B'] = 0
    soundStructure[b]['C'] = 0
end

local function loadCache()
    local f = io.open(CACHE_FILE, "r")
    if f ~= nil then
        local content = io.read(f, 4096)
        io.close(f)
        if content then
            for k, v in string.gmatch(content, "(%w+)=([^;]+);") do
                if string.sub(k, 1, 1) == "C" then
                    local b = tonumber(string.sub(k, 2, 2))
                    local p = string.sub(k, 3, 3)
                    local count = tonumber(v)
                    if b and p and count and soundStructure[b] then
                        soundStructure[b][p] = count
                    end
                elseif string.sub(k, 1, 1) == "P" then
                    local b = string.sub(k, 2, 2)
                    local p = string.sub(k, 3, 3)
                    pageNames[b .. p] = v
                else
                    soundNames[k] = v
                end
            end
        end
    end
end

local function saveCache()
    if not cacheDirty then return end
    local f = io.open(CACHE_FILE, "w")
    if f ~= nil then
        for k, v in pairs(soundNames) do io.write(f, k .. "=" .. v .. ";\n") end
        for k, v in pairs(pageNames) do io.write(f, "P" .. k .. "=" .. v .. ";\n") end
        for b=1,4 do
            for _, p in ipairs({'A', 'B', 'C'}) do
                 io.write(f, "C" .. b .. p .. "=" .. soundStructure[b][p] .. ";\n")
            end
        end
        io.close(f)
        cacheDirty = false
    end
end

local function init()
    loadCache()
end

local function background()
    local now = getTime()
    if cacheDirty and (now - lastPvSaveTime > 1000) then
        saveCache()
        lastPvSaveTime = now
    end
end

local function run(event)
  lcd.clear()

  if event == EVT_PAGE_BREAK or event == EVT_ENTER_BREAK then
      activeScreen = (activeScreen == 1) and 2 or 1
  end

  -- 1. TELEMETRY - FLIGHT MODE
  local fmString = getValue('FM') 
  if fmString ~= nil and type(fmString) == "string" and string.len(fmString) >= 4 then
      local bStr, p, sChar, name = string.match(fmString, "(%d)(%u)(.)(.+)")
      if bStr and p and sChar and name then
          local b = tonumber(bStr)
          local i = 0
          local byteVal = string.byte(sChar)
          if byteVal == 48 then
              local key = b .. p
              local clean = sanitize(name)
              if pageNames[key] ~= clean then
                  pageNames[key] = clean; cacheDirty = true
              end
          elseif byteVal >= 49 and byteVal <= 57 then i = byteVal - 48
          elseif byteVal >= 97 and byteVal <= 122 then i = byteVal - 87
          end
          
          if i > 0 and b <= 4 then
              local key = b .. p .. i
              local clean = sanitize(name)
              if soundNames[key] ~= clean then
                  soundNames[key] = clean; cacheDirty = true
              end
          end
      end
  end

  -- 2. INPUTS
  local pageVal = getValue('ch13')
  local page = 'A'
  if pageVal < -500 then page = 'A' elseif pageVal > 500 then page = 'C' else page = 'B' end
  currentPage = page

  local val = getValue('ch7') 
  if val < -1024 then val = -1024 end
  if val > 1024 then val = 1024 end
  local sliderNorm = (val + 1024) / 2048
  
  -- 3. TELEMETRY - SENSORS
  local rawVoltage = getValue('RxBt') or 0
  local batt1, batt2, batt3 = 0, 0, 0
  
  if rawVoltage < 1000 then
      batt1 = rawVoltage
  elseif rawVoltage >= 1000 and rawVoltage < 2000 then
      batt2 = rawVoltage - 1000
  elseif rawVoltage >= 2000 then
      batt3 = rawVoltage - 2000
  end

  local current = getValue('Curr') or 0
  local peakCurrent = getValue('Capa') or 0
  
  local volume = math.floor(getValue('GSpd') or 0)
  local rawSats = getValue('Sats') or 0
  local speedMode = rawSats & 0x03       -- Bits 0-1: speed mode
  local autodome = (rawSats & 0x04) ~= 0  -- Bit 2: autodome enabled
  local autochirp = (rawSats & 0x08) ~= 0 -- Bit 3: autochirp enabled
  local linkQuality = getValue('TQly') or 0
  local txVoltage = getValue('tx-voltage') or 0
  
  local rawHeading = math.floor((getValue('Hdg') or 0) * 100 + 0.5)
  local rawAlt = getValue('GAlt')
  local altPayload = (rawAlt ~= nil) and math.floor(rawAlt + 0.5) or -1

  if rawHeading > 0 then
      for b = 1, 2 do
          local shift = (b - 1) * 5
          local c = (rawHeading >> shift) & 0x1F 
          if soundStructure[b][currentPage] ~= c then
              soundStructure[b][currentPage] = c; cacheDirty = true
          end
      end
  end

  if altPayload >= 0 then
      for i = 0, 1 do
          local b = i + 3
          local shift = i * 5
          local c = (altPayload >> shift) & 0x1F
          if soundStructure[b][currentPage] ~= c then
              soundStructure[b][currentPage] = c; cacheDirty = true
          end
      end
  end

  if activeScreen == 1 then
      -- DASHBOARD SCREEN
      local speedStr = "DIS"
      if speedMode == 1 then speedStr = "SLO"
      elseif speedMode == 2 then speedStr = "MED"
      elseif speedMode == 3 then speedStr = "FST"
      end
      lcd.drawText(0, 0, speedStr .. "  v" .. volume .. "  LQ" .. linkQuality, SMLSIZE)
      
      lcd.drawText(128, 0, string.format("P:%.1fv", batt1), SMLSIZE | RIGHT)
      lcd.drawLine(0, 7, 128, 7, SOLID, FORCE)
      lcd.drawLine(64, 7, 64, 64, DOTTED, FORCE)
      lcd.drawLine(0, 35, 128, 35, DOTTED, FORCE)
      lcd.drawText(59, 32, " " .. page .. " ", SMLSIZE | INVERS)
      
      local function getEffectivePage(bankNum)
          if bankNum == 1 then return 'A' end
          local count = soundStructure[bankNum][page]
          return (count and count > 0) and page or 'A'
      end

      local function drawBankBlock(x, y, bankNum, alignRight)
          local displayPage = getEffectivePage(bankNum)
          local count = soundStructure[bankNum][displayPage]
          if count == nil or count == 0 then count = 1 end
          
          local localIndex = math.floor(sliderNorm * count) + 1
          if localIndex < 1 then localIndex = 1 end
          if localIndex > count then localIndex = count end
          
          local nameCurr = soundNames[bankNum .. displayPage .. localIndex] or ("Snd " .. localIndex)
          
          local namePrev = ""
          if localIndex == 1 then
              namePrev = "[" .. (pageNames[bankNum .. displayPage] or ("B"..bankNum)) .. "]"
          else
              namePrev = soundNames[bankNum .. displayPage .. (localIndex - 1)] or ("Snd " .. (localIndex - 1))
          end
          
          local nameNext = ""
          if localIndex == count then
              nameNext = "[" .. (pageNames[bankNum .. displayPage] or ("B"..bankNum)) .. "]"
          else
              nameNext = soundNames[bankNum .. displayPage .. (localIndex + 1)] or ("Snd " .. (localIndex + 1))
          end
          
          local flags = alignRight and (SMLSIZE | RIGHT) or SMLSIZE
          local tx = alignRight and x+60 or x
          
          lcd.drawText(tx, y, namePrev, flags)
          lcd.drawText(tx, y+8, nameCurr, alignRight and RIGHT or 0) 
          lcd.drawText(tx, y+16, nameNext, flags)
      end
      
      drawBankBlock(0, 10, 2, false)
      drawBankBlock(65, 10, 1, true)
      drawBankBlock(0, 39, 3, false)
      drawBankBlock(65, 39, 4, true)
      
  else
      -- DIAGNOSTICS SCREEN
      lcd.drawText(0, 0, "CHIRP2 Diagnostics     [Pg 2]", SMLSIZE | INVERS)
      lcd.drawLine(0, 7, 128, 7, SOLID, FORCE)
      
      lcd.drawText(0, 10, string.format("Batt1: %.1fv  Batt2: %.1fv", batt1, batt2), SMLSIZE)
      lcd.drawText(0, 18, string.format("Current: %.1fA Peak: %.1fA", current, peakCurrent), SMLSIZE)
      
      local speedStr = "DIS"
      if speedMode == 1 then speedStr = "SLO"
      elseif speedMode == 2 then speedStr = "MED"
      elseif speedMode == 3 then speedStr = "FST"
      end
      
      lcd.drawText(0, 26, string.format("Speed: %s  Dome: %d", speedStr, 0), SMLSIZE) -- Dome angle pending
      lcd.drawText(0, 34, "Autodome: " .. (autodome and "ON" or "OFF") .. " Autochirp: " .. (autochirp and "ON" or "OFF"), SMLSIZE)
      
      lcd.drawLine(0, 42, 128, 42, DOTTED, FORCE)
      lcd.drawText(0, 45, "> WiFi: OFF", SMLSIZE)
      lcd.drawText(0, 53, "  Bank1 Page: A    Link: " .. linkQuality .. "%", SMLSIZE)
  end

  return 0
end

return { run=run, background=background, init=init }