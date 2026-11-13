// Nightzim BoomMod V - renderizador volumetrico (ray marching) desenhado por cima do quadro do DCS.
// Mesma receita do render offline (render.py): fuligem escura que absorve e emite como corpo negro, poeira clara
// que espalha, auto-sombra pelo sol (pre-calculada), luz do ceu e do chao, luz do fogo espalhada, chama.
// Compilado em tempo de execucao pelo add-on: da para editar este arquivo e recarregar com Ctrl+Shift+R.

cbuffer NZ : register(b0)
{
	float4 camO;		// xyz: posicao da camera dentro da grade (m), w: passo (m)
	float4 camF;		// frente  (espaco da grade), w: tan(fov horizontal / 2)
	float4 camR;		// direita (espaco da grade), w: tan(fov vertical / 2)
	float4 camU;		// cima    (espaco da grade), w: plano proximo (m) para linearizar a profundidade
	float4 grid;		// xyz: tamanho da grade (m), w: peso de interpolacao entre os quadros
	float4 scales;		// fuligem max, poeira max, chama max, opacidade global
	float4 scales2;		// tau max, fogo max, exposicao, modo depuracao
	float4 screen;		// largura, altura, 1/largura, 1/altura
	float4 depthInfo;	// x: tem profundidade (0/1), y: invertida (0/1), z: largura da textura, w: altura
	float4 fog;			// rgb: cor da nevoa, w: distancia de 1/e (m)
	float4 misc;		// x: numero maximo de passos, y: sonda (nao usado)
	float4 sunDir;		// xyz: direcao do sol no espaco da grade (x norte, y leste, z cima), w: 1 = sombra calculada em tempo real
	float4 sunCol;		// rgb: luz do sol
	float4 skyCol;		// rgb: luz do ceu; w: luz do dia (0 noite .. 1 dia) para a luz rebatida do chao
	float4 nearInfo;	// x: tem camada da cabine (0/1), y: limiar de profundidade bruta da cabine
	float4 detail;		// x: forca do detalhe, y: escala do ruido (m), z: tempo (s), w: ganho do fogo
	float4 detail2;		// x: temperatura minima da chama (K), y: largura do desvanecimento nas bordas da caixa, z: tem chao, w: semente
	float4 wind;		// xy: vento no espaco da grade (m/s), z: segundos de deriva, w: altura do cisalhamento (m; 0 = sem chao)
	float4 shock;		// onda de choque: x raio (m), y espessura (m), z amplitude da refracao (m), w nuvem de Wilson (0..1)
	float4 shock2;		// x: distancia focal em pixels (onda de choque) | na nuvem: y tempo desde a detonacao (s), z luz do fogo, w realismo de cor (0 = v1.0)
};

// deslocamento pelo vento na altura z (perto do chao a poeira anda menos que a nuvem la em cima)
float2 windOffset(float z)
{
	float k = wind.w > 0 ? 0.35 + 0.65 * saturate(z / wind.w) : 1.0;
	return wind.xy * wind.z * k;
}

Texture3D<float4> A0 : register(t0);
Texture3D<float4> B0 : register(t1);
Texture3D<float4> A1 : register(t2);
Texture3D<float4> B1 : register(t3);
// volume comprimido (BC5, opcao "comprimir na VRAM"): t0..t3 trazem os canais r,g e t10..t13 os canais b,a; misc.y = 1
Texture3D<float2> A0b : register(t10);
Texture3D<float2> B0b : register(t11);
Texture3D<float2> A1b : register(t12);
Texture3D<float2> B1b : register(t13);
#ifdef DEPTH_MS
Texture2DMS<float> DepthTex : register(t4);
Texture2DMS<float> DepthNear : register(t5);
#define LOADD(ip) DepthTex.Load(ip, 0)
#define LOADN(ip) DepthNear.Load(ip, 0)
#else
Texture2D<float>  DepthTex : register(t4);
Texture2D<float>  DepthNear : register(t5);
#define LOADD(ip) DepthTex.Load(int3(ip, 0))
#define LOADN(ip) DepthNear.Load(int3(ip, 0))
#endif
Texture3D<float>  SunTau : register(t6);
Texture3D<float>  Noise : register(t7);
Texture2D<float4> SceneTex : register(t8);
SamplerState LinWrap : register(s1);
RWTexture3D<float> OutTau : register(u0);
SamplerState LinClamp : register(s0);
#define NZ_VOLFN(N, T, TB) float4 S##N(float3 uvw) { float4 v = T.SampleLevel(LinClamp, uvw, 0); if (misc.y > 0.5) v.ba = TB.SampleLevel(LinClamp, uvw, 0); return v; }
NZ_VOLFN(A0, A0, A0b)
NZ_VOLFN(A1, A1, A1b)
NZ_VOLFN(B0, B0, B0b)
NZ_VOLFN(B1, B1, B1b)

