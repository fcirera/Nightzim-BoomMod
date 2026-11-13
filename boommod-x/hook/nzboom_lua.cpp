// nzboom.dll - modulo C (Lua 5.1) carregado pelo hook do DCS. So escreve/le a memoria compartilhada.
#include "nzshared.h"
#include <string.h>

typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State *);
typedef double lua_Number;
struct luaL_Reg { const char *name; lua_CFunction func; };

static lua_Number (*p_tonumber)(lua_State *, int);
static void (*p_pushnumber)(lua_State *, lua_Number);
static void (*p_pushboolean)(lua_State *, int);
static void (*p_pushnil)(lua_State *);
static const char *(*p_tolstring)(lua_State *, int, size_t *);
static int (*p_gettop)(lua_State *);
static void (*p_register)(lua_State *, const char *, const luaL_Reg *);

static HANDLE g_map = nullptr;
static NzShared *g = nullptr;

static bool open_shm()
{
	if (g) return true;
	g_map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(NzShared), NZ_SHM_NAME);
	if (!g_map) return false;
	bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;
	g = (NzShared *)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NzShared));
	if (!g) return false;
	if (fresh || g->version != NZ_SHM_VERSION) { memset((void *)g, 0, sizeof(NzShared)); g->version = NZ_SHM_VERSION; g->spawn_distance = 600.f; g->opt_jdam = -1; g->opt_realism = -1; g->opt_shock = -1; }
	return true;
}

static double num(lua_State *L, int i) { return p_tonumber(L, i); }

static int l_open(lua_State *L) { p_pushboolean(L, open_shm()); return 1; }

static int l_camera(lua_State *L)
{
	if (!open_shm()) return 0;
	InterlockedIncrement(&g->cam_seq);
	MemoryBarrier();
	for (int k = 0; k < 3; k++) { g->cam_p[k] = num(L, 1 + k); g->cam_x[k] = num(L, 4 + k); g->cam_y[k] = num(L, 7 + k); g->cam_z[k] = num(L, 10 + k); }
	g->model_time = num(L, 13);
	MemoryBarrier();
	InterlockedIncrement(&g->cam_seq);
	InterlockedIncrement(&g->lua_frames);
	return 0;
}

static int l_spawn_pending(lua_State *L)
{
	if (!open_shm() || g->spawn_req == g->spawn_ack) { p_pushboolean(L, 0); return 1; }
	p_pushboolean(L, 1);
	p_pushnumber(L, g->spawn_distance > 10.f ? g->spawn_distance : 600.0);
	p_pushnumber(L, g->spawn_req_type);
	return 3;
}

static int l_spawn(lua_State *L)
{
	if (!open_shm()) return 0;
	LONG n = g->spawn_seq;
	auto &e = g->ring[n % NZ_RING];
	e.p[0] = num(L, 1); e.p[1] = num(L, 2); e.p[2] = num(L, 3);
	e.t = num(L, 4);
	e.type = p_gettop(L) >= 5 ? (int32_t)num(L, 5) : 0;
	e.pad = p_gettop(L) >= 6 ? (int32_t)num(L, 6) : 0;	// 1 = bomba real (o DCS ja toca o som)
	e.wx = p_gettop(L) >= 8 ? (float)num(L, 7) : 0.f;
	e.wz = p_gettop(L) >= 8 ? (float)num(L, 8) : 0.f;
	e.fixed = p_gettop(L) >= 9 ? 1 : 0;				// 9o argumento: direcao fixa (tiro de tanque = direcao do cano)
	e.yaw = e.fixed ? (float)num(L, 9) : 0.f;
	MemoryBarrier();
	InterlockedIncrement(&g->spawn_seq);
	if (p_gettop(L) < 6 || num(L, 6) == 0) g->spawn_ack = g->spawn_req;	// 6o argumento = 1: veio de bomba real, nao do atalho
	return 0;
}

