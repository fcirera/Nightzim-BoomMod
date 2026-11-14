// Nightzim BoomMod X - instalador / gerenciador (ativar, desativar, desinstalar, com backup e verificacao SHA-256)
// Os arquivos do mod ficam na pasta "payload" ao lado deste .exe:
//   payload\bin\...              -> <DCS>\bin\...
//   payload\SavedGames\...       -> <Saved Games>\DCS\...
//   payload\ffx\...              -> <DCS>\Bazar\Effects\ParticleSystem2\ffx\...   (modelos em branco no lugar das explosoes do jogo)
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <windowsx.h>
#include <objidl.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
namespace fs = std::filesystem;

static const wchar_t *APP = L"Nightzim BoomMod X";
static HWND g_wnd, g_path, g_log;
// controles desenhados por nos (botao, chave liga/desliga, seletor segmentado, aba)
enum { W_BTN, W_TOG, W_SEG, W_TAB };
struct Wd { int type = 0, page = 0, id = 0, value = 0, style = 0; Gdiplus::RectF r; std::wstring text; std::vector<std::wstring> opts; };
static std::vector<Wd> g_w;
static int g_cJdam, g_cCor, g_cQual, g_cDet, g_kShock, g_kWilson, g_kWind, g_kStock, g_cVar, g_kLazy, g_kBC;	// indices em g_w
static int g_cN[6];	// variacoes por tipo de explosao (0 = desligada)
static const wchar_t *N_FILE[6] = {L"jdam", L"aerea", L"nar", L"mk82", L"napalm", L"tank"}, *N_KEY[6] = {L"n_jdam", L"n_aerea", L"n_pequena", L"n_media", L"n_napalm", L"n_tanque"};
static int g_page = 0, g_state = 0, g_hover_part = 0;	// g_state: -1 sem DCS, 0 nao instalado, 1 ativo, 2 desativado
static std::wstring g_vram, g_vram_big, g_vram_note; static std::vector<std::wstring> g_lines; static bool g_wnd_title_dirty = false;
static fs::path g_exe_dir, g_payload, g_sg, g_root;	// g_root = Saved Games\DCS\Nightzim BoomMod X

static bool g_pt = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_PORTUGUESE;	// Windows em portugues -> PT, senao EN
static inline const wchar_t *T(const wchar_t *pt, const wchar_t *en) { return g_pt ? pt : en; }
static bool g_busy = false;
static void set_busy(bool b)
{
	g_busy = b;
	if (g_wnd) { InvalidateRect(g_wnd, nullptr, FALSE); UpdateWindow(g_wnd); }
}
static void log(const std::wstring &s)
{
	int n = GetWindowTextLengthW(g_log);
	SendMessageW(g_log, EM_SETSEL, n, n);
	std::wstring t = s + L"\r\n";
	SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)t.c_str());
	std::wofstream f(g_root / L"instalador.log", std::ios::app);
	if (f) f << s << L"\n";
	g_lines.push_back(s); if (g_lines.size() > 200) g_lines.erase(g_lines.begin());
	if (g_wnd) { InvalidateRect(g_wnd, nullptr, FALSE); UpdateWindow(g_wnd); }
	MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }	// janela continua respondendo
}

static std::wstring sha256(const fs::path &p)
{
	std::ifstream f(p, std::ios::binary);
	if (!f) return L"";
	BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE h;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) return L"";
	BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
	std::vector<char> buf(1 << 20);
	while (f) { f.read(buf.data(), buf.size()); if (f.gcount()) BCryptHashData(h, (PUCHAR)buf.data(), (ULONG)f.gcount(), 0); }
	unsigned char d[32]; BCryptFinishHash(h, d, 32, 0); BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(alg, 0);
	wchar_t s[65]; for (int i = 0; i < 32; i++) swprintf(s + 2 * i, 3, L"%02x", d[i]);
	return s;
}

static bool dcs_running()
{
	HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32W e = {sizeof e}; bool r = false;
	for (BOOL ok = Process32FirstW(sn, &e); ok; ok = Process32NextW(sn, &e)) if (!_wcsicmp(e.szExeFile, L"DCS.exe")) r = true;
	CloseHandle(sn); return r;
}

// ---------------------------------------------------------------- achar o DCS e o Saved Games
static std::wstring reg_str(HKEY root, const wchar_t *key, const wchar_t *val)
{
	wchar_t b[1024]; DWORD sz = sizeof b;
	if (RegGetValueW(root, key, val, RRF_RT_REG_SZ, nullptr, b, &sz) == ERROR_SUCCESS) return b;
	return L"";
}
static bool is_dcs(const fs::path &p) { std::error_code ec; return !p.empty() && fs::exists(p / L"bin" / L"DCS.exe", ec) && fs::exists(p / L"Bazar", ec); }

static fs::path find_saved_games()
{
	PWSTR sg = nullptr; fs::path best;
	if (SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &sg) == S_OK)
	{
		fs::path base = sg; CoTaskMemFree(sg);
		fs::file_time_type bt{};
		for (const wchar_t *n : {L"DCS", L"DCS.openbeta", L"DCS.server"})
		{
			std::error_code ec; fs::path p = base / n, lg = p / L"Logs" / L"dcs.log";
			if (fs::exists(p, ec)) { auto t = fs::exists(lg, ec) ? fs::last_write_time(lg, ec) : fs::file_time_type{}; if (best.empty() || t > bt) { best = p; bt = t; } }
		}
		if (best.empty()) best = base / L"DCS";
	}
	return best;
}

