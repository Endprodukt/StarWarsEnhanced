-- swcapture.lua - record Star Wars game ticks from MAME for Star Wars Enhanced.
--
--   SWCAP_OUT=attract.swcap mame starwars1 -autoboot_script tools/mame/swcapture.lua
--
-- One TICK record each time the main line finishes a display list (VGDONE
-- writes FF to VGSYNC), with VG RAM, program RAM and math box RAM: see
-- src/capture/capture.h for the format. Addresses are from WSROOT.MAP, the
-- load map of the 22-June-1983 program, which is MAME's "starwars1".
--
-- For checking the native game frame by frame (tools/swcompare), also a
-- FRAM record each time the main line starts a frame (WAITFRAME shifts a 1
-- out of GMSYNC): the same memory as it is before the frame, with the
-- NOVRAM (NVRM) and the random numbers the main line read during the frame
-- before (RNDM) written ahead of it.
--
-- Environment:
--   SWCAP_OUT     output file (default swcapture.swcap)
--   SWCAP_TICKS   stop MAME after this many ticks (default: until MAME exits)

local VGSYNC  = 0x483F    -- FF = main line done with its buffer
local GMSYNC  = 0x483D    -- counts game frames due; WAITFRAME shifts it right
local PRND    = 0x4703    -- the random number generator
local FRAMEH  = 0x4842    -- FRAME, the game's 16-bit tick counter
local IRQCLR  = 0x4660    -- written once per IRQ (0x4660-0x467F)
local VGVARB  = 0x0070    -- offset of a display list within its buffer

local out_path = os.getenv("SWCAP_OUT") or "swcapture.swcap"
local max_ticks = tonumber(os.getenv("SWCAP_TICKS") or "0") or 0

local machine = manager.machine
if machine.system.name ~= "starwars1" then
    emu.print_error("swcapture: this is " .. machine.system.name ..
        ", the addresses are for starwars1; recording anyway")
end

local cpu = machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local f = assert(io.open(out_path, "wb"))

f:write("SWCAP1\0\0")
f:write("VROM", string.pack("<I4", 0x1000), space:read_range(0x3000, 0x3FFF, 8))

local irq_count = 0
local ticks = 0
local t_go, t_hand = 0.0, 0.0
local TICK_SIZE = 12 + 0x3000 + 0x800 + 0x1000

-- Keep the taps referenced: they are removed when collected.
taps = {}

-- The script's own reads go through the read taps too: not to be recorded.
local quiet = false

local function memory()
    quiet = true
    local d = space:read_range(0x0000, 0x2FFF, 8) ..
              space:read_range(0x4800, 0x4FFF, 8) ..
              space:read_range(0x5000, 0x5FFF, 8)
    quiet = false
    return d
end

local function nvram()
    quiet = true
    local d = space:read_range(0x4500, 0x45FF, 8)
    quiet = false
    return d
end

local function peek(a)
    quiet = true
    local v = space:read_u8(a)
    quiet = false
    return v
end

