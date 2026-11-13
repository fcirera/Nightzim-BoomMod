// Nightzim BoomMod V - memoria compartilhada entre o hook Lua do DCS (nzboom.dll) e o add-on do ReShade.
#pragma once
#include <windows.h>
#include <stdint.h>

#define NZ_SHM_NAME "Local\\NightzimBoomV1"
#define NZ_SHM_VERSION 9
#define NZ_RING 32
#define NZ_PROBE_NX 9
#define NZ_PROBE_NY 7
#define NZ_PROBE_N (NZ_PROBE_NX * NZ_PROBE_NY)

#pragma pack(push, 8)
struct NzShared
{
	uint32_t version;
	volatile LONG cam_seq;		// seqlock: impar = escrevendo (lado Lua)
	double cam_p[3], cam_x[3], cam_y[3], cam_z[3];	// mundo DCS: x norte, y cima, z leste; base: x frente, y cima, z direita
	double model_time;
	volatile LONG lua_frames;	// contador de quadros do simulador (diagnostico)

	// explosoes novas (anel escrito pelo Lua; spawn_seq = total ja escrito)
	volatile LONG spawn_seq;
	struct { double p[3]; double t; float wx, wz; int32_t type; int32_t pad; float yaw; int32_t fixed; } ring[NZ_RING];	// wx, wz: vento; yaw: direcao fixa (rad, do norte para o leste) se fixed
	volatile LONG clear_seq;	// Lua incrementa para apagar tudo (fim da missao)

	// pedidos do add-on
	volatile LONG spawn_req;	// add-on incrementa (Ctrl+Shift+B); Lua atende quando difere de spawn_ack
	volatile LONG spawn_ack;
	float spawn_distance;		// m a frente da camera
	int32_t spawn_req_type;		// 0 jdam, 1 aerea, 2 pequena, 3 media, 4 napalm, 5 tanque

	// calibracao continua (plano proximo e campo de visao) por uma grade de sondas de profundidade
	volatile LONG probe_seq;	// add-on incrementa depois de preencher os campos abaixo
	double probe_cam[12];		// camera no instante da sonda (p, x, y, z)
	float probe_aspect, probe_near;
	float probe_ndc[NZ_PROBE_N][2];
	float probe_d[NZ_PROBE_N];	// profundidade bruta (0 = ceu)
	volatile LONG calib_seq;	// Lua incrementa depois de responder
	int32_t calib_ok;			// bit 1 = plano proximo valido, bit 2 = fov valido
	int32_t calib_nsamp;
	double calib_near, calib_tanv, calib_spread;
	char lua_msg[160];

	// sol (calculado no Lua pela data, hora e posicao da missao); mundo DCS: x norte, y cima, z leste
	volatile LONG sun_seq;
	double sun_dir[3];
	double sun_el_deg, sun_az_deg;
	int32_t dev;				// 1 = modo do desenvolvedor (atalhos e menu F10); escrito pelo add-on
	// escolhas feitas no menu F10 (escritas pelo Lua; -1 = usar o ini)
	volatile LONG opt_seq;
	int32_t opt_jdam;			// 1 = nova (v2), 2 = classica (v1)
	float opt_realism, opt_shock;
};
#pragma pack(pop)