static fs::path find_dcs()
{
	// 1) o DCS que voce abriu por ultimo (linha de comando gravada no dcs.log)
	{
		std::wifstream f(g_sg / L"Logs" / L"dcs.log");
		std::wstring line;
		for (int n = 0; n < 400 && std::getline(f, line); n++)
		{
			auto i = line.find(L"Command line:");
			if (i == std::wstring::npos) continue;
			std::wstring s = line.substr(i + 13);
			auto q = s.find(L'"'); if (q != std::wstring::npos) s = s.substr(q + 1);
			auto e = s.find(L"\\bin\\"); if (e == std::wstring::npos) e = s.find(L"\\bin\\", 0);
			if (e != std::wstring::npos) { fs::path p = s.substr(0, e); if (is_dcs(p)) return p; }
		}
	}
	// 2) registro, Steam e pastas comuns
	for (const wchar_t *k : {L"Software\\Eagle Dynamics\\DCS World", L"Software\\Eagle Dynamics\\DCS World OpenBeta"})
		for (HKEY r : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) { fs::path p = reg_str(r, k, L"Path"); if (is_dcs(p)) return p; }
	fs::path steam = reg_str(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
	for (fs::path p : {steam / L"steamapps\\common\\DCSWorld", fs::path(L"C:\\Program Files\\Eagle Dynamics\\DCS World"),
					   fs::path(L"D:\\DCS World"), fs::path(L"E:\\DCS World"), fs::path(L"F:\\DCS World")})
		if (is_dcs(p)) return p;
	return L"";
}

static fs::path dcs_path() { wchar_t b[1024]; GetWindowTextW(g_path, b, 1024); return fs::path(b); }

// ---------------------------------------------------------------- lista de arquivos do pacote
struct Item { fs::path src, dst; std::wstring rel; bool ffx; };
static std::vector<Item> items(const fs::path &dcs)
{
	std::vector<Item> v; std::error_code ec;
	auto add = [&](const fs::path &from, const fs::path &to, const wchar_t *tag, bool ffx) {
		if (!fs::exists(from, ec)) return;
		for (auto &e : fs::recursive_directory_iterator(from, ec))
			if (e.is_regular_file()) { fs::path r = fs::relative(e.path(), from); v.push_back({e.path(), to / r, std::wstring(tag) + L"\\" + r.wstring(), ffx}); }
	};
	add(g_payload / L"bin", dcs / L"bin", L"bin", false);
	add(g_payload / L"SavedGames", g_sg, L"SavedGames", false);
	add(g_payload / L"ffx", dcs / L"Bazar\\Effects\\ParticleSystem2\\ffx", L"ffx", true);
	return v;
}

// manifesto: rel|destino|sha do arquivo instalado|tem backup (0/1)
struct Man { std::wstring dst, sha; bool bk; };
static std::map<std::wstring, Man> load_man()
{
	std::map<std::wstring, Man> m; std::wifstream f(g_root / L"manifesto.txt"); std::wstring l;
	while (std::getline(f, l)) { std::wstringstream ss(l); std::wstring a, b, c, d; std::getline(ss, a, L'|'); std::getline(ss, b, L'|'); std::getline(ss, c, L'|'); std::getline(ss, d, L'|'); if (!a.empty()) m[a] = {b, c, d == L"1"}; }
	return m;
}
static void save_man(const std::map<std::wstring, Man> &m)
{
	std::wofstream f(g_root / L"manifesto.txt");
	for (auto &kv : m) f << kv.first << L"|" << kv.second.dst << L"|" << kv.second.sha << L"|" << (kv.second.bk ? L"1" : L"0") << L"\n";
}

// ---------------------------------------------------------------- ReShade
static void check_reshade(const fs::path &dcs)
{
	std::error_code ec; fs::path bin = dcs / L"bin", rs;
	for (const wchar_t *n : {L"dxgi.dll", L"d3d11.dll"})
	{
		fs::path p = bin / n; if (!fs::exists(p, ec)) continue;
		DWORD hnd, sz = GetFileVersionInfoSizeW(p.c_str(), &hnd); std::vector<char> b(sz);
		if (sz && GetFileVersionInfoW(p.c_str(), 0, sz, b.data()))
		{
			struct LC { WORD lang, cp; } *tr = nullptr; UINT tl = 0;
			std::vector<std::wstring> keys = {L"040904b0", L"000004b0", L"040904e4"};
			if (VerQueryValueW(b.data(), L"\\VarFileInfo\\Translation", (void **)&tr, &tl) && tl >= sizeof(LC))
			{ wchar_t k[16]; swprintf(k, 16, L"%04x%04x", tr->lang, tr->cp); keys.insert(keys.begin(), k); }
			for (auto &k : keys)
				for (const wchar_t *field : {L"ProductName", L"FileDescription"})
				{
					wchar_t *pn = nullptr; UINT l = 0;
					std::wstring q = L"\\StringFileInfo\\" + k + L"\\" + field;
					if (VerQueryValueW(b.data(), q.c_str(), (void **)&pn, &l) && pn && wcsstr(pn, L"ReShade")) rs = p;
				}
		}
	}
	if (rs.empty()) { log(T(L"AVISO: ReShade nao encontrado em ", L"WARNING: ReShade not found in ") + bin.wstring() + T(L". Instale o ReShade \"with full add-on support\" (reshade.me) apontando para bin\\DCS.exe, DirectX 10/11/12.", L". Install ReShade \"with full add-on support\" (reshade.me) on bin\\DCS.exe, DirectX 10/11/12.")); return; }
	log(T(L"ReShade encontrado: ", L"ReShade found: ") + rs.filename().wstring());
	std::wifstream f(bin / L"ReShade.log"); std::wstring l; bool lim = false;
	while (std::getline(f, l)) if (l.find(L"limited add-on functionality") != std::wstring::npos) lim = true;
	if (lim) log(T(L"AVISO: este ReShade e a versao comum (add-ons limitados). Instale a versao \"with full add-on support\" de reshade.me, por cima (Update ReShade only).", L"WARNING: this is the regular ReShade build (limited add-ons). Install the \"with full add-on support\" build from reshade.me over it (Update ReShade only)."));
}

// ---------------------------------------------------------------- acoes
static fs::path legacy_backup(const fs::path &name)	// backups das versoes anteriores do Nightzim BoomMod
{
	std::error_code ec; fs::path p = g_sg / L"Nightzim BoomMod" / L"BackupV_branco" / name;
	return fs::exists(p, ec) ? p : fs::path();
}

// ---------------------------------------------------------------- opcoes (gravadas no NightzimBoom.ini)
static fs::path ini_path() { return dcs_path() / L"bin" / L"NightzimBoom" / L"NightzimBoom.ini"; }
static std::wstring ini_get(const wchar_t *sec, const wchar_t *key, const wchar_t *def)
{
	wchar_t b[256]; GetPrivateProfileStringW(sec, key, def, b, 256, ini_path().c_str()); return b;
}
static void ini_set(const wchar_t *sec, const wchar_t *key, const std::wstring &v) { WritePrivateProfileStringW(sec, key, v.c_str(), ini_path().c_str()); }
static int sel(int c) { return g_w[c].value; }
static void setsel(int c, int i) { g_w[c].value = i; }
static bool chk(int c) { return g_w[c].value != 0; }
static void setchk(int c, bool v) { g_w[c].value = v ? 1 : 0; }
static bool want_stock_replace() { return chk(g_kStock); }

// estimativa de VRAM pelos .nzv do pacote (ou da instalacao): cada arquivo ocupa ~ o proprio tamanho na placa
static void update_vram_label()
{
	if (g_w.empty()) return;
	static const wchar_t *nome_pt[6] = {L"JDAM (2000 lb)", L"Explosão aérea", L"Foguete", L"Mk-82 (média)", L"Napalm", L"Tiro de tanque"};
	static const wchar_t *nome_en[6] = {L"JDAM (2000 lb)", L"Air burst", L"Rocket", L"Mk-82 (medium)", L"Napalm", L"Tank gun"};
	fs::path dir = g_payload / L"bin" / L"NightzimBoom"; std::error_code ec;
	if (!fs::exists(dir / L"jdam.nzv", ec)) dir = dcs_path() / L"bin" / L"NightzimBoom";
	double k = sel(g_kBC) == 2 ? 0.25 : sel(g_kBC) == 1 ? 0.5 : 1.0;
	double ini = 0, tot = 0; int nv = 0; bool falta = false, algum_off = false;
	auto mb = [&](const std::wstring &f) { fs::path p = dir / f; return fs::exists(p, ec) ? (double)fs::file_size(p, ec) / 1048576.0 : -1.0; };
	for (int i = 0; i < 6; i++)
	{
		int n = sel(g_cN[i]); double row = 0, um = mb(std::wstring(N_FILE[i]) + L".nzv");
		for (int v = 0; v < n; v++)
		{
			double m = mb(std::wstring(N_FILE[i]) + (v ? L"_" + std::to_wstring(v + 1) : L"") + L".nzv");
			if (m < 0) { falta = true; break; }
			row += m; nv++; if (v == 0) ini += m;
		}
		if (i == 0 && n > 0) { double m = mb(L"jdam_v1.nzv"); if (m > 0) { row += m; ini += m; nv++; } }	// JDAM classica acompanha a JDAM
		if (n == 0) algum_off = true;
		tot += row;
		wchar_t b[128];
		if (n == 0) swprintf(b, 128, L"%ls", (g_pt ? nome_pt : nome_en)[i]);
		else swprintf(b, 128, L"%ls  ·  %.0f MB", (g_pt ? nome_pt : nome_en)[i], row * k);
		g_w[g_cN[i]].text = b; (void)um;
	}
	wchar_t b[256];
	bool lazy = chk(g_kLazy);
	swprintf(b, 256, L"%.2f GB", (lazy ? ini : tot) * k / 1024); g_vram_big = b;
	if (lazy && tot > ini + 1) swprintf(b, 256, T(L"na abertura do jogo; até %.2f GB se todas as variações forem usadas (%d arquivos)", L"at game start; up to %.2f GB if every variation gets used (%d files)"), tot * k / 1024, nv);
	else swprintf(b, 256, T(L"somando as explosões escolhidas (%d arquivos)", L"adding up the chosen explosions (%d files)"), nv);
	g_vram = b;
	g_vram_note = std::wstring(falta ? T(L"Algumas variações escolhidas ainda não acompanham o pacote: valem as que existirem. ", L"Some chosen variations are not in the package yet: the ones that exist are used. ") : L"") +
		(algum_off ? T(L"\"Não\": esse tipo não é carregado e a explosão dele não aparece.", L"\"Off\": that type is not loaded and its explosion is not shown.") : L"");
	if (g_wnd) InvalidateRect(g_wnd, nullptr, FALSE);
}

static void load_options()
{
	std::error_code ec;
	bool has = fs::exists(ini_path(), ec);
	auto f = [&](const wchar_t *s, const wchar_t *k, const wchar_t *d) { return has ? ini_get(s, k, d) : std::wstring(d); };
	setsel(g_cJdam, f(L"instalador", L"jdam", L"v2") == L"v1" ? 1 : 0);
	setsel(g_cCor, _wtof(f(L"color", L"realism", L"1").c_str()) > 0.5 ? 0 : 1);
	int ms = _wtoi(f(L"render", L"max_steps", L"420").c_str()); setsel(g_cQual, ms <= 320 ? 0 : ms <= 450 ? 1 : 2);
	double dt = _wtof(f(L"render", L"detail", L"0.8").c_str()); setsel(g_cDet, dt < 0.6 ? 0 : dt < 1.0 ? 1 : 2);
	setchk(g_kShock, _wtof(f(L"shock", L"gain", L"1").c_str()) > 0);
	setchk(g_kWilson, _wtof(f(L"shock", L"wilson", L"1").c_str()) > 0);
	setchk(g_kWind, _wtoi(f(L"wind", L"enabled", L"1").c_str()) != 0);
	setchk(g_kStock, _wtoi(f(L"instalador", L"substituir", L"1").c_str()) != 0);
	{
		std::wstring va = f(L"vram", L"variedade", L"baixa"); int def = va == L"alta" ? 5 : va == L"media" ? 2 : 1;
		for (int i = 0; i < 6; i++) { int n = _wtoi(f(L"vram", N_KEY[i], std::to_wstring(def).c_str()).c_str()); setsel(g_cN[i], n < 0 ? 0 : n > 5 ? 5 : n); }
	}
	setchk(g_kLazy, _wtoi(f(L"vram", L"sob_demanda", L"1").c_str()) != 0);
	{ int c = _wtoi(f(L"vram", L"comprimir", L"0").c_str()); setsel(g_kBC, c < 0 || c > 2 ? 0 : c); }
	update_vram_label();
}

static void restore_ffx_from_backup()
{
	auto man = load_man(); std::error_code ec;
	for (auto &kv : man)
	{
		if (kv.first.rfind(L"ffx\\", 0) != 0) continue;
		fs::path b = g_root / L"backup" / kv.first;
		if (kv.second.bk && fs::exists(b, ec)) { fs::copy_file(b, kv.second.dst, fs::copy_options::overwrite_existing, ec); log(T(L"original do jogo de volta: ", L"stock file restored: ") + kv.first); }
	}
}

static bool activate();
static void apply_options()
{
	std::error_code ec;
	if (!fs::exists(ini_path(), ec)) { log(T(L"Instale primeiro (Instalar / Ativar); as opcoes sao gravadas na instalacao.", L"Install first (Install / Activate); options are saved into the installation.")); return; }
	if (dcs_running()) { log(T(L"ERRO: feche o DCS antes de aplicar.", L"ERROR: close DCS before applying.")); return; }
	ini_set(L"color", L"realism", sel(g_cCor) == 0 ? L"1.0" : L"0.0");
	ini_set(L"color", L"firelight", sel(g_cCor) == 0 ? L"1.6" : L"1.0");
	const wchar_t *steps[3] = {L"300", L"420", L"600"}, *stepm[3] = {L"2.0", L"1.5", L"1.0"}, *det[3] = {L"0.4", L"0.8", L"1.1"};
	ini_set(L"render", L"max_steps", steps[sel(g_cQual)]); ini_set(L"render", L"step_m", stepm[sel(g_cQual)]);
	ini_set(L"render", L"detail", det[sel(g_cDet)]);
	ini_set(L"shock", L"gain", chk(g_kShock) ? L"1.0" : L"0");
	ini_set(L"shock", L"wilson", chk(g_kWilson) ? L"1.0" : L"0");
	ini_set(L"wind", L"enabled", chk(g_kWind) ? L"1" : L"0");
	{
		for (int i = 0; i < 6; i++) ini_set(L"vram", N_KEY[i], std::to_wstring(sel(g_cN[i])));
		ini_set(L"vram", L"sob_demanda", chk(g_kLazy) ? L"1" : L"0");
		ini_set(L"vram", L"comprimir", std::to_wstring(sel(g_kBC) < 0 ? 0 : sel(g_kBC)));
	}
	// versao da JDAM: as duas ficam instaladas; o add-on escolhe pelo ini (ou pelo menu F10 no jogo)
	std::wstring jv = sel(g_cJdam) == 1 ? L"v1" : L"v2";
	log(L"JDAM: " + std::wstring(jv == L"v1" ? T(L"classica (v1.0)", L"classic (v1.0)") : T(L"nova (video real)", L"new (real footage)")));
	ini_set(L"instalador", L"jdam", jv);
	// substituir (ou nao) as explosoes do jogo
	bool rep = chk(g_kStock);
	bool was = ini_get(L"instalador", L"substituir", L"1") != L"0";
	ini_set(L"instalador", L"substituir", rep ? L"1" : L"0");
	if (!rep && was) { restore_ffx_from_backup(); log(T(L"Explosoes do jogo: originais (as nossas aparecem junto).", L"Game explosions: stock (ours appear together).")); }
	if (rep && !was) { activate(); }
	log(T(L"Opcoes aplicadas. Valem na proxima vez que abrir o DCS.", L"Options applied. They take effect next time you start DCS."));
}

static bool activate()
{
	fs::path dcs = dcs_path();
	if (!is_dcs(dcs)) { log(T(L"ERRO: pasta do DCS invalida (precisa ter bin\\DCS.exe).", L"ERROR: invalid DCS folder (it must contain bin\\DCS.exe).")); return false; }
	if (dcs_running()) { log(T(L"ERRO: feche o DCS antes.", L"ERROR: close DCS first.")); return false; }
	auto v = items(dcs);
	log(T(L"Instalando em ", L"Installing to ") + dcs.wstring() + L" (" + std::to_wstring(v.size()) + T(L" arquivos)...", L" files)..."));
	if (v.empty()) { log(T(L"ERRO: pasta payload nao encontrada ao lado do instalador.", L"ERROR: payload folder not found next to the installer.")); return false; }
	auto man = load_man(); std::error_code ec;
	fs::path bkdir = g_root / L"backup";
	int n = 0;
	bool rep = want_stock_replace();
	for (auto &it : v)
	{
		if (it.ffx && !rep) continue;	// usuario escolheu manter as explosoes do jogo
		// arquivo de configuracao do usuario: nao sobrescreve se ja existir
		if (it.dst.filename() == L"NightzimBoom.ini" && fs::exists(it.dst, ec)) { log(T(L"mantido o seu NightzimBoom.ini", L"kept your NightzimBoom.ini")); continue; }
		// desativado antes? remove o .off
		fs::path off = it.dst; off += L".off";
		if (fs::exists(off, ec)) fs::remove(off, ec);
		std::wstring cur = fs::exists(it.dst, ec) ? sha256(it.dst) : L"", nw = sha256(it.src);
		auto m = man.find(it.rel);
		bool ours = m != man.end() && m->second.sha == cur;
		bool bk = m != man.end() && m->second.bk;
		// instalacoes antigas do Nightzim ja tinham colocado os modelos em branco: o original esta no backup antigo
		if (!bk && it.ffx && cur == nw && !legacy_backup(it.dst.filename()).empty())
		{
			fs::path b = bkdir / it.rel; fs::create_directories(b.parent_path(), ec);
			fs::copy_file(legacy_backup(it.dst.filename()), b, fs::copy_options::overwrite_existing, ec);
			std::wofstream(fs::path(b.wstring() + L".sha256")) << sha256(b);
			bk = true; log(T(L"backup (de versao anterior): ", L"backup (from a previous version): ") + it.rel);
		}
		// so os efeitos do jogo (ffx) tem original para guardar; os outros arquivos sao todos do mod
		if (it.ffx && !cur.empty() && !ours && cur != nw && !bk)
		{
			fs::path b = bkdir / it.rel; fs::create_directories(b.parent_path(), ec);
			fs::path leg = it.ffx ? legacy_backup(it.dst.filename()) : fs::path();
			fs::copy_file(leg.empty() ? it.dst : leg, b, fs::copy_options::overwrite_existing, ec);
			if (ec) { log(T(L"ERRO no backup de ", L"ERROR backing up ") + it.dst.wstring() + L": " + std::wstring(ec.message().begin(), ec.message().end())); return false; }
			std::wofstream(fs::path(b.wstring() + L".sha256")) << sha256(b);
			bk = true; log(T(L"backup: ", L"backup: ") + it.rel);
		}
		fs::create_directories(it.dst.parent_path(), ec);
		if (fs::file_size(it.src, ec) > 20u * 1024 * 1024) log(T(L"copiando ", L"copying ") + it.dst.filename().wstring() + L"...");
		fs::copy_file(it.src, it.dst, fs::copy_options::overwrite_existing, ec);
		if (ec) { log(T(L"ERRO copiando ", L"ERROR copying ") + it.dst.wstring() + T(L" (rode como administrador?): ", L" (run as administrator?): ") + std::wstring(ec.message().begin(), ec.message().end())); return false; }
		if (sha256(it.dst) != nw) { log(T(L"ERRO: verificacao falhou em ", L"ERROR: verification failed for ") + it.dst.wstring()); return false; }
		man[it.rel] = {it.dst.wstring(), nw, bk};
		n++;
	}
	save_man(man);
	if (!fs::exists(ini_path(), ec)) {}
	ini_set(L"instalador", L"substituir", rep ? L"1" : L"0");
	log(T(L"ATIVADO: ", L"ACTIVE: ") + std::to_wstring(n) + T(L" arquivos instalados e conferidos (SHA-256).", L" files installed and verified (SHA-256)."));
	check_reshade(dcs);
	return true;
}

// desativa: o add-on e o hook viram .off (ReShade e DCS ignoram), e as explosoes originais voltam do backup
static bool deactivate(bool remove_all)
{
	fs::path dcs = dcs_path();
	if (dcs_running()) { log(T(L"ERRO: feche o DCS antes.", L"ERROR: close DCS first.")); return false; }
	auto man = load_man(); std::error_code ec;
	if (man.empty()) { log(T(L"Nada instalado (manifesto vazio).", L"Nothing installed (empty manifest).")); return true; }
	for (auto it = man.begin(); it != man.end();)
	{
		fs::path dst = it->second.dst, b = g_root / L"backup" / it->first;
		bool restore = it->second.bk && fs::exists(b, ec);
		bool toggle = dst.extension() == L".addon64" || dst.extension() == L".lua" && dst.parent_path().filename() == L"Hooks";
		if (restore)
		{
			std::wifstream hf(fs::path(b.wstring() + L".sha256")); std::wstring hs; hf >> hs;
			if (!hs.empty() && sha256(b) != hs) { log(T(L"ERRO: o backup de ", L"ERROR: the backup of ") + it->first + T(L" nao confere - parei", L" does not match - stopped")); return false; }
			fs::copy_file(b, dst, fs::copy_options::overwrite_existing, ec);
			log(T(L"restaurado: ", L"restored: ") + it->first);
			if (remove_all) { fs::remove(b, ec); fs::remove(b.wstring() + L".sha256", ec); }
		}
		else if (remove_all)
		{
			fs::remove(dst, ec); fs::path off = dst; off += L".off"; fs::remove(off, ec);
		}
		else if (toggle && fs::exists(dst, ec))
		{
			fs::path off = dst; off += L".off"; fs::rename(dst, off, ec); log(T(L"desligado: ", L"disabled: ") + it->first);
		}
		else if (it->first.rfind(L"ffx\\", 0) == 0)
		{
			log(T(L"AVISO: sem backup do original de ", L"WARNING: no backup of the original ") + it->first + T(L" - use o Repair do DCS se quiser a explosao original dele", L" - use DCS Repair if you want its original explosion back"));
		}
		if (remove_all) it = man.erase(it); else ++it;
	}
	if (remove_all)
	{
		fs::remove_all(dcs / L"bin" / L"NightzimBoom", ec);
		fs::remove_all(g_sg / L"Scripts" / L"NightzimBoom", ec);
		fs::remove(g_root / L"manifesto.txt", ec);
		log(T(L"DESINSTALADO. Explosoes originais do jogo restauradas.", L"UNINSTALLED. Stock explosions restored."));
	}
	else log(T(L"DESATIVADO. Explosoes originais do jogo de volta; o mod continua instalado para religar.", L"DISABLED. Stock explosions are back; the mod stays installed so you can re-enable it."));
	return true;
}

static void refresh_status()
{
	fs::path dcs = dcs_path(); std::error_code ec;
	if (!is_dcs(dcs)) g_state = -1;
	else if (fs::exists(dcs / L"bin" / L"NightzimBoom.addon64", ec)) g_state = 1;
	else if (fs::exists(dcs / L"bin" / L"NightzimBoom.addon64.off", ec)) g_state = 2;
	else g_state = 0;
	if (g_wnd) InvalidateRect(g_wnd, nullptr, FALSE);
}

// ---------------------------------------------------------------- janela (interface propria, desenhada com GDI+)
enum { ID_PATH = 100, ID_BROWSE, ID_ON, ID_OFF, ID_DEL, ID_LOGDIR, ID_APPLY, ID_VRAM, ID_TAB0, ID_TAB1, ID_TAB2, ID_TAB3, ID_LANG, ID_PAY, ID_PIX, ID_DISC, ID_INSTA, ID_GH };
static bool g_pix = false; static Gdiplus::Bitmap *g_qr = nullptr, *g_icDisc = nullptr, *g_icInsta = nullptr, *g_icPix = nullptr;	// icones fornecidos pelo autor
static const wchar_t *PIX_KEY = L"f5124246-e867-42ce-a16b-d139017560a5";
static const wchar_t *PAY_EMAIL = L"fcirera2007@gmail.com";
static const wchar_t *PAY_URL = L"https://www.paypal.com/myaccount/transfer/homepage/pay";
static DWORD g_copied = 0;

static void browse()
{
	BROWSEINFOW bi = {}; bi.hwndOwner = g_wnd; bi.lpszTitle = T(L"Escolha a pasta de instalação do DCS World (a que tem bin\\DCS.exe)", L"Choose the DCS World install folder (the one with bin\\DCS.exe)");
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	PIDLIST_ABSOLUTE pl = SHBrowseForFolderW(&bi);
	if (pl) { wchar_t b[MAX_PATH]; SHGetPathFromIDListW(pl, b); CoTaskMemFree(pl); SetWindowTextW(g_path, b); refresh_status(); load_options(); }
}

using namespace Gdiplus;
static const int CW = 1060, CH = 660, HERO = 400, X0 = 432, X1 = 1028;
static float g_s = 1.f;	// escala de DPI
static Bitmap *g_hero[3] = {};
static int g_hover = -1, g_down = -1;
static HBRUSH g_brEdit, g_brLog;
static HFONT g_fEdit, g_fLog;
static const Color C_BG(255, 15, 17, 20), C_CARD(255, 26, 29, 35), C_CARD2(255, 36, 40, 48), C_TXT(255, 232, 234, 237), C_MUT(255, 139, 146, 156),
	C_ACC(255, 255, 90, 31), C_ACC2(255, 255, 122, 61), C_GRN(255, 62, 207, 110), C_AMB(255, 242, 179, 61), C_RED(255, 232, 64, 64), C_LINE(255, 44, 49, 58);

static Bitmap *load_res_img(int id)
{
	HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA); if (!r) return nullptr;
	HGLOBAL g = LoadResource(nullptr, r); if (!g) return nullptr;
	IStream *st = SHCreateMemStream((const BYTE *)LockResource(g), SizeofResource(nullptr, r)); if (!st) return nullptr;
	Bitmap *b = Bitmap::FromStream(st); st->Release();
	if (b && b->GetLastStatus() != Ok) { delete b; b = nullptr; }
	return b;
}

