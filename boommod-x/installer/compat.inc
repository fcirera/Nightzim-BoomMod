// ---------------------------------------------------------------- compatibilidade com outros "proxies" (OptiScaler etc.)
// O ReShade entra no DCS como dxgi.dll em <DCS>\bin. O OptiScaler (FSR 4 em placas AMD) tambem e um dxgi.dll. Um sobrescreve
// o outro. Solucao: ReShade renomeado para ReShade64.dll, OptiScaler como dxgi.dll e LoadReshade=true no OptiScaler.ini (o
// OptiScaler carrega o ReShade). Este modulo acha os proxies, escolhe o cenario e desfaz so o que fez (manifesto JSON).
enum { PK_RESHADE = 1, PK_OPTI = 2, PK_OTHER = 3 };
#ifdef COMPAT_TEST
static int cmsg(const wchar_t *t, UINT f) { wprintf(L"[pergunta] %ls -> SIM\n", t); return (f & MB_YESNO) ? IDYES : IDOK; }
static std::wstring g_test_file, g_test_folder;
#else
static int cmsg(const wchar_t *t, UINT f) { return MessageBoxW(g_wnd, t, APP, f); }
#endif
struct Proxy { std::wstring name; fs::path path; int kind = PK_OTHER; std::wstring prod, desc, comp, ver; };
static std::wstring vi_get(const std::vector<char> &b, const wchar_t *field)
{
	struct LC { WORD lang, cp; } *tr = nullptr; UINT tl = 0;
	std::vector<std::wstring> keys = {L"040904b0", L"000004b0", L"040904e4", L"041604b0"};
	if (VerQueryValueW((void *)b.data(), L"\\VarFileInfo\\Translation", (void **)&tr, &tl) && tl >= sizeof(LC)) { wchar_t k[16]; swprintf(k, 16, L"%04x%04x", tr->lang, tr->cp); keys.insert(keys.begin(), k); }
	for (auto &k : keys) { wchar_t *pn = nullptr; UINT l = 0; std::wstring q = L"\\StringFileInfo\\" + k + L"\\" + field; if (VerQueryValueW((void *)b.data(), q.c_str(), (void **)&pn, &l) && pn && *pn) return pn; }
	return L"";
}
static bool classify(const fs::path &p, Proxy &x)
{
	std::error_code ec; if (!fs::is_regular_file(p, ec)) return false;
	x.path = p; x.name = p.filename().wstring(); x.kind = PK_OTHER;
	DWORD hnd, sz = GetFileVersionInfoSizeW(p.c_str(), &hnd);
	if (sz) { std::vector<char> b(sz); if (GetFileVersionInfoW(p.c_str(), 0, sz, b.data())) { x.prod = vi_get(b, L"ProductName"); x.desc = vi_get(b, L"FileDescription"); x.comp = vi_get(b, L"CompanyName"); x.ver = vi_get(b, L"FileVersion"); } }
	std::wstring all = x.prod + L"|" + x.desc + L"|" + x.comp;
	std::wstring low = all; for (auto &c : low) c = towlower(c);
	if (low.find(L"reshade") != std::wstring::npos || low.find(L"crosire") != std::wstring::npos) x.kind = PK_RESHADE;
	else if (low.find(L"optiscaler") != std::wstring::npos) x.kind = PK_OPTI;
	return true;
}
static const wchar_t *PROXY_NAMES[] = {L"dxgi.dll", L"d3d11.dll", L"d3d12.dll", L"dinput8.dll", L"winmm.dll", L"version.dll", L"dbghelp.dll", L"wininet.dll", L"winhttp.dll", L"ReShade64.dll"};
static std::vector<Proxy> scan_proxies(const fs::path &dir)
{
	std::vector<Proxy> v;
	for (auto n : PROXY_NAMES) { Proxy x; if (classify(dir / n, x)) v.push_back(x); }
	return v;
}
static const Proxy *find_px(const std::vector<Proxy> &v, const wchar_t *name) { for (auto &x : v) if (!_wcsicmp(x.name.c_str(), name)) return &x; return nullptr; }
static std::wstring kind_txt(int k) { return k == PK_RESHADE ? L"ReShade" : k == PK_OPTI ? L"OptiScaler" : T(L"desconhecido", L"unknown"); }
static std::wstring px_label(const Proxy &x)
{
	std::wstring s = x.name + L" = " + kind_txt(x.kind);
	std::wstring info = x.prod.empty() ? x.desc : x.prod; if (!x.comp.empty()) info += (info.empty() ? L"" : L", ") + x.comp; if (!x.ver.empty()) info += L" " + x.ver;
	if (!info.empty()) s += L" (" + info + L")";
	return s;
}
// atalho .lnk -> caminho do alvo
static std::wstring lnk_target(const fs::path &lnk)
{
	std::wstring out; IShellLinkW *sl = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void **)&sl)))
	{
		IPersistFile *pf = nullptr;
		if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void **)&pf)))
		{
			if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) { wchar_t b[MAX_PATH * 2] = {}; if (SUCCEEDED(sl->GetPath(b, MAX_PATH * 2, nullptr, 0))) out = b; }
			pf->Release();
		}
		sl->Release();
	}
	return out;
}
// qual pasta o usuario usa (bin ou bin-mt): pelos atalhos do DCS na Area de Trabalho e no Menu Iniciar; na duvida, pergunta
static fs::path g_compat_bin;
static fs::path chosen_bin(const fs::path &dcs, bool ask)
{
	std::error_code ec;
	if (!fs::exists(dcs / L"bin-mt" / L"DCS.exe", ec)) return dcs / L"bin";
	if (!g_compat_bin.empty()) return g_compat_bin;
	int nbin = 0, nmt = 0;
	std::wstring dl = dcs.wstring(); for (auto &c : dl) c = towlower(c);
	for (int csidl : {CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY, CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS})
	{
		wchar_t b[MAX_PATH]; if (FAILED(SHGetFolderPathW(nullptr, csidl, nullptr, 0, b))) continue;
		fs::recursive_directory_iterator it(b, fs::directory_options::skip_permission_denied, ec), end;
		for (; it != end; it.increment(ec))
		{
			if (ec) break;
			if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
			if (_wcsicmp(it->path().extension().c_str(), L".lnk")) continue;
			std::wstring t = lnk_target(it->path()); for (auto &c : t) c = towlower(c);
			if (t.find(dl) != 0 || t.find(L"dcs.exe") == std::wstring::npos) continue;
			if (t.find(L"\\bin-mt\\") != std::wstring::npos) nmt++; else if (t.find(L"\\bin\\") != std::wstring::npos) nbin++;
		}
	}
	fs::path r;
	if (nmt > 0 && nbin == 0) r = dcs / L"bin-mt";
	else if (nbin > 0 && nmt == 0) r = dcs / L"bin";
	else if (ask)
		r = cmsg(T(L"Você abre o DCS pela versão multithread (bin-mt\\DCS.exe)?\n\nSim = bin-mt     Não = bin", L"Do you start DCS with the multithreaded build (bin-mt\\DCS.exe)?\n\nYes = bin-mt     No = bin"), MB_YESNO | MB_ICONQUESTION) == IDYES ? dcs / L"bin-mt" : dcs / L"bin";
	else r = dcs / L"bin";
	g_compat_bin = r;
	return r;
}
// troca SO o valor de uma chave num ini, preservando o resto do arquivo byte a byte; acrescenta a chave na secao se faltar
static bool ini_key_set(const fs::path &p, const std::string &sec, const std::string &key, const std::string &val, std::string *old)
{
	std::ifstream f(p, std::ios::binary); if (!f) return false;
	std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); f.close();
	auto lower = [](std::string x) { for (auto &c : x) c = (char)tolower((unsigned char)c); return x; };
	std::string nl = s.find("\r\n") != std::string::npos ? "\r\n" : "\n";
	size_t pos = 0; std::string cur_sec; size_t sec_end = std::string::npos; bool in_sec = false;
	while (pos < s.size())
	{
		size_t e = s.find('\n', pos); if (e == std::string::npos) e = s.size();
		std::string line = s.substr(pos, e - pos); if (!line.empty() && line.back() == '\r') line.pop_back();
		size_t a = line.find_first_not_of(" \t");
		if (a != std::string::npos && line[a] == '[') { if (in_sec) { sec_end = pos; break; } in_sec = lower(line.substr(a)).rfind("[" + lower(sec) + "]", 0) == 0; }
		else if (in_sec && a != std::string::npos && line[a] != ';' && line[a] != '#')
		{
			size_t eq = line.find('=');
			std::string k = eq == std::string::npos ? "" : line.substr(a, eq - a); while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
			if (lower(k) == lower(key))
			{
				size_t v0 = eq + 1; while (v0 < line.size() && (line[v0] == ' ' || line[v0] == '\t')) v0++;
				size_t v1 = line.size(); size_t cm = line.find_first_of(";#", v0); if (cm != std::string::npos) { v1 = cm; while (v1 > v0 && (line[v1 - 1] == ' ' || line[v1 - 1] == '\t')) v1--; }
				if (old) *old = line.substr(v0, v1 - v0);
				s.replace(pos + v0, v1 - v0, val);
				std::ofstream o(p, std::ios::binary); o << s; return (bool)o;
			}
		}
		pos = e + 1;
	}
	if (old) *old = "";
	if (in_sec) { size_t at = sec_end == std::string::npos ? s.size() : sec_end; std::string ins = key + "=" + val + nl; if (at == s.size() && !s.empty() && s.back() != '\n') ins = nl + ins; s.insert(at, ins); }
	else { if (!s.empty() && s.back() != '\n') s += nl; s += "[" + sec + "]" + nl + key + "=" + val + nl; }
	std::ofstream o(p, std::ios::binary); o << s; return (bool)o;
}
// manifesto da compatibilidade (JSON simples): o que este instalador colocou, com hash, e o cenario aplicado
struct CompatMan { std::wstring scenario, bin, backup, ini, ini_old; std::vector<std::pair<std::wstring, std::wstring>> files; };
static fs::path compat_man_path() { return g_root / L"compat_manifest.json"; }
static std::string u8(const std::wstring &w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s; }
static std::wstring w8(const std::string &s) { int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w; }
static std::string jesc(const std::wstring &w) { std::string s = u8(w), o; for (char c : s) { if (c == '\\' || c == '"') o += '\\'; o += c; } return o; }
static void compat_save(const CompatMan &m)
{
	std::error_code ec; fs::create_directories(g_root, ec);
	std::ofstream o(compat_man_path(), std::ios::binary);
	o << "{\n  \"scenario\": \"" << jesc(m.scenario) << "\",\n  \"bin\": \"" << jesc(m.bin) << "\",\n  \"backup\": \"" << jesc(m.backup) << "\",\n  \"ini\": \"" << jesc(m.ini) << "\",\n  \"ini_key\": \"LoadReshade\",\n  \"ini_old\": \"" << jesc(m.ini_old) << "\",\n  \"files\": [";
	for (size_t i = 0; i < m.files.size(); i++) o << (i ? "," : "") << "\n    {\"name\": \"" << jesc(m.files[i].first) << "\", \"sha256\": \"" << jesc(m.files[i].second) << "\"}";
	o << "\n  ]\n}\n";
}
static std::string jget(const std::string &s, size_t from, const std::string &key, size_t *endp = nullptr)
{
	size_t k = s.find("\"" + key + "\"", from); if (k == std::string::npos) return "";
	size_t q = s.find('"', s.find(':', k) + 1); if (q == std::string::npos) return "";
	std::string v; size_t i = q + 1; for (; i < s.size() && s[i] != '"'; i++) { if (s[i] == '\\' && i + 1 < s.size()) i++; v += s[i]; }
	if (endp) *endp = i; return v;
}
static bool compat_load(CompatMan &m)
{
	std::ifstream f(compat_man_path(), std::ios::binary); if (!f) return false;
	std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	m.scenario = w8(jget(s, 0, "scenario")); m.bin = w8(jget(s, 0, "bin")); m.backup = w8(jget(s, 0, "backup")); m.ini = w8(jget(s, 0, "ini")); m.ini_old = w8(jget(s, 0, "ini_old"));
	size_t p = s.find("\"files\""); size_t e;
	while (p != std::string::npos && (p = s.find("\"name\"", p)) != std::string::npos) { std::string n = jget(s, p, "name", &e); std::string h = jget(s, e, "sha256", &e); m.files.push_back({w8(n), w8(h)}); p = e; }
	return !m.scenario.empty();
}
static std::wstring pick_file(const wchar_t *title)
{
#ifdef COMPAT_TEST
	return g_test_file;
#endif
	wchar_t b[MAX_PATH * 2] = {}; OPENFILENAMEW o = {sizeof o}; o.hwndOwner = g_wnd; o.lpstrFilter = L"DLL (*.dll)\0*.dll\0"; o.lpstrFile = b; o.nMaxFile = MAX_PATH * 2; o.lpstrTitle = title; o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	return GetOpenFileNameW(&o) ? std::wstring(b) : L"";
}
static std::wstring pick_folder(const wchar_t *title)
{
#ifdef COMPAT_TEST
	return g_test_folder;
#endif
	BROWSEINFOW bi = {}; bi.hwndOwner = g_wnd; bi.lpszTitle = title; bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	PIDLIST_ABSOLUTE pl = SHBrowseForFolderW(&bi); if (!pl) return L"";
	wchar_t b[MAX_PATH]; std::wstring r = SHGetPathFromIDListW(pl, b) ? b : L""; CoTaskMemFree(pl); return r;
}
static fs::path new_backup_dir(const fs::path &bin)
{
	SYSTEMTIME t; GetLocalTime(&t); wchar_t d[32]; swprintf(d, 32, L"%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
	fs::path p = bin / L"_nightzim_backup" / d; std::error_code ec; fs::create_directories(p, ec); return p;
}
// pede ao usuario um arquivo do ReShade (o nosso instalador nao traz o ReShade; ele vem de reshade.me)
static fs::path ask_reshade_dll()
{
	cmsg(T(L"Escolha o arquivo do ReShade 6.x \"with full add-on support\" (ReShade64.dll). Ele está dentro do instalador do ReShade (abra o ReShade_Setup_x.x.x_Addon.exe com o 7-Zip) ou numa instalação anterior (o dxgi.dll do ReShade de outro jogo).",
		L"Pick the ReShade 6.x \"with full add-on support\" file (ReShade64.dll). It is inside the ReShade setup (open ReShade_Setup_x.x.x_Addon.exe with 7-Zip) or in a previous install (the ReShade dxgi.dll of another game)."), MB_OK | MB_ICONINFORMATION);
	std::wstring f = pick_file(T(L"Arquivo do ReShade (64 bits)", L"ReShade file (64-bit)"));
	if (f.empty()) return {};
	Proxy x; if (!classify(f, x) || x.kind != PK_RESHADE) { log(T(L"[X] O arquivo escolhido não é o ReShade: ", L"[X] The chosen file is not ReShade: ") + f); return {}; }
	return f;
}
static void ensure_addon_path(const fs::path &bin, const fs::path &dcs)
{	// DCS por bin-mt: o ReShade fica em bin-mt, mas o add-on do mod esta em bin -> AddonPath no ReShade.ini (so essa chave)
	if (_wcsicmp(bin.filename().c_str(), L"bin-mt")) return;
	fs::path ini = bin / L"ReShade.ini"; std::error_code ec;
	if (!fs::exists(ini, ec)) { std::ofstream(ini, std::ios::binary) << "[ADDON]\r\nAddonPath=" << u8((dcs / L"bin").wstring()) << "\r\n"; }
	else ini_key_set(ini, "ADDON", "AddonPath", u8((dcs / L"bin").wstring()), nullptr);
	log(T(L"[OK] ReShade.ini de bin-mt aponta para os add-ons em bin (AddonPath).", L"[OK] bin-mt ReShade.ini points to the add-ons in bin (AddonPath)."));
}
// when: 0 = instalar, 1 = desinstalar (so lista), 2 = botao Verificar compatibilidade
static void compat_check(const fs::path &dcs, int when)
{
	std::error_code ec;
	if (!is_dcs(dcs)) return;
	fs::path bin = chosen_bin(dcs, true);
	log(T(L"--- Compatibilidade (proxies em ", L"--- Compatibility (proxies in ") + bin.filename().wstring() + L") ---");
	for (const wchar_t *d : {L"bin", L"bin-mt"})
	{
		if (!fs::exists(dcs / d, ec)) continue;
		auto v = scan_proxies(dcs / d);
		for (auto &x : v) log(std::wstring(L"  ") + d + L"\\" + px_label(x));
		if (v.empty()) log(std::wstring(L"  ") + d + T(L": nenhum proxy", L": no proxies"));
	}
	if (when == 1) return;	// desinstalar: so mostra o que ha (quem desfaz e o compat_uninstall)
	if (dcs_running()) { log(T(L"[!] Feche o DCS para o instalador poder mexer nos proxies.", L"[!] Close DCS so the installer can change the proxies.")); return; }
	auto v = scan_proxies(bin);
	const Proxy *dx = find_px(v, L"dxgi.dll"), *r64 = find_px(v, L"ReShade64.dll"), *d11 = find_px(v, L"d3d11.dll");
	CompatMan m; m.bin = bin.wstring();
	if (!dx || dx->kind == PK_RESHADE)
	{
		if (!dx && d11 && d11->kind == PK_RESHADE) { log(T(L"[OK] ReShade como d3d11.dll (modo compatível com outro dxgi.dll).", L"[OK] ReShade as d3d11.dll (compatible with another dxgi.dll).")); ensure_addon_path(bin, dcs); return; }
		if (!dx) { log(T(L"Cenário A: nenhum proxy. Instale o ReShade 6.x \"with full add-on support\" (reshade.me) em ", L"Scenario A: no proxy. Install ReShade 6.x \"with full add-on support\" (reshade.me) for ") + (bin / L"DCS.exe").wstring() + T(L", marcando só DirectX 10/11/12.", L", ticking only DirectX 10/11/12.")); return; }
		log(T(L"Cenário B: o ReShade é o dxgi.dll. Tudo certo.", L"Scenario B: ReShade is dxgi.dll. All good."));
		ensure_addon_path(bin, dcs);
		if (when != 2) return;
		if (cmsg(T(L"O ReShade está como dxgi.dll.\n\nQuer ligar o \"Modo compatível com OptiScaler\" (FSR 4)? O instalador faz backup, renomeia o ReShade para ReShade64.dll, copia o OptiScaler da pasta que você indicar e liga LoadReshade=true no OptiScaler.ini.",
			L"ReShade is dxgi.dll.\n\nTurn on \"OptiScaler compatible mode\" (FSR 4)? The installer backs up, renames ReShade to ReShade64.dll, copies OptiScaler from the folder you pick and sets LoadReshade=true in OptiScaler.ini."), MB_YESNO | MB_ICONQUESTION) != IDYES) return;
		// cenario D
		std::wstring od = pick_folder(T(L"Pasta do OptiScaler (a que tem OptiScaler.dll)", L"OptiScaler folder (the one with OptiScaler.dll)"));
		if (od.empty()) return;
		fs::path src = od, odll = src / L"OptiScaler.dll"; if (!fs::exists(odll, ec)) odll = src / L"dxgi.dll";
		Proxy ox; if (!classify(odll, ox) || ox.kind != PK_OPTI) { log(T(L"[X] Não achei o OptiScaler.dll nessa pasta.", L"[X] OptiScaler.dll not found in that folder.")); return; }
		fs::path bk = new_backup_dir(bin); m.backup = bk.wstring(); m.scenario = L"D";
		copy_over(bin / L"dxgi.dll", bk / L"dxgi.dll", ec); if (ec) { log(T(L"[X] Backup falhou: ", L"[X] Backup failed: ") + errw(ec)); return; }
		if (r64) copy_over(bin / L"ReShade64.dll", bk / L"ReShade64.dll", ec);
		move_over(bin / L"dxgi.dll", bin / L"ReShade64.dll", ec); if (ec) { log(T(L"[X] Não consegui renomear o ReShade: ", L"[X] Could not rename ReShade: ") + errw(ec)); return; }
		m.files.push_back({L"ReShade64.dll", sha256(bin / L"ReShade64.dll")});
		for (auto &e : fs::directory_iterator(src, ec))
		{
			if (!e.is_regular_file()) continue;
			std::wstring n = e.path().filename().wstring(), ext = e.path().extension().wstring(); for (auto &c : ext) c = towlower(c);
			if (ext != L".dll" && ext != L".ini" && ext != L".json") continue;	// leia-me, licencas etc. ficam fora
			std::wstring dn = !_wcsicmp(n.c_str(), L"OptiScaler.dll") ? L"dxgi.dll" : n;
			if (!_wcsicmp(dn.c_str(), L"ReShade64.dll")) continue;
			if (fs::exists(bin / dn, ec) && _wcsicmp(dn.c_str(), L"dxgi.dll")) copy_over(bin / dn, bk / dn, ec);
			copy_over(e.path(), bin / dn, ec); if (ec) { log(L"[X] " + dn + L": " + errw(ec)); continue; }
			m.files.push_back({dn, sha256(bin / dn)});
		}
		fs::path oi = bin / L"OptiScaler.ini"; std::string old;
		if (fs::exists(oi, ec) && ini_key_set(oi, "Plugins", "LoadReshade", "true", &old)) { m.ini = oi.wstring(); m.ini_old = w8(old); }
		compat_save(m);
		log(T(L"[OK] Modo compatível com OptiScaler ligado: OptiScaler = dxgi.dll, ReShade = ReShade64.dll, LoadReshade=true. Backup em ", L"[OK] OptiScaler compatible mode on: OptiScaler = dxgi.dll, ReShade = ReShade64.dll, LoadReshade=true. Backup in ") + bk.wstring());
		return;
	}
	if (dx->kind == PK_OPTI)	// cenario C
	{
		m.scenario = L"C";
		bool added = false;
		if (!r64 || r64->kind != PK_RESHADE)
		{
			if (r64) { log(T(L"[X] Existe um ReShade64.dll que não é o ReShade: ", L"[X] There is a ReShade64.dll that is not ReShade: ") + px_label(*r64) + T(L". Não mexi.", L". Left untouched.")); return; }
			fs::path rs = (d11 && d11->kind == PK_RESHADE) ? d11->path : ask_reshade_dll();
			if (rs.empty()) { log(T(L"Cenário C: o OptiScaler é o dxgi.dll e falta o ReShade64.dll. Cancelado.", L"Scenario C: OptiScaler is dxgi.dll and ReShade64.dll is missing. Cancelled.")); return; }
			copy_over(rs, bin / L"ReShade64.dll", ec); if (ec) { log(L"[X] ReShade64.dll: " + errw(ec)); return; }
			added = true;
		}
		fs::path oi = bin / L"OptiScaler.ini"; std::string old;
		if (!fs::exists(oi, ec)) { log(T(L"[!] OptiScaler.ini não encontrado: abra o jogo uma vez com o OptiScaler (ele cria o arquivo) e verifique de novo.", L"[!] OptiScaler.ini not found: start the game once with OptiScaler (it creates the file) and check again.")); }
		else if (ini_key_set(oi, "Plugins", "LoadReshade", "true", &old)) { m.ini = oi.wstring(); m.ini_old = w8(old); }
		CompatMan prev; bool had = compat_load(prev);
		if (added) m.files.push_back({L"ReShade64.dll", sha256(bin / L"ReShade64.dll")});
		else if (had && prev.scenario == L"C") m.files = prev.files;
		if (had && !prev.ini_old.empty() && _wcsicmp(prev.ini_old.c_str(), L"true")) m.ini_old = prev.ini_old;	// guarda o valor de antes da 1a vez
		if (added || !m.ini.empty()) compat_save(m);
		log(T(L"Cenário C: OptiScaler = dxgi.dll, ReShade = ReShade64.dll, LoadReshade=true", L"Scenario C: OptiScaler = dxgi.dll, ReShade = ReShade64.dll, LoadReshade=true") + std::wstring(added ? T(L" (ReShade64.dll copiado agora).", L" (ReShade64.dll copied now).") : L"."));
		ensure_addon_path(bin, dcs);
		return;
	}
	// cenario E: dxgi.dll desconhecido -> nunca sobrescrever
	log(T(L"Cenário E: o dxgi.dll é de outro programa: ", L"Scenario E: dxgi.dll belongs to another program: ") + px_label(*dx));
	if (d11 && d11->kind == PK_RESHADE) { log(T(L"[OK] O ReShade já está como d3d11.dll.", L"[OK] ReShade is already d3d11.dll.")); ensure_addon_path(bin, dcs); return; }
	if (d11) { log(T(L"[X] Também existe um d3d11.dll de outro programa: ", L"[X] There is also a d3d11.dll from another program: ") + px_label(*d11) + T(L". Não mexi em nada.", L". Nothing changed.")); return; }
	std::wstring q = std::wstring(T(L"O dxgi.dll do DCS não é o ReShade nem o OptiScaler:\n\n", L"DCS dxgi.dll is neither ReShade nor OptiScaler:\n\n")) + px_label(*dx) +
		T(L"\n\nO instalador NÃO vai sobrescrever esse arquivo. Quer instalar o ReShade como d3d11.dll (o DCS é DirectX 11)?", L"\n\nThe installer will NOT overwrite it. Install ReShade as d3d11.dll instead (DCS is DirectX 11)?");
	if (cmsg(q.c_str(), MB_YESNO | MB_ICONWARNING) != IDYES) { log(T(L"Cancelado: nada mudou.", L"Cancelled: nothing changed.")); return; }
	fs::path rs = (r64 && r64->kind == PK_RESHADE) ? r64->path : ask_reshade_dll();
	if (rs.empty()) return;
	copy_over(rs, bin / L"d3d11.dll", ec); if (ec) { log(L"[X] d3d11.dll: " + errw(ec)); return; }
	m.scenario = L"E"; m.files.push_back({L"d3d11.dll", sha256(bin / L"d3d11.dll")}); compat_save(m);
	ensure_addon_path(bin, dcs);
	log(T(L"[OK] ReShade instalado como d3d11.dll; o dxgi.dll do outro programa ficou como estava.", L"[OK] ReShade installed as d3d11.dll; the other program's dxgi.dll was left alone."));
}
// desinstalar: tira so o que o instalador colocou (pelo manifesto) e restaura o backup
static void compat_uninstall()
{
	CompatMan m; if (!compat_load(m)) return;
	std::error_code ec; fs::path bin = m.bin;
	log(T(L"--- Compatibilidade: desfazendo o cenário ", L"--- Compatibility: undoing scenario ") + m.scenario + L" ---");
	Proxy dx; bool opti_stays = classify(bin / L"dxgi.dll", dx) && dx.kind == PK_OPTI && m.scenario != L"D";
	for (auto &f : m.files)
	{
		fs::path p = bin / f.first; if (!fs::exists(p, ec)) continue;
		if (!_wcsicmp(f.first.c_str(), L"dxgi.dll") && m.scenario != L"D") continue;	// nunca o dxgi.dll do OptiScaler do usuario
		if (sha256(p) != f.second) { log(T(L"[!] ", L"[!] ") + f.first + T(L" mudou desde a instalação: deixei como está.", L" changed since install: left as is.")); continue; }
		fs::remove(p, ec); log(T(L"removido ", L"removed ") + f.first);
	}
	if (!m.ini.empty() && fs::exists(m.ini, ec) && (opti_stays || m.scenario == L"C"))
	{
		bool ours = false; for (auto &f : m.files) if (!_wcsicmp(f.first.c_str(), L"ReShade64.dll")) ours = true;
		std::string v = ours || m.ini_old.empty() ? "false" : u8(m.ini_old);	// ReShade64.dll do proprio usuario: volta ao valor que ele tinha
		ini_key_set(m.ini, "Plugins", "LoadReshade", v, nullptr); log(L"OptiScaler.ini: LoadReshade=" + w8(v));
	}
	if (!m.backup.empty() && fs::exists(m.backup, ec))
	{
		for (auto &e : fs::directory_iterator(m.backup, ec)) if (e.is_regular_file()) { copy_over(e.path(), bin / e.path().filename(), ec); if (!ec) log(T(L"restaurado ", L"restored ") + e.path().filename().wstring()); }
		if (m.scenario == L"D" && !m.ini.empty()) { fs::path oi = m.ini; if (fs::exists(oi, ec) && !fs::exists(fs::path(m.backup) / L"OptiScaler.ini", ec)) { bool ours = false; for (auto &f : m.files) if (!_wcsicmp(f.first.c_str(), L"OptiScaler.ini")) ours = true; if (ours) fs::remove(oi, ec); } }
	}
	fs::remove(compat_man_path(), ec);
}