static LONG g_probe_seen = 0;
// nz.probe() -> nil | n, aspect, near, cam(12)
static int l_probe(lua_State *L)
{
	if (!open_shm() || g->probe_seq == g_probe_seen) { p_pushnil(L); return 1; }
	g_probe_seen = g->probe_seq;
	MemoryBarrier();
	p_pushnumber(L, NZ_PROBE_N); p_pushnumber(L, g->probe_aspect); p_pushnumber(L, g->probe_near);
	for (int k = 0; k < 12; k++) p_pushnumber(L, g->probe_cam[k]);
	return 15;
}
// nz.probe_sample(i) -> ndc_x, ndc_y, d   (i de 1 a n)
static int l_probe_sample(lua_State *L)
{
	int i = (int)num(L, 1) - 1;
	if (!open_shm() || i < 0 || i >= NZ_PROBE_N) { p_pushnil(L); return 1; }
	p_pushnumber(L, g->probe_ndc[i][0]); p_pushnumber(L, g->probe_ndc[i][1]); p_pushnumber(L, g->probe_d[i]);
	return 3;
}
// nz.calib(ok, near, tanv, nsamp, spread, msg)
static int l_calib(lua_State *L)
{
	if (!open_shm()) return 0;
	g->calib_ok = (int32_t)num(L, 1);
	g->calib_near = num(L, 2); g->calib_tanv = num(L, 3); g->calib_nsamp = (int32_t)num(L, 4); g->calib_spread = num(L, 5);
	const char *m = p_gettop(L) >= 6 ? p_tolstring(L, 6, nullptr) : nullptr;
	strncpy(g->lua_msg, m ? m : "", sizeof(g->lua_msg) - 1);
	MemoryBarrier();
	InterlockedIncrement(&g->calib_seq);
	return 0;
}
// nz.sun(dx, dy, dz, el, az)
static int l_sun(lua_State *L)
{
	if (!open_shm()) return 0;
	for (int k = 0; k < 3; k++) g->sun_dir[k] = num(L, 1 + k);
	g->sun_el_deg = num(L, 4); g->sun_az_deg = num(L, 5);
	MemoryBarrier();
	InterlockedIncrement(&g->sun_seq);
	return 0;
}

static int l_reset(lua_State *L)
{
	if (!open_shm()) return 0;
	InterlockedIncrement(&g->clear_seq);
	g->spawn_ack = g->spawn_req;
	return 0;
}

// nz.set_opts(jdam, realism, shock)  (-1 = usar o ini)
static int l_set_opts(lua_State *L)
{
	if (!open_shm()) return 0;
	g->opt_jdam = (int32_t)num(L, 1); g->opt_realism = (float)num(L, 2); g->opt_shock = (float)num(L, 3);
	MemoryBarrier(); InterlockedIncrement(&g->opt_seq);
	return 0;
}
static int l_ack(lua_State *L) { if (open_shm()) g->spawn_ack = g->spawn_req; return 0; }
static int l_dev(lua_State *L) { p_pushboolean(L, open_shm() && g->dev == 1); return 1; }

static const luaL_Reg funcs[] = {
	{"dev", l_dev}, {"ack", l_ack}, {"set_opts", l_set_opts},
	{"open", l_open}, {"camera", l_camera}, {"spawn_pending", l_spawn_pending}, {"spawn", l_spawn},
	{"probe", l_probe}, {"probe_sample", l_probe_sample}, {"calib", l_calib}, {"sun", l_sun}, {"reset", l_reset}, {nullptr, nullptr}};

template <class T> static bool R(HMODULE h, T &f, const char *n) { f = (T)GetProcAddress(h, n); return f != nullptr; }

extern "C" __declspec(dllexport) int luaopen_nzboom(lua_State *L)
{
	HMODULE h = GetModuleHandleA("lua.dll");
	if (!h) return 0;
	if (!(R(h, p_tonumber, "lua_tonumber") && R(h, p_pushnumber, "lua_pushnumber") && R(h, p_pushboolean, "lua_pushboolean") &&
		  R(h, p_pushnil, "lua_pushnil") && R(h, p_tolstring, "lua_tolstring") && R(h, p_gettop, "lua_gettop") && R(h, p_register, "luaL_register")))
		return 0;
	open_shm();
	p_register(L, "nzboom", funcs);
	return 1;
}
