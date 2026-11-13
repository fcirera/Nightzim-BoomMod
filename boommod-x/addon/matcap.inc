// ---------------------------------------------------------------- captura das matrizes de camera do proprio DCS
// Em vez de adivinhar FOV e esperar a camera do Lua (que chega atrasada), lemos as matrizes que o DCS envia para a GPU
// (constant buffers). Fase de aprendizado: varre os buffers por alguns quadros e acha onde estao a projecao e a
// orientacao da camera (comparando com a camera do Lua). Depois so le aquele ponto, todo quadro.

struct MatLoc { ID3D11Resource *r = nullptr; uint32_t off = 0; int kind = -1, layout = 0, perm = 0, sgn = 0; };
static MatLoc g_loc_proj, g_loc_view;
// o DCS gira entre 2-3 copias do mesmo buffer (um por quadro em voo): guardamos todas
static const int NALT = 4;
static MatLoc g_alt_proj[NALT], g_alt_view[NALT];
static bool is_loc_res(ID3D11Resource *r)
{
	if (!r) return false;
	for (int i = 0; i < NALT; i++) if (g_alt_proj[i].r == r || g_alt_view[i].r == r) return true;
	return false;
}
static int g_learn = 0;									// quadros de aprendizado restantes
static std::unordered_map<std::string, std::pair<MatLoc, int>> g_learn_hits;
static double g_ref[3][3];								// camera do Lua (frente, cima, direita) no mundo
static float g_aspect_ref = 1.7778f;
static std::unordered_map<ID3D11Resource *, void *> g_mapped;
static std::unordered_map<ID3D11Resource *, uint32_t> g_cb_size;	// 0 = nao e constant buffer
// resultado mais recente
static float g_cap_tanv = 0, g_cap_near = 0; static uint64_t g_cap_proj_frame = 0, g_cap_view_frame = 0;
static float g_cap_ax[3][3];							// frente, cima, direita (mundo DCS)
static uint64_t g_frame_mt = 0;
// ultima camera escrita nos pontos conhecidos, de QUALQUER vista (cabine, pod de mira, espelhos...): o add-on associa
// cada uma ao buffer de profundidade em uso no desenho seguinte

static int g_perms[48][3][3];
static void init_perms()
{
	int p[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}, n = 0;
	for (int a = 0; a < 6; a++)
		for (int s = 0; s < 8; s++, n++)
		{
			memset(g_perms[n], 0, sizeof g_perms[n]);
			for (int r = 0; r < 3; r++) g_perms[n][r][p[a][r]] = (s >> r & 1) ? -1 : 1;
		}
}

