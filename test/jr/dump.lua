-- dump.lua: MAME's dkongjr, dumped the way test/jr/jrhost.c dumps the model.
-- At each frame's end (vertical blank, line 240, before the NMI is taken):
-- the Z80's $6000-$77FF, the 8035's 64 bytes of RAM, A, PSW, PC, P1, P2.
-- JR_OUT names the folder, JR_FRAMES the frames, JR_SCRIPT the inputs
-- ("F:in0=XX F:in2=XX", hex, active high, from frame F on), JR_SNAPS the
-- frames to snapshot (as OUT/mame-F.png). Run headless by test/jr/mame.sh.
if _G.jr_done then return end
_G.jr_done = true
local out = os.getenv("JR_OUT")
local frames = tonumber(os.getenv("JR_FRAMES"))
local snaps = {}
for f in string.gmatch(os.getenv("JR_SNAPS") or "", "%d+") do snaps[tonumber(f)] = true end
local ev = {}
for f, p, v in string.gmatch(os.getenv("JR_SCRIPT") or "", "(%d+):(in%d)=(%x+)") do
  ev[#ev + 1] = { frame = tonumber(f), port = p, val = tonumber(v, 16) }
end
local evi = 1
local m = manager.machine
local main = m.devices[":maincpu"].spaces["program"]
local snd = m.devices[":soundcpu"]
local sdata = snd.spaces["data"]
local ports = m.ioport.ports
local function set_port(tag, val)
  for name, field in pairs(ports[tag].fields) do
    if field.mask & 0x40 == 0 or tag ~= ":IN2" then
      field:set_value((val & field.mask) ~= 0 and 1 or 0)
    end
  end
end
local f = assert(io.open(out .. "/dump.bin", "wb"))
local frame = 0
if m.debugger then
  -- the sprite copy's hold, as MAME has it: the Z80's cycles at the
  -- instruction that starts it ($0173) and at the one after ($0176)
  m.debugger:command('bpset 0173,1,{logerror "W %d\\n",totalcycles; g}')
  m.debugger:command('bpset 0176,1,{logerror "R %d\\n",totalcycles; g}')
  m.debugger:command('bpset 0066,1,{logerror "N %d\\n",totalcycles; g}')
  m.debugger:command("go")
end
-- JR_TRACE=A-B: every Z80 instruction from frame A's end to frame B's, in OUT/z80.trace
local tr_a, tr_b = string.match(os.getenv("JR_TRACE") or "", "(%d+)-(%d+)")
tr_a, tr_b = tonumber(tr_a), tonumber(tr_b)
-- JR_M48TRACE=A-B: the same for the 8035, in OUT/m48.trace
local mt_a, mt_b = string.match(os.getenv("JR_M48TRACE") or "", "(%d+)-(%d+)")
mt_a, mt_b = tonumber(mt_a), tonumber(mt_b)
emu.register_frame_done(function()
  frame = frame + 1
  if tr_a and frame == tr_a then
    m.debugger:command('trace ' .. out .. '/z80.trace,maincpu,noloop,{tracelog "C=%d AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X ",totalcycles,af&ffd7,bc,de,hl,ix,iy,sp}')
  end
  if tr_b and frame == tr_b then m.debugger:command("trace off,maincpu") end
  if mt_a and frame == mt_a then
    -- (tracelog writes to the trace of the debugger's visible CPU)
    m.debugger.visible_cpu = snd
    m.debugger:command('trace ' .. out .. '/m48.trace,soundcpu,noloop,{tracelog "C=%d A=%02X R0=%02X R1=%02X R2=%02X R3=%02X R4=%02X R5=%02X R6=%02X R7=%02X P2=%02X ",totalcycles,a,r0,r1,r2,r3,r4,r5,r6,r7,p2}')
  end
  if mt_b and frame == mt_b then m.debugger:command("trace off,soundcpu") end
  local t = {}
  for a = 0x6000, 0x77FF do t[#t + 1] = string.char(main:read_u8(a)) end
  for a = 0, 63 do t[#t + 1] = string.char(sdata:read_u8(a)) end
  local st = snd.state
  local pc = st["PC"].value
  t[#t + 1] = string.char(st["A"].value & 0xFF, st["SP"].value & 0xFF, pc & 0xFF, (pc >> 8) & 0xFF,
                          st["P1"].value & 0xFF, st["P2"].value & 0xFF)
  f:write(table.concat(t))
  if snaps[frame] then
    m.video:snapshot()
  end
  while evi <= #ev and ev[evi].frame <= frame do
    set_port(ev[evi].port == "in0" and ":IN0" or ":IN2", ev[evi].val)
    evi = evi + 1
  end
  if frame >= frames then
    f:close()
    m:exit()
  end
end)
