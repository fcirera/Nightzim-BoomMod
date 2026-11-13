// NightzimBoom.addon64 - Nightzim BoomMod V: explosao volumetrica (ray marching) desenhada por cima do quadro do DCS.
// Add-on do ReShade 6.x (API 20). Usa D3D11 nativo. Profundidade: rastreio proprio do buffer de profundidade principal
// (copia antes de limpar). Camera: vem do hook Lua do DCS pela memoria compartilhada (nzshared.h).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_1.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include "reshade.hpp"
#include "bc7/bc7enc.h"	// compressor BC7 de Richard Geldreich Jr. (bc7enc_rdo, licenca MIT / dominio publico)
#include "nzshared.h"

using namespace reshade::api;

extern "C" __declspec(dllexport) const char *NAME = "Nightzim BoomMod V";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Explosao volumetrica simulada (Nightzim BoomMod V, prova de conceito).";

// ---------------------------------------------------------------- utilidades
static std::string g_dir;	// ...\bin\NightzimBoom\ .
static FILE *g_log = nullptr;

static void logf(const char *fmt, ...)
{
	if (!g_log) { g_log = fopen((g_dir + "NightzimBoom.log").c_str(), "w"); if (!g_log) return; }
	SYSTEMTIME st; GetLocalTime(&st);
	fprintf(g_log, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list a; va_start(a, fmt); vfprintf(g_log, fmt, a); va_end(a);
	fputc('\n', g_log); fflush(g_log);
}

template <class T> static void SR(T *&p) { if (p) { p->Release(); p = nullptr; } }

struct Config
{
	float vfov_deg = 50.f, near_m = 0.3f, step_m = 1.5f, exposure = 1.f, opacity = 1.f, spawn_distance = 600.f;
	float sun_az = 120.f, sun_el = 52.f;
	int cockpit_mask = 1, calib_every = 12;
	float dist[6] = {600, 600, 200, 400, 400, 150};
	float shock_gain = 1.f, wilson_gain = 1.f, volume = 0.f;
	float shock_type[6] = {0.8f, 0.1f, 0.13f, 0.3f, 0.f, 0.1f};	// fracao da onda de choque de referencia (3 m de refracao)
	int dev = 0; int jdam_v1 = 0;
	int debris_on = 0; float realism = 1.f, firelight = 1.6f; float debris_density = 1.f, debris_glow = 1.f;
	int wind_on = 1; float wind_gain = 1.f, wind_shear_m = 80.f, wind_delay_s = 0.6f;
	float detail = 0.8f, detail_scale = 12.f, fire_gain = 0.6f, flame_min_t = 1450.f, edge_fade = 0.08f;
	float fog_dist = 15000.f, fog_r = 0.62f, fog_g = 0.68f, fog_b = 0.76f, hold_s = 24.f, fade_end = 40.f, freeze_t = -1.f;
	float night_ambient = 0.02f, night_fire = 1.5f, night_glow = 2.0f;	// noite: luz ambiente minima, ganho do fogo e da luz do fogo na fumaca
	int vram_var = 2, vram_lazy = 1, vram_bc = 0;	// variacoes por tipo na VRAM, carregar so quando usar, comprimir (0 nao, 1 = metade/BC5, 2 = um quarto/BC7)
	int pod_on = 1, pod_min_draws = 40, pod_log = 1; float pod_max_fov = 12.f;	// pod: campo estreito (NFOV ~1,3 graus, WFOV ~4,5)
	int reversed = 1, max_steps = 420, flip_right = 0, debug = 0, auto_calib = 1, depth_min_draws = 20;
	std::string nzv = "jdam.nzv";
} cfg;

static float ini_f(const char *sec, const char *key, float def)
{
	char b[64], d[64]; snprintf(d, sizeof d, "%g", def);
	GetPrivateProfileStringA(sec, key, d, b, sizeof b, (g_dir + "NightzimBoom.ini").c_str());
	return (float)atof(b);
}
static void load_config()
{
	Config c;
	c.vfov_deg = ini_f("camera", "vfov_deg", c.vfov_deg);
	c.near_m = ini_f("camera", "near_m", c.near_m);
	c.reversed = (int)ini_f("camera", "reversed_depth", (float)c.reversed);
	c.flip_right = (int)ini_f("camera", "flip_right", (float)c.flip_right);
	c.auto_calib = (int)ini_f("camera", "auto_calibrate", (float)c.auto_calib);
	c.step_m = ini_f("render", "step_m", c.step_m);
	c.max_steps = (int)ini_f("render", "max_steps", (float)c.max_steps);
	c.exposure = ini_f("render", "exposure", c.exposure);
	c.opacity = ini_f("render", "opacity", c.opacity);
	c.fog_dist = ini_f("render", "fog_dist_m", c.fog_dist);
	c.fog_r = ini_f("render", "fog_r", c.fog_r); c.fog_g = ini_f("render", "fog_g", c.fog_g); c.fog_b = ini_f("render", "fog_b", c.fog_b);
	c.debug = (int)ini_f("render", "debug", (float)c.debug);
	c.cockpit_mask = (int)ini_f("render", "cockpit_mask", (float)c.cockpit_mask);
	c.calib_every = (int)ini_f("camera", "calibrate_every_frames", (float)c.calib_every);
	c.sun_az = ini_f("sun", "default_az_deg", c.sun_az); c.sun_el = ini_f("sun", "default_el_deg", c.sun_el);
	c.depth_min_draws = (int)ini_f("render", "depth_min_draws", (float)c.depth_min_draws);
	c.spawn_distance = ini_f("explosion", "spawn_distance_m", c.spawn_distance);
	{ const char *k[6] = {"dist_jdam_m", "dist_aerea_m", "dist_pequena_m", "dist_media_m", "dist_napalm_m", "dist_tanque_m"}; for (int i = 0; i < 6; i++) c.dist[i] = ini_f("explosion", k[i], c.dist[i]); }
	c.detail = ini_f("render", "detail", c.detail); c.detail_scale = ini_f("render", "detail_scale_m", c.detail_scale);
	c.fire_gain = ini_f("render", "fire_gain", c.fire_gain); c.flame_min_t = ini_f("render", "flame_min_temp_k", c.flame_min_t);
	c.edge_fade = ini_f("render", "edge_fade", c.edge_fade);
	c.shock_gain = ini_f("shock", "gain", c.shock_gain);
	{ const char *k[6] = {"jdam", "aerea", "pequena", "media", "napalm", "tanque"}; for (int i = 0; i < 6; i++) c.shock_type[i] = ini_f("shock", k[i], c.shock_type[i]); } c.wilson_gain = ini_f("shock", "wilson", c.wilson_gain);
	c.volume = ini_f("sound", "volume", c.volume);
	c.dev = (int)ini_f("nightzim", "dev", 0.f);
	{ char b[16]; GetPrivateProfileStringA("instalador", "jdam", "v2", b, sizeof b, (g_dir + "NightzimBoom.ini").c_str()); c.jdam_v1 = !_stricmp(b, "v1"); }
	c.debris_on = (int)ini_f("debris", "enabled", (float)c.debris_on);
	c.realism = ini_f("color", "realism", c.realism); c.firelight = ini_f("color", "firelight", c.firelight);
	c.debris_density = ini_f("debris", "smoke", c.debris_density); c.debris_glow = ini_f("debris", "glow", c.debris_glow);
	c.wind_on = (int)ini_f("wind", "enabled", (float)c.wind_on); c.wind_gain = ini_f("wind", "gain", c.wind_gain);
	c.wind_shear_m = ini_f("wind", "shear_height_m", c.wind_shear_m); c.wind_delay_s = ini_f("wind", "delay_s", c.wind_delay_s);
	{
		char b2[16]; GetPrivateProfileStringA("vram", "variedade", "media", b2, sizeof b2, (g_dir + "NightzimBoom.ini").c_str());
		c.vram_var = !_stricmp(b2, "alta") ? 5 : !_stricmp(b2, "media") ? 2 : (atoi(b2) > 0 ? std::min(5, atoi(b2)) : 1);
		c.vram_lazy = (int)ini_f("vram", "sob_demanda", (float)c.vram_lazy); c.vram_bc = (int)ini_f("vram", "comprimir", (float)c.vram_bc);
	}
	c.night_ambient = ini_f("night", "ambient", c.night_ambient); c.night_fire = ini_f("night", "fire_boost", c.night_fire); c.night_glow = ini_f("night", "glow_boost", c.night_glow);
	c.pod_on = (int)ini_f("pod", "enabled", (float)c.pod_on); c.pod_min_draws = (int)ini_f("pod", "min_draws", (float)c.pod_min_draws);
	c.pod_max_fov = ini_f("pod", "max_fov_deg", c.pod_max_fov); c.pod_log = (int)ini_f("pod", "log", (float)c.pod_log);
	c.hold_s = ini_f("explosion", "hold_s", c.hold_s);
	c.fade_end = ini_f("explosion", "fade_end_s", c.fade_end);
	c.freeze_t = ini_f("explosion", "freeze_t", c.freeze_t);
	char b[260]; GetPrivateProfileStringA("explosion", "file", c.nzv.c_str(), b, sizeof b, (g_dir + "NightzimBoom.ini").c_str()); c.nzv = b;
	cfg = c;
	logf("config: vfov %.1f near %.3f rev %d step %.2f max %d exp %.2f", cfg.vfov_deg, cfg.near_m, cfg.reversed, cfg.step_m, cfg.max_steps, cfg.exposure);
}

#include "sound.inc"

// ---------------------------------------------------------------- memoria compartilhada
static HANDLE g_map = nullptr;
static NzShared *g_sh = nullptr;
static bool open_shm()
{
	if (g_sh) return true;
	g_map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(NzShared), NZ_SHM_NAME);
	if (!g_map) return false;
	bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;
	g_sh = (NzShared *)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(NzShared));
	if (!g_sh) return false;
	if (fresh || g_sh->version != NZ_SHM_VERSION) { memset((void *)g_sh, 0, sizeof(NzShared)); g_sh->version = NZ_SHM_VERSION; g_sh->spawn_distance = 600.f; }
	return true;
}

struct Cam { double p[3], x[3], y[3], z[3], t; };
static bool read_cam(Cam &c)
{
	for (int tries = 0; tries < 64; tries++)
	{
		LONG s0 = g_sh->cam_seq; if (s0 & 1) { YieldProcessor(); continue; }
		MemoryBarrier();
		memcpy(c.p, g_sh->cam_p, 24); memcpy(c.x, g_sh->cam_x, 24); memcpy(c.y, g_sh->cam_y, 24); memcpy(c.z, g_sh->cam_z, 24); c.t = g_sh->model_time;
		MemoryBarrier();
		if (g_sh->cam_seq == s0) return s0 != 0;
	}
	return false;
}

// ---------------------------------------------------------------- volume .nzv
static const int NTYPES = 7;	// 6 = JDAM classica (v1), usada no lugar da 0 quando escolhida
static const char *TYPE_FILE[NTYPES] = {"jdam.nzv", "aerea.nzv", "nar.nzv", "mk82.nzv", "napalm.nzv", "tank.nzv", "jdam_v1.nzv"};
static const char *TYPE_NAME[NTYPES] = {"JDAM", "aerea", "pequena", "media", "napalm", "tanque", "JDAM classica"};
static const bool TYPE_GROUND[NTYPES] = {true, false, true, true, true, true, true};
static const int MAXV = 5;	// variacoes por tipo: jdam.nzv, jdam_2.nzv ... jdam_5.nzv

struct Volume
{
	int nx = 0, ny = 0, nz = 0, nf = 0; float dx = 0, burst[3] = {}, mx[5] = {}, t_end = 0;
	std::vector<float> times;
	std::vector<ID3D11ShaderResourceView *> A, B, A2, B2;	// A2/B2: canais b,a quando comprimido (BC5)
	std::vector<ID3D11Texture3D *> tex;
	volatile LONG ready = 0, loading = 0; bool exists = false; int bc = 0;
	void release() { ready = 0; for (auto *p : A) p->Release(); for (auto *p : B) p->Release(); for (auto *p : A2) p->Release(); for (auto *p : B2) p->Release(); for (auto *p : tex) p->Release(); A.clear(); B.clear(); A2.clear(); B2.clear(); tex.clear(); nf = 0; bc = 0; }
} vols[NTYPES * MAXV];
static int g_nvar[NTYPES];	// variacoes existentes e permitidas (>= 1 se o arquivo base existe)
#define VOL(I) vols[(I).type * MAXV + (I).var]
static std::string vol_file(int type, int var)
{
	std::string f = TYPE_FILE[type];
	if (var > 0) f = f.substr(0, f.size() - 4) + "_" + std::to_string(var + 1) + ".nzv";
	return f;
}