static inline float dot3(const float *a, const double *b) { return (float)(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]); }
static inline float len3(const float *a) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
static inline float dotf(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// vetores 3D da matriz: layout 0 = colunas (convencao vetor-linha), 1 = linhas
static void vecs(const float *m, int layout, float v[4][3])
{
	for (int j = 0; j < 4; j++)
		for (int i = 0; i < 3; i++) v[j][i] = layout == 0 ? m[i * 4 + j] : m[j * 4 + i];
}

// projecao com profundidade invertida (plano distante no infinito ou muito longe)
static bool test_proj(const float *m, float &tanv, float &nearv, int &layout, bool any_aspect = false)
{
	float sx = m[0], sy = m[5];
	if (!(sx > 0.05f && sy > 0.05f && sy < 400.f)) return false;
	if (!any_aspect && (sy > 30.f || fabsf(sy / sx - g_aspect_ref) > 0.03f * g_aspect_ref)) return false;
	if (any_aspect && (sy / sx < 0.4f || sy / sx > 4.f)) return false;
	auto z = [&](int i) { return fabsf(m[i]) < 1e-4f; };
	if (!(z(1) && z(3) && z(4) && z(7) && z(12) && z(13) && z(15))) return false;
	if (fabsf(m[2]) > 0.05f || fabsf(m[6]) > 0.05f || fabsf(m[8]) > 0.05f || fabsf(m[9]) > 0.05f) return false;
	if (fabsf(fabsf(m[11]) - 1.f) < 1e-3f && fabsf(m[10]) < 0.02f && fabsf(m[14]) > 1e-3f && fabsf(m[14]) < 5.f) { layout = 0; nearv = fabsf(m[14]); }
	else if (fabsf(fabsf(m[14]) - 1.f) < 1e-3f && fabsf(m[10]) < 0.02f && fabsf(m[11]) > 1e-3f && fabsf(m[11]) < 5.f) { layout = 1; nearv = fabsf(m[11]); }
	else return false;
	tanv = 1.f / sy;
	return true;
}

// orientacao: matriz de vista pura (3 eixos unitarios ortogonais) ou vista*projecao (eixos com escalas sx, sy, 1)
// Devolve os eixos frente/cima/direita no mundo DCS conforme a permutacao/sinal; tol = cosseno minimo com a camera do Lua
static bool test_view(const float *m, int layout, int kind, int perm, int sgn, float ax[3][3], float &tanv, float tol, float *score = nullptr, bool any_aspect = false)
{
	float v[4][3]; vecs(m, layout, v);
	float f[3], u[3], r[3];
	if (kind == 1)
	{
		for (int j = 0; j < 3; j++) if (fabsf(len3(v[j]) - 1.f) > 0.02f) return false;
		if (fabsf(dotf(v[0], v[1])) > 0.02f || fabsf(dotf(v[0], v[2])) > 0.02f || fabsf(dotf(v[1], v[2])) > 0.02f) return false;
		if (len3(v[3]) > 1e-3f) return false;
		memcpy(r, v[0], 12); memcpy(u, v[1], 12); memcpy(f, v[2], 12);
		tanv = 0;
	}
	else
	{
		float l0 = len3(v[0]), l1 = len3(v[1]), l3 = len3(v[3]);
		if (fabsf(l3 - 1.f) > 0.02f || l0 < 0.05f || l1 < 0.2f || l1 > (any_aspect ? 400.f : 30.f)) return false;
		if (!any_aspect && fabsf(l1 / l0 - g_aspect_ref) > 0.03f * g_aspect_ref) return false;
		if (len3(v[2]) > 0.05f) return false;
		if (fabsf(dotf(v[0], v[1])) > 0.02f * l0 * l1 || fabsf(dotf(v[0], v[3])) > 0.02f * l0 || fabsf(dotf(v[1], v[3])) > 0.02f * l1) return false;
		for (int i = 0; i < 3; i++) { r[i] = v[0][i] / l0; u[i] = v[1][i] / l1; f[i] = v[3][i]; }
		tanv = 1.f / l1;
	}
	// mundo da GPU -> mundo DCS (permutacao com sinais) e sinal de cada eixo da camera
	const int (*P)[3] = g_perms[perm];
	float *src[3] = {f, u, r};
	float sc = 0;
	for (int a = 0; a < 3; a++)
	{
		float s = (sgn >> a & 1) ? -1.f : 1.f;
		for (int i = 0; i < 3; i++) ax[a][i] = s * (P[i][0] * src[a][0] + P[i][1] * src[a][1] + P[i][2] * src[a][2]);
		float d = dot3(ax[a], g_ref[a]);
		if (d < tol) return false;
		sc += d;
	}
	if (score) *score = sc;
	return true;
}

static void scan_buffer(ID3D11Resource *r, const float *data, uint32_t nfloats)
{
	for (uint32_t o = 0; o + 16 <= nfloats; o += 4)
	{
		const float *m = data + o;
		float tv, nr; int lay;
		if (test_proj(m, tv, nr, lay))
		{
			MatLoc L; L.r = r; L.off = o; L.kind = 0; L.layout = lay;
			char k[64]; snprintf(k, sizeof k, "p%p:%u:%d", (void *)r, o, lay);
			auto &e = g_learn_hits[k]; e.first = L; e.second++;
		}
		for (int kind = 1; kind <= 2; kind++)
			for (int lay2 = 0; lay2 < 2; lay2++)
			{
				float ax[3][3]; float t2;
				// teste estrutural barato antes das 384 combinacoes
				if (!test_view(m, lay2, kind, 0, 0, ax, t2, -2.f)) continue;
				int bp = -1, bs = 0; float best = -1e9f, sc;
				for (int pm = 0; pm < 48; pm++)
					for (int sg = 0; sg < 8; sg++)
						if (test_view(m, lay2, kind, pm, sg, ax, t2, 0.94f, &sc) && sc > best) { best = sc; bp = pm; bs = sg; }
				if (bp >= 0)
				{
					MatLoc L; L.r = r; L.off = o; L.kind = kind; L.layout = lay2; L.perm = bp; L.sgn = bs;
					char k[80]; snprintf(k, sizeof k, "v%p:%u:%d:%d:%d:%d", (void *)r, o, kind, lay2, bp, bs);
					auto &e = g_learn_hits[k]; e.first = L; e.second++;
				}
			}
	}
}

static void on_cb_data(ID3D11Resource *r, const void *data, uint64_t offset, uint64_t size)
{
	if (!r || !data) return;
	std::lock_guard<std::mutex> lk(g_mx);
	auto it = g_cb_size.find(r);
	uint32_t cbs;
	if (it == g_cb_size.end())
	{
		ID3D11Buffer *b = nullptr; D3D11_BUFFER_DESC d = {};
		if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Buffer), (void **)&b))) { b->GetDesc(&d); b->Release(); }
		cbs = (d.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && d.ByteWidth <= 65536 ? d.ByteWidth : 0;
		g_cb_size[r] = cbs;
	}
	else cbs = it->second;
	if (!cbs) return;
	uint32_t nbytes = (uint32_t)std::min<uint64_t>(size, cbs - std::min<uint64_t>(offset, cbs));
	const float *f = (const float *)data;
	uint32_t base = (uint32_t)(offset / 4);
	if (g_learn > 0 && nbytes <= 16384) { std::vector<float> tmp(base + nbytes / 4, 0.f); memcpy(tmp.data() + base, f, nbytes); scan_buffer(r, tmp.data(), base + nbytes / 4); }
	// leitura nos pontos ja conhecidos
	auto rd = [&](const MatLoc &L, float m[16]) -> bool {
		if (L.r != r || L.off < base || (L.off + 16 - base) * 4 > nbytes) return false;
		memcpy(m, f + (L.off - base), 64); return true; };
	float m[16];
	for (int i = 0; i < NALT; i++)
	{
		const MatLoc &LP = g_alt_proj[i];
		if (rd(LP, m))
		{
			float tv, nr; int lay;
			if (test_proj(m, tv, nr, lay) && lay == LP.layout) { g_cap_tanv = tv; g_cap_near = nr; g_cap_proj_frame = g_frame_mt; }
			if (test_proj(m, tv, nr, lay, true) && lay == LP.layout) { g_any.tanv = tv; g_any.nearv = nr; g_any.asp = m[5] / m[0]; g_any.p = true; }
		}
		const MatLoc &LV = g_alt_view[i];
		if (rd(LV, m))
		{
			float ax[3][3], tv;
			if (test_view(m, LV.layout, LV.kind, LV.perm, LV.sgn, ax, tv, -2.f, nullptr, true))
			{
				memcpy(g_any.ax, ax, sizeof ax); g_any.v = true;
				if (LV.kind == 2) { float v4[4][3]; vecs(m, LV.layout, v4); g_any.tanv = tv; g_any.asp = len3(v4[1]) / len3(v4[0]); g_any.p = true; }
			}
			if (test_view(m, LV.layout, LV.kind, LV.perm, LV.sgn, ax, tv, 0.6f))
			{
				memcpy(g_cap_ax, ax, sizeof ax); g_cap_view_frame = g_frame_mt;
				if (LV.kind == 2 && g_loc_proj.kind < 0) { g_cap_tanv = tv; g_cap_proj_frame = g_frame_mt; }
			}
		}
	}
}

