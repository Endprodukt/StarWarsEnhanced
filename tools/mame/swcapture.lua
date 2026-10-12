-- swcapture.lua - record Star Wars game ticks from MAME for StarWarsVP.
--
--   SWCAP_OUT=attract.swcap mame starwars1 -autoboot_script tools/mame/swcapture.lua
--
-- One TICK record each time the main line finishes a display list (VGDONE
-- writes FF to VGSYNC), with VG RAM, program RAM and math box RAM: see
-- src/capture/capture.h for the format. Addresses are from WSROOT.MAP, the
-- load map of the 22-June-1983 program, which is MAME's "starwars1".
--
-- Environment:
--   SWCAP_OUT     output file (default swcapture.swcap)
--   SWCAP_TICKS   stop MAME after this many ticks (default: until MAME exits)

local VGSYNC  = 0x483F    -- FF = main line done with its buffer
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

local space = machine.devices[":maincpu"].spaces["program"]
local f = assert(io.open(out_path, "wb"))

f:write("SWCAP1\0\0")
f:write("VROM", string.pack("<I4", 0x1000), space:read_range(0x3000, 0x3FFF, 8))

local irq_count = 0
local ticks = 0
local TICK_SIZE = 12 + 0x3000 + 0x800 + 0x1000

-- Keep the taps referenced: they are removed when collected.
taps = {}

taps.irq = space:install_write_tap(IRQCLR, IRQCLR + 0x1F, "swcap_irq",
    function(offset, data, mask)
        irq_count = irq_count + 1
    end)

taps.vgsync = space:install_write_tap(VGSYNC, VGSYNC, "swcap_vgsync",
    function(offset, data, mask)
        if data ~= 0xFF then return end            -- the IRQ handing out a buffer
        local base = space:read_u8(VGSYNC)          -- tap runs before the write
        if base == 0xFF then return end             -- reset, or no buffer held
        local frame = (space:read_u8(FRAMEH) << 8) | space:read_u8(FRAMEH + 1)
        local buffer = ((base << 8) + VGVARB) & 0xFFFF
        f:write("TICK", string.pack("<I4", TICK_SIZE))
        f:write(string.pack("<I4I4I2I2", frame, irq_count, buffer, 0))
        f:write(space:read_range(0x0000, 0x2FFF, 8))
        f:write(space:read_range(0x4800, 0x4FFF, 8))
        f:write(space:read_range(0x5000, 0x5FFF, 8))
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