// BC4 de um bloco 4x4 (16 valores): 8 niveis entre o minimo e o maximo do bloco
static void bc4_block(const unsigned char *v, unsigned char *out)
{
	unsigned char lo = 255, hi = 0;
	for (int i = 0; i < 16; i++) { lo = std::min(lo, v[i]); hi = std::max(hi, v[i]); }
	out[0] = hi; out[1] = lo;	// hi > lo: modo de 8 niveis (indice 0 = hi, 1 = lo, 2..7 = interpolados)
	uint64_t bits = 0;
	if (hi > lo)
		for (int i = 0; i < 16; i++)
		{
			int k = (int)floorf((v[i] - lo) * 7.f / (hi - lo) + 0.5f);	// 0 = lo .. 7 = hi
			int idx = k == 7 ? 0 : k == 0 ? 1 : 8 - k;
			bits |= (uint64_t)idx << (3 * i);
		}
	for (int b = 0; b < 6; b++) out[2 + b] = (unsigned char)(bits >> (8 * b));
}
// RGBA8 (x mais rapido) -> duas texturas BC5: (r,g) e (b,a)
static void to_bc5(const unsigned char *rgba, int nx, int ny, int nz, std::vector<unsigned char> &rg, std::vector<unsigned char> &ba)
{
	int bx = nx / 4, by = ny / 4;
	rg.resize((size_t)bx * by * nz * 16); ba.resize(rg.size());
	unsigned char c[4][16];
	for (int z = 0; z < nz; z++)
		for (int j = 0; j < by; j++)
			for (int i = 0; i < bx; i++)
			{
				for (int y = 0; y < 4; y++)
					for (int x = 0; x < 4; x++)
					{
						const unsigned char *p = rgba + (((size_t)z * ny + j * 4 + y) * nx + i * 4 + x) * 4;
						for (int ch = 0; ch < 4; ch++) c[ch][y * 4 + x] = p[ch];
					}
				size_t o = (((size_t)z * by + j) * bx + i) * 16;
				bc4_block(c[0], &rg[o]); bc4_block(c[1], &rg[o + 8]); bc4_block(c[2], &ba[o]); bc4_block(c[3], &ba[o + 8]);
			}
}

static bool load_volume(ID3D11Device *dev, int type, int var = 0)
{
	Volume &vol = vols[type * MAXV + var];
	vol.release();
	std::string path = g_dir + vol_file(type, var);
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) { if (var == 0) logf("aviso: nao achou %s (explosao %s indisponivel)", path.c_str(), TYPE_NAME[type]); return false; }
	unsigned char h[128]; fread(h, 1, 128, f);
	if (memcmp(h, "NZV1", 4)) { logf("ERRO: %s nao e NZV1", path.c_str()); fclose(f); return false; }
	int32_t ii[4]; memcpy(ii, h + 4, 16); vol.nx = ii[0]; vol.ny = ii[1]; vol.nz = ii[2]; vol.nf = ii[3];
	float ff[12]; memcpy(ff, h + 20, 48);
	vol.dx = ff[0]; vol.burst[0] = ff[1]; vol.burst[1] = ff[2]; vol.burst[2] = ff[3];
	for (int k = 0; k < 5; k++) vol.mx[k] = ff[4 + k];
	vol.t_end = ff[11];
	vol.times.resize(vol.nf); fread(vol.times.data(), 4, vol.nf, f);
	size_t n = (size_t)vol.nx * vol.ny * vol.nz * 4;
	std::vector<unsigned char> buf(n);
	D3D11_TEXTURE3D_DESC td = {}; td.Width = vol.nx; td.Height = vol.ny; td.Depth = vol.nz; td.MipLevels = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	int mode = (vol.nx % 4 == 0 && vol.ny % 4 == 0) ? cfg.vram_bc : 0;
	bool bc = mode == 1, bc7 = mode == 2;
	bc7enc_compress_block_params bp;
	if (bc7)
	{
		static std::once_flag once; std::call_once(once, [] { bc7enc_compress_block_init(); });
		bc7enc_compress_block_params_init(&bp); bc7enc_compress_block_params_init_linear_weights(&bp);
		bp.m_uber_level = 0; bp.m_max_partitions = 0;	// os 4 canais sao dados independentes: modo 6 (RGBA 7 bits) e o que serve
	}
	DWORD tload = GetTickCount();
	std::vector<unsigned char> rg, ba;
	auto make = [&](const void *data, DXGI_FORMAT fmt, UINT pitch, UINT slice, std::vector<ID3D11ShaderResourceView *> &dst) -> bool {
		D3D11_TEXTURE3D_DESC d = td; d.Format = fmt;
		D3D11_SUBRESOURCE_DATA sd = {data, pitch, slice};
		ID3D11Texture3D *t = nullptr; ID3D11ShaderResourceView *v = nullptr;
		if (FAILED(dev->CreateTexture3D(&d, &sd, &t)) || FAILED(dev->CreateShaderResourceView(t, nullptr, &v))) { SR(t); SR(v); return false; }
		vol.tex.push_back(t); dst.push_back(v); return true; };
	for (int fr = 0; fr < vol.nf; fr++)
		for (int ab = 0; ab < 2; ab++)
		{
			if (fread(buf.data(), 1, n, f) != n) { logf("ERRO: %s curto no quadro %d", vol_file(type, var).c_str(), fr); fclose(f); vol.release(); return false; }
			bool ok;
			if (bc)
			{
				to_bc5(buf.data(), vol.nx, vol.ny, vol.nz, rg, ba);
				UINT pitch = (UINT)(vol.nx / 4) * 16, slice = pitch * (UINT)(vol.ny / 4);
				ok = make(rg.data(), DXGI_FORMAT_BC5_UNORM, pitch, slice, ab ? vol.B : vol.A) && make(ba.data(), DXGI_FORMAT_BC5_UNORM, pitch, slice, ab ? vol.B2 : vol.A2);
			}
			else if (bc7)
			{
				int bx = vol.nx / 4, by = vol.ny / 4;
				rg.resize((size_t)bx * by * vol.nz * 16);
				unsigned char px[64];
				for (int z = 0; z < vol.nz; z++)
					for (int j = 0; j < by; j++)
						for (int i = 0; i < bx; i++)
						{
							for (int y = 0; y < 4; y++) memcpy(px + y * 16, buf.data() + (((size_t)z * vol.ny + j * 4 + y) * vol.nx + i * 4) * 4, 16);
							bc7enc_compress_block(&rg[(((size_t)z * by + j) * bx + i) * 16], px, &bp);
						}
				ok = make(rg.data(), DXGI_FORMAT_BC7_UNORM, (UINT)bx * 16, (UINT)bx * 16 * (UINT)by, ab ? vol.B : vol.A);
			}
			else ok = make(buf.data(), DXGI_FORMAT_R8G8B8A8_UNORM, (UINT)vol.nx * 4, (UINT)(vol.nx * vol.ny * 4), ab ? vol.B : vol.A);
			if (!ok) { logf("ERRO: CreateTexture3D (%s)", bc ? "BC5" : bc7 ? "BC7" : "RGBA8"); fclose(f); vol.release(); return false; }
		}
	fclose(f);
	vol.bc = bc ? 1 : 0;
	MemoryBarrier();
	vol.ready = 1;
	logf("volume %s (%s, variacao %d): %dx%dx%d, %d quadros, dx %.2f, burst %.1f %.1f %.1f, t_end %.1f, VRAM %.0f MB%s", vol_file(type, var).c_str(), TYPE_NAME[type], var + 1, vol.nx, vol.ny, vol.nz, vol.nf, vol.dx,
		vol.burst[0], vol.burst[1], vol.burst[2], vol.t_end, (double)n * 2 * vol.nf / (bc ? 2 : bc7 ? 4 : 1) / 1048576.0, bc ? " (comprimido BC5)" : bc7 ? " (comprimido BC7)" : "");
	if (bc7) logf("  compressao BC7 de %s: %.1f s", vol_file(type, var).c_str(), (GetTickCount() - tload) / 1000.0);
	return true;
}

static DWORD WINAPI loader_thread(LPVOID p)
{
	ID3D11Device *dev = (ID3D11Device *)p;
	// quais variacoes existem (limitadas pela opcao "variedade" do instalador)
	for (int k = 0; k < NTYPES; k++)
	{
		int n = 0;
		for (int v = 0; v < MAXV && n < cfg.vram_var; v++) { DWORD a = GetFileAttributesA((g_dir + vol_file(k, v)).c_str()); if (a == INVALID_FILE_ATTRIBUTES) break; vols[k * MAXV + v].exists = true; n++; }
		g_nvar[k] = n;
	}
	// a primeira de cada tipo sempre carrega na abertura; as outras agora ou so quando forem sorteadas
	for (int k = 0; k < NTYPES; k++) load_volume(dev, k, 0);
	if (!cfg.vram_lazy)
		for (int k = 0; k < NTYPES; k++) for (int v = 1; v < g_nvar[k]; v++) load_volume(dev, k, v);
	logf("variacoes por tipo: ate %d, %s, %s", cfg.vram_var, cfg.vram_lazy ? "extras carregam quando sorteadas" : "todas na abertura", cfg.vram_bc == 2 ? "comprimidas a 1/4 (BC7)" : cfg.vram_bc ? "comprimidas a 1/2 (BC5)" : "sem compressao");
	dev->Release();
	return 0;
}
// carrega uma variacao extra em segundo plano (primeira vez que foi sorteada)
static ID3D11Device *volatile g_load_dev = nullptr;	// preenchido em init_gfx
static DWORD WINAPI loader_one(LPVOID p)
{
	int slot = (int)(intptr_t)p;
	ID3D11Device *dev = g_load_dev;
	if (dev) { dev->AddRef(); load_volume(dev, slot / MAXV, slot % MAXV); dev->Release(); }
	InterlockedExchange(&vols[slot].loading, 0);
	return 0;
}
static void release_all_volumes() { for (auto &v : vols) v.release(); }

// ---------------------------------------------------------------- shaders
typedef HRESULT(WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
static PFN_D3DCompile pD3DCompile = nullptr;

struct Gfx
{
	ID3D11Device *dev = nullptr; ID3D11Device1 *dev1 = nullptr; ID3D11DeviceContext *ctx = nullptr; ID3D11DeviceContext1 *ctx1 = nullptr;
	ID3DDeviceContextState *state = nullptr;
	ID3D11VertexShader *vs = nullptr;
	ID3D11PixelShader *ps[2] = {}, *probe[2] = {};	// [0] profundidade normal, [1] MSAA
	ID3D11Buffer *cb = nullptr;
	ID3D11SamplerState *samp = nullptr; ID3D11BlendState *blend = nullptr; ID3D11RasterizerState *rs = nullptr; ID3D11DepthStencilState *dss = nullptr;
	ID3D11Texture2D *probe_rt = nullptr, *probe_st = nullptr; ID3D11RenderTargetView *probe_rtv = nullptr;
	ID3D11ComputeShader *cs_sun = nullptr;
	ID3D11PixelShader *ps_shock[2] = {};
	ID3D11PixelShader *ps_trail[2] = {}; ID3D11ComputeShader *cs_trail = nullptr; ID3D11Buffer *cb_tr = nullptr;
	ID3D11Texture2D *scene = nullptr; ID3D11ShaderResourceView *scene_srv = nullptr; UINT scene_w = 0, scene_h = 0; DXGI_FORMAT scene_fmt = DXGI_FORMAT_UNKNOWN;
	ID3D11SamplerState *samp_wrap = nullptr;
	ID3D11Texture3D *noise_tex = nullptr; ID3D11ShaderResourceView *noise_srv = nullptr;
	std::unordered_map<ID3D11Resource *, ID3D11RenderTargetView *> bb_rtv;
	bool ok = false;
	void release_shaders() { SR(ps_trail[0]); SR(ps_trail[1]); SR(cs_trail); SR(ps_shock[0]); SR(ps_shock[1]); SR(cs_sun); SR(vs); for (int k = 0; k < 2; k++) { SR(ps[k]); SR(probe[k]); } }
	void release()
	{
		release_shaders(); SR(cb); SR(samp); SR(blend); SR(rs); SR(dss); SR(probe_rt); SR(probe_st); SR(probe_rtv); SR(state); SR(cb_tr); SR(scene_srv); SR(scene); SR(samp_wrap); SR(noise_srv); SR(noise_tex);
		for (auto &kv : bb_rtv) kv.second->Release(); bb_rtv.clear();
		SR(ctx1); SR(ctx); SR(dev1); dev = nullptr; ok = false;
	}
} G;

static ID3DBlob *compile(const std::string &src, const char *entry, const char *target, bool ms)
{
	D3D_SHADER_MACRO mac[] = {{"DEPTH_MS", "1"}, {nullptr, nullptr}};
	ID3DBlob *code = nullptr, *err = nullptr;
	HRESULT hr = pD3DCompile(src.data(), src.size(), "NightzimBoom.hlsl", ms ? mac : mac + 1, nullptr, entry, target, 1 << 15 /*O3*/, 0, &code, &err);
	if (FAILED(hr)) { logf("ERRO compilando %s%s: %s", entry, ms ? " (MS)" : "", err ? (const char *)err->GetBufferPointer() : "?"); SR(err); return nullptr; }
	SR(err);
	return code;
}

static bool build_shaders()
{
	if (!pD3DCompile)
	{
		HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
		if (m) pD3DCompile = (PFN_D3DCompile)GetProcAddress(m, "D3DCompile");
		if (!pD3DCompile) { logf("ERRO: d3dcompiler_47.dll ausente"); return false; }
	}
	FILE *f = fopen((g_dir + "NightzimBoom.hlsl").c_str(), "rb");
	if (!f) { logf("ERRO: NightzimBoom.hlsl ausente"); return false; }
	std::string src; char b[4096]; size_t r; while ((r = fread(b, 1, sizeof b, f)) > 0) src.append(b, r); fclose(f);
	ID3D11VertexShader *vs = nullptr; ID3D11PixelShader *ps[2] = {}, *pr[2] = {};
	bool good = true;
	if (ID3DBlob *c = compile(src, "vs_main", "vs_5_0", false)) { good &= SUCCEEDED(G.dev->CreateVertexShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &vs)); c->Release(); } else good = false;
	for (int k = 0; k < 2 && good; k++)
	{
		if (ID3DBlob *c = compile(src, "ps_main", "ps_5_0", k)) { good &= SUCCEEDED(G.dev->CreatePixelShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &ps[k])); c->Release(); } else good = false;
		if (ID3DBlob *c = compile(src, "ps_probe", "ps_5_0", k)) { good &= SUCCEEDED(G.dev->CreatePixelShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &pr[k])); c->Release(); } else good = false;
	}
	ID3D11PixelShader *psk[2] = {};
	for (int k = 0; k < 2 && good; k++) { if (ID3DBlob *c = compile(src, "ps_shock", "ps_5_0", k)) { good &= SUCCEEDED(G.dev->CreatePixelShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &psk[k])); c->Release(); } else good = false; }
	ID3D11PixelShader *pst[2] = {}; ID3D11ComputeShader *cst = nullptr;
	for (int k = 0; k < 2 && good; k++) { if (ID3DBlob *c = compile(src, "ps_trail", "ps_5_0", k)) { good &= SUCCEEDED(G.dev->CreatePixelShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &pst[k])); c->Release(); } else good = false; }
	if (good) { if (ID3DBlob *c = compile(src, "cs_trail", "cs_5_0", false)) { good &= SUCCEEDED(G.dev->CreateComputeShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &cst)); c->Release(); } else good = false; }
	ID3D11ComputeShader *cs = nullptr;
	if (good) { if (ID3DBlob *c = compile(src, "cs_sun", "cs_5_0", false)) { good &= SUCCEEDED(G.dev->CreateComputeShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &cs)); c->Release(); } else good = false; }
	if (!good) { SR(vs); SR(cs); SR(cst); for (int k = 0; k < 2; k++) { SR(ps[k]); SR(pr[k]); SR(psk[k]); SR(pst[k]); } return false; }
	G.release_shaders();
	G.cs_sun = cs; G.ps_shock[0] = psk[0]; G.ps_shock[1] = psk[1];
	G.cs_trail = cst; G.ps_trail[0] = pst[0]; G.ps_trail[1] = pst[1];
	G.vs = vs; for (int k = 0; k < 2; k++) { G.ps[k] = ps[k]; G.probe[k] = pr[k]; }
	logf("shaders compilados");
	return true;
}