static void on_map_buf(device *, resource res, uint64_t, uint64_t, map_access, void **data)
{
	if (!G.ok || !data || !*data) return;
	ID3D11Resource *r = (ID3D11Resource *)res.handle;
	if (g_learn <= 0 && !is_loc_res(r)) return;
	std::lock_guard<std::mutex> lk(g_mx);
	g_mapped[r] = *data;
}
static void on_unmap_buf(device *, resource res)
{
	if (!G.ok) return;
	ID3D11Resource *r = (ID3D11Resource *)res.handle;
	void *p = nullptr;
	{
		std::lock_guard<std::mutex> lk(g_mx);
		auto it = g_mapped.find(r);
		if (it == g_mapped.end()) return;
		p = it->second; g_mapped.erase(it);
	}
	if (g_learn <= 0 && !is_loc_res(r)) return;
	on_cb_data(r, p, 0, 65536);
}
static bool on_update_buf(device *, const void *data, resource dest, uint64_t off, uint64_t size)
{
	if (G.ok && (g_learn > 0 || g_loc_proj.r || g_loc_view.r)) on_cb_data((ID3D11Resource *)dest.handle, data, off, size);
	return false;
}
static bool on_update_buf_cmd(command_list *, const void *data, resource dest, uint64_t off, uint64_t size)
{
	if (G.ok && (g_learn > 0 || g_loc_proj.r || g_loc_view.r)) on_cb_data((ID3D11Resource *)dest.handle, data, off, size);
	return false;
}