static void rr_path(GraphicsPath &p, RectF r, float rad)
{
	float d = rad * 2;
	p.AddArc(r.X, r.Y, d, d, 180, 90); p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
	p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90); p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90); p.CloseFigure();
}
static void fill_rr(Graphics &g, RectF r, float rad, Color c) { GraphicsPath p; rr_path(p, r, rad); SolidBrush b(c); g.FillPath(&b, &p); }
static void line_rr(Graphics &g, RectF r, float rad, Color c, float w = 1.f) { GraphicsPath p; rr_path(p, r, rad); Pen pen(c, w); g.DrawPath(&pen, &p); }
// texto: tamanho em px logicos; align 0 esq, 1 centro, 2 dir
static void text(Graphics &g, const std::wstring &s, RectF r, float px, Color c, int align = 0, int style = FontStyleRegular, const wchar_t *fam = L"Segoe UI", bool wrap = false)
{
	FontFamily ff(fam); Font f(&ff, px, style, UnitPixel);
	StringFormat sf; sf.SetAlignment(align == 1 ? StringAlignmentCenter : align == 2 ? StringAlignmentFar : StringAlignmentNear);
	sf.SetLineAlignment(wrap ? StringAlignmentNear : StringAlignmentCenter);
	sf.SetTrimming(StringTrimmingEllipsisCharacter);
	if (!wrap) sf.SetFormatFlags(StringFormatFlagsNoWrap);
	SolidBrush b(c); g.DrawString(s.c_str(), -1, &f, r, &sf, &b);
}
static float text_w(Graphics &g, const std::wstring &s, float px, int style = FontStyleRegular, const wchar_t *fam = L"Segoe UI")
{
	FontFamily ff(fam); Font f(&ff, px, style, UnitPixel); RectF o; StringFormat sf(StringFormat::GenericTypographic()); sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
	g.MeasureString(s.c_str(), -1, &f, PointF(0, 0), &sf, &o); return o.Width;
}
// texto com espacamento entre letras (para rotulos em caixa alta)
static void spaced(Graphics &g, const std::wstring &s, float x, float y, float px, Color c, float gap, int style = FontStyleBold)
{
	FontFamily ff(L"Segoe UI"); Font f(&ff, px, style, UnitPixel); SolidBrush b(c); StringFormat sf(StringFormat::GenericTypographic());
	for (wchar_t ch : s) { wchar_t t[2] = {ch, 0}; g.DrawString(t, 1, &f, PointF(x, y), &sf, &b); x += text_w(g, t, px, style) + gap; }
}