-- A played script: SWCAP_PLAY is a list of "FRAME WHAT [ARGS]" separated by
-- ';', FRAME counted in game frames from power-on:
--   coin N | fire N | rfire N | thumb N   close the switch for N frames
--   test N | aux N                        the self test switch, the aux coin switch
--   stick X Y                             the yoke, 00..FF each, 80 the middle
--   weave N                               from here on: the yoke circles and
--                                         the trigger is pulled every N frames
--   aim N                                 from here on: the sight follows the
--                                         last object the game drew (a ship, if
--                                         one is in view) and the trigger is
--                                         pulled every N frames
--   poke ADDR VAL                         write a byte (hex) into the machine,
--                                         to get at what play does not reach
--                                         (4860 00: no shields left)
local play = {}
for entry in (os.getenv("SWCAP_PLAY") or ""):gmatch("[^;]+") do
    local w = {}
    for word in entry:gmatch("%S+") do w[#w + 1] = word end
    if #w >= 2 then play[#play + 1] = { frame = tonumber(w[1]), what = w[2], a = w[3], b = w[4] } end
end
local ports = machine.ioport.ports
local switches = {
    coin = ports[":IN0"].fields["Coin 1"], fire = ports[":IN0"].fields["P1 Button 1"],
    rfire = ports[":IN0"].fields["P1 Button 4"], thumb = ports[":IN1"].fields["P1 Button 2"],
    test = ports[":IN0"].fields["Service Mode"], aux = ports[":IN0"].fields["Service 1"],
}
local stick_x, stick_y = ports[":STICKX"].fields["AD Stick X"], ports[":STICKY"].fields["AD Stick Y"]
local held = {}             -- switch name -> frames left
local frames_seen = 0
local weave = 0
local aim = 0
local target = nil           -- screen x, y of the last object drawn this frame
local sx, sy = 128, 128      -- the yoke as the script holds it

local function s16(v) if v >= 0x8000 then return v - 0x10000 end return v end

function play_frame()
    frames_seen = frames_seen + 1
    for name, left in pairs(held) do
        if left <= 0 then
            switches[name]:set_value(0)
            held[name] = nil
        else
            held[name] = left - 1
        end
    end
    for _, e in ipairs(play) do
        if e.frame == frames_seen then
            if switches[e.what] then
                switches[e.what]:set_value(1)
                held[e.what] = tonumber(e.a or "3")
            elseif e.what == "stick" then
                stick_x:set_value(tonumber(e.a, 16))
                stick_y:set_value(tonumber(e.b, 16))
            elseif e.what == "weave" then
                weave = tonumber(e.a or "8")
                aim = 0
            elseif e.what == "aim" then
                aim = tonumber(e.a or "6")
                weave = 0
            elseif e.what == "poke" then
                quiet = true
                space:write_u8(tonumber(e.a, 16), tonumber(e.b, 16))
                f:write("POKE", string.pack("<I4I2B", 3, tonumber(e.a, 16), tonumber(e.b, 16)))
                quiet = false
            end
        end
    end
    if aim > 0 then
        -- Steer the yoke until the sight (VG.CX, VG.CY) is on the target.
        if target then
            local cx = s16((peek(0x482D) << 8) | peek(0x482E))
            local cy = s16((peek(0x482F) << 8) | peek(0x4830))
            sx = math.max(1, math.min(255, sx + (target[1] - cx) / 6))
            sy = math.max(1, math.min(255, sy + (target[2] - cy) / 6))
        end
        target = nil
        stick_x:set_value(math.floor(sx))
        stick_y:set_value(math.floor(sy))
        if frames_seen % aim == 0 and not held.fire then
            switches.fire:set_value(1)
            held.fire = 2
        end
    end
    if weave > 0 then
        local a = frames_seen / 40.0
        stick_x:set_value(math.floor(128 + 70 * math.sin(a)))
        stick_y:set_value(math.floor(128 + 50 * math.sin(a * 1.7)))
        if frames_seen % weave == 0 and not held.fire then
            switches.fire:set_value(1)
            held.fire = 2
        end
    end
end

-- For `aim`: where the game puts the center of each object it draws
-- (OBJCEN stores BJ.CY, then BJ.CX).
taps.bjcx = space:install_write_tap(0x48D7, 0x48D7, "swcap_bjcx",
    function(offset, data, mask)
        if aim > 0 then
            quiet = true
            local x = s16((space:read_u8(0x48D6) << 8) | (data & 0xFF))
            local y = s16((space:read_u8(0x48D8) << 8) | space:read_u8(0x48D9))
            quiet = false
            target = { x, y }
        end
    end)

-- Random numbers read by the main line (the interrupt reads them too, for
-- the laser splash: it runs with the I flag set).
local rnd = {}
-- Inside the interrupt routine: from the CPU fetching the IRQ vector until
-- the routine acknowledges the interrupt, just before its RTI.
local in_irq = false
taps.vector = space:install_read_tap(0xFFF8, 0xFFF8, "swcap_vector",
    function(offset, data, mask) in_irq = true end)
taps.prnd = space:install_read_tap(PRND, PRND, "swcap_prnd",
    function(offset, data, mask)
        if not in_irq and not quiet then rnd[#rnd + 1] = string.char(data & 0xFF) end
    end)

-- What the main line reads of the memory the interrupt keeps (switches,
-- pots and sight, timers, the frame and VG handshakes): address and value
-- of every read, in order. With these a frame can be run again without
-- the interrupts and comes out the same.
local irds = {}
local function ird(offset, data, mask)
    if not in_irq and not quiet then irds[#irds + 1] = string.pack("<I2B", offset, data & 0xFF) end
end
taps.ird1 = space:install_read_tap(0x4809, 0x4840, "swcap_ird1", ird)
taps.ird2 = space:install_read_tap(0x4851, 0x4852, "swcap_ird2", ird)
taps.ird3 = space:install_read_tap(0x4866, 0x4878, "swcap_ird3", ird)
taps.ird4 = space:install_read_tap(0x4AF4, 0x4AF4, "swcap_ird4", ird)
taps.ird5 = space:install_read_tap(0x4B04, 0x4B07, "swcap_ird5", ird)

taps.gmsync = space:install_write_tap(GMSYNC, GMSYNC, "swcap_gmsync",
    function(offset, data, mask)
        local old = peek(GMSYNC)           -- tap runs before the write
        if (old & 1) == 0 or data ~= (old >> 1) then return end   -- the IRQ's INC, or no frame due
        local frame = (peek(FRAMEH) << 8) | peek(FRAMEH + 1)
        t_go = machine.time:as_double()
        play_frame()
        local r = table.concat(rnd)
        rnd = {}
        f:write("RNDM", string.pack("<I4", #r), r)
        irds[#irds] = nil                           -- this LSR's own read belongs to the new frame
        local d = table.concat(irds)
        irds = {}
        f:write("IRDS", string.pack("<I4", #d), d)
        f:write("NVRM", string.pack("<I4", 0x100), nvram())
        f:write("FRAM", string.pack("<I4", TICK_SIZE))
        f:write(string.pack("<I4I4I2I2", frame, irq_count, 0, 0))
        f:write(memory())
    end)

taps.irq = space:install_write_tap(IRQCLR, IRQCLR + 0x1F, "swcap_irq",
    function(offset, data, mask)
        irq_count = irq_count + 1
        in_irq = false
    end)

taps.vgsync = space:install_write_tap(VGSYNC, VGSYNC, "swcap_vgsync",
    function(offset, data, mask)
        if data ~= 0xFF then                        -- the IRQ handing out a buffer
            t_hand = machine.time:as_double()
            return
        end
        local base = peek(VGSYNC)          -- tap runs before the write
        if base == 0xFF then return end             -- reset, or no buffer held
        local frame = (peek(FRAMEH) << 8) | peek(FRAMEH + 1)
        local buffer = ((base << 8) + VGVARB) & 0xFFFF
        -- When the frame started, when it got its buffer, when it was done
        -- with the list (seconds): how long the 6809 took over it.
        f:write("TIME", string.pack("<I4", 24), string.pack("<ddd", t_go, t_hand, machine.time:as_double()))
        f:write("TICK", string.pack("<I4", TICK_SIZE))
        f:write(string.pack("<I4I4I2I2", frame, irq_count, buffer, 0))
        f:write(memory())
        ticks = ticks + 1
        if max_ticks > 0 and ticks >= max_ticks then
            f:flush()
            machine:exit()
        end
    end)

stop_sub = emu.add_machine_stop_notifier(function()
    f:close()
    emu.print_info(string.format("swcapture: %d ticks written to %s", ticks, out_path))
end)