// chamado no present: atualiza a referencia e fecha o aprendizado
static void matcap_frame(const Cam &cam, bool have_cam)
{
	std::lock_guard<std::mutex> lk(g_mx);
	g_frame_mt++;
	if (have_cam) for (int i = 0; i < 3; i++) { g_ref[0][i] = cam.x[i]; g_ref[1][i] = cam.y[i]; g_ref[2][i] = cam.z[i]; }
	g_aspect_ref = g_bb_h ? (float)g_bb_w / g_bb_h : 1.7778f;
	// busca automatica: enquanto nao achou as matrizes, tenta de novo a cada ~10 s (sem precisar de atalho)
	if (have_cam && g_learn == 0 && (!g_loc_proj.r || !g_loc_view.r) && g_frame_mt % 600 == 300) g_learn = 90;
	if (g_learn > 0 && --g_learn == 0)
	{
		std::vector<std::pair<int, MatLoc>> P, V;
		for (auto &kv : g_learn_hits) (kv.second.first.kind == 0 ? P : V).push_back({kv.second.second, kv.second.first});
		auto byc = [](const std::pair<int, MatLoc> &a, const std::pair<int, MatLoc> &b) { return a.first > b.first; };
		std::sort(P.begin(), P.end(), byc); std::sort(V.begin(), V.end(), byc);
		logf("aprendizado das matrizes: %zu candidatos de projecao, %zu de orientacao", P.size(), V.size());
		for (size_t i = 0; i < std::min<size_t>(5, P.size()); i++) logf("  proj  %p+%u lay %d: %d vezes", (void *)P[i].second.r, P[i].second.off * 4, P[i].second.layout, P[i].first);
		for (size_t i = 0; i < std::min<size_t>(5, V.size()); i++) logf("  vista %p+%u tipo %d lay %d perm %d sinal %d: %d vezes", (void *)V[i].second.r, V[i].second.off * 4, V[i].second.kind, V[i].second.layout, V[i].second.perm, V[i].second.sgn, V[i].first);
		auto pick = [](std::vector<std::pair<int, MatLoc>> &L, MatLoc &main, MatLoc *alt) {
			for (int i = 0; i < NALT; i++) alt[i] = MatLoc();
			if (L.empty()) return;
			main = L[0].second; int n = 0;
			for (auto &c : L)
				if (n < NALT && c.first * 2 >= L[0].first && c.second.off == main.off && c.second.kind == main.kind && c.second.layout == main.layout &&
					c.second.perm == main.perm && c.second.sgn == main.sgn)
					alt[n++] = c.second;
			logf("  usando %d copia(s) do buffer", n);
		};
		pick(P, g_loc_proj, g_alt_proj);
		pick(V, g_loc_view, g_alt_view);
		g_learn_hits.clear();
	}
}