struct VSOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };

VSOut vs_main(uint id : SV_VertexID)
{
	VSOut o;
	float2 uv = float2((id << 1) & 2, id & 2);
	o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
	o.ndc = o.pos.xy;
	return o;
}

static const float KS = 22.0, KD = 3.0, SH0 = 0.004, SH1 = 0.03;
static const float3 ALB_S = float3(0.022, 0.023, 0.027);
static const float3 ALB_D = float3(0.34, 0.30, 0.25);

static const float3 GND_RGB = float3(0.42, 0.37, 0.30) * 0.35;
static const float  EM = 0.7e8, FLK = 0.2e8;
static const float3 FIRE_GB = float3(1.0, 0.3486, 0.0844);	// razao G/R e B/R da luz do fogo (~1300 K)

float3 planck3(float T)
{
	float3 lam = float3(0.61, 0.55, 0.465);
	return 1.0 / (lam * lam * lam * lam * lam * (exp(14388.0 / (lam * T)) - 1.0));
}

float3 tonemap(float3 x)
{
	float m = max(max(x.r, x.g), x.b) * 1.25 + 1e-6;
	float3 y = x * 1.25 * (1 - exp(-m)) / m;
	float wht = pow(saturate((m - 14.0) / 60.0), 0.8);
	y = lerp(y, max(y, max(max(y.r, y.g), y.b) * float3(1.0, 0.93, 0.80)), wht);
	return pow(saturate(y), 1.0 / 2.2);
}

float viewDepthAt(float2 pix)
{
	if (depthInfo.x < 0.5) return 1e9;
	float2 uv = pix * screen.zw;
	int2 ip = int2(uv * depthInfo.zw);
	float d = LOADD(ip);
	if (depthInfo.y > 0.5)
	{
		// profundidade invertida com plano distante no infinito: d = near / z
		if (d <= 1e-7) return 1e9;
		return camU.w / d;
	}
	// profundidade convencional com plano distante no infinito: d = 1 - near / z
	if (d >= 0.9999999) return 1e9;
	return camU.w / (1.0 - d);
}

// sonda: grade 9x7 de profundidades brutas (ndc x de -0.8 a 0.8, y de 0.4 a -0.8) para a calibracao continua
float ps_probe(VSOut i) : SV_Target
{
	int2 g = int2(i.pos.xy);
	float2 ndc = float2(-0.8 + 0.2 * g.x, 0.4 - 0.2 * g.y);
	int2 p = int2((ndc.x * 0.5 + 0.5) * depthInfo.z, (0.5 - 0.5 * ndc.y) * depthInfo.w);
	return LOADD(p);
}

// densidade (coeficiente de extincao, 1/m) e fracao de fuligem num ponto da grade (uvw 0..1)
float2 extinction(float3 uvw)
{
	float4 a = lerp(SA0(uvw), SA1(uvw), grid.w);
	float ss = a.r * a.r * scales.x * KS;
	float sd = a.g * a.g * scales.y * KD;
	if (ss < SH1) ss = ss <= SH0 ? 0 : ss * smoothstep(0, 1, (ss - SH0) / (SH1 - SH0));
	return float2((ss + sd) * scales.w, ss / max(ss + sd, 1e-9));
}

// sombra propria pelo sol real: profundidade optica de cada celula ate sair da grade na direcao do sol
[numthreads(4, 4, 4)]
void cs_sun(uint3 id : SV_DispatchThreadID)
{
	uint3 n; OutTau.GetDimensions(n.x, n.y, n.z);
	if (any(id >= n)) return;
	float3 cell = grid.xyz / float3(n);
	float3 p = (float3(id) + 0.5) * cell;
	float stepm = min(cell.x, min(cell.y, cell.z)) * 1.0;
	float3 d = normalize(sunDir.xyz);
	float tau = 0;
	p += d * stepm * 0.5;
	[loop]
	for (int k = 0; k < 220; k++)
	{
		if (any(p < 0) || any(p > grid.xyz) || tau > 60) break;
		tau += extinction(p / grid.xyz).x * stepm;
		p += d * stepm;
	}
	OutTau[id] = tau;
}