static void layout_children()
{
	auto P = [](int v) { return (int)(v * g_s + 0.5f); };
	MoveWindow(g_path, P(X0 + 14), P(229), P(X1 - X0 - 14 - 122), P(22), TRUE);
	MoveWindow(g_log, P(X0), P(84), P(X1 - X0), P(CH - 84 - 56), TRUE);
	ShowWindow(g_path, g_page == 0 ? SW_SHOW : SW_HIDE);
	ShowWindow(g_log, g_page == 2 ? SW_SHOW : SW_HIDE);
}

static void build_ui()
{
	int keep_sel[32], keep_n = (int)g_w.size() < 32 ? (int)g_w.size() : 32; for (int i = 0; i < keep_n; i++) keep_sel[i] = g_w[i].value;
	bool had = !g_w.empty();
	g_w.clear();
	auto add = [&](int type, int page, int id, float x, float y, float w, float h, const wchar_t *txt, std::initializer_list<const wchar_t *> opts = {}, int style = 0) {
		Wd d; d.type = type; d.page = page; d.id = id; d.r = RectF(x, y, w, h); d.text = txt; d.style = style; for (auto o : opts) d.opts.push_back(o);
		g_w.push_back(d); return (int)g_w.size() - 1; };
	float W = (float)(X1 - X0);
	// --- opcoes (sempre nos mesmos indices: os valores sobrevivem a troca de idioma)
	float y = 104, LBL = 176;
	g_cJdam = add(W_SEG, 1, 0, X0, y, W, 34, T(L"Explosão JDAM", L"JDAM explosion"), {T(L"Nova (vídeo real)", L"New (real footage)"), T(L"Clássica (v1.0)", L"Classic (v1.0)")}); y += 40;
	g_cCor = add(W_SEG, 1, 0, X0, y, W, 34, T(L"Cores", L"Colours"), {T(L"Realistas", L"Realistic"), T(L"Clássicas", L"Classic")}); y += 40;
	g_cQual = add(W_SEG, 1, 0, X0, y, W, 34, T(L"Qualidade", L"Quality"), {T(L"Desempenho", L"Performance"), T(L"Equilíbrio", L"Balanced"), T(L"Máxima", L"Maximum")}); y += 40;
	g_cDet = add(W_SEG, 1, 0, X0, y, W, 34, T(L"Detalhe da fumaça", L"Smoke detail"), {T(L"Baixo", L"Low"), T(L"Médio", L"Medium"), T(L"Alto", L"High")}); y += 46;
	g_kShock = add(W_TOG, 1, 0, X0, y, W / 2 - 8, 30, T(L"Onda de choque", L"Shockwave"));
	g_kWilson = add(W_TOG, 1, 0, X0 + W / 2 + 8, y, W / 2 - 8, 30, T(L"Nuvem de Wilson", L"Wilson cloud")); y += 34;
	g_kWind = add(W_TOG, 1, 0, X0, y, W / 2 - 8, 30, T(L"Fumaça deriva com o vento", L"Smoke drifts with the wind"));
	g_kStock = add(W_TOG, 1, 0, X0 + W / 2 + 8, y, W / 2 - 8, 30, T(L"Substituir explosões do jogo", L"Replace stock explosions")); y += 70;
	g_cVar = add(W_SEG, 9, ID_VRAM, X0, y, W, 34, L"", {L"", L"", L""});	// (antigo seletor global; fora de uso)
	g_kBC = add(W_SEG, 3, ID_VRAM, X0, 346, W, 34, T(L"Compressão", L"Compression"), {T(L"Desligada", L"Off"), T(L"Metade", L"Half"), T(L"Um quarto (BC7)", L"One quarter (BC7)")});
	g_kLazy = add(W_TOG, 3, ID_VRAM, X0, 390, W, 30, T(L"Carregar variações extras só quando usadas", L"Load extra variations only when used"));
	for (int i = 0; i < 6; i++) g_cN[i] = add(W_SEG, 3, ID_VRAM, X0, 104.f + 38 * i, W, 32, L"", {T(L"Não", L"Off"), L"1", L"2", L"3", L"4", L"5"});
	(void)LBL;
	if (had) for (int i = 0; i < keep_n && i < (int)g_w.size(); i++) g_w[i].value = keep_sel[i];
	else for (int i = 0; i < 6; i++) g_w[g_cN[i]].value = 1;
	add(W_BTN, 1, ID_APPLY, X1 - 200, CH - 62, 200, 42, T(L"APLICAR OPÇÕES", L"APPLY OPTIONS"), {}, 1);
	add(W_BTN, 3, ID_APPLY, X1 - 200, CH - 62, 200, 42, T(L"APLICAR OPÇÕES", L"APPLY OPTIONS"), {}, 1);
	// --- abas e idioma (todas as paginas)
	add(W_TAB, -1, ID_TAB0, X0, 22, 0, 40, T(L"INSTALAÇÃO", L"INSTALL"));
	add(W_TAB, -1, ID_TAB1, 0, 22, 0, 40, T(L"OPÇÕES", L"OPTIONS"));
	add(W_TAB, -1, ID_TAB3, 0, 22, 0, 40, L"VRAM");
	add(W_TAB, -1, ID_TAB2, 0, 22, 0, 40, T(L"REGISTRO", L"LOG"));
	add(W_BTN, -1, ID_PAY, 30, CH - 100.f, 200, 46, T(L"Apoie  ·  PayPal", L"Support me  ·  PayPal"), {}, 4);
	add(W_BTN, -1, ID_PIX, 238, CH - 100.f, 132, 46, L"Pix", {}, 6);
	add(W_BTN, -1, ID_DISC, X1 - 52 - 10 - 46 - 8 - 22 - 8 - 22, 28, 22, 28, L"", {}, 5);
	add(W_BTN, -1, ID_INSTA, X1 - 52 - 10 - 46 - 8 - 22, 28, 22, 28, L"", {}, 5);
	add(W_BTN, -1, ID_GH, X1 - 52 - 10 - 46, 28, 46, 28, L"GitHub", {}, 5);
	add(W_BTN, -1, ID_LANG, X1 - 52, 28, 52, 28, g_pt ? L"PT | en" : L"pt | EN", {}, 3);
	// --- instalacao
	add(W_BTN, 0, ID_BROWSE, X1 - 104, 224, 96, 32, T(L"Procurar", L"Browse"), {}, 2);
	add(W_BTN, 0, ID_ON, X0, 284, W, 56, T(L"INSTALAR  /  ATIVAR", L"INSTALL  /  ACTIVATE"), {}, 1);
	float bw = (W - 24) / 3;
	add(W_BTN, 0, ID_OFF, X0, 352, bw, 40, T(L"Desativar", L"Disable"), {}, 0);
	add(W_BTN, 0, ID_DEL, X0 + bw + 12, 352, bw, 40, T(L"Desinstalar", L"Uninstall"), {}, 0);
	add(W_BTN, 0, ID_LOGDIR, X0 + 2 * (bw + 12), 352, bw, 40, T(L"Backup e log", L"Backup && log"), {}, 0);
	g_wnd_title_dirty = true;
}

