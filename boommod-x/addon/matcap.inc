// ---------------------------------------------------------------- captura das matrizes de camera do proprio DCS
// Em vez de adivinhar FOV e esperar a camera do Lua (que chega atrasada), lemos as matrizes que o DCS envia para a GPU
// (constant buffers). Fase de aprendizado: varre os buffers por alguns quadros e acha onde estao a projecao e a
// orientacao da camera (comparando com a camera do Lua). Depois so le aquele ponto, todo quadro.

struct MatLoc { ID3D11Resource *r = nullptr; uint32_t off = 0; int kind = -1, layout = 0, perm = 0, sgn = 0; float score = 0; };
static MatLoc g_loc_proj, g_loc_view;
// o DCS gira entre 2-3 copias do mesmo buffer (um por quadro em voo): guardamos todas
static const int NALT = 4;
static MatLoc g_alt_proj[NALT], g_alt_view[NALT];
static MatLoc g_loc_vp, g_alt_vp[NALT];	// vista*projecao geral (serve para projecao descentrada: cada olho no VR)
static bool is_loc_res(ID3D11Resource *r)
{
	if (!r) return false;
	for (int i = 0; i < NALT; i++) if (g_alt_proj[i].r == r || g_alt_view[i].r == r || g_alt_vp[i].r == r) return true;
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

static inline void cross3(const float *a, const float *b, float *o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
// Matriz vista*projecao (relativa a camera) com projecao qualquer, inclusive descentrada. Devolve, nos eixos da GPU,
// A, B, C tais que a direcao do raio de um pixel e  A + ndc.x * B + ndc.y * C  (componente frontal = 1), e o plano proximo.
static bool parse_vp(const float *m, int layout, float A[3], float B[3], float C[3], float &nearv)
{
	float v[4][3]; vecs(m, layout, v);
	float t[4]; for (int j = 0; j < 4; j++) t[j] = layout == 0 ? m[12 + j] : m[j * 4 + 3];
	float l3 = len3(v[3]);
	if (fabsf(l3 - 1.f) > 0.02f || len3(v[2]) > 0.05f) return false;
	float l0 = len3(v[0]), l1 = len3(v[1]);
	if (l0 < 0.05f || l0 > 400.f || l1 < 0.2f || l1 > 400.f) return false;
	float o0 = dotf(v[0], v[3]), o1 = dotf(v[1], v[3]), r[3], u[3];
	for (int i = 0; i < 3; i++) { r[i] = v[0][i] - o0 * v[3][i]; u[i] = v[1][i] - o1 * v[3][i]; }
	float lr = len3(r), lu = len3(u);
	if (lr < 0.05f || lu < 0.2f || fabsf(dotf(r, u)) > 0.03f * lr * lu) return false;
	if (fabsf(o0) > 0.8f || fabsf(o1) > 0.8f) return false;
	if (lu / lr < 0.4f || lu / lr > 4.f) return false;
	if (fabsf(t[3]) > 0.05f || fabsf(t[0]) > 0.5f * lr || fabsf(t[1]) > 0.5f * lu) return false;
	nearv = fabsf(t[2]); if (!(nearv > 1e-3f && nearv < 5.f)) return false;
	float a[3]; cross3(v[0], v[1], a); float den = dotf(a, v[3]);
	if (fabsf(den) < 1e-6f) return false;
	float b[3], c[3]; cross3(v[1], v[3], b); cross3(v[3], v[0], c);
	for (int i = 0; i < 3; i++) { A[i] = a[i] / den; B[i] = b[i] / den; C[i] = c[i] / den; }
	return true;
}
// projecao pura qualquer (inclusive descentrada, como a de cada olho no VR): raio no espaco da vista = A + ndc.x*B + ndc.y*C
static bool test_projgen(const float *m, int layout, float A[3], float B[3], float C[3], float &nearv)
{
	if (!parse_vp(m, layout, A, B, C, nearv)) return false;
	float v[4][3]; vecs(m, layout, v);
	if (fabsf(v[3][0]) > 0.02f || fabsf(v[3][1]) > 0.02f) return false;					// w = +-z da vista
	float l0 = len3(v[0]), l1 = len3(v[1]);
	if (fabsf(v[0][1]) > 0.02f * l0 || fabsf(v[1][0]) > 0.02f * l1) return false;	// x da tela so depende de x (e z), y so de y (e z)
	if (nearv > 0.5f) return false;													// descarta projecoes auxiliares (mapas de ambiente etc.)
	return true;
}
static struct GenProj { float A[3], B[3], C[3], nearv; bool ok = false; } g_gp;	// ultima projecao geral escrita
static struct GenView { float v[3][3]; bool ok = false; } g_gv;					// ultimos eixos da vista (x, y, z) no mundo DCS
static void combine_any3()
{
	if (!g_gp.ok || !g_gv.ok) return;
	for (int i = 0; i < 3; i++)
	{
		g_any3.A[i] = g_gp.A[0] * g_gv.v[0][i] + g_gp.A[1] * g_gv.v[1][i] + g_gp.A[2] * g_gv.v[2][i];
		g_any3.B[i] = g_gp.B[0] * g_gv.v[0][i] + g_gp.B[1] * g_gv.v[1][i] + g_gp.B[2] * g_gv.v[2][i];
		g_any3.C[i] = g_gp.C[0] * g_gv.v[0][i] + g_gp.C[1] * g_gv.v[1][i] + g_gp.C[2] * g_gv.v[2][i];
	}
	g_any3.nearv = g_gp.nearv; g_any3.ok = true; g_any3.serial++;
}
static void perm_apply(int perm, const float *s, float *o) { const int (*P)[3] = g_perms[perm]; for (int i = 0; i < 3; i++) o[i] = (float)(P[i][0] * s[0] + P[i][1] * s[1] + P[i][2] * s[2]); }

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
		else for (int lg = 0; lg < 2; lg++)
		{
			float A[3], B[3], C[3], ng;
			if (!test_projgen(m, lg, A, B, C, ng)) continue;
			MatLoc L; L.r = r; L.off = o; L.kind = 0; L.layout = lg; L.sgn = 1;	// sgn = 1: projecao geral (descentrada)
			char k[64]; snprintf(k, sizeof k, "g%p:%u:%d", (void *)r, o, lg);
			auto &e = g_learn_hits[k]; e.first = L; e.second++;
			break;
		}
		for (int kind = 1; kind <= 2; kind++)
			for (int lay2 = 0; lay2 < 2; lay2++)
			{
				float ax[3][3]; float t2;
				// teste estrutural barato antes das 384 combinacoes
				if (!test_view(m, lay2, kind, 0, 0, ax, t2, -2.f)) continue;
				// matriz identidade ou so troca de eixos: nao e camera (e matriz de objeto parado); so casaria por acaso
				// quando a camera esta alinhada com os eixos do mundo. A camera de verdade nunca fica exatamente alinhada.
				if (kind == 1) { int big = 0; for (int q = 0; q < 11; q++) if (q % 4 != 3 && fabsf(fabsf(m[q]) - 1.f) < 2e-3f) big++; if (big >= 3) continue; }
				int bp = -1, bs = 0; float best = -1e9f, sc;
				for (int pm = 0; pm < 48; pm++)
					for (int sg = 0; sg < 8; sg++)
						if (test_view(m, lay2, kind, pm, sg, ax, t2, 0.94f, &sc) && sc > best) { best = sc; bp = pm; bs = sg; }
				if (bp >= 0)
				{
					MatLoc L; L.r = r; L.off = o; L.kind = kind; L.layout = lay2; L.perm = bp; L.sgn = bs;
					char k[80]; snprintf(k, sizeof k, "v%p:%u:%d:%d:%d:%d", (void *)r, o, kind, lay2, bp, bs);
					auto &e = g_learn_hits[k]; float acc = e.first.score + best; e.first = L; e.first.score = acc; e.second++;
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
			float A[3], B[3], C[3], ng;
			if (test_projgen(m, LP.layout, A, B, C, ng)) { memcpy(g_gp.A, A, 12); memcpy(g_gp.B, B, 12); memcpy(g_gp.C, C, 12); g_gp.nearv = ng; g_gp.ok = true; combine_any3(); }
		}
		const MatLoc &LV = g_alt_view[i];
		if (rd(LV, m))
		{
			float ax[3][3], tv;
			if (LV.kind == 1 && test_view(m, LV.layout, 1, LV.perm, LV.sgn, ax, tv, -2.f, nullptr, true))
			{	// eixos x, y, z da vista como vetores do mundo (sem sinais: so a permutacao GPU -> DCS)
				float vv[4][3]; vecs(m, LV.layout, vv);
				for (int k2 = 0; k2 < 3; k2++) perm_apply(LV.perm, vv[k2], g_gv.v[k2]);
				g_gv.ok = true; combine_any3();
			}
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
	if (G.ok && (g_learn > 0 || g_loc_proj.r || g_loc_view.r || g_loc_vp.r)) on_cb_data((ID3D11Resource *)dest.handle, data, off, size);
	return false;
}
static bool on_update_buf_cmd(command_list *, const void *data, resource dest, uint64_t off, uint64_t size)
{
	if (G.ok && (g_learn > 0 || g_loc_proj.r || g_loc_view.r || g_loc_vp.r)) on_cb_data((ID3D11Resource *)dest.handle, data, off, size);
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
	// (em VR a projecao de cada olho e descentrada e nunca e achada: cada tentativa falha dobra a espera, ate 10 min,
	//  para a varredura nao ficar engasgando o jogo a cada 10 s)
	static uint64_t next_try = 300; static int fails = 0;
	if (have_cam && g_learn == 0 && (!g_loc_proj.r || !g_loc_view.r) && g_frame_mt >= next_try) { g_learn = 90; next_try = g_frame_mt + 100000000ull; }
	if (g_learn > 0 && --g_learn == 0)
	{
		std::vector<std::pair<int, MatLoc>> P, V, Wv;
		for (auto &kv : g_learn_hits) (kv.second.first.kind == 0 ? P : kv.second.first.kind == 3 ? Wv : V).push_back({kv.second.second, kv.second.first});
		auto byc = [](const std::pair<int, MatLoc> &a, const std::pair<int, MatLoc> &b) { return a.first > b.first; };
		std::sort(P.begin(), P.end(), byc); std::sort(V.begin(), V.end(), byc); std::sort(Wv.begin(), Wv.end(), byc);
		for (size_t i = 0; i < std::min<size_t>(5, Wv.size()); i++) logf("  vista*proj geral %p+%u lay %d perm %d: %d vezes", (void *)Wv[i].second.r, Wv[i].second.off * 4, Wv[i].second.layout, Wv[i].second.perm, Wv[i].first);
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
		// orientacao: so vale matriz que mora no mesmo buffer da projecao (o buffer "da camera"); matrizes de objetos, que
		// aparecem centenas de vezes por quadro em outros buffers, ficam de fora. Entre uma matriz e a transposta (as duas
		// passam quando a cabeca esta quase alinhada com os eixos) vence a de maior soma de semelhanca com a camera do Lua.
		{
			std::vector<std::pair<int, MatLoc>> V2;
			for (auto &c : V) { bool in = false; for (int i = 0; i < NALT; i++) if (g_alt_proj[i].r && g_alt_proj[i].r == c.second.r) in = true; if (in && c.second.kind == 1) V2.push_back(c); }
			if (!V2.empty())
			{
				std::sort(V2.begin(), V2.end(), [](const std::pair<int, MatLoc> &a, const std::pair<int, MatLoc> &b) { return a.second.score > b.second.score; });
				for (size_t i = 0; i < std::min<size_t>(6, V2.size()); i++) logf("  vista (buffer da camera) %p+%u lay %d perm %d sinal %d: %d vezes, soma %.1f", (void *)V2[i].second.r, V2[i].second.off * 4, V2[i].second.layout, V2[i].second.perm, V2[i].second.sgn, V2[i].first, V2[i].second.score);
				// (a contagem do primeiro vira a referencia do "pick")
				V = V2;
			}
		}
		pick(V, g_loc_view, g_alt_view);
		if (!g_loc_proj.r || !g_loc_view.r)
		{
			fails++; uint64_t wait = std::min<uint64_t>(600ull << std::min(fails, 6), g_loc_proj.r ? 1500ull : 36000ull);	// projecao ja achada: so falta a camera sair do alinhamento
			next_try = g_frame_mt + wait;
			logf("  matrizes incompletas (tentativa %d): proxima busca em ~%llu quadros%s", fails, (unsigned long long)wait, fails == 3 ? " - provavel VR (projecao descentrada)" : "");
		}
		g_learn_hits.clear();
	}
}