// ---------------------------------------------------------------- rastreio do buffer de profundidade
// camera de uma vista do DCS (cabine, pod de mira, espelho...): tangente do meio fov vertical, plano proximo, aspecto, eixos
struct ViewCap { float tanv = 0, nearv = 0, asp = 0; float ax[3][3] = {}; bool p = false, v = false; };
static ViewCap g_any;	// ultima camera escrita pelo DCS (preenchida em matcap.inc)
struct DStat { uint32_t draws = 0, since_clear = 0, clears = 0; ViewCap cam; bool vp_ok = false; D3D11_VIEWPORT vp = {}; bool has_rt = false; UINT rw = 0, rh = 0; int rfmt = 0; };
static bool g_dump = false;
static std::mutex g_mx;
static std::unordered_map<ID3D11Resource *, DStat> g_dstat;
static std::unordered_map<command_list *, ID3D11Resource *> g_cur_dsv;
static std::unordered_map<command_list *, ID3D11RenderTargetView *> g_cur_rtv;
// pod de mira (targeting pod): passada separada do DCS com camera, alvo e profundidade proprios
static ID3D11Resource *g_pod_dsv = nullptr;	// (mantido por compatibilidade: primeira do conjunto)
struct PodSlot { ID3D11Resource *dsv = nullptr; uint64_t seen = 0; };
static PodSlot g_pods[6];
static uint64_t g_frame_d = 0;
static bool is_pod(ID3D11Resource *r) { if (!r) return false; for (auto &p : g_pods) if (p.dsv == r) return true; return false; }
static int g_pod_draws_this_frame = 0;
static void pod_inject(ID3D11DeviceContext *ctx, ID3D11Resource *dsv, ID3D11RenderTargetView *rtv, const DStat &st);
static ID3D11Resource *g_sel = nullptr;		// buffer escolhido (mais desenhos no quadro anterior, tamanho da tela)
static uint32_t g_copied_draws = 0;
static uint32_t g_bb_w = 0, g_bb_h = 0;
struct DepthCopy
{
	ID3D11Texture2D *tex = nullptr; ID3D11ShaderResourceView *srv = nullptr;
	UINT w = 0, h = 0, samples = 0; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; bool valid = false;
	void release() { SR(srv); SR(tex); valid = false; w = h = 0; }
} g_dc, g_dc2, g_dcp;	// g_dcp: profundidade do pod de mira	// g_dc2: camada da cabine (ultimo trecho desenhado depois da limpeza)
static bool g_dc2_frame = false;

static DXGI_FORMAT typeless_of(DXGI_FORMAT f, DXGI_FORMAT &srv)
{
	switch (f)
	{
	case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: srv = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; return DXGI_FORMAT_R24G8_TYPELESS;
	case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R32_FLOAT: srv = DXGI_FORMAT_R32_FLOAT; return DXGI_FORMAT_R32_TYPELESS;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS: srv = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; return DXGI_FORMAT_R32G8X24_TYPELESS;
	case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: srv = DXGI_FORMAT_R16_UNORM; return DXGI_FORMAT_R16_TYPELESS;
	default: srv = DXGI_FORMAT_UNKNOWN; return DXGI_FORMAT_UNKNOWN;
	}
}

static ID3D11Resource *res_of_view(resource_view v)
{
	if (!v.handle) return nullptr;
	ID3D11Resource *r = nullptr; ((ID3D11View *)v.handle)->GetResource(&r);
	if (r) r->Release();	// so usamos o ponteiro como chave; o jogo mantem a referencia
	return r;
}

// copia o buffer de profundidade para a nossa textura (no contexto dado)
static void copy_depth_to(DepthCopy &g_dc, ID3D11DeviceContext *ctx, ID3D11Resource *src)
{
	ID3D11Texture2D *t = nullptr;
	if (FAILED(src->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&t))) return;
	D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); t->Release();
	DXGI_FORMAT sf; DXGI_FORMAT tl = typeless_of(d.Format, sf);
	if (tl == DXGI_FORMAT_UNKNOWN) return;
	if (!g_dc.tex || g_dc.w != d.Width || g_dc.h != d.Height || g_dc.samples != d.SampleDesc.Count || g_dc.fmt != tl)
	{
		g_dc.release();
		D3D11_TEXTURE2D_DESC n = d; n.Format = tl; n.BindFlags = D3D11_BIND_SHADER_RESOURCE; n.MipLevels = 1; n.ArraySize = 1; n.CPUAccessFlags = 0; n.MiscFlags = 0; n.Usage = D3D11_USAGE_DEFAULT;
		if (d.ArraySize != 1 || d.MipLevels != 1) return;
		if (FAILED(G.dev->CreateTexture2D(&n, nullptr, &g_dc.tex))) { logf("ERRO: copia de profundidade (fmt %d)", (int)d.Format); return; }
		D3D11_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = sf;
		if (d.SampleDesc.Count > 1) sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
		else { sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1; }
		if (FAILED(G.dev->CreateShaderResourceView(g_dc.tex, &sv, &g_dc.srv))) { logf("ERRO: SRV de profundidade"); g_dc.release(); return; }
		g_dc.w = d.Width; g_dc.h = d.Height; g_dc.samples = d.SampleDesc.Count; g_dc.fmt = tl;
		logf("profundidade: %ux%u fmt %d amostras %u", d.Width, d.Height, (int)d.Format, d.SampleDesc.Count);
	}
	ctx->CopyResource(g_dc.tex, src);
	g_dc.valid = true;
}
static void copy_depth(ID3D11DeviceContext *ctx, ID3D11Resource *src) { copy_depth_to(g_dc, ctx, src); }

static bool tex_matches_screen(ID3D11Resource *r)
{
	ID3D11Texture2D *t = nullptr;
	if (!r || FAILED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&t))) return false;
	D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); t->Release();
	return d.Width == g_bb_w && d.Height == g_bb_h;
}

static void on_bind_rt_ds(command_list *cmd, uint32_t count, const resource_view *rtvs, resource_view dsv)
{
	if (!G.ok) return;
	ID3D11Resource *r = res_of_view(dsv);
	ID3D11RenderTargetView *rv = count > 0 && rtvs ? (ID3D11RenderTargetView *)rtvs[0].handle : nullptr;
	ID3D11Resource *prev = nullptr; ID3D11RenderTargetView *prev_rtv = nullptr; DStat st; bool fire = false;
	{
		std::lock_guard<std::mutex> lk(g_mx);
		auto it = g_cur_dsv.find(cmd);
		if (it != g_cur_dsv.end()) prev = it->second;
		auto ir = g_cur_rtv.find(cmd);
		if (ir != g_cur_rtv.end()) prev_rtv = ir->second;
		g_cur_dsv[cmd] = r; g_cur_rtv[cmd] = rv;
		// fim de uma passada do pod: a imagem dele esta pronta e ainda nao foi usada nos MFDs -> desenha a explosao nela
		if (cfg.pod_on && prev && prev != r && prev_rtv && is_pod(prev))
		{
			auto is = g_dstat.find(prev);
			if (is != g_dstat.end() && is->second.since_clear >= (uint32_t)cfg.pod_min_draws / 2 && is->second.cam.p && is->second.cam.v)
			{ st = is->second; fire = true; }
		}
	}
	if (fire)
	{
		if ((ID3D11DeviceContext *)cmd->get_native() == G.ctx) pod_inject(G.ctx, prev, prev_rtv, st);
		else { static bool told = false; if (!told) { told = true; logf("pod de mira: passada num contexto diferido (nao suportado ainda)"); } }
	}
}
static void count_draw(command_list *cmd)
{
	std::lock_guard<std::mutex> lk(g_mx);
	auto it = g_cur_dsv.find(cmd);
	if (it == g_cur_dsv.end() || !it->second) return;
	DStat &s = g_dstat[it->second]; s.draws++; s.since_clear++;
	if (g_any.p && g_any.v) s.cam = g_any;
	auto ir = g_cur_rtv.find(cmd);
	if (ir != g_cur_rtv.end() && ir->second && !s.has_rt)
	{
		s.has_rt = true;
		ID3D11Resource *rr = nullptr; ir->second->GetResource(&rr);
		ID3D11Texture2D *t = nullptr;
		if (rr && SUCCEEDED(rr->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&t))) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); t->Release(); s.rw = d.Width; s.rh = d.Height; s.rfmt = (int)d.Format; }
		if (rr) rr->Release();
	}
	if (!s.vp_ok)
	{
		ID3D11DeviceContext *c = (ID3D11DeviceContext *)cmd->get_native();
		if (c == G.ctx) { UINT n = 1; c->RSGetViewports(&n, &s.vp); s.vp_ok = n == 1; }
	}
}
static bool on_draw(command_list *cmd, uint32_t, uint32_t, uint32_t, uint32_t) { if (G.ok) count_draw(cmd); return false; }
static bool on_draw_indexed(command_list *cmd, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) { if (G.ok) count_draw(cmd); return false; }
static bool on_draw_indirect(command_list *cmd, indirect_command type, resource, uint64_t, uint32_t, uint32_t)
{
	if (G.ok && type != indirect_command::dispatch) count_draw(cmd);
	return false;
}
static bool on_clear_ds(command_list *cmd, resource_view dsv, const float *depth, const uint8_t *, uint32_t, const rect *)
{
	if (!G.ok || !depth) return false;
	ID3D11Resource *r = res_of_view(dsv);
	std::lock_guard<std::mutex> lk(g_mx);
	auto it = g_dstat.find(r);
	if (it == g_dstat.end()) return false;
	if (r == g_sel && it->second.since_clear >= (uint32_t)cfg.depth_min_draws && it->second.since_clear > g_copied_draws)
	{
		copy_depth((ID3D11DeviceContext *)cmd->get_native(), r);
		g_copied_draws = it->second.since_clear;
	}
	it->second.since_clear = 0; it->second.clears++;
	return false;
}