float4 ps_main(VSOut i) : SV_Target
{
	if (scales2.w > 1.5)	// depuracao 2: mostra a profundidade linear (perto = claro), para conferir o buffer escolhido
	{
		float z = viewDepthAt(i.pos.xy);
		float g = z > 1e8 ? 0.0 : saturate(1.0 - log(1.0 + z) / log(1.0 + 20000.0));
		return float4(g, g * 0.8, g * 0.6, 0.0);
	}
	float3 dir = camF.xyz + i.ndc.x * camF.w * camR.xyz + i.ndc.y * camR.w * camU.xyz;	// componente frontal = 1: t = profundidade
	float dl = length(dir);
	float zmax = viewDepthAt(i.pos.xy);
	if (nearInfo.x > 0.5)	// cabine: qualquer coisa muito perto da camera esconde a explosao
	{
		int2 ipn = int2(i.pos.xy * screen.zw * depthInfo.zw);
		float dn = LOADN(ipn);
		if (depthInfo.y > 0.5 ? dn > nearInfo.y : dn < nearInfo.y) discard;
	}

	// intersecao com a caixa da grade [0, grid], aumentada para onde o vento levou a fumaca
	float2 wo = wind.xy * wind.z;
	float3 lo = float3(min(0, wo), 0), hi = float3(grid.xy + max(0, wo), grid.z);
	float3 inv = 1.0 / (abs(dir) > 1e-7 ? dir : 1e-7);
	float3 ta = (lo - camO.xyz) * inv, tb = (hi - camO.xyz) * inv;
	float3 tmn = min(ta, tb), tmx = max(ta, tb);
	float t0 = max(max(max(tmn.x, tmn.y), tmn.z), 0.0);
	float t1 = min(min(tmx.x, tmx.y), tmx.z);
	t1 = min(t1, zmax);
	if (t1 <= t0) discard;

	float dt = camO.w / dl;
	float jit = frac(sin(dot(i.pos.xy, float2(12.9898, 78.233))) * 43758.5453);
	float t = t0 + dt * jit;
	float w = grid.w;
	float lt = log(1.0 + scales2.x), lf = log(1.0 + scales2.y);

	float tr = 1.0;
	float3 col = 0;
	int nmax = (int)misc.x;
	[loop]
	for (int k = 0; k < nmax && t < t1 && tr > 0.004; k++)
	{
		float3 pq = camO.xyz + dir * t;
		pq.xy -= windOffset(pq.z);
		float3 uvw = pq / grid.xyz;
		if (any(uvw < 0) || any(uvw > 1)) { t += dt * 1.6; continue; }
		float4 a = lerp(SA0(uvw), SA1(uvw), w);
		float ss = a.r * a.r * scales.x * KS;
		float sd = a.g * a.g * scales.y * KD;
		if (ss < SH1) ss = ss <= SH0 ? 0 : ss * smoothstep(0, 1, (ss - SH0) / (SH1 - SH0));
		float s = (ss + sd) * scales.w;
		if (s < 2e-4) { t += dt * 1.6; continue; }

		// detalhe: ruido de "couve-flor" subindo devagar; corroi mais as bordas finas do que o miolo
		float3 pw = pq;
		float3 drift = float3(0.0, 0.0, -detail.z * 0.9) + detail2.w;
		float n1 = Noise.SampleLevel(LinWrap, (pw + drift) / detail.y, 0);
		float n2 = Noise.SampleLevel(LinWrap, (pw + drift * 1.7) / (detail.y * 0.38), 0);
		float nn = n1 * 0.65 + n2 * 0.35;
		float low = saturate(1.0 - s / 0.35);
		s *= saturate(1.0 + (nn - 0.5) * 2.0 * detail.x * (0.5 + 1.5 * low));
		// sem corte reto nas paredes da caixa: desvanece perto das faces (a de baixo so se nao houver chao)
		float3 q = uvw;
		float ef = min(min(min(q.x, 1 - q.x), min(q.y, 1 - q.y)), min(1 - q.z, detail2.z > 0.5 ? 1.0 : q.z));
		s *= smoothstep(0.0, detail2.y, ef);
		if (s < 2e-4) { t += dt; continue; }
		ss *= s / max((ss + sd) * scales.w, 1e-9);
		sd = s / max(scales.w, 1e-6) - ss;

		float4 b = lerp(SB0(uvw), SB1(uvw), w);
		float tauS = sunDir.w > 0.5 ? SunTau.SampleLevel(LinClamp, uvw, 0) : exp(b.r * lt) - 1.0;
		float tauU = exp(b.g * lt) - 1.0, tauD = exp(b.b * lt) - 1.0;
		float Lf = exp(b.a * lf) - 1.0;
		float temp = 300.0 + 3000.0 * a.b;
		temp *= 0.86 + 0.28 * nn;									// regioes quentes e frias dentro da bola de fogo
		float fl = a.a * a.a * scales.z * pow(saturate(0.2 + 1.5 * n2), 1.5);	// linguas de chama, nao bolhas lisas
		// v1.0b (cores do video real): 0-0,4 s bola amarelo-branca curta; depois o fogo esfria para vermelho-escuro
		// e fica escondido na fumaca, so aparecendo pelos buracos
		{
			float tt = shock2.y, rz = shock2.w;
			float early = 1.0 - saturate(tt / 0.4);
			float late = saturate((tt - 0.4) / 1.2);
			temp *= 1.0 + rz * (0.30 * early - 0.16 * late);
			fl *= 1.0 + rz * (0.6 * early - 0.6 * saturate((tt - 0.4) / 1.5));
		}

		float ws = ss / (ss + sd), wd = 1.0 - ws;
		float3 alb = ws * lerp(ALB_S, float3(0.015, 0.017, 0.024), shock2.w) + wd * lerp(ALB_D, float3(0.50, 0.48, 0.45), shock2.w);	// fuligem mais preta/azulada, poeira mais clara
		float am = (alb.r + alb.g + alb.b) / 3.0;
		float sunv = exp(-tauS) + am * (0.55 * exp(-0.28 * tauS) + 0.25 * am * exp(-0.07 * tauS));
		float skyv = exp(-0.45 * tauU) * 0.85 + 0.15 * exp(-0.1 * tauU);
		float gv = exp(-0.45 * tauD);
		float3 S = alb * (sunCol.rgb * sunv + skyCol.rgb * skyv + GND_RGB * gv * skyCol.w + Lf * FIRE_GB * shock2.z);
		if (temp > 750.0) S += (1.0 - am) * EM * detail.w * planck3(temp);

		float stepm = dt * dl;
		float wgt = 1.0 - exp(-s * stepm);
		col += tr * wgt * S;
		if (fl > 1e-4) col += FLK * detail.w * fl * stepm * tr * planck3(max(temp, detail2.x));
		tr *= 1.0 - wgt;
		t += dt;
	}

	// cor "reta" (sem pre-multiplicar pela cobertura) antes da curva e da gama: senao as bordas finas
	// ganham brilho falso (halo) e a fumaca fica leitosa
	float cov = 1.0 - tr;
	if (cov < 1e-4 && scales2.w < 0.5) discard;
	float3 ce = col * scales2.z;
	float cv = max(max(cov, 1e-4), saturate(max(ce.r, max(ce.g, ce.b)) * 0.5));	// chama sem fumaca continua brilhando
	float3 c = tonemap(ce / cv);
	// nevoa atmosferica: o que esta longe fica com a cor do horizonte, como o resto da cena
	float dist = (t0 + 0.5 * (min(t, t1) - t0)) * dl;
	float fa = 1.0 - exp(-dist / max(fog.w, 1.0));
	c = lerp(c, fog.rgb, fa) * cv;
	if (scales2.w > 0.5) { c += float3(0.25, 0, 0) * (1.0 - c); tr *= 0.85; }	// depuracao: caixa visivel
	return float4(c, tr);	// mistura: destino = fonte + destino * alfa
}


