-- Nightzim BoomMod V - hook do DCS (Saved Games\DCS\Scripts\Hooks)
-- Passa a camera para o add-on do ReShade, cria a explosao quando voce aperta Ctrl+Shift+B (e lanca um AIM-120 com Ctrl+Shift+Z)
-- e responde a calibracao (plano proximo e campo de visao) usando a altura real do terreno.
local function L(msg) pcall(function() log.write("NIGHTZIM", log.INFO, msg) end) end

local nz
do
	local ok, err = pcall(function()
		package.cpath = package.cpath .. ";" .. lfs.writedir() .. "Scripts\\NightzimBoom\\?.dll"
		nz = require("nzboom")
	end)
	if not ok or not nz then L("nao carregou nzboom.dll: " .. tostring(err)); return end
	nz.open()
	L("nzboom carregado")
end

local WIND_FILE = lfs.writedir() .. "Scripts\\NightzimBoom\\vento.txt"
local OPT_FILE = lfs.writedir() .. "Scripts\\NightzimBoom\\opcoes.txt"

local inject	-- definida mais abaixo
local tank_shots
local cb = {}

local terrainLib
pcall(function() terrainLib = require("terrain") end)

local function ground(x, z)
	if Export and Export.LoGetAltitude then
		local h = Export.LoGetAltitude(x, z)
		if h then return h end
	end
	if terrainLib and terrainLib.GetHeight then return terrainLib.GetHeight(x, z) end
	return 0
end

-- distancia ate o chao ao longo de um raio (d unitario)
local function hit(p, d)
	local t, step = 2, 4
	local prev = 2
	while t < 40000 do
		local x, y, z = p.x + d.x * t, p.y + d.y * t, p.z + d.z * t
		if y <= ground(x, z) then
			local a, b = prev, t
			for _ = 1, 40 do
				local m = 0.5 * (a + b)
				if p.y + d.y * m <= ground(p.x + d.x * m, p.z + d.z * m) then b = m else a = m end
			end
			return 0.5 * (a + b)
		end
		prev = t
		t = t + step
		step = math.min(step * 1.12, 150)
	end
	return nil
end