static bool g_shot = false;
static void render(Bitmap &back)
{
	Graphics g(&back);
	g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit); g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
	g.ScaleTransform(g_s, g_s);
	g.Clear(C_BG);
	// ---- painel da esquerda: captura do jogo + marca
	RectF hr(0, 0, (REAL)HERO, (REAL)CH);
	Bitmap *img = g_hero[g_page % 3];
	if (img)
	{
		float iw = (float)img->GetWidth(), ih = (float)img->GetHeight(), sc = std::max(HERO / iw, CH / ih);
		float sw = HERO / sc, sh = CH / sc;
		g.DrawImage(img, hr, (iw - sw) / 2, (ih - sh) / 2, sw, sh, UnitPixel);
	}
	{
		LinearGradientBrush top(PointF(0, 0), PointF(0, 150), Color(150, 8, 9, 11), Color(0, 8, 9, 11)); top.SetWrapMode(WrapModeTileFlipXY); g.FillRectangle(&top, RectF(0, 0, (REAL)HERO, 149));
		LinearGradientBrush bot(PointF(0, CH - 330.f), PointF(0, (REAL)CH), Color(0, 8, 9, 11), Color(245, 8, 9, 11)); bot.SetWrapMode(WrapModeTileFlipXY); g.FillRectangle(&bot, RectF(0, CH - 329.f, (REAL)HERO, 330));
		LinearGradientBrush edge(PointF(HERO - 60.f, 0), PointF((REAL)HERO, 0), Color(0, 15, 17, 20), Color(255, 15, 17, 20)); edge.SetWrapMode(WrapModeTileFlipXY); g.FillRectangle(&edge, RectF(HERO - 59.f, 0, 60, (REAL)CH));
	}
	spaced(g, L"NIGHTZIM", 30, 30, 13, Color(235, 255, 255, 255), 5.5f);
	{
		const wchar_t *fam = L"Segoe UI Black"; float px = 54, x = 26, yb = CH - 250.f;
		{ FontFamily t(fam); if (!t.IsAvailable()) fam = L"Segoe UI"; }
		int st = !wcscmp(fam, L"Segoe UI") ? FontStyleBold : FontStyleRegular;
		text(g, L"BOOMMOD", RectF(x, yb, 360, 66), px, Color(255, 255, 255, 255), 0, st, fam);
		float w = text_w(g, L"BOOMMOD", px, st, fam);
		text(g, L"X", RectF(x + w + 12, yb, 80, 66), px, C_ACC, 0, st, fam);
		fill_rr(g, RectF(30, yb + 72, 46, 4), 2, C_ACC);
		text(g, T(L"Explosões volumétricas simuladas\npara o DCS World", L"Simulated volumetric explosions\nfor DCS World"), RectF(30, yb + 88, 350, 48), 16, Color(235, 232, 234, 237), 0, FontStyleRegular, L"Segoe UI", true);
		fill_rr(g, RectF(30, CH - 40.f, 54, 22), 11, Color(70, 255, 255, 255));
		text(g, L"v1.2", RectF(30, CH - 40.f, 54, 22), 12, Color(255, 255, 255, 255), 1, FontStyleBold);
		text(g, T(L"Capturas feitas no jogo", L"Captured in game"), RectF(94, CH - 40.f, 260, 22), 11.5f, Color(170, 232, 234, 237));
	}
	// ---- abas
	{
		float x = (float)X0;
		for (auto &d : g_w) if (d.type == W_TAB)
		{
			float w = text_w(g, d.text, 13.5f, FontStyleBold) + (float)d.text.size() * 1.4f + 8;	// largura com espacamento
			d.r.X = x; d.r.Width = w;
			bool on = d.id - ID_TAB0 == g_page, hov = &d - &g_w[0] == g_hover;
			spaced(g, d.text, x, 33, 13.5f, on ? C_TXT : hov ? Color(255, 190, 196, 204) : C_MUT, 1.4f);
			if (on) fill_rr(g, RectF(x, 60, w - 8, 3), 1.5f, C_ACC);
			x += w + 12;
		}
		Pen ln(C_LINE, 1); g.DrawLine(&ln, (REAL)X0, 63.5f, (REAL)X1, 63.5f);
	}
	float W = (float)(X1 - X0);
	// ---- pagina: instalacao
	if (g_page == 0)
	{
		RectF card((REAL)X0, 84, W, 96); fill_rr(g, card, 12, C_CARD);
		Color sc = g_state == 1 ? C_GRN : g_state == 2 ? C_AMB : g_state == 0 ? C_MUT : C_RED;
		const wchar_t *st = g_state == 1 ? T(L"ATIVO", L"ACTIVE") : g_state == 2 ? T(L"DESATIVADO", L"DISABLED") : g_state == 0 ? T(L"NÃO INSTALADO", L"NOT INSTALLED") : T(L"DCS NÃO ENCONTRADO", L"DCS NOT FOUND");
		const wchar_t *sub = g_state == 1 ? T(L"As explosões volumétricas estão ligadas.", L"Volumetric explosions are on.") :
			g_state == 2 ? T(L"Instalado, mas desligado. As explosões originais do jogo estão de volta.", L"Installed but switched off. Stock explosions are back.") :
			g_state == 0 ? T(L"Clique em Instalar / Ativar para começar.", L"Click Install / Activate to get started.") : T(L"Escolha a pasta que tem bin\\DCS.exe.", L"Choose the folder that contains bin\\DCS.exe.");
		{ SolidBrush halo(Color(50, sc.GetR(), sc.GetG(), sc.GetB())); g.FillEllipse(&halo, X0 + 20.f, 110.f, 28.f, 28.f); SolidBrush dot(sc); g.FillEllipse(&dot, X0 + 27.f, 117.f, 14.f, 14.f); }
		text(g, st, RectF(X0 + 62.f, 100, W - 80, 30), 22, C_TXT, 0, FontStyleBold);
		text(g, sub, RectF(X0 + 62.f, 130, W - 80, 20), 13.5f, C_MUT);
		text(g, L"Saved Games: " + g_sg.wstring(), RectF(X0 + 62.f, 150, W - 80, 18), 12, Color(255, 104, 111, 121));
		spaced(g, T(L"PASTA DO DCS WORLD", L"DCS WORLD FOLDER"), (float)X0, 198, 11.5f, C_MUT, 1.4f);
		fill_rr(g, RectF((REAL)X0, 220, W, 40), 9, C_CARD); line_rr(g, RectF((REAL)X0, 220, W, 40), 9, C_LINE);
		spaced(g, T(L"ATIVIDADE", L"ACTIVITY"), (float)X0, 414, 11.5f, C_MUT, 1.4f);
		RectF ac((REAL)X0, 436, W, CH - 436 - 56.f); fill_rr(g, ac, 12, C_CARD);
		int n = (int)g_lines.size(), show = 7; float ly = ac.Y + 10;
		for (int i = std::max(0, n - show); i < n; i++, ly += 21)
		{
			const std::wstring &s = g_lines[i];
			bool err = s.find(L"ERRO") != std::wstring::npos || s.find(L"ERROR") != std::wstring::npos, warn = s.find(L"AVISO") != std::wstring::npos || s.find(L"WARNING") != std::wstring::npos;
			text(g, s, RectF(ac.X + 16, ly, ac.Width - 32, 20), 12.5f, err ? C_RED : warn ? C_AMB : i == n - 1 ? C_TXT : C_MUT);
		}
	}
	if (g_page == 1)
	{
		spaced(g, T(L"EXPLOSÕES", L"EXPLOSIONS"), (float)X0, 80, 11.5f, C_MUT, 1.4f);
		text(g, T(L"Valem na próxima vez que abrir o DCS.", L"They take effect next time you start DCS."), RectF((REAL)X0, CH - 62.f, W - 220, 42), 12.5f, C_MUT);
	}
	if (g_page == 3)
	{
		spaced(g, T(L"EXPLOSÕES E VARIAÇÕES NA MEMÓRIA DE VÍDEO", L"EXPLOSIONS AND VARIATIONS IN VIDEO MEMORY"), (float)X0, 80, 11.5f, C_MUT, 1.4f);
		fill_rr(g, RectF((REAL)X0, 432, W, 62), 12, C_CARD);
		text(g, T(L"VRAM DO MOD", L"MOD VRAM"), RectF(X0 + 18.f, 440, 160, 18), 11.5f, C_MUT, 0, FontStyleBold);
		text(g, g_vram_big, RectF(X0 + 18.f, 458, 200, 30), 24, C_ACC2, 0, FontStyleBold);
		text(g, g_vram, RectF(X0 + 200.f, 440, W - 216, 48), 12.5f, C_TXT, 0, FontStyleRegular, L"Segoe UI", true);
		text(g, g_vram_note, RectF((REAL)X0, 502, W, 36), 12, C_MUT, 0, FontStyleRegular, L"Segoe UI", true);
		text(g, T(L"Valem na próxima vez que abrir o DCS.", L"They take effect next time you start DCS."), RectF((REAL)X0, CH - 62.f, W - 220, 42), 12.5f, C_MUT);
	}
	// ---- controles
	for (int i = 0; i < (int)g_w.size(); i++)
	{
		Wd &d = g_w[i];
		if (d.page >= 0 && d.page != g_page) continue;
		bool hov = i == g_hover && !g_busy, dn = i == g_down && !g_busy;
		if (d.type == W_BTN)
		{
			bool dis = g_busy && d.id != ID_LANG;
			if (d.style == 1)
			{
				GraphicsPath p; rr_path(p, d.r, 10);
				LinearGradientBrush br(PointF(d.r.X, d.r.Y), PointF(d.r.X + d.r.Width, d.r.Y + d.r.Height), dis ? Color(255, 90, 60, 48) : dn ? Color(255, 214, 66, 14) : hov ? Color(255, 255, 112, 48) : C_ACC,
					dis ? Color(255, 90, 50, 44) : dn ? Color(255, 190, 40, 30) : hov ? Color(255, 240, 70, 50) : Color(255, 226, 52, 40));
				g.FillPath(&br, &p);
				spaced(g, d.text, d.r.X + (d.r.Width - (text_w(g, d.text, d.r.Height > 50 ? 16.f : 13.5f, FontStyleBold) + 1.5f * d.text.size())) / 2, d.r.Y + d.r.Height / 2 - (d.r.Height > 50 ? 11 : 9.5f),
					d.r.Height > 50 ? 16.f : 13.5f, Color(dis ? 150 : 255, 255, 255, 255), 1.5f);
			}
			else if (d.style == 3) { text(g, d.text, d.r, 12.5f, hov ? C_TXT : C_MUT, 2, FontStyleBold); }
			else if (d.style == 5)
			{
				Bitmap *ic = d.id == ID_DISC ? g_icDisc : d.id == ID_INSTA ? g_icInsta : nullptr; RectF tr = d.r;
				if (ic) { g.DrawImage(ic, RectF(d.r.X + 1, d.r.Y + 4, 20, 20)); tr.X += 22; tr.Width -= 22; }
				text(g, d.text, tr, 12, hov ? C_ACC2 : Color(255, 170, 177, 187), ic ? 0 : 1, FontStyleRegular); if (hov) { Pen u(C_ACC2, 1); g.DrawLine(&u, tr.X + 2, d.r.Y + 23, d.r.X + d.r.Width - 2, d.r.Y + 23); }
			}
			else if (d.style == 6)	// Pix: abre o QR Code
			{
				fill_rr(g, d.r, 10, hov ? Color(120, 20, 22, 26) : Color(90, 20, 22, 26)); line_rr(g, d.r, 10, hov ? Color(255, 50, 188, 173) : Color(90, 255, 255, 255));
				// icone generico de QR (tres cantos)
				SolidBrush q(Color(255, 50, 188, 173)), bgq(Color(255, 20, 22, 26)); float qx = d.r.X + 10, qy = d.r.Y + 10;
				if (g_icPix) g.DrawImage(g_icPix, RectF(qx - 1, qy - 1, 28, 28));
				else for (int k = 0; k < 3; k++) { float ax = qx + (k == 1 ? 16.f : 0.f), ay = qy + (k == 2 ? 16.f : 0.f); g.FillRectangle(&q, ax, ay, 10.f, 10.f); g.FillRectangle(&bgq, ax + 2.5f, ay + 2.5f, 5.f, 5.f); g.FillRectangle(&q, ax + 4.f, ay + 4.f, 2.f, 2.f); }
				if (!g_icPix) { g.FillRectangle(&q, qx + 16.f, qy + 16.f, 4.f, 4.f); g.FillRectangle(&q, qx + 22.f, qy + 22.f, 4.f, 4.f); g.FillRectangle(&q, qx + 22.f, qy + 16.f, 4.f, 3.f); }
				text(g, L"Pix", RectF(d.r.X + 46, d.r.Y + 5, d.r.Width - 50, 18), 13, Color(255, 255, 255, 255), 0, FontStyleBold);
				text(g, L"QR Code", RectF(d.r.X + 46, d.r.Y + 23, d.r.Width - 50, 18), 12.5f, Color(230, 232, 234, 237));
			}
			else if (d.style == 4)	// apoio: coracao + PayPal + e-mail (clique copia o e-mail)
			{
				fill_rr(g, d.r, 10, hov ? Color(120, 20, 22, 26) : Color(90, 20, 22, 26)); line_rr(g, d.r, 10, hov ? C_ACC : Color(90, 255, 255, 255));
				SolidBrush cb(Color(40, 255, 90, 31)); g.FillEllipse(&cb, d.r.X + 8.f, d.r.Y + 7.f, 32.f, 32.f);
				text(g, L"\u2665", RectF(d.r.X + 8, d.r.Y + 6, 32, 32), 19, C_ACC, 1, FontStyleRegular, L"Segoe UI Symbol");
				bool cp = g_copied && GetTickCount() - g_copied < 6000;
				text(g, d.text, RectF(d.r.X + 50, d.r.Y + 5, d.r.Width - 56, 18), 13, Color(255, 255, 255, 255), 0, FontStyleBold);
				text(g, cp ? std::wstring(T(L"PayPal aberto - cole o e-mail (Ctrl+V)", L"PayPal opened - paste the e-mail (Ctrl+V)")) : std::wstring(PAY_EMAIL), RectF(d.r.X + 50, d.r.Y + 23, d.r.Width - 56, 18), 12.5f, cp ? C_GRN : Color(230, 232, 234, 237));
			}
			else
			{
				fill_rr(g, d.r, d.style == 2 ? 7.f : 9.f, dn ? Color(255, 52, 57, 67) : hov ? Color(255, 44, 49, 58) : C_CARD2);
				if (d.id == ID_DEL && hov) line_rr(g, d.r, 9, C_RED);
				text(g, d.text, d.r, 13.5f, dis ? C_MUT : C_TXT, 1, FontStyleBold);
			}
		}
		else if (d.type == W_TOG)
		{
			RectF sw(d.r.X, d.r.Y + (d.r.Height - 20) / 2, 38, 20);
			fill_rr(g, sw, 10, d.value ? C_ACC : Color(255, 58, 63, 73));
			SolidBrush kn(Color(255, 255, 255, 255)); g.FillEllipse(&kn, d.value ? sw.X + 20.f : sw.X + 2.f, sw.Y + 2.f, 16.f, 16.f);
			text(g, d.text, RectF(d.r.X + 50, d.r.Y, d.r.Width - 50, d.r.Height), 13.5f, hov ? Color(255, 255, 255, 255) : C_TXT);
		}
		else if (d.type == W_SEG)
		{
			const float LB = 176;
			text(g, d.text, RectF(d.r.X, d.r.Y, LB, d.r.Height), 13.5f, C_TXT);
			RectF box(d.r.X + LB, d.r.Y, d.r.Width - LB, d.r.Height); fill_rr(g, box, 9, C_CARD);
			int n = (int)d.opts.size(); float cw = (box.Width - 6) / n;
			for (int k = 0; k < n; k++)
			{
				RectF c(box.X + 3 + k * cw, box.Y + 3, cw, box.Height - 6);
				if (k == d.value) fill_rr(g, c, 7, C_ACC);
				else if (hov && g_hover_part == k) fill_rr(g, c, 7, C_CARD2);
				text(g, d.opts[k], c, 13, k == d.value ? Color(255, 255, 255, 255) : C_MUT, 1, k == d.value ? FontStyleBold : FontStyleRegular);
			}
		}
	}
	// ---- rodape
	if (g_page != 1 && g_page != 3) text(g, T(L"Requer ReShade 6.x \"with full add-on support\"", L"Requires ReShade 6.x \"with full add-on support\""), RectF((REAL)X0, CH - 36.f, 380, 22), 11.5f, Color(255, 104, 111, 121));
	if (g_page != 1 && g_page != 3) text(g, L"github.com/fcirera/Nightzim-BoomMod", RectF(X1 - 300.f, CH - 36.f, 300, 22), 11.5f, Color(255, 104, 111, 121), 2);
	if (g_pix)
	{
		SolidBrush dim(Color(225, 10, 11, 13)); g.FillRectangle(&dim, RectF((REAL)HERO, 0, (REAL)(CW - HERO), (REAL)CH));
		float cw = 340, ch = 470, cx = HERO + (CW - HERO - cw) / 2, cy = (CH - ch) / 2;
		fill_rr(g, RectF(cx, cy, cw, ch), 16, C_CARD); line_rr(g, RectF(cx, cy, cw, ch), 16, C_LINE);
		text(g, T(L"Apoie com Pix", L"Support with Pix"), RectF(cx, cy + 18, cw, 28), 20, C_TXT, 1, FontStyleBold);
		text(g, T(L"Aponte a câmera do app do seu banco", L"Scan with your Brazilian banking app"), RectF(cx, cy + 46, cw, 20), 12.5f, C_MUT, 1);
		fill_rr(g, RectF(cx + 35, cy + 78, 270, 270), 10, Color(255, 255, 255, 255));
		if (g_qr) { g.SetInterpolationMode(InterpolationModeNearestNeighbor); g.SetPixelOffsetMode(PixelOffsetModeHalf); g.DrawImage(g_qr, RectF(cx + 40, cy + 83, 260, 260)); g.SetPixelOffsetMode(PixelOffsetModeDefault); g.SetInterpolationMode(InterpolationModeHighQualityBicubic); }
		text(g, T(L"Chave Pix (aleatória)", L"Pix key (random)"), RectF(cx, cy + 360, cw, 18), 12, C_MUT, 1);
		text(g, PIX_KEY, RectF(cx, cy + 378, cw, 26), 13.5f, C_TXT, 1, FontStyleBold);
		text(g, T(L"Obrigado pelo apoio!", L"Thank you for the support!"), RectF(cx, cy + 408, cw, 20), 13, Color(255, 50, 188, 173), 1);
		text(g, T(L"clique em qualquer lugar para fechar", L"click anywhere to close"), RectF(cx, cy + 436, cw, 18), 11.5f, Color(255, 104, 111, 121), 1);
	}
	if (g_shot && g_page == 0) text(g, dcs_path().wstring(), RectF(X0 + 14.f, 229, X1 - X0 - 136.f, 22), 15, C_TXT);
	if (g_shot && g_page == 2) { fill_rr(g, RectF((REAL)X0, 84, (REAL)(X1 - X0), CH - 140.f), 8, C_CARD); float ly = 92; for (auto &ln : g_lines) { text(g, ln, RectF(X0 + 10.f, ly, X1 - X0 - 20.f, 18), 13, Color(255, 190, 196, 204), 0, FontStyleRegular, L"Consolas"); ly += 18; } }
}
static void paint(HDC hdc)
{
	Bitmap back((INT)(CW * g_s + 1), (INT)(CH * g_s + 1), PixelFormat32bppPARGB);
	render(back);
	Graphics out(hdc); out.DrawImage(&back, 0, 0);
}
static void save_shots(const std::wstring &prefix)
{
	CLSID png; CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &png);
	g_shot = true;
	for (int p = 0; p < 4; p++)
	{
		g_page = p; Bitmap b((INT)(CW * g_s), (INT)(CH * g_s), PixelFormat32bppPARGB); render(b);
		b.Save((prefix + std::to_wstring(p) + (g_pt ? L"_pt.png" : L"_en.png")).c_str(), &png, nullptr);
	}
	{ g_page = 0; g_pix = true; Bitmap b((INT)(CW * g_s), (INT)(CH * g_s), PixelFormat32bppPARGB); render(b); b.Save((prefix + L"4" + (g_pt ? L"_pt.png" : L"_en.png")).c_str(), &png, nullptr); }
}