// fim do quadro: copia o que sobrou e escolhe o buffer do proximo quadro
static bool g_have_spawn_any();
static void depth_end_frame()
{
	std::lock_guard<std::mutex> lk(g_mx);
	g_dc2_frame = false;
	if (g_sel)
	{
		auto it = g_dstat.find(g_sel);
		if (it != g_dstat.end() && it->second.since_clear >= (uint32_t)cfg.depth_min_draws && it->second.since_clear > g_copied_draws)
			copy_depth(G.ctx, g_sel);
		else if (it != g_dstat.end() && it->second.since_clear > 0 && g_copied_draws > 0)
		{
			// sobrou um trecho menor depois da limpeza que copiamos (tipicamente a cabine): guarda como mascara
			copy_depth_to(g_dc2, G.ctx, g_sel);
			g_dc2_frame = g_dc2.valid && g_dc2.samples == g_dc.samples;
			static bool told = false;
			if (!told) { told = true; logf("camada da cabine detectada (%u desenhos depois da limpeza)", it->second.since_clear); }
		}
	}
	if (g_dump)
	{
		g_dump = false;
		for (auto &kv : g_dstat)
		{
			ID3D11Texture2D *t = nullptr; D3D11_TEXTURE2D_DESC d = {};
			if (SUCCEEDED(kv.first->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&t))) { t->GetDesc(&d); t->Release(); }
			logf("  profundidade %p: %ux%u fmt %d amostras %u, %u desenhos, %u limpezas, %u no ultimo trecho%s", (void *)kv.first, d.Width, d.Height,
				(int)d.Format, d.SampleDesc.Count, kv.second.draws, kv.second.clears, kv.second.since_clear, kv.first == g_sel ? "  <- escolhido" : "");
		}
		logf("  copia principal: %u desenhos; cabine neste quadro: %d", g_copied_draws, (int)g_dc2_frame);
	}
	// pod de mira: passadas com alvo de cor, muitos desenhos e campo de visao estreito (pode haver mais de uma: NFOV, WFOV,
	// copias para os dois MFDs). Cada uma fica no conjunto enquanto aparecer pelo menos a cada 60 quadros.
	{
		g_frame_d++;
		float tmax = tanf(cfg.pod_max_fov * 0.5f * 0.0174533f);
		bool changed = false;
		for (auto &kv : g_dstat)
		{
			const DStat &d = kv.second;
			if (kv.first == g_sel || !d.has_rt || d.draws < (uint32_t)cfg.pod_min_draws) continue;
			if (!d.cam.p || !d.cam.v || d.cam.tanv <= 0 || d.cam.tanv > tmax) continue;
			PodSlot *slot = nullptr;
			for (auto &p : g_pods) if (p.dsv == kv.first) slot = &p;
			if (!slot) { for (auto &p : g_pods) if (!p.dsv) { slot = &p; break; } if (slot) { slot->dsv = kv.first; changed = true; } }
			if (slot) slot->seen = g_frame_d;
			if (changed && cfg.pod_log)
			{
				static DWORD lastlog = 0;
				if (GetTickCount() - lastlog > 5000)
				{
					lastlog = GetTickCount();
					ID3D11Texture2D *t = nullptr; D3D11_TEXTURE2D_DESC dd = {};
					if (SUCCEEDED(kv.first->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&t))) { t->GetDesc(&dd); t->Release(); }
					logf("pod de mira: passada %p %ux%u fmt %d amostras %u, %u desenhos, fov %.2f graus, plano proximo %.3f, cor %ux%u fmt %d", (void *)kv.first, dd.Width, dd.Height, (int)dd.Format,
						dd.SampleDesc.Count, d.draws, atanf(d.cam.tanv) * 114.5916f, d.cam.nearv, d.rw, d.rh, d.rfmt);
				}
			}
		}
		for (auto &p : g_pods) if (p.dsv && g_frame_d - p.seen > 60) { p = PodSlot(); }
		g_pod_dsv = g_pods[0].dsv;
		// tabela de diagnostico (no maximo a cada 30 s, enquanto houver explosao e nenhum pod)
		static DWORD last_table = 0;
		bool anypod = false; for (auto &p : g_pods) anypod |= p.dsv != nullptr;
		if (cfg.pod_log && g_have_spawn_any() && !anypod && GetTickCount() - last_table > 30000)
		{
			last_table = GetTickCount();
			logf("pod de mira: nenhuma passada estreita encontrada; vistas deste quadro:");
			for (auto &kv : g_dstat)
			{
				if (kv.second.draws < 10) continue;
				ID3D11Texture2D *t = nullptr; D3D11_TEXTURE2D_DESC d = {};
				if (SUCCEEDED(kv.first->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&t))) { t->GetDesc(&d); t->Release(); }
				const DStat &st = kv.second;
				logf("    passada %p: %ux%u fmt %d am %u, %u desenhos, cor %ux%u fmt %d, fov %.2f aspecto %.2f%s", (void *)kv.first, d.Width, d.Height, (int)d.Format,
					d.SampleDesc.Count, st.draws, st.rw, st.rh, st.rfmt, st.cam.p ? atanf(st.cam.tanv) * 114.5916f : -1.f, st.cam.asp, kv.first == g_sel ? "  <- principal" : "");
			}
		}
	}
	ID3D11Resource *best = nullptr; uint32_t bd = 0;
	for (auto &kv : g_dstat)
		if (kv.second.draws > bd && tex_matches_screen(kv.first)) { bd = kv.second.draws; best = kv.first; }
	if (best != g_sel) logf("buffer de profundidade escolhido: %p (%u desenhos)", (void *)best, bd);
	g_sel = best;
	g_dstat.clear();
	g_copied_draws = 0;
}

// ---------------------------------------------------------------- ruido de detalhe (3D, repete nas bordas)
// Celulas de Worley invertidas (formato de "couve-flor") + ruido de valor, 64^3, uma vez na inicializacao
static bool make_noise()
{
	const int N = 64;
	std::vector<unsigned char> v((size_t)N * N * N);
	uint32_t seed = 1234567;
	auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.f; };
	auto worley = [&](int cells, std::vector<float> &out) {
		std::vector<float> pts((size_t)cells * cells * cells * 3);
		for (auto &p : pts) p = rnd();
		out.assign((size_t)N * N * N, 0.f);
		for (int z = 0; z < N; z++) for (int y = 0; y < N; y++) for (int x = 0; x < N; x++)
		{
			float px = (x + 0.5f) / N * cells, py = (y + 0.5f) / N * cells, pz = (z + 0.5f) / N * cells;
			int cx = (int)px, cy = (int)py, cz = (int)pz; float best = 9.f;
			for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
			{
				int ix = cx + dx, iy = cy + dy, iz = cz + dz;
				int wx = (ix % cells + cells) % cells, wy = (iy % cells + cells) % cells, wz = (iz % cells + cells) % cells;
				const float *q = &pts[((size_t)(wz * cells + wy) * cells + wx) * 3];
				float ddx = ix + q[0] - px, ddy = iy + q[1] - py, ddz = iz + q[2] - pz;
				best = std::min(best, ddx * ddx + ddy * ddy + ddz * ddz);
			}
			out[((size_t)z * N + y) * N + x] = 1.f - std::min(1.f, sqrtf(best));
		}
	};
	std::vector<float> w1, w2, w3;
	worley(4, w1); worley(8, w2); worley(16, w3);
	for (size_t i = 0; i < v.size(); i++)
	{
		float n = 0.5f * w1[i] + 0.3f * w2[i] + 0.2f * w3[i];
		v[i] = (unsigned char)std::min(255.f, std::max(0.f, (n - 0.15f) / 0.7f * 255.f));
	}
	D3D11_TEXTURE3D_DESC td = {}; td.Width = td.Height = td.Depth = N; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8_UNORM;
	td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = {v.data(), (UINT)N, (UINT)(N * N)};
	if (FAILED(G.dev->CreateTexture3D(&td, &sd, &G.noise_tex)) || FAILED(G.dev->CreateShaderResourceView(G.noise_tex, nullptr, &G.noise_srv))) { logf("ERRO: textura de ruido"); return false; }
	return true;
}

// ---------------------------------------------------------------- dispositivo
static bool init_gfx(device *device)
{
	if (device->get_api() != device_api::d3d11) { logf("API grafica nao e D3D11 - add-on inativo"); return false; }
	G.dev = (ID3D11Device *)device->get_native();
	if (FAILED(G.dev->QueryInterface(__uuidof(ID3D11Device1), (void **)&G.dev1))) { logf("ERRO: sem ID3D11Device1"); return false; }
	G.dev->GetImmediateContext(&G.ctx);
	if (FAILED(G.ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&G.ctx1))) { logf("ERRO: sem ID3D11DeviceContext1"); return false; }
	D3D_FEATURE_LEVEL fl = G.dev->GetFeatureLevel();
	UINT fl_flags = (G.dev->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;
	if (FAILED(G.dev1->CreateDeviceContextState(fl_flags, &fl, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), nullptr, &G.state))) { logf("ERRO: CreateDeviceContextState"); return false; }

	D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16 * 22; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	G.dev->CreateBuffer(&bd, nullptr, &G.cb);
	{ D3D11_BUFFER_DESC tb = bd; tb.ByteWidth = 16 * 34; G.dev->CreateBuffer(&tb, nullptr, &G.cb_tr); }
	D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
	G.dev->CreateSamplerState(&sd, &G.samp);
	D3D11_BLEND_DESC bl = {}; auto &rt = bl.RenderTarget[0];
	rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D11_BLEND_ZERO; rt.DestBlendAlpha = D3D11_BLEND_ONE; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
	G.dev->CreateBlendState(&bl, &G.blend);
	D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = FALSE;
	G.dev->CreateRasterizerState(&rd, &G.rs);
	D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = FALSE; dd.StencilEnable = FALSE;
	G.dev->CreateDepthStencilState(&dd, &G.dss);
	D3D11_TEXTURE2D_DESC pd = {}; pd.Width = NZ_PROBE_NX; pd.Height = NZ_PROBE_NY; pd.MipLevels = pd.ArraySize = 1; pd.Format = DXGI_FORMAT_R32_FLOAT; pd.SampleDesc.Count = 1;
	pd.Usage = D3D11_USAGE_DEFAULT; pd.BindFlags = D3D11_BIND_RENDER_TARGET;
	G.dev->CreateTexture2D(&pd, nullptr, &G.probe_rt);
	G.dev->CreateRenderTargetView(G.probe_rt, nullptr, &G.probe_rtv);
	pd.Usage = D3D11_USAGE_STAGING; pd.BindFlags = 0; pd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	G.dev->CreateTexture2D(&pd, nullptr, &G.probe_st);
	if (!G.cb || !G.samp || !G.blend || !G.rs || !G.dss || !G.probe_rtv || !G.probe_st) { logf("ERRO: criando estados"); return false; }
	if (!build_shaders()) return false;
	{
		D3D11_SAMPLER_DESC sw = {}; sw.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sw.AddressU = sw.AddressV = sw.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sw.MaxLOD = D3D11_FLOAT32_MAX;
		G.dev->CreateSamplerState(&sw, &G.samp_wrap);
		if (!make_noise()) return false;
	}
	G.dev->AddRef(); g_load_dev = G.dev;
	CloseHandle(CreateThread(nullptr, 0, loader_thread, G.dev, 0, nullptr));
	if (cfg.volume > 0) load_sounds();	// sons proprios so se o usuario ligar; o padrao e o som original do DCS
	G.ok = true;
	logf("pronto (%s)", cfg.dev ? "modo desenvolvedor: atalhos ligados" : "explosoes automaticas pelas armas reais");
	return true;
}

// Obs.: o add-on e compilado com MinGW; metodos virtuais do ReShade que devolvem estruturas (resource, resource_desc...)
// tem ABI diferente do MSVC. Por isso usamos so os ponteiros nativos (get_native) e o D3D11/DXGI direto.
static ID3D11Texture2D *back_buffer(swapchain *sc)
{
	IDXGISwapChain *dsc = (IDXGISwapChain *)sc->get_native();
	ID3D11Texture2D *t = nullptr;
	if (!dsc || FAILED(dsc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&t))) return nullptr;
	t->Release();	// a swapchain mantem a referencia
	return t;
}