-- calibracao continua: o add-on manda uma grade de profundidades e a camera daquele instante.
-- Plano proximo: raio do centro da tela ate o chao. FOV: para cada sonda abaixo do centro que bate no
-- terreno (nao na cabine nem no ceu), acha o fov que poe aquele ponto em cima do terreno; usa a mediana.
local function solve_calib(n, aspect, near_cur, cam)
	local c = {p = {x = cam[1], y = cam[2], z = cam[3]}, x = {x = cam[4], y = cam[5], z = cam[6]},
		y = {x = cam[7], y = cam[8], z = cam[9]}, z = {x = cam[10], y = cam[11], z = cam[12]}}
	local ok, near_est, msg = 0, 0, ""
	local samples = {}
	for i = 1, n do
		local x, y, d = nz.probe_sample(i)
		samples[i] = {x = x, y = y, d = d}
		if math.abs(x) < 1e-4 and math.abs(y) < 1e-4 and d > 0 then
			local D = hit(c.p, c.x)
			if D and D > 15 and near_cur / d > 15 then near_est = d * D; ok = 1 end
		end
	end
	local near_use = (ok == 1) and near_est or near_cur
	local ss = {}
	for _, s in ipairs(samples) do
		if s.y <= -0.15 and s.d > 0 then
			local z = near_use / s.d
			if z > 15 and z < 25000 then
				local function f(k)
					local dx = c.x.x + s.x * k * aspect * c.z.x + s.y * k * c.y.x
					local dy = c.x.y + s.x * k * aspect * c.z.y + s.y * k * c.y.y
					local dz = c.x.z + s.x * k * aspect * c.z.z + s.y * k * c.y.z
					local px, py, pz = c.p.x + z * dx, c.p.y + z * dy, c.p.z + z * dz
					return py - ground(px, pz)
				end
				local a, b = 0.05, 3.0
				if f(a) > 0 and f(b) < 0 then
					for _ = 1, 32 do
						local m = 0.5 * (a + b)
						if f(m) > 0 then a = m else b = m end
					end
					ss[#ss + 1] = 0.5 * (a + b)
				end
			end
		end
	end
	local tanv, spread = 0, 0
	if #ss >= 3 then
		table.sort(ss)
		tanv = ss[math.floor((#ss + 1) / 2)]
		spread = (ss[math.max(1, math.floor(#ss * 0.75))] - ss[math.max(1, math.floor(#ss * 0.25))]) / tanv
		if spread < 0.15 then ok = ok + 2 end
	end
	nz.calib(ok, near_est, tanv, #ss, spread, string.format("%d sondas no terreno", #ss))
end

-- posicao do sol (algoritmo simplificado da NOAA). Fuso por mapa (DCS usa a hora local do teatro).
local TZ = {Caucasus = 4, PersianGulf = 4, Syria = 3, MarianaIslands = 10, Nevada = -7, Normandy = 2,
	TheChannel = 2, SinaiMap = 3, Kola = 3, Afghanistan = 4.5, Iraq = 3, Falklands = -3, GermanyCW = 1}
local L_sun
local function sun_update(x, z, tmodel)
	local ok, err = pcall(function()
		local miss = DCS.getCurrentMission()
		local m = miss and miss.mission or miss
		local dt = m and m.date or {Year = 2016, Month = 6, Day = 21}
		local th = m and m.theatre or "Caucasus"
		local geo = Export.LoLoCoordinatesToGeoCoordinates(x, z)
		local lat, lon = geo.latitude, geo.longitude
		local tz = TZ[th] or math.floor(lon / 15 + 0.5)
		local t0 = (Export.LoGetMissionStartTime and Export.LoGetMissionStartTime()) or (m and m.start_time) or 43200
		local hours = (t0 + tmodel) / 3600 - tz
		local Y, M, Dd = dt.Year, dt.Month, dt.Day
		local a = math.floor((14 - M) / 12); local y = Y + 4800 - a; local mm = M + 12 * a - 3
		local JDN = Dd + math.floor((153 * mm + 2) / 5) + 365 * y + math.floor(y / 4) - math.floor(y / 100) + math.floor(y / 400) - 32045
		local n = JDN + (hours - 12) / 24 - 2451545.0
		local r = math.pi / 180
		local L = (280.460 + 0.9856474 * n) % 360
		local g = ((357.528 + 0.9856003 * n) % 360) * r
		local lam = (L + 1.915 * math.sin(g) + 0.020 * math.sin(2 * g)) * r
		local eps = (23.439 - 0.0000004 * n) * r
		local ra = math.atan2(math.cos(eps) * math.sin(lam), math.cos(lam))
		local dec = math.asin(math.sin(eps) * math.sin(lam))
		local gmst = (18.697374558 + 24.06570982441908 * n) % 24
		local H = (gmst * 15 + lon) * r - ra
		local la = lat * r
		local el = math.asin(math.sin(la) * math.sin(dec) + math.cos(la) * math.cos(dec) * math.cos(H))
		local az = math.atan2(-math.sin(H), math.tan(dec) * math.cos(la) - math.sin(la) * math.cos(H))
		nz.sun(math.cos(el) * math.cos(az), math.sin(el), math.cos(el) * math.sin(az), el / r, (az / r) % 360)
		L_sun = string.format("sol: %s %04d-%02d-%02d %.2f h UTC, lat %.2f lon %.2f -> elev %.1f az %.1f", th, Y, M, Dd, hours, lat, lon, el / r, (az / r) % 360)
	end)
	if not ok then L("sol: " .. tostring(err)) elseif L_sun then L(L_sun) end
end

-- vento real da missao no ponto (100 m acima do chao), pelo ambiente de script da missao
local function wind_at(x, y, z)
	local ok, r = pcall(net.dostring_in, cb.state or "scripting",
		string.format("local w = (nz_wind or atmosphere.getWind)({x=%.1f,y=%.1f,z=%.1f}) return string.format('%%.3f,%%.3f', w.x, w.z)", x, y + 100, z))
	if ok and r then
		local wx, wz = string.match(r, "([%-%d%.]+),([%-%d%.]+)")
		if wx then return tonumber(wx), tonumber(wz) end
	end
	return 0, 0
end


-- tipo da explosao pelo nome da arma (modo multiplayer, sem a massa do explosivo)
local function classify_name(n)
	n = string.upper(n or "")
	if string.find(n, "FATTAH", 1, true) then return 0 end		-- mod Nightzim Fattah: explosao da JDAM no impacto
	for _, k in ipairs({"SCUD", "R-17", "R_17", "8K14"}) do		-- missil SCUD: explosao nuclear (ou JDAM, conforme o instalador); a MOAB fica para depois
		if string.find(n, k, 1, true) then return 9 end end
	for _, k in ipairs({"NAPALM", "MK_77", "MK-77", "MK77", "BLU_27", "BLU-27", "BLU_1B", "BLU-1B", "BLU_1/B", "BLU-1/B", "ZB_500", "ZB-500", "INCEND", "FIREBOMB", "FIRE_BOMB", "FIRE BOMB"}) do
		if string.find(n, k, 1, true) then return 4 end end
	for _, k in ipairs({"MK_84", "MK-84", "GBU_31", "GBU-31", "GBU_10", "GBU-10", "GBU_24", "GBU-24", "BLU_109", "FAB_500", "FAB-500", "FAB_1500", "KAB_500", "KAB_1500", "BETAB", "AGM_84", "AGM_154", "KH_29", "KH-29", "KH_25", "KH_59"}) do
		if string.find(n, k, 1, true) then return 0 end end
	for _, k in ipairs({"MK_82", "MK-82", "MK_83", "MK-83", "GBU_12", "GBU-12", "GBU_38", "GBU-38", "GBU_32", "GBU_54", "GBU_16", "FAB_250", "FAB_100", "OFAB", "KAB_250", "AGM_65", "AGM-65", "AGM_88", "AGM_114", "S_24", "S-24", "S_25", "AGM_62", "SNEB", "BDU"}) do
		if string.find(n, k, 1, true) then return 3 end end
	for _, k in ipairs({"HYDRA", "M151", "M156", "MK151", "APKWS", "S_8", "S-8", "S_5", "S-5", "S_13", "S-13", "ZUNI", "C_8", "C_5", "VIKHR", "9M1", "ATAKA", "SHTURM", "BGM_71", "TOW", "HELLFIRE"}) do
		if string.find(n, k, 1, true) then return 2 end end
	return 2
end

local function ignorable(n)
	n = string.upper(n or "")
	return n == "" or string.find(n, "FLARE") or string.find(n, "CHAFF") or string.find(n, "BULLET") or string.find(n, "SHELL") or
		string.find(n, "PARACHUTE") or string.find(n, "SMOKE") or string.find(n, "LUU") or string.find(n, "_TRACER") or
		string.find(n, "SEAT") or string.find(n, "PILOT") or string.find(n, "FUEL") or string.find(n, "FPU") or string.find(n, "PTB") or
		string.find(n, "CANOPY") or string.find(n, "TANK ") or
		string.find(n, "MK77MOD", 1, true)	-- A-4E-C: o corpo da Mk-77 some a ~35 m e solta o NAPALM_MOD, que e quem conta
end

-- tiro de canhao de tanque: granada nova perto de um blindado -> sopro na boca do cano, na direcao do cano
local AAA = {"ZSU", "SHILKA", "TUNGUSKA", "2S6", "GEPARD", "VULCAN", "ZU-23", "ZU_23", "S-60", "S_60", "BOFORS", "FLAK", "AAA", "KS-19", "M163", "M6 LINE"}
local function is_aaa(n)
	n = string.upper(n or "")
	for _, k in ipairs(AAA) do if string.find(n, k, 1, true) then return true end end
	return false
end
tank_shots = function(t, shells, all)
	cb.sh = cb.sh or {}
	for id, o in pairs(shells) do
		local e = cb.sh[id]
		if not e then
			cb.sh[id] = {p = o.Position, t = t, n = 1}
		elseif e.n == 1 then
			e.n = 2
			local dt = math.max(t - e.t, 1e-3)
			local d = {x = (o.Position.x - e.p.x) / dt, y = (o.Position.y - e.p.y) / dt, z = (o.Position.z - e.p.z) / dt}
			local sp = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
			if sp > 500 then
				d.x, d.y, d.z = d.x / sp, d.y / sp, d.z / sp
				-- blindado mais perto da reta de volta da granada (ate 200 m para tras, ate 8 m de lado)
				local best, bu = 1e9, nil
				for _, u in pairs(all) do
					local ty = u.Type
					if ty and ty.level1 == 2 and u.Position and not is_aaa(u.Name) then
						local rx, ry, rz = u.Position.x - e.p.x, u.Position.y - e.p.y, u.Position.z - e.p.z
						local along = rx * d.x + ry * d.y + rz * d.z
						if along < 5 and along > -200 then
							local px, py, pz = rx - along * d.x, ry - along * d.y, rz - along * d.z
							local side = math.sqrt(px * px + py * py + pz * pz)
							if side < 8 and side < best then best, bu = side, u end
						end
					end
				end
				if bu then
					local hx, hz = d.x, d.z
					local hn = math.sqrt(hx * hx + hz * hz)
					if hn > 0.1 then
						hx, hz = hx / hn, hz / hn
						local mx, mz = bu.Position.x + hx * 4.5, bu.Position.z + hz * 4.5	-- boca do cano a ~4,5 m do centro da torre
						local my = ground(mx, mz)
						local yaw = math.atan2(hz, hx)
						local wx, wz = wind_at(mx, my, mz)
						nz.spawn(mx, my, mz, t, 5, 1, wx, wz, yaw)
						L(string.format("tiro de canhao: %s (%s) direcao %.0f graus", tostring(bu.Name), tostring(o.Name), math.deg(yaw) % 360))
					end
				end
			end
		end
	end
	for id, e in pairs(cb.sh) do if not shells[id] and t - e.t > 1 then cb.sh[id] = nil end end
end

-- queda de aeronave (teste): uma explosao por queda, venha do Export (aviao inteiro que bate) ou do evento da missao (destroco de aviao abatido)
local function crash_spawn(x, z, t, vx, vz, origem)
	cb.crashes = cb.crashes or {}
	local keep = {}
	for _, c in ipairs(cb.crashes) do
		if t - c.t < 8 and t >= c.t then
			keep[#keep + 1] = c
			if (c.x - x) ^ 2 + (c.z - z) ^ 2 < 400 * 400 then cb.crashes = keep; L("queda de aeronave repetida (" .. origem .. "): ignorada"); return end
		end
	end
	keep[#keep + 1] = {x = x, z = z, t = t}; cb.crashes = keep
	local y = ground(x, z)
	local wx, wz = wind_at(x, y, z)
	if vx and (vx * vx + vz * vz) > 9 then nz.spawn(x, y, z, t, 11, 1, wx, wz, math.atan2(vz, vx)) else nz.spawn(x, y, z, t, 11, 1, wx, wz) end
	sun_update(x, z, t)
	L(string.format("queda de aeronave (%s) em x=%.1f y=%.1f z=%.1f", origem, x, y, z))
end

-- acompanha avioes e helicopteros pelo Export; se um some rapido e rente ao chao, bateu
local function crash_step(t, planes)
	cb.air = cb.air or {}
	for id, o in pairs(planes) do
		local p, e = o.Position, cb.air[id]
		if e then
			local dt = t - e.t
			if dt > 1e-3 then
				e.v = {x = (p.x - e.p.x) / dt, y = (p.y - e.p.y) / dt, z = (p.z - e.p.z) / dt}
				e.p = {x = p.x, y = p.y, z = p.z}; e.t = t
			end
		else
			cb.air[id] = {p = {x = p.x, y = p.y, z = p.z}, t = t, name = o.Name}
		end
	end
	for id, e in pairs(cb.air) do
		if not planes[id] then
			cb.air[id] = nil
			local v, gap = e.v, t - e.t
			if v and gap >= 0 and gap < 1.0 then
				local sp = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
				local agl = e.p.y - ground(e.p.x, e.p.z)
				local x, z, hit = e.p.x, e.p.z, false
				if v.y < -1 then
					local k, lim = 0, math.min(gap + 0.5, 2.0)
					while k <= lim do
						local px, py, pz = e.p.x + v.x * k, e.p.y + v.y * k, e.p.z + v.z * k
						if py <= ground(px, pz) + 2 then x, z = px, pz; hit = true; break end
						k = k + 0.02
					end
				end
				L(string.format("aeronave %s sumiu: %.0f m acima do chao, %.0f m/s (vertical %.0f), chao projetado: %s", tostring(e.name), agl, sp, v.y, tostring(hit)))
				if sp > 30 and (hit or agl < 25) then
					local hs = math.sqrt(v.x * v.x + v.z * v.z)
					crash_spawn(x, z, t, v.x, v.z, "export, " .. tostring(e.name) .. string.format(", mergulho %.0f graus", math.deg(math.atan2(-v.y, math.max(hs, 0.1)))))
				end
			end
		end
	end
end

-- multiplayer: acompanha as armas em voo pelo Export e marca o impacto quando somem
local function ballistic_step(t)
	if not Export.LoGetWorldObjects then return end
	-- armas em voo vem na lista geral de objetos (Type.level1 = 4: arma); "ballistic" so tem assentos ejetaveis,
	-- tanques alijados e afins. level2: 4 missil, 5 bomba, 7 foguete, 8 torpedo (6 = granada de canhao, ignorada)
	local all = Export.LoGetWorldObjects()
	local objs
	if all then
		objs = {}
		local shells, units, planes = {}, nil, {}
		for id, o in pairs(all) do
			local ty = o.Type
			if ty and ty.level1 == 1 and o.Position then planes[id] = o end
			if ty and ty.level1 == 4 and ty.level2 == 6 and o.Position then shells[id] = o end
			if ty and ty.level1 == 4 and ty.level2 ~= 6 then
				objs[id] = o
				cb.seen_names = cb.seen_names or {}
				if not cb.seen_names[o.Name or "?"] then
					cb.seen_names[o.Name or "?"] = true
					L(string.format("arma vista: %s (%s,%s,%s,%s)", tostring(o.Name), tostring(ty.level1), tostring(ty.level2), tostring(ty.level3), tostring(ty.level4)))
				end
			end
		end
		if not cb.bal_info then cb.bal_info = true; L("export de objetos ativo no servidor") end
		tank_shots(t, shells, all)
		pcall(crash_step, t, planes)
	end
	if not objs then
		if not cb.bal_warned then cb.bal_warned = true; L("o servidor nao permite exportar objetos: so as explosoes pelos atalhos") end
		return
	end
	cb.bal = cb.bal or {}
	local seen = {}
	for id, o in pairs(objs) do
		if o.Position and not ignorable(o.Name) then
			seen[id] = true
			local e = cb.bal[id]
			if e then
				local dt = math.max(t - e.t, 1e-3)
				e.v = {x = (o.Position.x - e.p.x) / dt, y = (o.Position.y - e.p.y) / dt, z = (o.Position.z - e.p.z) / dt}
				e.p = {x = o.Position.x, y = o.Position.y, z = o.Position.z}; e.t = t
			else
				cb.bal[id] = {p = {x = o.Position.x, y = o.Position.y, z = o.Position.z}, t = t, name = o.Name}
				L(string.format("acompanhando %s (id %s) a %.0f m de altura", tostring(o.Name), tostring(id), o.Position.y))
				e = cb.bal[id]
				e.trail = nz.track and classify_name(o.Name) == 9	-- SCUD: rastro de fumaca proprio
				e.tid = tonumber(id) or 0
				if nz.track and string.find(string.upper(o.Name or ""), "FATTAH", 1, true) then	-- mod Nightzim Fattah: mesmo rastro, mais fino
					e.trail = true; e.tid = -math.abs(e.tid)										-- id negativo = Fattah (o add-on separa por isso)
				end
				if e.trail then sun_update(o.Position.x, o.Position.z, t) end	-- sem isto o rastro usava o sol do ini (dia) ate a primeira explosao
			end
			if e.trail then
				if not e.wt or t - e.wt > 2 then e.wt = t; e.wx, e.wz = wind_at(o.Position.x, o.Position.y, o.Position.z) end
				nz.track(e.tid, o.Position.x, o.Position.y, o.Position.z, t, e.wx or 0, e.wz or 0)
			end
		end
	end
	for id, e in pairs(cb.bal) do
		if not seen[id] then
			cb.bal[id] = nil
			if e.trail then nz.track_end(e.tid or tonumber(id) or 0) end
			local v = e.v or {x = 0, y = 0, z = 0}
			local gap = t - e.t						-- tempo entre a ultima posicao vista e o sumico
			local ty = classify_name(e.name)
			-- descendo? projeta a trajetoria ate o chao: se bateria dentro do intervalo, o impacto foi no chao
			local x, y, z, hit = e.p.x, e.p.y, e.p.z, false
			if v.y < -2 then
				local lim = math.min(gap + 0.3, 3.0)
				local k = 0
				while k <= lim do
					local px, py, pz = e.p.x + v.x * k, e.p.y + v.y * k, e.p.z + v.z * k
					if py <= ground(px, pz) + 1 then x, z = px, pz; y = ground(px, pz); hit = true; break end
					k = k + 0.01
				end
			end
			if not hit then
				local dt = math.min(gap, 0.1) * 0.5
				x, y, z = e.p.x + v.x * dt, e.p.y + v.y * dt, e.p.z + v.z * dt
			end
			local hg = ground(x, z)
			if ty ~= 9 and not hit and y - hg > 20 then ty = 1 else y = hg end
			-- so conta se estava caindo ou rapido (descarta armas que so sairam do alcance do export)
			local sp = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
			L(string.format("%s sumiu: ultima posicao %.0f m acima do chao, %.0f m/s, %.2f s antes; chao projetado: %s",
				tostring(e.name), e.p.y - ground(e.p.x, e.p.z), math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z), gap, tostring(hit)))
			if y - hg < 60 or sp > 150 then
				local wx, wz = wind_at(x, y, z)
				-- napalm: a faixa de fogo segue a direcao em que a bomba vinha e acende da ponta do impacto para a frente
				if ty == 4 and (v.x * v.x + v.z * v.z) > 25 then nz.spawn(x, y, z, t, ty, 1, wx, wz, math.atan2(v.z, v.x))
				else nz.spawn(x, y, z, t, ty, 1, wx, wz) end
				sun_update(x, z, t)
				L(string.format("impacto (export) %s tipo %d em x=%.1f y=%.1f z=%.1f", tostring(e.name), ty, x, y, z))
			end
		end
	end
end

function cb.onSimulationFrame()
	local ok, err = pcall(function()
		cb.f = (cb.f or 0) + 1
		if not cb.injected and (cb.inj_try or 0) < 30 and cb.f % 120 == 1 then cb.inj_try = (cb.inj_try or 0) + 1; pcall(inject) end
		if cb.injected and cb.f % 300 == 0 then
			local ok, r = pcall(net.dostring_in, cb.state, "return tostring(nzq_installed)")
			if not (ok and r == "true") then L("script da missao sumiu (" .. tostring(r) .. ") - reinjetando"); cb.injected = false; cb.inj_try = 0 end
		end
		local c = Export.LoGetCameraPosition()
		if not c then return end
		local t = DCS.getModelTime()
		nz.camera(c.p.x, c.p.y, c.p.z, c.x.x, c.x.y, c.x.z, c.y.x, c.y.y, c.y.z, c.z.x, c.z.y, c.z.z, t)

		local pend, dist, typ = nz.spawn_pending()
		if pend and typ == 9 then
			-- Ctrl+Shift+Z: lanca um AIM-120 na direcao da camera (um F-16 da IA contra um Su-24M, criados na hora)
			pcall(nz.ack); pend = false
			local fx, fz = c.x.x, c.x.z
			if math.sqrt(fx * fx + fz * fz) < 1e-3 then fx, fz = c.y.x, c.y.z end
			if cb.state then
				local ok2, r2 = pcall(net.dostring_in, cb.state, string.format("return tostring(nz_fire_aim and nz_fire_aim(%.1f, %.1f, %.4f, %.4f) or 'script da missao sem nz_fire_aim')", c.p.x, c.p.z, fx, fz))
				L("AIM-120 pedido: " .. tostring(ok2) .. " " .. tostring(r2))
			else
				L("AIM-120: sem ambiente de script da missao (precisa de single player ou missao propria)")
			end
		end
		if pend then
			typ = typ or 0
			-- "mira laser": raio do centro da visao ate o terreno
			local D = hit(c.p, c.x)
			local fx, fz = c.x.x, c.x.z
			local n = math.sqrt(fx * fx + fz * fz)
			if n < 1e-3 then fx, fz, n = c.y.x, c.y.z, math.sqrt(c.y.x ^ 2 + c.y.z ^ 2) end
			fx, fz = fx / n, fz / n
			local x, y, z
			if D and D < 30000 then
				x, z = c.p.x + c.x.x * D, c.p.z + c.x.z * D
				y = ground(x, z)
				if typ == 1 then y = y + 30 end					-- aerea mirando o chao: 30 m acima do ponto
			elseif typ == 1 then
				x, y, z = c.p.x + c.x.x * 50, c.p.y + c.x.y * 50, c.p.z + c.x.z * 50	-- ceu: aerea a 50 m na linha de visada
			else
				L("mira no ceu: so a explosao aerea (Ctrl+Shift+Z) pode ser criada sem terreno na mira")
			end
			if not x then
				pcall(nz.ack)	-- descarta o pedido
			else
			local wx, wz = wind_at(x, y, z)
			if typ == 5 or typ == 4 then nz.spawn(x, y, z, t, typ, 0, wx, wz, math.atan2(fz, fx))	-- tanque e napalm: para longe da camera
			elseif typ == 11 then nz.spawn(x, y, z, t, typ, 0, wx, wz, math.atan2(fz, fx) + math.pi / 2)	-- queda de aeronave (teste): de lado para a camera
			else nz.spawn(x, y, z, t, typ, 0, wx, wz) end
			-- som original do DCS: uma explosao "de verdade" do jogo no mesmo ponto (o visual dela esta em branco).
			-- So no ambiente de script da missao (single player / missao propria). Potencia em kg de TNT por tipo.
			if cb.state then
				local POW = {[0] = 430, [1] = 40, [2] = 6, [3] = 90, [4] = 120, [5] = 2}
				pcall(net.dostring_in, cb.state, string.format("trigger.action.explosion({x=%.1f,y=%.1f,z=%.1f}, %g)", x, y + 5, z, POW[typ] or 50))
			end
			sun_update(x, z, t)
			L(string.format("explosao tipo %d em x=%.1f y=%.1f z=%.1f t=%.2f vento %.1f %.1f m/s (mira a %.0f m)", typ, x, y, z, t, wx, wz, D or -1))
			end	-- if not x
		end

		-- impactos: sempre pelas armas exportadas (funciona igual no single player e no multiplayer)
		ballistic_step(t)

		-- impactos reais (bombas, foguetes, misseis) vindos do script da missao
		cb.n = (cb.n or 0) + 1
		if cb.injected and cb.n % 60 == 30 then
			local o = net.dostring_in(cb.state or "scripting", "return nz_opt_str and nz_opt_str() or ''")
			if o and o ~= "" and o ~= cb.opt_saved then
				cb.opt_saved = o
				local j, r, sh = string.match(o, "^([%-%d]+),([%-%d%.]+),([%-%d%.]+)$")
				if j then
					nz.set_opts(tonumber(j), tonumber(r), tonumber(sh))
					local f = io.open(OPT_FILE, "w"); if f then f:write(o); f:close() end
					L("opcoes do menu F10: " .. o)
				end
			end
		end
		if cb.injected and cb.n % 60 == 0 then
			local w = net.dostring_in(cb.state or "scripting", "return nz_wo_str and nz_wo_str() or ''")
			if w and w ~= "" and w ~= cb.wind_saved then
				cb.wind_saved = w
				local f = io.open(WIND_FILE, "w"); if f then f:write(w == "real" and "real" or w); f:close() end
				L("vento das explosoes: " .. w)
			end
		end
		if false and cb.injected and cb.n % 3 == 0 then	-- (desligado: o rastreador do script da missao duplicava o do export)
			local q = net.dostring_in(cb.state or "scripting", "return nzq_pop and nzq_pop() or ''")
			if q and q ~= "" then
				for x, y, z, ty in string.gmatch(q, "([%-%d%.]+),([%-%d%.]+),([%-%d%.]+),(%d+)") do
					x, y, z, ty = tonumber(x), tonumber(y), tonumber(z), tonumber(ty)
					if ty ~= 1 then y = ground(x, z) end
					local wx, wz = wind_at(x, y, z)
					nz.spawn(x, y, z, t, ty, 1, wx, wz)
					sun_update(x, z, t)
					L(string.format("impacto real tipo %d em x=%.1f y=%.1f z=%.1f", ty, x, y, z))
				end
			end
		end

		-- quedas vistas pelo script da missao (S_EVENT_CRASH: inclui o destroco do aviao abatido, que o Export nao mostra)
		if cb.injected and cb.n % 3 == 1 then
			local q = net.dostring_in(cb.state or "scripting", "return nzc_pop and nzc_pop() or ''")
			if q and q ~= "" then
				for x, z, vx, vz in string.gmatch(q, "([%-%d%.]+),([%-%d%.]+),([%-%d%.]+),([%-%d%.]+)") do
					crash_spawn(tonumber(x), tonumber(z), t, tonumber(vx), tonumber(vz), "evento da missao")
				end
			end
		end

		-- explosoes pedidas pelo script da missao: nzx[#nzx + 1] = "x,y,z,tipo" (ex.: a MOAB lancada pelo C-130 numa missao)
		if cb.injected and cb.n % 3 == 0 then
			local q = net.dostring_in(cb.state or "scripting", "return nzx_pop and nzx_pop() or ''")
			if q and q ~= "" then
				for x, y, z, ty in string.gmatch(q, "([%-%d%.]+),([%-%d%.]+),([%-%d%.]+),(%d+)") do
					x, y, z, ty = tonumber(x), tonumber(y), tonumber(z), tonumber(ty)
					if ty ~= 1 then y = ground(x, z) end
					local wx, wz = wind_at(x, y, z)
					nz.spawn(x, y, z, t, ty, 1, wx, wz)
					sun_update(x, z, t)
					L(string.format("explosao pedida pela missao: tipo %d em x=%.1f y=%.1f z=%.1f", ty, x, y, z))
				end
			end
		end

		local r = {nz.probe()}
		if r[1] then solve_calib(r[1], r[2], r[3], {unpack(r, 4, 15)}) end
	end)
	if not ok then
		cb.errs = (cb.errs or 0) + 1
		if cb.errs < 5 then L("erro: " .. tostring(err)) end
	end
end

-- script injetado no ambiente da missao: acompanha cada arma disparada e anota onde ela deixou de existir
local MISSION_SCRIPT = [==[
if not nzq_installed then
	nzq_installed = true
	nzq = {}
	local tracked = {}
	local function classify(w)
		local d = w:getDesc() or {}
		local name = string.upper(w:getTypeName() or "")
		local m = (d.warhead and (d.warhead.explosiveMass or d.warhead.mass)) or 0
		if string.find(name, "NAPALM") or string.find(name, "INCEND") or string.find(name, "BLU_1") or string.find(name, "BLU-1") then return 4 end
		if m >= 100 then return 0 end
		if m >= 25 then return 3 end
		if m >= 4 then return 5 end
		return 2
	end
	local h = {}
	function h:onEvent(e)
		if e.id == world.event.S_EVENT_SHOT and e.weapon then
			local ok, d = pcall(function() return e.weapon:getDesc() end)
			if ok and d and (d.category == Weapon.Category.BOMB or d.category == Weapon.Category.ROCKET or d.category == Weapon.Category.MISSILE) then
				tracked[#tracked + 1] = {w = e.weapon, ty = classify(e.weapon)}
			end
		end
		-- queda de aeronave: o destroco tocou o chao
		if e.id == world.event.S_EVENT_CRASH and e.initiator then
			pcall(function()
				local p = e.initiator:getPoint(); local v = {x = 0, y = 0, z = 0}
				pcall(function() v = e.initiator:getVelocity() end)
				local nome = "?"; pcall(function() nome = e.initiator:getTypeName() end)
				local agl = p.y - land.getHeight({x = p.x, y = p.z})
				env.info(string.format("NIGHTZIM: S_EVENT_CRASH %s a %.0f m do chao, vel %.0f %.0f %.0f", tostring(nome), agl, v.x, v.y, v.z))
				if agl < 60 then nzc[#nzc + 1] = string.format("%.1f,%.1f,%.1f,%.1f", p.x, p.z, v.x, v.z) end
			end)
		end
	end
	world.addEventHandler(h)
	nzc = {}
	nzc_pop = function() local s = table.concat(nzc, ";"); nzc = {}; return s end
	local function tick(_, t)
		local keep = {}
		for _, o in ipairs(tracked) do
			local alive = false
			pcall(function() alive = o.w:isExist() end)
			if alive then
				pcall(function() o.p = o.w:getPoint(); o.v = o.w:getVelocity() end)
				keep[#keep + 1] = o
			elseif o.p then
				local v = o.v or {x = 0, y = 0, z = 0}
				local x, y, z = o.p.x + v.x * 0.025, o.p.y + v.y * 0.025, o.p.z + v.z * 0.025
				local hg = land.getHeight({x = x, y = z})
				local ty = o.ty
				if y - hg > 20 then ty = 1 end
				nzq[#nzq + 1] = string.format("%.1f,%.1f,%.1f,%d", x, y, z, ty)
			end
		end
		tracked = keep
		return t + 0.05
	end
	timer.scheduleFunction(tick, nil, timer.getTime() + 0.05)
	nzq_pop = function() local s = table.concat(nzq, ";"); nzq = {}; return s end
	nzx = nzx or {}
	nzx_pop = function() local q = nzx or {}; nzx = {}; return table.concat(q, ";") end

	-- vento das explosoes Nightzim (menu F10). O DCS nao deixa script mudar o vento real do clima;
	-- isto define o vento que a fumaca das nossas explosoes usa. nz_wo = {velocidade m/s, direcao DE ONDE vem, graus}
	nz_wind = function(p)
		if nz_wo then
			if nz_wo[1] <= 0 then return {x = 0, y = 0, z = 0} end
			local a = math.rad(nz_wo[2] + 180)	-- vento vem de 'dir' e sopra para o lado oposto
			return {x = math.cos(a) * nz_wo[1], y = 0, z = math.sin(a) * nz_wo[1]}
		end
		return atmosphere.getWind(p)
	end
	nz_wo_str = function() if nz_wo then return string.format("%g,%g", nz_wo[1], nz_wo[2]) end return "real" end
	-- menu F10 publico: escolhas do BoomMod X (valem para as proximas explosoes; ficam salvas)
	nz_opt = nz_opt or {j = -1, r = -1, s = -1}
	nz_opt_str = function() return string.format("%d,%g,%g", nz_opt.j, nz_opt.r, nz_opt.s) end
	do
		local function say(t) trigger.action.outText("Nightzim BoomMod X: " .. t, 6) end
		local m = missionCommands.addSubMenu("Nightzim BoomMod X")
		local mj = missionCommands.addSubMenu("Explosao JDAM", m)
		missionCommands.addCommand("Nova (video real)", mj, function() nz_opt.j = 1; say("JDAM nova") end)
		missionCommands.addCommand("Classica (v1.0)", mj, function() nz_opt.j = 2; say("JDAM classica") end)
		local mc = missionCommands.addSubMenu("Cores", m)
		missionCommands.addCommand("Realistas (v1.0b)", mc, function() nz_opt.r = 1; say("cores realistas") end)
		missionCommands.addCommand("Classicas (v1.0)", mc, function() nz_opt.r = 0; say("cores classicas") end)
		local ms = missionCommands.addSubMenu("Onda de choque", m)
		missionCommands.addCommand("Ligada", ms, function() nz_opt.s = 1; say("onda de choque ligada") end)
		missionCommands.addCommand("Desligada", ms, function() nz_opt.s = 0; say("onda de choque desligada") end)
		missionCommands.addCommand("Voltar ao padrao do instalador", m, function() nz_opt = {j = -1, r = -1, s = -1}; say("padrao do instalador") end)
	end

	if nz_dev then
	local function msg(t) trigger.action.outText(t, 6) end
	local function setv(v) local d = nz_wo and nz_wo[2] or 270; nz_wo = {v, d}; msg(string.format("Nightzim: vento das explosoes %d m/s vindo de %03d", v, d)) end
	local function setd(d) local v = nz_wo and nz_wo[1] or 5; nz_wo = {v, d}; msg(string.format("Nightzim: vento das explosoes %d m/s vindo de %03d", v, d)) end
	local root = missionCommands.addSubMenu("Nightzim - vento das explosoes")
	missionCommands.addCommand("Usar o vento real da missao", root, function() nz_wo = nil; msg("Nightzim: vento real da missao") end)
	missionCommands.addCommand("Calmo (sem vento)", root, function() setv(0) end)
	local vel = missionCommands.addSubMenu("Velocidade", root)
	for _, v in ipairs({3, 5, 8, 12, 18}) do missionCommands.addCommand(v .. " m/s", vel, function() setv(v) end) end
	local dir = missionCommands.addSubMenu("Vindo de", root)
	for _, d in ipairs({{"Norte", 0}, {"Nordeste", 45}, {"Leste", 90}, {"Sudeste", 135}, {"Sul", 180}, {"Sudoeste", 225}, {"Oeste", 270}, {"Noroeste", 315}}) do
		missionCommands.addCommand(d[1] .. string.format(" (%03d)", d[2]), dir, function() setd(d[2]) end)
	end
	end	-- nz_dev

	-- Ctrl+Shift+Z (so na versao de teste): cria um F-16 com AIM-120C atras da camera e um Su-24M de frente, e manda atacar.
	nz_fire_n = nz_fire_n or 0
	nz_fire_aim = function(px, pz, fx, fz)
		local d = math.sqrt(fx * fx + fz * fz)
		if d < 1e-3 then return "direcao invalida" end
		fx, fz = fx / d, fz / d
		local rx, rz = -fz, fx
		nz_fire_n = nz_fire_n + 1
		local n = nz_fire_n
		local alt = math.max(land.getHeight({x = px, y = pz}) + 1200, 2000)
		local hd, hd2 = math.atan2(fz, fx), math.atan2(-fz, -fx)
		local function wp(x, z, s)
			return {x = x, y = z, alt = alt, alt_type = "BARO", type = "Turning Point", action = "Turning Point", speed = s, speed_locked = true,
				ETA = 0, ETA_locked = false, formation_template = "", name = "", task = {id = "ComboTask", params = {tasks = {}}}}
		end
		local function mk(name, typ, x, z, h, s, pylons, task, fuel)
			return {name = name, task = task, x = x, y = z, start_time = 0, communication = true, uncontrolled = false, visible = false, hidden = false, taskSelected = true,
				route = {points = {wp(x, z, s), wp(x + math.cos(h) * 60000, z + math.sin(h) * 60000, s)}},
				units = {{name = name .. "-1", type = typ, x = x, y = z, alt = alt, alt_type = "BARO", speed = s, heading = h, psi = -h, skill = "High",
					payload = {pylons = pylons, fuel = fuel, flare = 60, chaff = 120, gun = 100}, onboard_num = tostring(100 + n)}}}
		end
		local sn, tn = "NZ AIM120 atirador " .. n, "NZ AIM120 alvo " .. n
		local aim = "{40EF17B7-F508-45de-8566-6FFECC0C1AB8}"
		local sx, sz = px - fx * 2000 + rx * 500, pz - fz * 2000 + rz * 500
		local tx, tz = px + fx * 10000 + rx * 500, pz + fz * 10000 + rz * 500
		coalition.addGroup(country.id.USA, Group.Category.AIRPLANE, mk(sn, "F-16C_50", sx, sz, hd, 250, {[1] = {CLSID = aim}, [9] = {CLSID = aim}}, "CAP", 3249))
		coalition.addGroup(country.id.RUSSIA, Group.Category.AIRPLANE, mk(tn, "Su-24M", tx, tz, hd2, 200, {}, "Nothing", 6000))
		timer.scheduleFunction(function()
			local ok, err = pcall(function()
				local sg, tg, tu = Group.getByName(sn), Group.getByName(tn), Unit.getByName(tn .. "-1")
				if not (sg and tg and tu) then env.info("NIGHTZIM: AIM-120: grupos nao foram criados"); return end
				local cs, ct = sg:getController(), tg:getController()
				pcall(function() ct:setOption(AI.Option.Air.id.ROE, AI.Option.Air.val.ROE.WEAPON_HOLD) end)
				pcall(function() ct:setOption(AI.Option.Air.id.REACTION_ON_THREAT, AI.Option.Air.val.REACTION_ON_THREAT.NO_REACTION) end)
				pcall(function() cs:setOption(AI.Option.Air.id.ROE, AI.Option.Air.val.ROE.WEAPON_FREE) end)
				cs:setTask({id = "AttackUnit", params = {unitId = tu:getID(), expend = "One", attackQtyLimit = true, attackQty = 1, groupAttack = false}})
				env.info("NIGHTZIM: AIM-120: F-16 mandado atacar o Su-24M")
			end)
			if not ok then env.info("NIGHTZIM: AIM-120 erro: " .. tostring(err)) end
		end, nil, timer.getTime() + 3)
		timer.scheduleFunction(function()
			pcall(function() local g = Group.getByName(sn); if g then g:destroy() end end)
			pcall(function() local g = Group.getByName(tn); if g then g:destroy() end end)
		end, nil, timer.getTime() + 300)
		trigger.action.outText("Nightzim: AIM-120 a caminho (Ctrl+Shift+Z)", 6)
		return "criados " .. sn
	end
	env.info("NIGHTZIM: rastreador de impactos e menu de vento ativos")
end
]==]

local function saved_wind()
	local f = io.open(WIND_FILE, "r"); if not f then return "" end
	local s = f:read("*l") or ""; f:close()
	local v, d = string.match(s, "^([%d%.]+),([%d%.]+)$")
	if v then return string.format("nz_wo = {%s, %s} ", v, d) end
	return ""
end

-- o ambiente de script da missao (onde existem world, timer, missionCommands) se chama "scripting" no DCS atual;
-- em versoes antigas era alcancado por "mission" + a_do_script. Testa e usa o que responder.
local function probe_state(st)
	local ok, r = pcall(net.dostring_in, st, "return tostring(world ~= nil and timer ~= nil and missionCommands ~= nil)")
	return ok and r == "true", tostring(r)
end

inject = function()
	if not cb.state then
		for _, st in ipairs({"scripting", "mission"}) do
			local ok, r = probe_state(st)
			L("estado '" .. st .. "': " .. r)
			if ok then cb.state = st; break end
		end
	end
	local dev = nz.dev and nz.dev() or false
	local opt = ""
	do
		local f = io.open(OPT_FILE, "r")
		if f then
			local l = f:read("*l") or ""; f:close()
			local j, r, sh = string.match(l, "^([%-%d]+),([%-%d%.]+),([%-%d%.]+)$")
			if j then opt = string.format("nz_opt = {j = %s, r = %s, s = %s} ", j, r, sh); pcall(nz.set_opts, tonumber(j), tonumber(r), tonumber(sh)) end
		end
	end
	local code = "nz_dev = " .. tostring(dev) .. " " .. opt .. MISSION_SCRIPT .. (dev and saved_wind() or "") .. " return tostring(nzq_installed)"
	if cb.state then
		local ok, res = pcall(net.dostring_in, cb.state, code)
		cb.injected = ok and res == "true"
		L("rastreador de impactos e menu F10 (" .. cb.state .. "): " .. tostring(res))
	else
		-- cliente de multiplayer: o script da missao roda no servidor. Usa as armas em voo exportadas pelo DCS
		-- (funciona se o servidor permitir exportar objetos) e o menu F10 nao existe aqui.
		cb.mode = "ballistic"; cb.inj_try = 99
		L("sem ambiente de script (cliente de multiplayer?): usando as armas exportadas pelo DCS para achar os impactos")
	end
end

function cb.onSimulationStart() cb.injected = false; cb.inj_try = 0 end
function cb.onMissionLoadEnd() cb.injected = false; cb.state = nil; cb.mode = nil; cb.inj_try = 0; cb.bal = {} end
function cb.onSimulationStop() cb.injected = false; cb.state = nil; cb.mode = nil; cb.bal = {}; pcall(nz.reset) end

DCS.setUserCallbacks(cb)
L("hook ativo")