// ---------------------------------------------------------------- onda de choque
// Casca esferica (hemisferio no chao) que cresce na velocidade do som. O ar comprimido desvia a luz: quem olha
// rente a casca ve o fundo "dobrado" num anel. Logo no inicio, a JDAM e a Mk-82 fazem a nuvem de Wilson
// (condensacao branca dentro da casca). camO = camera relativa ao centro, eixos (norte, leste, cima).
float4 ps_shock(VSOut i) : SV_Target
{
	float3 dir = camF.xyz + i.ndc.x * camF.w * camR.xyz + i.ndc.y * camR.w * camU.xyz;
	float dl = length(dir);
	float3 d = dir / dl;
	float3 o = camO.xyz;
	float r = shock.x, th = shock.y;
	float tc = -dot(o, d);
	float b = length(o + d * tc);
	if (b > r + th) discard;
	float h = sqrt(max(r * r - b * b, 0));
	float tS = b < r ? tc - h : tc;
	if (tS < 0) tS = tc + h;
	if (tS < 0) discard;
	float zS = tS / dl;
	if (zS > viewDepthAt(i.pos.xy)) discard;			// casca atras do terreno/objeto

	// direcao radial na tela, a partir do centro projetado
	float3 c = -o;
	float zc = dot(c, camF.xyz);
	float2 pix = i.pos.xy;
	float2 cpix = screen.xy * 0.5;
	if (zc > 0.1)
	{
		float2 nc = float2(dot(c, camR.xyz) / (zc * camF.w), dot(c, camU.xyz) / (zc * camR.w));
		cpix = float2((nc.x * 0.5 + 0.5) * screen.x, (0.5 - nc.y * 0.5) * screen.y);
	}
	float2 rad = pix - cpix;
	rad = length(rad) > 1e-3 ? normalize(rad) : float2(1, 0);

	float x = saturate((b - (r - th)) / (2.0 * th));			// 0 dentro .. 1 fora do anel
	float g = sin(3.14159 * x) * (b > r - th ? 1.0 : 0.0);
	float amp = shock.z * shock2.x / max(zS, 1.0);			// pixels
	float2 uv = (pix - rad * g * amp) * screen.zw;
	float3 col = SceneTex.SampleLevel(LinClamp, uv, 0).rgb;
	col *= 1.0 + 0.04 * g * saturate(amp / 4.0);			// brilho leve do ar comprimido

	if (shock.w > 0 && b < r)
	{
		float path = 2.0 * h;
		float a = shock.w * saturate(path / (1.2 * r)) * 0.75;
		col = lerp(col, float3(0.90, 0.91, 0.93), a);
	}
	if (g <= 0.0 && !(shock.w > 0 && b < r)) discard;
	return float4(col, 1);
}