static void on_init_swapchain(swapchain *sc, bool)
{
	device *dev = sc->get_device();
	if (!dev || dev->get_api() != device_api::d3d11) { logf("API grafica nao e D3D11 - add-on inativo"); return; }
	ID3D11Texture2D *bb = back_buffer(sc);
	if (!bb) { logf("ERRO: sem back buffer"); return; }
	D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
	g_bb_w = d.Width; g_bb_h = d.Height;
	logf("swapchain %ux%u formato %d", g_bb_w, g_bb_h, (int)d.Format);
	if (!G.dev && !G.ok)
	{
		if (!init_gfx(dev)) { logf("inicializacao falhou - add-on desligado"); G.release(); G.dev = nullptr; }
	}
}
static void on_destroy_swapchain(swapchain *, bool)
{
	std::lock_guard<std::mutex> lk(g_mx);
	for (auto &kv : G.bb_rtv) kv.second->Release();
	G.bb_rtv.clear();
	g_dstat.clear(); g_cur_dsv.clear(); g_sel = nullptr;
	g_dc.release(); g_dc2.release(); g_dcp.release();
}
static void on_destroy_device(device *)
{
	std::lock_guard<std::mutex> lk(g_mx);
	g_load_dev = nullptr; g_dc.release(); g_dc2.release(); g_dcp.release(); release_all_volumes(); G.release();
}

#include "matcap.inc"

// ---------------------------------------------------------------- estado da explosao, teclas, calibracao
struct Inst { bool on = false; int type = 0, var = 0; double p[3] = {}; double t0 = 0; float yaw = 0, rate = 1, wind[2] = {}; bool mirror = false, real = false, snd = false;
	int ntr = 0; float tv[16][4] = {}, tp[16][4] = {}; float tr_end = 0;
	ID3D11Texture3D *trt = nullptr; ID3D11UnorderedAccessView *truav = nullptr; ID3D11ShaderResourceView *trsrv = nullptr; ID3D11Texture3D *sun = nullptr; ID3D11UnorderedAccessView *uav = nullptr; ID3D11ShaderResourceView *srv = nullptr; int sx = 0, sy = 0, sz = 0; };
static const int MAXI = 12;
static Inst g_inst[MAXI];
static bool g_have_spawn = false;	// alguma explosao ativa
static LONG g_spawn_seq_seen = -1, g_clear_seq_seen = -1, g_calib_seq_seen = 0, g_sun_seq_seen = 0;
static bool g_probe_force = false; static int g_probe_wait = 0; static uint64_t g_frame = 0;
static float g_tanv = 0.f, g_near = 0.f;
static double g_sun[3] = {0, 0, 0}; static double g_sun_el = 52.0; static bool g_sun_from_dcs = false;
static bool g_freeze = false;
static LONG g_last_lua_frames = 0; static DWORD g_lua_check = 0;
static int g_probe_state = 0; static double g_probe_cam[12]; static float g_probe_near = 0, g_probe_aspect = 0;
static DWORD g_last_calib_log = 0;

static bool key_edge(int vk)
{
	static bool prev[256] = {};
	bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
	bool e = down && !prev[vk]; prev[vk] = down;
	return e;
}
static bool we_are_foreground()
{
	DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	return pid == GetCurrentProcessId();
}

static void request_spawn(int type)
{
	if (!vols[type * MAXV].ready) { logf("explosao %s ainda carregando ou ausente", TYPE_NAME[type]); return; }
	g_sh->spawn_distance = cfg.dist[type]; g_sh->spawn_req_type = type;
	MemoryBarrier();
	InterlockedIncrement(&g_sh->spawn_req);
	logf("pedido de explosao %s", TYPE_NAME[type]);
	if (!g_loc_view.r || !g_loc_proj.r) { std::lock_guard<std::mutex> lk(g_mx); g_learn = 90; }
}

static void handle_keys()
{
	if (!cfg.dev) return;	// versao publica: sem atalhos
	bool mod = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) && we_are_foreground();
	int keys[] = {'B', 'Z', 'X', 'C', 'V', 'N', 'K', 'D', 'R', VK_END, 'F', VK_PRIOR, VK_NEXT, 'L'};
	const int NK = sizeof keys / sizeof keys[0];
	bool e[NK]; for (int k = 0; k < NK; k++) e[k] = key_edge(keys[k]) && mod;
	int tmap[6] = {0, 1, 2, 3, 4, 5};
	for (int k = 0; k < 6; k++) if (e[k]) request_spawn(tmap[k]);
	if (e[6]) { g_probe_force = true; g_probe_wait = 0; logf("calibracao pedida"); }
	if (e[7]) { cfg.debug = (cfg.debug + 1) % 3; logf("depuracao %d", cfg.debug); }
	if (e[8]) { float tv = g_tanv, nr = g_near; load_config(); g_tanv = tv; g_near = nr; build_shaders(); }
	if (e[9]) { for (auto &i : g_inst) i.on = false; logf("explosoes removidas"); }
	if (e[10]) { g_freeze = !g_freeze; logf("congelar em 13 s: %d", (int)g_freeze); }
	if (e[11] || e[12])
	{
		float v = atanf(g_tanv > 0 ? g_tanv : tanf(cfg.vfov_deg * 0.00872665f)) * 114.5916f + (e[11] ? 1.f : -1.f);
		if (v < 5.f) v = 5.f;
		if (v > 150.f) v = 150.f;
		g_tanv = tanf(v * 0.00872665f); logf("fov vertical %.1f graus (manual)", v);
	}
	if (e[13]) { g_dump = true; logf("diagnostico do quadro (e novo aprendizado das matrizes):"); std::lock_guard<std::mutex> lk(g_mx); g_learn = 90; g_loc_proj = MatLoc(); g_loc_view = MatLoc(); for (int i = 0; i < NALT; i++) { g_alt_proj[i] = MatLoc(); g_alt_view[i] = MatLoc(); } }
}

static void poll_shared()
{
	if (g_spawn_seq_seen < 0) { g_spawn_seq_seen = g_sh->spawn_seq; g_clear_seq_seen = g_sh->clear_seq; }
	if (g_sh->clear_seq != g_clear_seq_seen) { g_clear_seq_seen = g_sh->clear_seq; for (auto &i : g_inst) i.on = false; }
	LONG seq = g_sh->spawn_seq; MemoryBarrier();
	if (seq - g_spawn_seq_seen > NZ_RING) g_spawn_seq_seen = seq - NZ_RING;
	for (; g_spawn_seq_seen < seq; g_spawn_seq_seen++)
	{
		auto &e = g_sh->ring[g_spawn_seq_seen % NZ_RING];
		int ty = (e.type >= 0 && e.type < 6 && vols[e.type * MAXV].ready) ? e.type : 0;
		bool v1 = g_sh->opt_jdam == 2 || (g_sh->opt_jdam != 1 && cfg.jdam_v1);
		if (ty == 0 && v1 && vols[6 * MAXV].ready) ty = 6;
		if (!vols[ty * MAXV].ready) continue;
		// variacao sorteada; se ainda nao esta na VRAM, carrega em segundo plano e esta usa a primeira
		int var = 0;
		if (g_nvar[ty] > 1)
		{
			static uint32_t rv = GetTickCount() * 2246822519u;
			rv ^= rv << 13; rv ^= rv >> 17; rv ^= rv << 5;
			int want = (int)(rv % (uint32_t)g_nvar[ty]);
			Volume &w = vols[ty * MAXV + want];
			if (w.ready) var = want;
			else if (w.exists && InterlockedCompareExchange(&w.loading, 1, 0) == 0) CloseHandle(CreateThread(nullptr, 0, loader_one, (LPVOID)(intptr_t)(ty * MAXV + want), 0, nullptr));
		}
		// vaga livre ou a mais antiga
		int k = 0; double oldest = 1e300;
		for (int i = 0; i < MAXI; i++) { if (!g_inst[i].on) { k = i; break; } if (g_inst[i].t0 < oldest) { oldest = g_inst[i].t0; k = i; } }
		Inst &I = g_inst[k];
		I.on = true; I.type = ty; I.var = var; memcpy(I.p, e.p, 24); I.t0 = e.t; I.real = e.pad != 0; I.snd = false;
		// cada explosao diferente: giro aleatorio em torno da vertical, espelho e velocidade da animacao +-10%
		static uint32_t rs = GetTickCount() * 2654435761u;
		auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xffffff) / 16777216.f; };
		I.yaw = rnd() * 6.2831853f; I.mirror = rnd() < 0.5f; I.rate = 0.9f + 0.2f * rnd();
		if (e.fixed) { I.yaw = e.yaw; I.mirror = false; }	// tiro de tanque: o jato sai na direcao do cano
		I.wind[0] = cfg.wind_on ? e.wx * cfg.wind_gain : 0.f; I.wind[1] = cfg.wind_on ? e.wz * cfg.wind_gain : 0.f;
		// destrocos com rastro de fumaca (so explosoes no chao com fragmentos)
		{
			static const int NT[7] = {14, 0, 5, 10, 0, 5, 14};
			static const float VMIN[7] = {70, 0, 30, 50, 0, 25, 70}, VMAX[7] = {150, 0, 60, 110, 0, 50, 150}, SZ[7] = {1.3f, 0, 0.5f, 1.0f, 0, 0.5f, 1.3f};
			I.ntr = cfg.debris_on ? NT[ty] : 0; I.tr_end = 0;
			for (int k = 0; k < I.ntr; k++)
			{
				float az = rnd() * 6.2831853f, el = (35.f + 45.f * powf(rnd(), 0.7f)) * 0.0174533f;
				float v = VMIN[ty] + (VMAX[ty] - VMIN[ty]) * rnd(), kd = 0.25f + 0.25f * rnd();
				I.tv[k][0] = v * cosf(el) * cosf(az); I.tv[k][1] = v * cosf(el) * sinf(az); I.tv[k][2] = v * sinf(el); I.tv[k][3] = kd;
				// tempo ate tocar o chao (z da trajetoria com arrasto)
				float vinf = -9.81f / kd, tl = 0;
				for (tl = 0.05f; tl < 30.f; tl += 0.05f) { float z = 2.f + vinf * tl + (I.tv[k][2] - vinf) * (1.f - expf(-kd * tl)) / kd; if (z <= 0.f) break; }
				I.tp[k][0] = 0.08f * rnd(); I.tp[k][1] = SZ[ty] * (0.75f + 0.5f * rnd()); I.tp[k][2] = 0.6f + 0.4f * rnd(); I.tp[k][3] = tl;
				I.tr_end = std::max(I.tr_end, tl + 12.f);
			}
		}
		logf("explosao %s em %.1f %.1f %.1f (t %.2f) giro %.0f espelho %d ritmo %.2f vento %.1f %.1f", TYPE_NAME[ty], I.p[0], I.p[1], I.p[2], I.t0, I.yaw * 57.3f, (int)I.mirror, I.rate, I.wind[0], I.wind[1]);
	}
	if (g_sh->sun_seq != g_sun_seq_seen)
	{
		g_sun_seq_seen = g_sh->sun_seq; MemoryBarrier();
		memcpy(g_sun, g_sh->sun_dir, 24); g_sun_el = g_sh->sun_el_deg; g_sun_from_dcs = true;
		logf("sol da missao: elevacao %.1f, azimute %.1f graus", g_sh->sun_el_deg, g_sh->sun_az_deg);
	}
	if (g_sh->calib_seq != g_calib_seq_seen)
	{
		g_calib_seq_seen = g_sh->calib_seq; MemoryBarrier();
		int ok = g_sh->calib_ok;
		if ((ok & 1) && g_sh->calib_near > 0.001 && g_sh->calib_near < 2.0) g_near = g_near > 0 ? (float)(0.7 * g_near + 0.3 * g_sh->calib_near) : (float)g_sh->calib_near;
		if ((ok & 2) && g_sh->calib_tanv > 0.02 && g_sh->calib_tanv < 5.0)
		{
			float nt = (float)g_sh->calib_tanv;
			// troca rapida de zoom: pula direto; variacao pequena: suaviza
			g_tanv = (g_tanv <= 0 || fabsf(nt - g_tanv) / g_tanv > 0.08f) ? nt : 0.5f * g_tanv + 0.5f * nt;
		}
		DWORD now = GetTickCount();
		if (g_probe_force || now - g_last_calib_log > 4000)
		{
			g_last_calib_log = now;
			logf("calibracao: ok=%d plano proximo %.4f m, fov vertical %.1f graus, %d sondas, dispersao %.3f (%s)", ok, g_near > 0 ? g_near : cfg.near_m,
				atanf(g_tanv > 0 ? g_tanv : tanf(cfg.vfov_deg * 0.00872665f)) * 114.5916f, g_sh->calib_nsamp, g_sh->calib_spread, g_sh->lua_msg);
		}
	}
	DWORD now = GetTickCount();
	if (now - g_lua_check > 5000)
	{
		if (g_sh->lua_frames == g_last_lua_frames && g_have_spawn) logf("aviso: hook Lua parado (fora da missao ou nao instalado)");
		g_last_lua_frames = g_sh->lua_frames; g_lua_check = now;
	}
}

// ---------------------------------------------------------------- desenho
struct CB { float camO[4], camF[4], camR[4], camU[4], grid[4], scales[4], scales2[4], screen[4], depthInfo[4], fog[4], misc[4], sunDir[4], sunCol[4], skyCol[4], nearInfo[4], detail[4], detail2[4], wind[4], shock[4], shock2[4]; };

