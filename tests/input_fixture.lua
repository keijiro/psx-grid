-- The runner supplies the two ELF symbol offsets before loading this file.
local ffi = require('ffi')
local ram = PCSX.getMemPtr()
local phase = ffi.cast('uint32_t*', ram + input_phase_offset)
local expected = ffi.cast('uint32_t*', ram + input_expected_offset)
local pad = PCSX.SIO0.slots[1].pads[1]
local buttons = PCSX.CONSTS.PAD.BUTTON
local previous, frame = 0, 0
local function held(value)
    for _,button in ipairs({buttons.RIGHT, buttons.CROSS, buttons.START}) do
        if value then pad.setOverride(button) else pad.clearOverride(button) end
    end
end
input_listener = PCSX.Events.createEventListener('GPU::Vsync', function()
    local current = tonumber(phase[0])
    if current < 1 or current > 5 then return end
    if current ~= previous then
        previous, frame = current, 0
        PCSX.settings.pads[1].DeviceType = input_analog and 'Analog' or 'Digital'
        pad.map()
        pad.setAnalogMode(input_analog)
        held(false)
    end
    frame = frame + 1
    if current == 4 then
        -- Reconnect while held: none of these buttons may act until released.
        if frame == 16 then PCSX.settings.pads[1].Connected = false; held(true) end
        if frame == 32 then PCSX.settings.pads[1].Connected = true end
        if frame == 48 then held(false) end
        if frame == 64 then held(true); expected[0] = expected[0] + 1 end
        if frame == 65 then held(false) end
    else
        -- 100 one-frame taps, separated by three neutral frames.
        local active = frame >= 16 and frame < (current == 5 and 56 or 416) and frame % 4 == 0
        held(active)
        if active then expected[0] = expected[0] + 1 end
    end
end)