// ---------------------------------------------------------------- destrocos com rastro de fumaca
// Fragmentos lancados para cima e para os lados (balistica com arrasto), cada um deixando um rastro de fumaca que
// engrossa e some com a idade; a ponta brilha nos primeiros segundos. Espaco local: x norte, y leste, z cima,
// origem no ponto da explosao (no chao). Volume proprio, calculado por um compute shader a cada quadro.
cbuffer TR : register(b1)
{
	float4 tv[16];		// xyz: velocidade inicial (m/s), w: arrasto (1/s)
	float4 tp[16];		// x: atraso (s), y: tamanho, z: calor da ponta, w: tempo ate tocar o chao (s)
	float4 tinfo;		// x: tempo desde a detonacao (s), y: numero de destrocos, z: meia largura da caixa (m), w: altura da caixa (m)
	float4 tinfo2;		// xy: vento no mundo (m/s, norte/leste), z: densidade, w: brilho
};
RWTexture3D<float2> OutTrail : register(u1);
Texture3D<float2> TrailTex : register(t9);

float3 trailPos(int i, float tau)
{
	float k = max(tv[i].w, 0.02);
	float3 vinf = float3(0, 0, -9.81 / k);
	return float3(0, 0, 2.0) + vinf * tau + (tv[i].xyz - vinf) * (1.0 - exp(-k * tau)) / k;
}