static ID3D11RenderTargetView *bb_rtv_for(swapchain *sc)
{
	ID3D11Resource *bb = back_buffer(sc);
	if (!bb) return nullptr;
	auto it = G.bb_rtv.find(bb);
	if (it != G.bb_rtv.end()) return it->second;
	ID3D11RenderTargetView *v = nullptr;
	if (FAILED(G.dev->CreateRenderTargetView(bb, nullptr, &v))) return nullptr;
	G.bb_rtv[bb] = v;
	return v;
}

static void common_state(const CB &cb)
{
	G.ctx->UpdateSubresource(G.cb, 0, nullptr, &cb, 0, 0);
	G.ctx->OMSetDepthStencilState(G.dss, 0);
	G.ctx->RSSetState(G.rs);
	G.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); G.ctx->IASetInputLayout(nullptr);
	G.ctx->VSSetShader(G.vs, nullptr, 0);
	G.ctx->PSSetConstantBuffers(0, 1, &G.cb); G.ctx->CSSetConstantBuffers(0, 1, &G.cb);
	G.ctx->PSSetSamplers(0, 1, &G.samp); G.ctx->CSSetSamplers(0, 1, &G.samp);
}

// sonda nao bloqueante: desenha a grade de profundidades num quadro e le alguns quadros depois
static void issue_probe(const CB &cb, const Cam &cam)
{
	ID3D11ShaderResourceView *srv[6] = {nullptr, nullptr, nullptr, nullptr, g_dc.srv, nullptr};
	D3D11_VIEWPORT vp = {0, 0, (float)NZ_PROBE_NX, (float)NZ_PROBE_NY, 0, 1};
	G.ctx->OMSetRenderTargets(1, &G.probe_rtv, nullptr);
	G.ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	G.ctx->RSSetViewports(1, &vp);
	G.ctx->PSSetShader(G.probe[g_dc.samples > 1], nullptr, 0);
	G.ctx->PSSetShaderResources(0, 6, srv);
	G.ctx->Draw(3, 0);
	ID3D11ShaderResourceView *nul[6] = {};
	G.ctx->PSSetShaderResources(0, 6, nul);
	ID3D11RenderTargetView *nrt = nullptr; G.ctx->OMSetRenderTargets(1, &nrt, nullptr);
	G.ctx->CopyResource(G.probe_st, G.probe_rt);
	for (int k = 0; k < 3; k++) { g_probe_cam[k] = cam.p[k]; g_probe_cam[3 + k] = cam.x[k]; g_probe_cam[6 + k] = cam.y[k]; g_probe_cam[9 + k] = cam.z[k]; }
	g_probe_near = cb.camU[3]; g_probe_aspect = g_bb_h ? (float)g_bb_w / g_bb_h : 1.7778f;
	g_probe_state = 1;
}