static int hit(float x, float y, int *part = nullptr)
{
	for (int i = (int)g_w.size() - 1; i >= 0; i--)
	{
		Wd &d = g_w[i];
		if (d.page >= 0 && d.page != g_page) continue;
		RectF r = d.r;
		if (d.type == W_SEG) { r.X += 176; r.Width -= 176; }
		if (d.type == W_TAB) { r.Y = 20; r.Height = 44; }
		if (x < r.X || y < r.Y || x >= r.X + r.Width || y >= r.Y + r.Height) continue;
		if (part) *part = d.type == W_SEG ? std::min((int)d.opts.size() - 1, (int)((x - r.X) / (r.Width / d.opts.size()))) : 0;
		return i;
	}
	return -1;
}

static void command(int id)
{
	HWND h = g_wnd;
	switch (id)
	{
	case ID_TAB0: case ID_TAB1: case ID_TAB2: case ID_TAB3: g_page = id - ID_TAB0; layout_children(); break;
	case ID_PAY:
		if (OpenClipboard(g_wnd))
		{
			EmptyClipboard(); size_t nb = (wcslen(PAY_EMAIL) + 1) * sizeof(wchar_t);
			HGLOBAL hm = GlobalAlloc(GMEM_MOVEABLE, nb); if (hm) { memcpy(GlobalLock(hm), PAY_EMAIL, nb); GlobalUnlock(hm); SetClipboardData(CF_UNICODETEXT, hm); }
			CloseClipboard(); g_copied = GetTickCount(); SetTimer(g_wnd, 1, 6100, nullptr);
		}
		ShellExecuteW(h, L"open", PAY_URL, nullptr, nullptr, SW_SHOW);	// pagina "enviar dinheiro" do PayPal; o e-mail ja vai copiado
		break;
	case ID_PIX: g_pix = true; ShowWindow(g_path, SW_HIDE); ShowWindow(g_log, SW_HIDE); break;
	case ID_DISC: ShellExecuteW(h, L"open", L"https://discord.gg/3zD4XBPwK", nullptr, nullptr, SW_SHOW); break;
	case ID_INSTA: ShellExecuteW(h, L"open", L"https://www.instagram.com/sireiras/", nullptr, nullptr, SW_SHOW); break;
	case ID_GH: ShellExecuteW(h, L"open", L"https://github.com/fcirera/Nightzim-BoomMod", nullptr, nullptr, SW_SHOW); break;
	case ID_LANG: g_pt = !g_pt; build_ui(); refresh_status(); update_vram_label(); break;
	case ID_BROWSE: if (!g_busy) browse(); break;
	case ID_ON: if (g_busy) break; set_busy(true); if (activate()) apply_options(); set_busy(false); refresh_status(); break;
	case ID_OFF: if (g_busy) break; set_busy(true); deactivate(false); set_busy(false); refresh_status(); break;
	case ID_DEL:
		if (g_busy) break;
		if (MessageBoxW(h, T(L"Remover o Nightzim BoomMod X e restaurar as explosões originais do DCS?", L"Remove Nightzim BoomMod X and restore the stock DCS explosions?"), APP, MB_YESNO | MB_ICONQUESTION) == IDYES) { set_busy(true); deactivate(true); set_busy(false); refresh_status(); }
		break;
	case ID_LOGDIR: ShellExecuteW(h, L"open", g_root.c_str(), nullptr, nullptr, SW_SHOW); break;
	case ID_APPLY: if (g_busy) break; set_busy(true); apply_options(); set_busy(false); break;
	case ID_VRAM: update_vram_label(); break;
	}
	InvalidateRect(g_wnd, nullptr, FALSE);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m)
	{
	case WM_ERASEBKGND: return 1;
	case WM_TIMER: KillTimer(h, 1); InvalidateRect(h, nullptr, FALSE); return 0;
	case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint(dc); EndPaint(h, &ps); return 0; }
	case WM_MOUSEMOVE:
	{
		int part = 0, i = hit(GET_X_LPARAM(l) / g_s, GET_Y_LPARAM(l) / g_s, &part);
		if (i != g_hover || part != g_hover_part) { g_hover = i; g_hover_part = part; InvalidateRect(h, nullptr, FALSE); }
		TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t);
		SetCursor(LoadCursor(nullptr, i >= 0 ? IDC_HAND : IDC_ARROW));
		return 0;
	}
	case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT && (HWND)w == h) return TRUE; break;
	case WM_MOUSELEAVE: if (g_hover != -1) { g_hover = -1; InvalidateRect(h, nullptr, FALSE); } return 0;
	case WM_LBUTTONDOWN: if (g_pix) { g_pix = false; g_down = -1; layout_children(); InvalidateRect(h, nullptr, FALSE); return 0; } g_down = hit(GET_X_LPARAM(l) / g_s, GET_Y_LPARAM(l) / g_s); SetCapture(h); SetFocus(h); InvalidateRect(h, nullptr, FALSE); return 0;
	case WM_LBUTTONUP:
	{
		ReleaseCapture();
		int part = 0, i = hit(GET_X_LPARAM(l) / g_s, GET_Y_LPARAM(l) / g_s, &part), dn = g_down; g_down = -1;
		InvalidateRect(h, nullptr, FALSE);
		if (i < 0 || i != dn) return 0;
		Wd &d = g_w[i]; int id = d.id;
		if (d.type == W_TOG) { if (g_busy) return 0; d.value = !d.value; }
		else if (d.type == W_SEG) { if (g_busy) return 0; d.value = part; }
		if (id) command(id);
		return 0;
	}
	case WM_COMMAND:
		if (LOWORD(w) == ID_PATH && HIWORD(w) == EN_KILLFOCUS) { refresh_status(); load_options(); InvalidateRect(h, nullptr, FALSE); }
		return 0;
	case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC:
	{
		HDC dc = (HDC)w; bool lg = (HWND)l == g_log;
		SetTextColor(dc, lg ? RGB(190, 196, 204) : RGB(232, 234, 237)); SetBkColor(dc, RGB(26, 29, 35));
		return (LRESULT)g_brEdit;
	}
	case WM_CLOSE: if (g_busy) { MessageBoxW(h, T(L"Aguarde terminar a operação.", L"Please wait for the operation to finish."), APP, MB_OK); return 0; } break;
	case WM_DESTROY: PostQuitMessage(0); return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	{	// idioma: automatico pelo Windows; /en ou /pt na linha de comando forcam
		std::wstring cl = GetCommandLineW();
		if (cl.find(L"/en") != std::wstring::npos || cl.find(L"-en") != std::wstring::npos) g_pt = false;
		if (cl.find(L"/pt") != std::wstring::npos || cl.find(L"-pt") != std::wstring::npos) g_pt = true;
	}
	GdiplusStartupInput gsi; ULONG_PTR gtok; GdiplusStartup(&gtok, &gsi, nullptr);
	INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
	wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
	g_exe_dir = fs::path(exe).parent_path(); g_payload = g_exe_dir / L"payload";
	g_sg = find_saved_games(); g_root = g_sg / L"Nightzim BoomMod X";
	std::error_code ec; fs::create_directories(g_root, ec);
	{ HDC sdc = GetDC(nullptr); g_s = GetDeviceCaps(sdc, LOGPIXELSX) / 96.f; ReleaseDC(nullptr, sdc); }
	for (int i = 0; i < 3; i++) g_hero[i] = load_res_img(101 + i);
	g_qr = load_res_img(104); g_icDisc = load_res_img(105); g_icInsta = load_res_img(106); g_icPix = load_res_img(107);

	WNDCLASSW wc = {}; wc.lpfnWndProc = wndproc; wc.hInstance = hi; wc.lpszClassName = L"NZBX"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr; wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1));
	RegisterClassW(&wc);
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
	RECT wr = {0, 0, (LONG)(CW * g_s), (LONG)(CH * g_s)}; AdjustWindowRect(&wr, style, FALSE);
	g_wnd = CreateWindowW(L"NZBX", APP, style, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, hi, nullptr);
	{ BOOL dark = TRUE; DwmSetWindowAttribute(g_wnd, 20, &dark, sizeof dark); COLORREF cap = RGB(15, 17, 20); DwmSetWindowAttribute(g_wnd, 35, &cap, sizeof cap); }
	g_brEdit = CreateSolidBrush(RGB(26, 29, 35));
	g_fEdit = CreateFontW(-(int)(15 * g_s), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
	g_fLog = CreateFontW(-(int)(13 * g_s), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
	g_path = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, g_wnd, (HMENU)(INT_PTR)ID_PATH, hi, nullptr);
	g_log = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 0, 0, 10, 10, g_wnd, nullptr, hi, nullptr);
	SendMessageW(g_path, WM_SETFONT, (WPARAM)g_fEdit, TRUE); SendMessageW(g_log, WM_SETFONT, (WPARAM)g_fLog, TRUE);
	SetWindowTheme(g_log, L"DarkMode_Explorer", nullptr);
	build_ui(); layout_children();

	fs::path dcs = find_dcs();
	SetWindowTextW(g_path, dcs.c_str());
	log(std::wstring(APP));
	log(dcs.empty() ? T(L"Não achei o DCS automaticamente: clique em Procurar.", L"Could not find DCS automatically: click Browse.") : T(L"DCS encontrado: ", L"DCS found: ") + dcs.wstring());
	if (!fs::exists(g_payload, ec)) log(T(L"AVISO: pasta payload não encontrada ao lado do instalador - extraia o zip inteiro.", L"WARNING: payload folder not found next to the installer - extract the whole zip."));
	if (!dcs.empty()) check_reshade(dcs);
	refresh_status();
	load_options();
	{	// /shot <prefixo>: grava a imagem de cada pagina e sai (para conferir o desenho)
		std::wstring cl = GetCommandLineW(); size_t k = cl.find(L"/shot ");
		if (k != std::wstring::npos) { std::wstring pf = cl.substr(k + 6); while (!pf.empty() && (pf.back() == L' ' || pf.back() == L'"')) pf.pop_back(); save_shots(pf); return 0; }
	}
	ShowWindow(g_wnd, SW_SHOW);
	MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
	GdiplusShutdown(gtok);
	return 0;
}