[numthreads(4, 4, 4)]
void cs_trail(uint3 id : SV_DispatchThreadID)
{
	uint3 n; OutTrail.GetDimensions(n.x, n.y, n.z);
	if (any(id >= n)) return;
	float3 lo = float3(-tinfo.z, -tinfo.z, 0), hi = float3(tinfo.z, tinfo.z, tinfo.w);
	float3 p = lo + (float3(id) + 0.5) / float3(n) * (hi - lo);
	float dens = 0, glow = 0;
	int cnt = (int)tinfo.y;
	[loop]
	for (int i = 0; i < cnt; i++)
	{
		float tl = tinfo.x - tp[i].x;				// tempo de voo deste destroco
		if (tl <= 0) continue;
		float tend = min(tl, tp[i].w);
		const int M = 12;
		float3 A = trailPos(i, 0) + float3(tinfo2.xy * tl, 0);
		[loop]
		for (int s = 1; s <= M; s++)
		{
			float tb = tend * s / M;
			float3 B = trailPos(i, tb) + float3(tinfo2.xy * (tl - tb), 0);	// fumaca mais velha ja andou com o vento
			float3 ab = B - A;
			float u = saturate(dot(p - A, ab) / max(dot(ab, ab), 1e-4));
			float d = length(p - (A + ab * u));
			float tau = tend * (s - 1 + u) / M;
			float age = tl - tau;
			float r = min(0.7 * tp[i].y + 0.55 * age * tp[i].y, 8.0);
			if (d < 2.5 * r)
				dens += tp[i].y * exp(-age / 6.0) * exp(-(d * d) / (r * r)) / (1.0 + 0.25 * age);
			A = B;
		}
		if (tl < tp[i].w)	// ponta ainda voando: brilho do fragmento quente
		{
			float3 H = trailPos(i, tl);
			float dh = length(p - H);
			glow += tp[i].z * exp(-tl / 1.3) * exp(-(dh * dh) / 6.0);
		}
	}
	OutTrail[id] = float2(dens * tinfo2.z, glow * tinfo2.w);
}

float4 ps_trail(VSOut i) : SV_Target
{
	float3 dir = camF.xyz + i.ndc.x * camF.w * camR.xyz + i.ndc.y * camR.w * camU.xyz;
	float dl = length(dir);
	float zmax = viewDepthAt(i.pos.xy);
	float3 lo = float3(-tinfo.z, -tinfo.z, 0), hi = float3(tinfo.z, tinfo.z, tinfo.w);
	float3 inv = 1.0 / (abs(dir) > 1e-7 ? dir : 1e-7);
	float3 ta = (lo - camO.xyz) * inv, tb = (hi - camO.xyz) * inv;
	float3 tmn = min(ta, tb), tmx = max(ta, tb);
	float t0 = max(max(max(tmn.x, tmn.y), tmn.z), 0.0);
	float t1 = min(min(min(tmx.x, tmx.y), tmx.z), zmax);
	if (t1 <= t0) discard;
	float stepm = 2.0;
	float dt = stepm / dl;
	float jit = frac(sin(dot(i.pos.xy, float2(12.9898, 78.233))) * 43758.5453);
	float t = t0 + dt * jit;
	float tr = 1.0; float3 col = 0;
	static const float3 ALB_T = float3(0.30, 0.28, 0.26);
	static const float3 GLOW_T = float3(1.0, 0.40, 0.09);
	[loop]
	for (int k = 0; k < 320 && t < t1 && tr > 0.01; k++)
	{
		float3 p = camO.xyz + dir * t;
		float2 v = TrailTex.SampleLevel(LinClamp, (p - lo) / (hi - lo), 0);
		if (v.x + v.y < 1e-4) { t += dt * 2.0; continue; }
		float3 pn = p + float3(0, 0, -detail.z * 0.6);
		float nn = Noise.SampleLevel(LinWrap, pn / 7.0, 0);
		float s = v.x * (0.4 + 1.2 * nn) * scales.w;
		float w = 1.0 - exp(-s * stepm);
		float3 S = ALB_T * (sunCol.rgb * 0.6 + skyCol.rgb * 0.9);
		col += tr * w * S;
		col += tr * GLOW_T * v.y * stepm * 6.0;
		tr *= 1.0 - w;
		t += dt;
	}
	float cov = 1.0 - tr;
	float3 ce = col * scales2.z;
	float cv = max(max(cov, 1e-4), saturate(max(ce.r, max(ce.g, ce.b)) * 0.5));
	if (cv < 2e-3) discard;
	float3 c = tonemap(ce / cv);
	float dist = (t0 + 0.5 * (min(t, t1) - t0)) * dl;
	float fa = 1.0 - exp(-dist / max(fog.w, 1.0));
	c = lerp(c, fog.rgb, fa) * cv;
	return float4(c, tr);
}