static void collect_probe()
{
	D3D11_MAPPED_SUBRESOURCE m;
	if (FAILED(G.ctx->Map(G.probe_st, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) return;
	for (int y = 0; y < NZ_PROBE_NY; y++)
		for (int x = 0; x < NZ_PROBE_NX; x++)
		{
			int i = y * NZ_PROBE_NX + x;
			g_sh->probe_d[i] = ((float *)((char *)m.pData + y * m.RowPitch))[x];
			g_sh->probe_ndc[i][0] = -0.8f + 0.2f * x; g_sh->probe_ndc[i][1] = 0.4f - 0.2f * y;
		}
	G.ctx->Unmap(G.probe_st, 0);
	if (!cfg.reversed) for (int i = 0; i < NZ_PROBE_N; i++) g_sh->probe_d[i] = 1.f - g_sh->probe_d[i];	// o Lua sempre recebe "near / z"
	memcpy(g_sh->probe_cam, g_probe_cam, sizeof g_probe_cam);
	g_sh->probe_aspect = g_probe_aspect; g_sh->probe_near = g_probe_near;
	MemoryBarrier();
	InterlockedIncrement(&g_sh->probe_seq);
	g_probe_state = 0;
}

// volume comprimido: canais b,a em t10..t13 (pixel e compute)
static void bind_bc(Volume &vol, int i0, int i1, bool cs)
{
	ID3D11ShaderResourceView *x[4] = {vol.bc ? vol.A2[i0] : nullptr, vol.bc ? vol.B2[i0] : nullptr, vol.bc ? vol.A2[i1] : nullptr, vol.bc ? vol.B2[i1] : nullptr};
	if (cs) G.ctx->CSSetShaderResources(10, 4, x); else G.ctx->PSSetShaderResources(10, 4, x);
}
static void unbind_bc(bool cs) { ID3D11ShaderResourceView *x[4] = {}; if (cs) G.ctx->CSSetShaderResources(10, 4, x); else G.ctx->PSSetShaderResources(10, 4, x); }

// camera e grade de uma explosao no CB (comum a cabine e ao pod de mira)
static void setup_job_cb(CB &cb, Inst &I, Volume &vol, const Cam &cam, const double *sd, double t, float fade, float w)
{
	struct { double t; float fade; } J = {t, fade};
	// mundo DCS -> grade desta explosao: grade x ~ norte, y ~ leste, z = cima, girada por I.yaw e espelhada se I.mirror
	float cy = cosf(I.yaw), sy = sinf(I.yaw);
	auto toGrid = [&](double vx, double vy, double vz, float *o) {	// vetor do mundo (x norte, y cima, z leste)
		double h0 = cy * vx + sy * vz, h1 = -sy * vx + cy * vz;
		if (I.mirror) h1 = -h1;
		o[0] = (float)h0; o[1] = (float)h1; o[2] = (float)vy; };
	double zbase = TYPE_GROUND[I.type] ? I.p[1] - 0.5 : I.p[1] - vol.burst[2];
	toGrid(cam.p[0] - I.p[0], cam.p[1] - zbase, cam.p[2] - I.p[2], cb.camO);
	cb.camO[0] += vol.burst[0]; cb.camO[1] += vol.burst[1];
	{ float o[3]; toGrid(cam.x[0], cam.x[1], cam.x[2], o); memcpy(cb.camF, o, 12); toGrid(cam.y[0], cam.y[1], cam.y[2], o); memcpy(cb.camU, o, 12);
	  toGrid(cam.z[0], cam.z[1], cam.z[2], o); memcpy(cb.camR, o, 12); if (cfg.flip_right) for (int k = 0; k < 3; k++) cb.camR[k] = -cb.camR[k];
	  toGrid(sd[0], sd[1], sd[2], o); memcpy(cb.sunDir, o, 12); if (cb.sunDir[2] < 0.02f) cb.sunDir[2] = 0.02f;
	  toGrid(I.wind[0], 0, I.wind[1], o); cb.wind[0] = o[0]; cb.wind[1] = o[1]; }
	cb.wind[2] = (float)std::max(0.0, J.t / I.rate - cfg.wind_delay_s);	// segundos de deriva (tempo real, nao o da animacao)
	cb.wind[3] = TYPE_GROUND[I.type] ? cfg.wind_shear_m : 0.f;			// cisalhamento: perto do chao anda menos
	cb.camO[3] = std::min(cfg.step_m, vol.dx * 0.6f);
	cb.grid[0] = vol.nx * vol.dx; cb.grid[1] = vol.ny * vol.dx; cb.grid[2] = vol.nz * vol.dx; cb.grid[3] = w;
	cb.scales[0] = vol.mx[0]; cb.scales[1] = vol.mx[1]; cb.scales[2] = vol.mx[2]; cb.scales[3] = cfg.opacity * J.fade;
	cb.scales2[0] = vol.mx[3]; cb.scales2[1] = vol.mx[4];
	cb.detail2[2] = TYPE_GROUND[I.type] ? 1.f : 0.f;	// chao: sem desvanecer a face de baixo
	cb.detail2[3] = (float)(I.p[0] * 0.013 + I.p[2] * 0.007);	// desloca o ruido por explosao
	cb.misc[1] = (float)vol.bc;
}

static float smooth01(float a, float b, float x) { float t = std::min(1.f, std::max(0.f, (x - a) / (b - a))); return t * t * (3 - 2 * t); }

// luz, neblina, detalhe e sol no CB (comum a cabine e ao pod); devolve a direcao do sol no mundo DCS
static float g_night = 0.f;	// 0 = dia, 1 = noite fechada (pela elevacao do sol)
static void fill_light(CB &cb, double tnow, double *sd)
{
	auto G3 = [](const double *v, float *o) { o[0] = (float)v[0]; o[1] = (float)v[2]; o[2] = (float)v[1]; };
	cb.scales2[2] = cfg.exposure; cb.scales2[3] = (float)cfg.debug;
	cb.fog[0] = cfg.fog_r; cb.fog[1] = cfg.fog_g; cb.fog[2] = cfg.fog_b; cb.fog[3] = cfg.fog_dist;
	cb.detail[0] = cfg.detail; cb.detail[1] = cfg.detail_scale; cb.detail[2] = (float)fmod(tnow, 10000.0); cb.detail[3] = cfg.fire_gain;
	cb.detail2[0] = cfg.flame_min_t; cb.detail2[1] = cfg.edge_fade;

	// sol: da missao (Lua) ou do ini; luz cai e esquenta perto do horizonte
	double el = g_sun_el;
	if (g_sun_from_dcs) memcpy(sd, g_sun, 24);
	else { double az = cfg.sun_az * 0.0174533, e = cfg.sun_el * 0.0174533; el = cfg.sun_el; sd[0] = cos(e) * cos(az); sd[1] = sin(e); sd[2] = cos(e) * sin(az); }
	G3(sd, cb.sunDir); cb.sunDir[3] = 1.f;
	if (cb.sunDir[2] < 0.02f) cb.sunDir[2] = 0.02f;
	float fs = smooth01(-3.f, 8.f, (float)el), warm = smooth01(0.f, 25.f, (float)el);
	float tint[3] = {1.f, 0.6f + 0.4f * warm, 0.35f + 0.65f * warm}, base[3] = {1.00f * 2.5f, 0.95f * 2.5f, 0.86f * 2.5f}, sky[3] = {0.30f * 0.75f, 0.42f * 0.75f, 0.62f * 0.75f};
	// noite: sem sol nao ha luz do ceu nem luz rebatida do chao; a fumaca so aparece pelo fogo (e um resto de luar)
	float day = smooth01(-9.f, 12.f, (float)el);
	g_night = 1.f - day;
	float fsk = cfg.night_ambient + (1.f - cfg.night_ambient) * smooth01(-9.f, 25.f, (float)el);
	for (int k = 0; k < 3; k++) { cb.sunCol[k] = base[k] * tint[k] * fs; cb.skyCol[k] = sky[k] * fsk; }
	cb.skyCol[3] = std::max(day, cfg.night_ambient);							// luz rebatida do chao
	float fk = 0.03f + 0.97f * day; for (int k = 0; k < 3; k++) cb.fog[k] *= fk;	// nevoa: de noite o horizonte e escuro
	cb.detail[3] = cfg.fire_gain * (1.f + cfg.night_fire * g_night);			// olho/camera adaptados ao escuro: o fogo estoura
}


static void on_present(command_queue *, swapchain *sc, const rect *, const rect *, uint32_t, const rect *)
{
	if (!G.ok) return;
	g_frame++;
	depth_end_frame();
	if (!open_shm()) return;
	handle_keys();
	g_sh->dev = cfg.dev;
	poll_shared();
	if (g_probe_state == 1) collect_probe();
	Cam cam;
	bool have_cam = read_cam(cam);
	matcap_frame(cam, have_cam);

	float tanv = g_tanv > 0 ? g_tanv : tanf(cfg.vfov_deg * 0.00872665f);
	float nearm = g_near > 0 ? g_near : cfg.near_m;
	// matrizes do proprio DCS (exatas e do mesmo quadro): tem prioridade sobre a calibracao
	bool cap_proj = g_cap_proj_frame && g_frame_mt - g_cap_proj_frame <= 8;
	bool cap_view = g_cap_view_frame && g_frame_mt - g_cap_view_frame <= 8;
	if (cap_proj) { tanv = g_cap_tanv; if (g_cap_near > 0) nearm = g_cap_near; }
	if (cap_view && have_cam)
		for (int i = 0; i < 3; i++) { cam.x[i] = g_cap_ax[0][i]; cam.y[i] = g_cap_ax[1][i]; cam.z[i] = g_cap_ax[2][i]; }
	{
		static int last = -1; int st = (cap_proj ? 1 : 0) + (cap_view ? 2 : 0);
		if (st != last) { last = st; logf("camera: projecao %s, orientacao %s (fov vertical %.1f, plano proximo %.4f)", cap_proj ? "DO DCS" : "calibrada", cap_view ? "DO DCS" : "do Lua", atanf(tanv) * 114.5916f, nearm); }
	}
	float aspect = g_bb_h ? (float)g_bb_w / g_bb_h : 1.7778f;

	CB cb = {};
	cb.screen[0] = (float)g_bb_w; cb.screen[1] = (float)g_bb_h; cb.screen[2] = 1.f / g_bb_w; cb.screen[3] = 1.f / g_bb_h;
	cb.depthInfo[0] = g_dc.valid ? 1.f : 0.f; cb.depthInfo[1] = (float)cfg.reversed; cb.depthInfo[2] = (float)g_dc.w; cb.depthInfo[3] = (float)g_dc.h;
	cb.misc[0] = (float)cfg.max_steps;
	cb.camU[3] = nearm;
	bool use_mask = cfg.cockpit_mask && g_dc2_frame;
	cb.nearInfo[0] = use_mask ? 1.f : 0.f;
	cb.nearInfo[1] = cfg.reversed ? nearm / 5.0f : 1.f - nearm / 5.0f;	// tudo a menos de 5 m da camera e cabine

	bool want_probe = have_cam && g_dc.valid && g_probe_state == 0 && cfg.auto_calib && !cap_proj &&
		((g_probe_force && g_probe_wait-- <= 0) || ((g_have_spawn || cfg.debug) && cfg.calib_every > 0 && g_frame % cfg.calib_every == 0));

	// explosoes ativas: tempo, desvanecimento, ordem de tras para frente
	struct Job { int k; double t; float fade; double dist2; };
	std::vector<Job> jobs;
	g_have_spawn = false;
	for (int k = 0; k < MAXI; k++)
	{
		Inst &I = g_inst[k];
		if (!I.on) continue;
		if (!VOL(I).ready) { I.on = false; continue; }
		double t = have_cam ? (cam.t - I.t0) * I.rate : 0;
		if (g_freeze) t = 13.0 * VOL(I).t_end / 23.5;
		else if (cfg.freeze_t >= 0) t = cfg.freeze_t;
		float hold = std::min(cfg.hold_s, VOL(I).t_end + 0.5f), fend = hold + (cfg.fade_end - cfg.hold_s);
		float fade = t > hold ? (float)std::max(0.0, 1.0 - (t - hold) / std::max(0.1f, fend - hold)) : 1.f;
		if (fade <= 0.f) { I.on = false; continue; }
		g_have_spawn = true;
		g_volume = cfg.volume;
		if (have_cam && !I.real && !I.snd)
		{
			double dx = I.p[0] - cam.p[0], dy = I.p[1] - cam.p[1], dz = I.p[2] - cam.p[2];
			double dist = sqrt(dx * dx + dy * dy + dz * dz);
			if (cam.t - I.t0 >= dist / 343.0)
			{
				I.snd = true;
				double rr = (dx * cam.z[0] + dy * cam.z[1] + dz * cam.z[2]) / std::max(dist, 1.0);
				if (dist < 20000) play_sound(I.type == 6 ? 0 : I.type, (float)dist, (float)std::max(-1.0, std::min(1.0, rr)));
			}
		}
		if (t < 0) continue;
		double dx = cam.p[0] - I.p[0], dy = cam.p[1] - I.p[1], dz = cam.p[2] - I.p[2];
		jobs.push_back({k, t, fade, dx * dx + dy * dy + dz * dz});
	}
	std::sort(jobs.begin(), jobs.end(), [](const Job &a, const Job &b) { return a.dist2 > b.dist2; });
	reap_sounds();
	bool draw = have_cam && !jobs.empty();
	if (!draw && !want_probe) return;

	ID3D11RenderTargetView *rtv = bb_rtv_for(sc);
	if (!rtv) return;

	auto G3 = [](const double *v, float *o) { o[0] = (float)v[0]; o[1] = (float)v[2]; o[2] = (float)v[1]; };
	if (have_cam) { G3(cam.x, cb.camF); G3(cam.z, cb.camR); G3(cam.y, cb.camU); if (cfg.flip_right) for (int k = 0; k < 3; k++) cb.camR[k] = -cb.camR[k]; }
	cb.camU[3] = nearm;
	cb.camF[3] = tanv * aspect; cb.camR[3] = tanv;
	double sd[3]; fill_light(cb, have_cam ? cam.t : 0.0, sd);

	ID3DDeviceContextState *prev = nullptr;
	G.ctx1->SwapDeviceContextState(G.state, &prev);
	common_state(cb);
	if (want_probe) { g_probe_force = false; issue_probe(cb, cam); }

	for (const Job &J : jobs)
	{
		Inst &I = g_inst[J.k];
		Volume &vol = VOL(I);
		int i0 = 0; float w = 0.f;
		if (J.t >= vol.times.back()) i0 = vol.nf - 1;
		else if (J.t > vol.times[0]) { while (i0 + 1 < vol.nf - 1 && vol.times[i0 + 1] <= J.t) i0++; w = (float)((J.t - vol.times[i0]) / (vol.times[i0 + 1] - vol.times[i0])); }
		int i1 = std::min(i0 + 1, vol.nf - 1);
		// textura de sombra da vaga (no tamanho deste volume)
		if (!I.sun || I.sx != vol.nx || I.sy != vol.ny || I.sz != vol.nz)
		{
			SR(I.uav); SR(I.srv); SR(I.sun);
			D3D11_TEXTURE3D_DESC td = {}; td.Width = vol.nx; td.Height = vol.ny; td.Depth = vol.nz; td.MipLevels = 1;
			td.Format = DXGI_FORMAT_R16_FLOAT; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			if (FAILED(G.dev->CreateTexture3D(&td, nullptr, &I.sun)) || FAILED(G.dev->CreateUnorderedAccessView(I.sun, nullptr, &I.uav)) ||
				FAILED(G.dev->CreateShaderResourceView(I.sun, nullptr, &I.srv))) { SR(I.uav); SR(I.srv); SR(I.sun); continue; }
			I.sx = vol.nx; I.sy = vol.ny; I.sz = vol.nz;
		}
		setup_job_cb(cb, I, vol, cam, sd, J.t, J.fade, w);
		// ---- destrocos (antes da nuvem: o que estiver atras da nuvem fica escondido por ela)
		double treal = J.t / I.rate;
		if (I.ntr > 0 && treal < I.tr_end && G.cs_trail && G.ps_trail[0])
		{
			const int TX = 100, TY = 100, TZ = 80;
			if (!I.trt)
			{
				D3D11_TEXTURE3D_DESC td = {}; td.Width = TX; td.Height = TY; td.Depth = TZ; td.MipLevels = 1;
				td.Format = DXGI_FORMAT_R16G16_FLOAT; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
				if (FAILED(G.dev->CreateTexture3D(&td, nullptr, &I.trt)) || FAILED(G.dev->CreateUnorderedAccessView(I.trt, nullptr, &I.truav)) ||
					FAILED(G.dev->CreateShaderResourceView(I.trt, nullptr, &I.trsrv))) { SR(I.truav); SR(I.trsrv); SR(I.trt); }
			}
			if (I.trt)
			{
				struct TRCB { float tv[16][4], tp[16][4], tinfo[4], tinfo2[4]; } tcb = {};
				memcpy(tcb.tv, I.tv, sizeof tcb.tv); memcpy(tcb.tp, I.tp, sizeof tcb.tp);
				tcb.tinfo[0] = (float)treal; tcb.tinfo[1] = (float)I.ntr; tcb.tinfo[2] = 200.f; tcb.tinfo[3] = 320.f;
				tcb.tinfo2[0] = I.wind[0]; tcb.tinfo2[1] = I.wind[1]; tcb.tinfo2[2] = cfg.debris_density; tcb.tinfo2[3] = cfg.debris_glow;
				G.ctx->UpdateSubresource(G.cb_tr, 0, nullptr, &tcb, 0, 0);
				// camera no espaco local dos destrocos: x norte, y leste, z cima, origem no chao da explosao (sem giro)
				CB cbt = cb;
				cbt.camO[0] = (float)(cam.p[0] - I.p[0]); cbt.camO[1] = (float)(cam.p[2] - I.p[2]); cbt.camO[2] = (float)(cam.p[1] - I.p[1]);
				G3(cam.x, cbt.camF); G3(cam.z, cbt.camR); G3(cam.y, cbt.camU);
				if (cfg.flip_right) for (int k = 0; k < 3; k++) cbt.camR[k] = -cbt.camR[k];
				cbt.camF[3] = tanv * aspect; cbt.camR[3] = tanv; cbt.camU[3] = nearm;
				cbt.scales[3] = cfg.opacity * std::min(1.f, (float)(I.tr_end - treal) / 6.f);
				G.ctx->UpdateSubresource(G.cb, 0, nullptr, &cbt, 0, 0);
				ID3D11Buffer *cbs[2] = {G.cb, G.cb_tr};
				G.ctx->CSSetConstantBuffers(0, 2, cbs); G.ctx->PSSetConstantBuffers(0, 2, cbs);
				G.ctx->CSSetShader(G.cs_trail, nullptr, 0);
				ID3D11UnorderedAccessView *uavs[2] = {nullptr, I.truav};
				G.ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
				G.ctx->Dispatch(TX / 4, TY / 4, TZ / 4);
				ID3D11UnorderedAccessView *nu2[2] = {}; G.ctx->CSSetUnorderedAccessViews(0, 2, nu2, nullptr);
				D3D11_VIEWPORT vpt = {0, 0, (float)g_bb_w, (float)g_bb_h, 0, 1};
				G.ctx->OMSetRenderTargets(1, &rtv, nullptr);
				G.ctx->OMSetBlendState(G.blend, nullptr, 0xffffffff);
				G.ctx->RSSetViewports(1, &vpt);
				G.ctx->PSSetShader(G.ps_trail[g_dc.samples > 1], nullptr, 0);
				ID3D11SamplerState *smp2[2] = {G.samp, G.samp_wrap}; G.ctx->PSSetSamplers(0, 2, smp2);
				ID3D11ShaderResourceView *ts[10] = {nullptr, nullptr, nullptr, nullptr, g_dc.valid ? g_dc.srv : nullptr, nullptr, nullptr, G.noise_srv, nullptr, I.trsrv};
				G.ctx->PSSetShaderResources(0, 10, ts);
				G.ctx->Draw(3, 0);
				ID3D11ShaderResourceView *n10[10] = {}; G.ctx->PSSetShaderResources(0, 10, n10);
				ID3D11RenderTargetView *nrt2 = nullptr; G.ctx->OMSetRenderTargets(1, &nrt2, nullptr);
			}
		}

		{
			float rz = g_sh->opt_realism >= 0 ? g_sh->opt_realism : cfg.realism;
			cb.shock2[1] = (float)(J.t / I.rate); cb.shock2[2] = (1.0f + (cfg.firelight - 1.0f) * (rz > 0.5f ? 1.f : 0.f)) * (1.f + cfg.night_glow * g_night) ; cb.shock2[3] = rz;
		}
		G.ctx->UpdateSubresource(G.cb, 0, nullptr, &cb, 0, 0);

		ID3D11ShaderResourceView *csr[3] = {vol.A[i0], nullptr, vol.A[i1]};
		G.ctx->CSSetShader(G.cs_sun, nullptr, 0);
		G.ctx->CSSetShaderResources(0, 3, csr); bind_bc(vol, i0, i1, true);
		G.ctx->CSSetUnorderedAccessViews(0, 1, &I.uav, nullptr);
		G.ctx->Dispatch((vol.nx + 3) / 4, (vol.ny + 3) / 4, (vol.nz + 3) / 4);
		unbind_bc(true);
		ID3D11UnorderedAccessView *nuav = nullptr; G.ctx->CSSetUnorderedAccessViews(0, 1, &nuav, nullptr);
		ID3D11ShaderResourceView *ncs[3] = {}; G.ctx->CSSetShaderResources(0, 3, ncs);

		D3D11_VIEWPORT vp = {0, 0, (float)g_bb_w, (float)g_bb_h, 0, 1};
		G.ctx->OMSetRenderTargets(1, &rtv, nullptr);
		G.ctx->OMSetBlendState(G.blend, nullptr, 0xffffffff);
		G.ctx->RSSetViewports(1, &vp);
		G.ctx->PSSetShader(G.ps[g_dc.samples > 1], nullptr, 0);
		ID3D11SamplerState *smp[2] = {G.samp, G.samp_wrap}; G.ctx->PSSetSamplers(0, 2, smp);
		ID3D11ShaderResourceView *srv[8] = {vol.A[i0], vol.B[i0], vol.A[i1], vol.B[i1], g_dc.valid ? g_dc.srv : nullptr, use_mask ? g_dc2.srv : nullptr, I.srv, G.noise_srv};
		G.ctx->PSSetShaderResources(0, 8, srv); bind_bc(vol, i0, i1, false);
		G.ctx->Draw(3, 0);
		unbind_bc(false);
		ID3D11ShaderResourceView *nul[8] = {};
		G.ctx->PSSetShaderResources(0, 8, nul);
		ID3D11RenderTargetView *nrt = nullptr; G.ctx->OMSetRenderTargets(1, &nrt, nullptr);
	}
	// ondas de choque (depois da fumaca, para dobrar tambem a fumaca atras delas)
	static const float WILSON[7] = {0.8f, 0.5f, 0.f, 0.35f, 0.f, 0.f, 0.8f};
	float SHOCK_AMP[7]; for (int k = 0; k < 6; k++) SHOCK_AMP[k] = 3.0f * cfg.shock_type[k];
	SHOCK_AMP[6] = SHOCK_AMP[0];
	float shock_gain = g_sh->opt_shock >= 0 ? g_sh->opt_shock : cfg.shock_gain;
	for (const Job &J : jobs)
	{
		Inst &I = g_inst[J.k];
		double tr = cam.t - I.t0;
		if (tr <= 0 || tr > 2.5 || SHOCK_AMP[I.type] * shock_gain <= 0) continue;
		ID3D11Resource *bbr = back_buffer(sc);
		ID3D11Texture2D *bbt = nullptr;
		if (!bbr || FAILED(bbr->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&bbt))) continue;
		D3D11_TEXTURE2D_DESC bd; bbt->GetDesc(&bd); bbt->Release();
		if (bd.SampleDesc.Count != 1) continue;
		if (!G.scene || G.scene_w != bd.Width || G.scene_h != bd.Height || G.scene_fmt != bd.Format)
		{
			SR(G.scene_srv); SR(G.scene);
			D3D11_TEXTURE2D_DESC nd = bd; nd.BindFlags = D3D11_BIND_SHADER_RESOURCE; nd.MiscFlags = 0; nd.CPUAccessFlags = 0; nd.Usage = D3D11_USAGE_DEFAULT; nd.MipLevels = 1; nd.ArraySize = 1;
			if (FAILED(G.dev->CreateTexture2D(&nd, nullptr, &G.scene)) || FAILED(G.dev->CreateShaderResourceView(G.scene, nullptr, &G.scene_srv))) { SR(G.scene); continue; }
			G.scene_w = bd.Width; G.scene_h = bd.Height; G.scene_fmt = bd.Format;
		}
		G.ctx->CopyResource(G.scene, bbr);
		float r = (float)(345.0 * tr * (1.0 + 1.2 * exp(-tr / 0.12)) + 2.0);
		cb.shock[0] = r; cb.shock[1] = 1.5f + 0.05f * r;
		cb.shock[2] = SHOCK_AMP[I.type] * shock_gain * (float)exp(-tr / 0.9) * std::min(1.f, r / 8.f);
		cb.shock[3] = WILSON[I.type] * cfg.wilson_gain * (float)std::max(0.0, 1.0 - tr / 0.4) * (float)std::min(1.0, tr / 0.03);
		cb.shock2[0] = g_bb_h / (2.f * tanv);
		G3(cam.x, cb.camF); G3(cam.z, cb.camR); G3(cam.y, cb.camU);
		cb.camF[3] = tanv * aspect; cb.camR[3] = tanv; cb.camU[3] = nearm;
		cb.camO[0] = (float)(cam.p[0] - I.p[0]); cb.camO[1] = (float)(cam.p[2] - I.p[2]); cb.camO[2] = (float)(cam.p[1] - I.p[1]);
		G.ctx->UpdateSubresource(G.cb, 0, nullptr, &cb, 0, 0);
		D3D11_VIEWPORT vp = {0, 0, (float)g_bb_w, (float)g_bb_h, 0, 1};
		G.ctx->OMSetRenderTargets(1, &rtv, nullptr);
		G.ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
		G.ctx->RSSetViewports(1, &vp);
		G.ctx->PSSetShader(G.ps_shock[g_dc.samples > 1], nullptr, 0);
		ID3D11ShaderResourceView *srv[9] = {nullptr, nullptr, nullptr, nullptr, g_dc.valid ? g_dc.srv : nullptr, nullptr, nullptr, nullptr, G.scene_srv};
		G.ctx->PSSetShaderResources(0, 9, srv);
		G.ctx->Draw(3, 0);
		ID3D11ShaderResourceView *nul[9] = {};
		G.ctx->PSSetShaderResources(0, 9, nul);
		ID3D11RenderTargetView *nrt = nullptr; G.ctx->OMSetRenderTargets(1, &nrt, nullptr);
	}
	G.ctx1->SwapDeviceContextState(prev, nullptr);
	if (prev) prev->Release();
}

// ---------------------------------------------------------------- pod de mira (targeting pod)
// O DCS desenha a imagem do pod numa passada propria (camera, alvo de cor e profundidade separados) que depois vai para
// os MFDs. Quando essa passada termina, desenhamos as explosoes nela com a camera capturada durante a passada.
static bool g_have_spawn_any() { return g_have_spawn; }

static void pod_inject(ID3D11DeviceContext *ctx, ID3D11Resource *dsv, ID3D11RenderTargetView *rtv, const DStat &st)
{
	if (!g_sh || !G.ok || !G.ps[0] || !g_have_spawn) return;
	Cam cam;
	if (!read_cam(cam)) return;
	// camera do pod: posicao ~ a da camera do Lua (na cabine, poucos metros do pod); eixos e lente do proprio DCS
	for (int i = 0; i < 3; i++) { cam.x[i] = st.cam.ax[0][i]; cam.y[i] = st.cam.ax[1][i]; cam.z[i] = st.cam.ax[2][i]; }
	float tanv = st.cam.tanv, aspect = st.cam.asp > 0.f ? st.cam.asp : 1.f, nearm = st.cam.nearv > 0.f ? st.cam.nearv : 0.02f;
	float diag = atanf(tanv * sqrtf(1.f + aspect * aspect));
	struct PJ { int k; double t; float fade; double d2; };
	std::vector<PJ> jobs;
	for (int k = 0; k < MAXI; k++)
	{
		Inst &I = g_inst[k];
		if (!I.on || !VOL(I).ready) continue;
		Volume &vol = VOL(I);
		double t = (cam.t - I.t0) * I.rate;
		if (g_freeze) t = 13.0 * vol.t_end / 23.5;
		else if (cfg.freeze_t >= 0) t = cfg.freeze_t;
		if (t < 0) continue;
		float hold = std::min(cfg.hold_s, vol.t_end + 0.5f), fend = hold + (cfg.fade_end - cfg.hold_s);
		float fade = t > hold ? (float)std::max(0.0, 1.0 - (t - hold) / std::max(0.1f, fend - hold)) : 1.f;
		if (fade <= 0.f) continue;
		// fora do campo de visao do pod? (centro da nuvem e raio da caixa)
		double c[3] = {I.p[0] - cam.p[0], I.p[1] + 0.35 * vol.nz * vol.dx - cam.p[1], I.p[2] - cam.p[2]};
		double d = sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
		double R = 0.6 * std::max(vol.nx, std::max(vol.ny, vol.nz)) * vol.dx;
		double cosa = (c[0] * cam.x[0] + c[1] * cam.x[1] + c[2] * cam.x[2]) / std::max(d, 1.0);
		double ang = acos(std::max(-1.0, std::min(1.0, cosa))), angr = d > R ? asin(R / d) : 3.2;
		if (ang > diag + angr) continue;
		jobs.push_back({k, t, fade, d * d});
	}
	if (jobs.empty()) return;
	std::sort(jobs.begin(), jobs.end(), [](const PJ &a, const PJ &b) { return a.d2 > b.d2; });

	ID3D11Resource *rr = nullptr; rtv->GetResource(&rr);
	if (!rr) return;
	ID3D11Texture2D *rt = nullptr; D3D11_TEXTURE2D_DESC rd = {};
	if (SUCCEEDED(rr->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&rt))) { rt->GetDesc(&rd); rt->Release(); }
	rr->Release();
	if (!rd.Width) return;

	ID3DDeviceContextState *prev = nullptr;
	G.ctx1->SwapDeviceContextState(G.state, &prev);
	copy_depth_to(g_dcp, ctx, dsv);
	CB cb = {};
	cb.screen[0] = (float)rd.Width; cb.screen[1] = (float)rd.Height; cb.screen[2] = 1.f / rd.Width; cb.screen[3] = 1.f / rd.Height;
	cb.depthInfo[0] = g_dcp.valid ? 1.f : 0.f; cb.depthInfo[1] = (float)cfg.reversed; cb.depthInfo[2] = (float)g_dcp.w; cb.depthInfo[3] = (float)g_dcp.h;
	cb.misc[0] = (float)cfg.max_steps;
	cb.nearInfo[0] = 0.f;
	double sd[3]; fill_light(cb, cam.t, sd);
	cb.camU[3] = nearm; cb.camF[3] = tanv * aspect; cb.camR[3] = tanv;
	common_state(cb);
	D3D11_VIEWPORT vp = {0, 0, (float)rd.Width, (float)rd.Height, 0, 1};
	if (st.vp_ok && st.vp.Width > 0 && st.vp.Height > 0) { vp = st.vp; vp.MinDepth = 0; vp.MaxDepth = 1; }
	bool ms = g_dcp.valid && g_dcp.samples > 1;
	float rz = g_sh->opt_realism >= 0 ? g_sh->opt_realism : cfg.realism;
	for (const PJ &J : jobs)
	{
		Inst &I = g_inst[J.k];
		Volume &vol = VOL(I);
		int i0 = 0; float w = 0.f;
		if (J.t >= vol.times.back()) i0 = vol.nf - 1;
		else if (J.t > vol.times[0]) { while (i0 + 1 < vol.nf - 1 && vol.times[i0 + 1] <= J.t) i0++; w = (float)((J.t - vol.times[i0]) / (vol.times[i0 + 1] - vol.times[i0])); }
		int i1 = std::min(i0 + 1, vol.nf - 1);
		setup_job_cb(cb, I, vol, cam, sd, J.t, J.fade, w);
		cb.shock2[1] = (float)(J.t / I.rate); cb.shock2[2] = (1.0f + (cfg.firelight - 1.0f) * (rz > 0.5f ? 1.f : 0.f)) * (1.f + cfg.night_glow * g_night); cb.shock2[3] = rz;
		G.ctx->UpdateSubresource(G.cb, 0, nullptr, &cb, 0, 0);
		G.ctx->OMSetRenderTargets(1, &rtv, nullptr);
		G.ctx->OMSetBlendState(G.blend, nullptr, 0xffffffff);
		G.ctx->RSSetViewports(1, &vp);
		G.ctx->PSSetShader(G.ps[ms ? 1 : 0], nullptr, 0);
		ID3D11SamplerState *smp[2] = {G.samp, G.samp_wrap}; G.ctx->PSSetSamplers(0, 2, smp);
		ID3D11ShaderResourceView *srv[8] = {vol.A[i0], vol.B[i0], vol.A[i1], vol.B[i1], g_dcp.valid ? g_dcp.srv : nullptr, nullptr, I.srv, G.noise_srv};
		G.ctx->PSSetShaderResources(0, 8, srv); bind_bc(vol, i0, i1, false);
		G.ctx->Draw(3, 0);
		unbind_bc(false);
		ID3D11ShaderResourceView *nul[8] = {};
		G.ctx->PSSetShaderResources(0, 8, nul);
	}
	ID3D11RenderTargetView *nrt = nullptr; G.ctx->OMSetRenderTargets(1, &nrt, nullptr);
	G.ctx1->SwapDeviceContextState(prev, nullptr);
	if (prev) prev->Release();
	static DWORD told = 0;
	if (cfg.pod_log && GetTickCount() - told > 10000) { told = GetTickCount(); logf("pod de mira: %zu explosao(oes) desenhada(s) na imagem do pod (%ux%u fmt %d, fov %.2f graus)", jobs.size(), rd.Width, rd.Height, (int)rd.Format, atanf(tanv) * 114.5916f); }
}

// ---------------------------------------------------------------- registro
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
	char p[MAX_PATH]; GetModuleFileNameA(addon_module, p, MAX_PATH);
	std::string s = p; s = s.substr(0, s.find_last_of("\\/") + 1) + "NightzimBoom\\";
	g_dir = s;
	if (!reshade::register_addon(addon_module, reshade_module)) return false;
	logf("Nightzim BoomMod V carregado (%s)", g_dir.c_str());
	load_config();
	open_shm();
	init_perms();
	reshade::register_event<reshade::addon_event::init_swapchain>(on_init_swapchain);
	reshade::register_event<reshade::addon_event::destroy_swapchain>(on_destroy_swapchain);
	reshade::register_event<reshade::addon_event::destroy_device>(on_destroy_device);
	reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(on_bind_rt_ds);
	reshade::register_event<reshade::addon_event::draw>(on_draw);
	reshade::register_event<reshade::addon_event::draw_indexed>(on_draw_indexed);
	reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(on_draw_indirect);
	reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(on_clear_ds);
	reshade::register_event<reshade::addon_event::present>(on_present);
	reshade::register_event<reshade::addon_event::map_buffer_region>(on_map_buf);
	reshade::register_event<reshade::addon_event::unmap_buffer_region>(on_unmap_buf);
	reshade::register_event<reshade::addon_event::update_buffer_region>(on_update_buf);
	reshade::register_event<reshade::addon_event::update_buffer_region_command>(on_update_buf_cmd);
	return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module)
{
	reshade::unregister_addon(addon_module, reshade_module);
	if (g_log) { fclose(g_log); g_log = nullptr; }
}
