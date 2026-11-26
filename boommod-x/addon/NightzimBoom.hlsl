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

	// passo por raio: o fino de sempre, e so cresce quando ESTE raio nao cabe nos passos disponiveis (antes o passo crescia
	// pela diagonal da caixa inteira: na poeira nuclear, larga e baixa, isso dava passos de ~30 m e listras horizontais)
	int nmx = max((int)misc.x - 4, 16);
	float dt = max(camO.w / dl, (t1 - t0) / nmx);
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
		if (grid.z > 900.0)	// nuvem nuclear (so ela tem caixa com mais de 900 m de altura): terra, fuligem e oxidos de nitrogenio -> marrom-avermelhado escuro, nao vapor branco
		{
			ws = 1.0 - wd * 0.5; wd = 1.0 - ws;
			float hz = saturate(uvw.z * 1.6);								// tronco e base mais avermelhados, capuz mais cinza-marrom
			float3 dN = lerp(float3(0.34, 0.20, 0.13), float3(0.27, 0.22, 0.19), hz);
			alb = ws * float3(0.040, 0.032, 0.028) + wd * dN;
		}
		else if (grid.x > 5000.0)	// saia de poeira da onda de choque nuclear (caixa larga e baixa): terra seca levantada, ocre-acinzentado
		{
			float hz = saturate(uvw.z * 2.5);
			alb = lerp(float3(0.42, 0.33, 0.24), float3(0.36, 0.31, 0.26), hz) * (0.85 + 0.15 * ws);
		}
		float am = (alb.r + alb.g + alb.b) / 3.0;
		float sunv = exp(-tauS) + am * (0.55 * exp(-0.28 * tauS) + 0.25 * am * exp(-0.07 * tauS));
		float skyv = exp(-0.45 * tauU) * 0.85 + 0.15 * exp(-0.1 * tauU);
		float gv = exp(-0.45 * tauD);
		float3 S = alb * (sunCol.rgb * sunv + skyCol.rgb * skyv + GND_RGB * gv * skyCol.w + Lf * FIRE_GB * shock2.z);
		if (temp > 750.0) S += (1.0 - am) * EM * detail.w * planck3(temp);

		float stepm = dt * dl;
		float wgt = 1.0 - exp(-s * stepm);
		col += tr * wgt * S;
		if (fl > 1e-4) col += FLK * detail.w * fl * min(1.0, scales.w) * stepm * tr * planck3(max(temp, detail2.x));	// a chama acompanha a opacidade (laco cruzado do incendio da queda)
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


// ---------------------------------------------------------------- clarao nuclear (tela inteira)
// a: x = clarao (mistura para o branco amarelado), y = vista "queimada" se readaptando, z = brilho da bola de fogo
//    (halo, raios e risco horizontal da lente), w = tempo desde a detonacao (s)
// b: xy = posicao da detonacao na tela (NDC), z = 1 se esta na frente da camera, w = escala do brilho (perto = maior)
// c: xy = tamanho da tela em tangente (para o halo ficar redondo em qualquer proporcao e no VR)
cbuffer FL : register(b3) { float4 fa; float4 fb; float4 fc; };
float fl_hash(float n) { return frac(sin(n) * 43758.5453); }
float fl_rays(float ang, float t)
{
	// raios irregulares saindo da bola de fogo: soma de faixas com largura e brilho sorteados, girando bem devagar
	float s = 0;
	[unroll] for (int k = 0; k < 3; k++)
	{
		float n = 9.0 + 14.0 * k;
		float a = ang * n / 6.2831853 + t * (0.010 + 0.006 * k) + 17.3 * k;
		float id = floor(a), fr = frac(a) - 0.5;
		float w = 0.08 + 0.22 * fl_hash(id + 31.7 * k);
		s += (0.35 + 0.65 * fl_hash(id * 1.37 + k)) * exp(-fr * fr / (w * w * 0.08));
	}
	return s / 3.0;
}
float4 ps_flash(VSOut i) : SV_Target
{
	float f = saturate(fa.x), d = saturate(fa.y), g = max(fa.z, 0.0), t = fa.w;
	float3 white = lerp(float3(1.0, 0.84, 0.52), float3(1.0, 0.95, 0.76), f);	// branco amarelado no pico, mais quente enquanto apaga
	float3 dark = float3(0.010, 0.011, 0.016);

	float3 L = 0;
	if (g > 0.001)
	{
		float sz = fb.w;
		float2 p = (i.ndc - fb.xy) * fc.xy / sz;	// distancia em tangente, normalizada pelo tamanho do brilho
		float r = length(p);
		float front = fb.z;
		float core = exp(-r * r / 0.0035);						// nucleo cegante
		float halo = 0.55 / (1.0 + r * r / 0.012) + 0.22 / (1.0 + r * r / 0.12);	// brilho que se espalha (bloom)
		// os raios giram quando a explosao corre pela tela (mexendo a cabeca), como os da lente
		float spin = atan2(fb.y * fc.y, fb.x * fc.x + 1e-4) * 0.55 + (fb.x * fc.x - fb.y * fc.y) * 0.35;
		float ang = atan2(p.y, p.x) + spin;
		float rays = fl_rays(ang, t) * 0.9 / (1.0 + r * 3.5) * smoothstep(0.0, 0.05, r);
		float streak = exp(-abs(p.y) / 0.010) * exp(-abs(p.x) / 0.9) * 0.9;	// risco horizontal da lente (anamorfico)
		float3 ring = float3(exp(-pow((r - 0.47) / 0.030, 2)), exp(-pow((r - 0.45) / 0.030, 2)), exp(-pow((r - 0.43) / 0.030, 2))) * 0.16;	// anel colorido da lente
		float3 hot = float3(1.0, 0.93, 0.78), warm = float3(1.0, 0.55, 0.22);
		L = front * g * (lerp(warm, hot, saturate(core * 1.5)) * (core * 2.0 + halo) + warm * rays + lerp(warm, hot, 0.5) * streak + ring);
		// reflexos da lente: discos coloridos na linha que liga a explosao ao centro da tela, do outro lado do centro
		float2 sp = fb.xy * fc.xy;
		float edge = saturate(1.0 - length(sp) * 0.35);
		static const float GS[6] = {0.42, -0.22, -0.55, -0.85, -1.25, -1.7};
		static const float GR[6] = {0.030, 0.055, 0.020, 0.090, 0.040, 0.140};
		static const float3 GC[6] = {float3(1.0, 0.75, 0.40), float3(0.45, 0.85, 0.55), float3(1.0, 0.45, 0.25), float3(0.40, 0.60, 1.00), float3(0.95, 0.85, 0.40), float3(0.70, 0.45, 0.95)};
		float2 pp = i.ndc * fc.xy;
		[unroll] for (int k = 0; k < 6; k++)
		{
			float dg = length(pp - sp * GS[k]) / (GR[k] * sz);
			float disc = saturate((1.0 - dg) * 6.0) * (0.35 + 0.65 * dg * dg);	// disco com borda mais clara
			L += front * edge * g * GC[k] * disc * 0.24;
		}
		L += g * float3(1.0, 0.52, 0.26) * 0.20;	// neblina laranja na tela toda (luz da bola de fogo espalhada no ar)
	}
	float a = (1.0 - f) * (1.0 - d) * (1.0 - saturate(g * 0.18));
	// mistura com o blend do mod (cor + destino * alfa)
	return float4(white * f + (dark * d + L) * (1.0 - f), a);
}

// ---------------------------------------------------------------- onda de choque nuclear
// Desenhada por cima da cena inteira (copia do quadro em SceneTex). Espaco: origem no ponto da explosao (no chao),
// x norte, y leste, z cima, sem o giro da grade. Tres partes: a casca que distorce o ar (e a nuvem de condensacao
// no comeco), o muro de poeira que corre pelo chao atras da frente, e o tranco quando a frente passa pela camera.
cbuffer NS : register(b4)
{
	float4 nsA;		// x: raio da frente (m), y: espessura (m), z: refracao (m), w: tempo desde a detonacao (s)
	float4 nsB;		// x: largura do muro de poeira (m), y: altura do muro (m), z: densidade (1/m), w: nuvem de condensacao (0..1)
	float4 nsC;		// xy: tremida da tela (pixels), z: pulso de distorcao na tela (0..1), w: distancia focal (pixels)
	float4 nsD;		// xyz: direcao do sol (mesmo espaco), w: luz do dia (0..1)
	float4 nsE;		// rgb: luz do sol, w: semente
	float4 nsF;		// rgb: luz do ceu, w: poeira deixada para tras (0..1)
};
float ns_noise(float3 p)
{
	return Noise.SampleLevel(LinWrap, p, 0);
}

// trecho [entrada, saida] do raio dentro do cilindro vertical de raio Rc centrado na origem (x > y = nao cruza)
float2 cyl_seg(float3 o, float3 d, float Rc)
{
	float a = dot(d.xy, d.xy), bq = 2.0 * dot(o.xy, d.xy), cq = dot(o.xy, o.xy) - Rc * Rc;
	float disc = bq * bq - 4.0 * a * cq;
	if (disc <= 0.0 || a < 1e-8) return float2(1, 0);
	float sq = sqrt(disc);
	return float2((-bq - sq) / (2.0 * a), (-bq + sq) / (2.0 * a));
}
void dust_march(float3 o, float3 d, float t0, float t1, int N, float j, float t, float Hm, inout float T, inout float3 acc)
{
	if (t1 <= t0 || T < 0.02) return;
	float R = nsA.x, wd = nsB.x;
	float dt = (t1 - t0) / N;
	float3 sunD = nsD.xyz;
	float3 Ld = nsE.rgb * (0.35 + 0.65 * saturate(sunD.z * 3.0)) * 0.55 + nsF.rgb * 0.75;
	float3 alb = float3(0.62, 0.53, 0.42);
	[loop] for (int k = 0; k < N; k++)
	{
		float s = t0 + (k + j) * dt;
		float3 p = o + d * s;
		float rr = length(p.xy);
		float behind = R - rr;									// > 0 = dentro da frente
		if (behind < -30.0 || rr < 180.0) continue;
		float edge = saturate((30.0 + min(behind, 0.0)) / 30.0);	// borda externa suave
		float wall = exp(-max(behind, 0.0) / wd);					// mais denso logo atras da frente
		float rest = nsF.w * saturate(behind / (2.0 * wd)) * saturate((7.0 * wd - behind) / (2.0 * wd));	// poeira que fica no ar
		float Hh = Hm * (0.35 + 0.65 * wall) + Hm * 0.5 * rest;
		float vz = saturate(1.0 - p.z / max(Hh, 1.0)); vz *= vz;
		float n = ns_noise(float3(p.xy * 0.0035, p.z * 0.006 - t * 0.015) + nsE.w) * 0.65
		        + ns_noise(float3(p.xy * 0.013, p.z * 0.02 + t * 0.03)) * 0.35;
		float dens = nsB.z * edge * (wall + 0.35 * rest) * vz * saturate(n * 2.0 - 0.45);
		if (dens <= 0.0) continue;
		float sh = 0.55 + 0.45 * saturate(p.z / max(Hh, 1.0));		// base mais escura (sombra propria)
		float a2 = 1.0 - exp(-dens * dt);
		acc += T * a2 * alb * Ld * sh;
		T *= 1.0 - a2;
		if (T < 0.02) break;
	}
}
float4 ps_nshock(VSOut i) : SV_Target
{
	float2 pix = i.pos.xy;
	float2 shake = nsC.xy;
	float3 dir = camF.xyz + i.ndc.x * camF.w * camR.xyz + i.ndc.y * camR.w * camU.xyz;
	float dl = length(dir);
	float3 d = dir / dl;
	float3 o = camO.xyz;
	float zmax = viewDepthAt(pix);					// profundidade da cena neste pixel (m, ao longo da frente)
	float tmax = zmax * dl;							// distancia ao longo do raio

	float R = nsA.x, th = nsA.y, t = nsA.w;
	float2 off = shake;

	// --- casca da frente: distorce o que esta atras dela (so acima do chao)
	float tc = -dot(o, d);
	float b = length(o + d * tc);
	float shellA = 0.0, wil = 0.0;
	if (R > 1.0 && b < R + th)
	{
		float h = sqrt(max(R * R - b * b, 0));
		float tS = b < R ? tc - h : tc;
		if (tS < 0) tS = tc + h;
		float3 pS = o + d * tS;
		if (tS > 0 && tS < tmax && pS.z > -5.0)
		{
			float x = saturate((b - (R - th)) / (2.0 * th));
			float g = sin(3.14159 * x) * (b > R - th ? 1.0 : 0.0);
			float zS = tS / dl;
			float c0 = dot(-o, camF.xyz);
			float2 cpix = screen.xy * 0.5;
			if (c0 > 0.1)
			{
				float2 nc = float2(dot(-o, camR.xyz) / (c0 * camF.w), dot(-o, camU.xyz) / (c0 * camR.w));
				cpix = float2((nc.x * 0.5 + 0.5) * screen.x, (0.5 - nc.y * 0.5) * screen.y);
			}
			float2 rad = pix - cpix; rad = length(rad) > 1e-3 ? normalize(rad) : float2(1, 0);
			off -= rad * g * nsA.z * nsC.w / max(zS, 1.0);
			shellA = g;
			if (nsB.w > 0.0 && b < R)	// nuvem de condensacao (Wilson): casca branca com textura
			{
				float n = ns_noise(pS * 0.0025 + float3(0.17, 0.31, t * 0.01)) * 0.7 + ns_noise(pS * 0.009) * 0.3;
				wil = nsB.w * saturate(2.0 * h / (1.1 * R)) * saturate(n * 1.6 - 0.25) * saturate(pS.z / 60.0);
			}
		}
	}
	// --- pulso na tela inteira quando a frente passa pela camera
	if (nsC.z > 0.0)
	{
		float2 q = (pix * screen.zw - 0.5);
		off += q * nsC.z * 22.0 * (0.6 + 0.4 * sin(t * 90.0));
	}

	float3 col = SceneTex.SampleLevel(LinClamp, (pix + off) * screen.zw, 0).rgb;
	col *= 1.0 + 0.05 * shellA + 0.25 * nsC.z;
	col = lerp(col, float3(0.92, 0.93, 0.95) * (0.55 + 0.45 * nsD.w), wil * 0.8);

	// --- muro de poeira: anel baixo que corre atras da frente e deixa poeira no ar (so no anel perto da frente)
	float Hm = nsB.y;
	if (nsB.z > 0.0 && R > 150.0)
	{
		float Rc = R + 30.0, Rin = max(R - 7.0 * nsB.x, 150.0), Hc = 2.2 * Hm;
		float2 so = cyl_seg(o, d, Rc), si = cyl_seg(o, d, Rin);
		// limite de altura
		float z0 = -1e9, z1 = 1e9;
		if (abs(d.z) > 1e-5) { float a0 = (-2.0 - o.z) / d.z, a1 = (Hc - o.z) / d.z; z0 = min(a0, a1); z1 = max(a0, a1); }
		else if (o.z > Hc || o.z < -2.0) { z0 = 1; z1 = 0; }
		so.x = max(so.x, max(z0, 0.0)); so.y = min(min(so.y, z1), tmax);
		float T = 1.0; float3 acc = 0;
		if (so.y > so.x)
		{
			float j = frac(sin(dot(pix, float2(12.9898, 78.233))) * 43758.5453);
			if (si.y > si.x && si.y > so.x && si.x < so.y)
			{	// o raio cruza o miolo (sem poeira): marcha antes e depois dele
				dust_march(o, d, so.x, min(si.x, so.y), 20, j, t, Hm, T, acc);
				dust_march(o, d, max(si.y, so.x), so.y, 20, j, t, Hm, T, acc);
			}
			else dust_march(o, d, so.x, so.y, 40, j, t, Hm, T, acc);
		}
		col = col * T + acc;
	}
	return float4(col, 1);
}

// ---------------------------------------------------------------- rastro de fumaca de missil (SCUD)
// Pontos relativos a camera (x norte, y leste, z cima): plP[2i] = posicao, raio; plP[2i+1] = densidade, -, idade, inicio.
// Passo 1: cada trecho entre dois pontos e um tubo; acha os ate 8 trechos que o raio de visao cruza e o intervalo do raio
// dentro deles. Passo 2: marcha 32 passos so nesse intervalo; a densidade e a uniao (maximo) dos tubos, roida nas bordas
// por ruido 3D preso no mundo (rolos), com sombra do sol (2 amostras), luz do ceu por cima e a luz da chama no rastro novo.
// A chama e um segmento brilhante atras da ponta (plR = ponta, plF = direcao para tras e comprimento).
cbuffer PL : register(b5)
{
	float4 plA;		// x: numero de pontos, y: luz do dia (0..1), z: tempo (s), w: ganho de densidade
	float4 plS;		// xyz: direcao do sol
	float4 plL;		// rgb: luz do sol
	float4 plK;		// rgb: luz do ceu
	float4 plO;		// xyz: posicao da camera no mundo (modulo 4096, para o ruido ficar preso no mundo)
	float4 plR;		// xyz: ponta do missil (relativa a camera), w: chama (0..1)
	float4 plF;		// xyz: direcao para tras, w: comprimento da chama (m)
	float4 plP[640];
};
#define PL_K 8
float pl_seg(float3 x, int k, out float r, out float rho, out float age, out float vz)
{
	float4 A = plP[2 * k - 2], Ai = plP[2 * k - 1], B = plP[2 * k], Bi = plP[2 * k + 1];
	float3 u = B.xyz - A.xyz;
	float tu = dot(x - A.xyz, u) / max(dot(u, u), 1e-6);
	float t = saturate(tu);
	float3 q = A.xyz + u * t;
	r = lerp(A.w, B.w, t); rho = lerp(Ai.x, Bi.x, t); age = lerp(Ai.z, Bi.z, t);
	bool last = (k == (int)plA.x - 1) || (plP[2 * k + 3].w > 0.5);
	if (last && tu > 1.0) rho *= saturate(1.0 - (tu - 1.0) * length(u) / (0.4 * r));	// nada de bola de fumaca na frente do missil
	float3 o = x - q;
	vz = clamp(o.z / (1.5 * r), -1.0, 1.0);
	return length(o);
}
float pl_dens(float3 x0, int cand[PL_K], int nc, out float age, out float vz, out float rnear)
{
	// serpenteia: desloca o ponto por um ruido de baixa frequencia (o tubo deixa de ser um cone liso)
	float3 pw0 = plO.xyz + x0;
	float3 wv = float3(Noise.SampleLevel(LinWrap, pw0 * 0.0021 + 0.11, 0), Noise.SampleLevel(LinWrap, pw0 * 0.0021 + 0.47, 0), Noise.SampleLevel(LinWrap, pw0 * 0.0021 + 0.83, 0)) - 0.5;
	float r0_, rh_, ag_, vz_; pl_seg(x0, cand[0], r0_, rh_, ag_, vz_);
	float3 x = x0 + wv * (0.8 * r0_ * saturate(ag_ / 3.0));	// o rastro recem-saido do bocal nao serpenteia
	float best = 0.0; age = 0.0; vz = 0.0; rnear = 10.0;
	float dmin = 1e9;
	float edges[PL_K]; float rhos[PL_K];
	[unroll] for (int j = 0; j < PL_K; j++)
	{
		edges[j] = 9.0; rhos[j] = 0.0;
		if (j < nc)
		{
			float r, rho, ag, v;
			float dist = pl_seg(x, cand[j], r, rho, ag, v);
			edges[j] = dist / (1.5 * r); rhos[j] = rho;
			if (edges[j] < dmin) { dmin = edges[j]; age = ag; vz = v; rnear = r; }
		}
	}
	if (dmin > 1.25) return 0.0;
	float form = plR.w > 0.0 ? saturate((length(x0 - plR.xyz) - 8.0) / 16.0) : 1.0;	// a fumaca vai se formando de ~8 a ~24 m atras do bocal (sem corte reto)
	if (form <= 0.0) return 0.0;
	float3 pw = plO.xyz + x;
	// ruido em escalas fixas do mundo (nao depende do raio do trecho mais proximo: sem faixas nas juntas dos trechos)
	float3 uvw = pw / 160.0 + float3(0.0, 0.0, -plA.z * 0.002);
	float fb = Noise.SampleLevel(LinWrap, uvw, 0), fm = Noise.SampleLevel(LinWrap, uvw * 3.1 + 0.37, 0), fs = Noise.SampleLevel(LinWrap, uvw * 9.7 + 0.71, 0);
	float wbig = saturate(rnear / 40.0);			// rastro grosso: lombos grandes; rastro fino: so o detalhe
	float f = lerp(fm * 0.7 + fs * 0.3, fb * 0.6 + fm * 0.3 + fs * 0.1, wbig);
	[unroll] for (int j2 = 0; j2 < PL_K; j2++)
	{
		float e = edges[j2];
		float dd = saturate((f * 1.6 - e * 0.9 + 0.15) * 2.5) * saturate(1.25 - e) * rhos[j2];
		best = max(best, dd);
	}
	return best * form;
}
float4 ps_plume(VSOut i) : SV_Target
{
	float2 pix = i.pos.xy;
	float3 dir = camF.xyz + i.ndc.x * camF.w * camR.xyz + i.ndc.y * camR.w * camU.xyz;
	float dl = length(dir);
	float3 d = dir / dl;
	float tmax = viewDepthAt(pix) * dl;
	int n = (int)plA.x;
	int cand[PL_K]; float csc[PL_K];
	[unroll] for (int c0 = 0; c0 < PL_K; c0++) { cand[c0] = 1; csc[c0] = 9.0; }
	int nc = 0; float s0 = 1e12, s1 = 0.0;
	// passo 1: trechos candidatos
	[loop] for (int k = 1; k < n; k++)
	{
		float4 A = plP[2 * k - 2], B = plP[2 * k];
		if (plP[2 * k + 1].w > 0.5) continue;
		float3 u = B.xyz - A.xyz;
		float L = length(u);
		if (L < 1e-3) continue;
		float3 e = u / L;
		float b = dot(d, e), dW = dot(d, -A.xyz), eW = dot(e, -A.xyz);
		float den = max(1.0 - b * b, 1e-6);
		float tq = clamp((eW - b * dW) / den, 0.0, L);
		float s = max(dot(d, A.xyz + e * tq), 0.0);
		tq = clamp(dot(d * s - A.xyz, e), 0.0, L);
		float dist = length(d * s - (A.xyz + e * tq));
		float Rm = 1.7 * max(A.w, B.w);
		float sc = dist / Rm;
		if (sc >= 1.0) continue;
		float half = Rm / max(sqrt(den), 0.25);
		s0 = min(s0, s - half); s1 = max(s1, s + half);
		// guarda nos 8 mais proximos (troca o pior)
		int w = 0; float wv = csc[0];
		[unroll] for (int j = 1; j < PL_K; j++) if (csc[j] > wv) { wv = csc[j]; w = j; }
		if (sc < wv) { [unroll] for (int j3 = 0; j3 < PL_K; j3++) if (j3 == w) { csc[j3] = sc; cand[j3] = k; } nc = min(nc + 1, PL_K); }
	}
	float3 col = 0; float T = 1.0; float fogS = 0.0, fogW = 0.0;
	float3 sunD = plS.xyz;
	float day = plA.y;
	float3 Lamb = plK.rgb + 0.02;
	float3 Lsun = plL.rgb * day * 0.9;
	if (nc > 0)
	{
		// candidatos validos primeiro
		int cc[PL_K]; int m = 0;
		[unroll] for (int j = 0; j < PL_K; j++) { cc[j] = cand[j]; }
		[unroll] for (int a = 0; a < PL_K; a++) [unroll] for (int b2 = a + 1; b2 < PL_K; b2++) if (csc[b2] < csc[a]) { float tf = csc[a]; csc[a] = csc[b2]; csc[b2] = tf; int ti = cc[a]; cc[a] = cc[b2]; cc[b2] = ti; }
		[unroll] for (int j4 = 0; j4 < PL_K; j4++) if (csc[j4] < 1.0) m = j4 + 1;
		s0 = max(s0, 0.0); s1 = min(s1, tmax);
		const int NS = 32;
		float ds = (s1 - s0) / NS;
		float jit = frac(sin(dot(pix, float2(12.9898, 78.233))) * 43758.5453);
		[loop] for (int st = 0; st < NS && s1 > s0; st++)
		{
			float s = s0 + (st + jit) * ds;
			float3 x = d * s;
			float age, vz, rn;
			float rho = pl_dens(x, cc, m, age, vz, rn) * 4.0 * plA.w;
			if (rho <= 0.0) continue;
			float a1, v1, r1, a2, v2, r2;
			float sh = pl_dens(x + sunD * 0.6 * rn, cc, m, a1, v1, r1) * 0.6 + pl_dens(x + sunD * 1.6 * rn, cc, m, a2, v2, r2);
			float ts = sh * rn * 4.0 * plA.w;
			float sunl = exp(-ts * 0.8) * 0.78 + 0.22 * exp(-ts * 0.25);	// sombra propria macia (espalhamento multiplo)
			float y = saturate(age / 4.0), o = saturate(age / 90.0);
			float3 alb = lerp(float3(0.40, 0.27, 0.18), float3(0.36, 0.30, 0.23), y);	// mesma faixa de albedo da poeira (o sol do mod vale ~2,5)	// novo: avermelhado (NO2 do acido nitrico)
			alb = lerp(alb, float3(0.37, 0.35, 0.32), o);	// velho: pardo-acinzentado
			float dh = length(x - plR.xyz);
			float3 fl = float3(1.0, 0.55, 0.25) * 1.2 * exp(-dh / 20.0) * plR.w;
			float3 amb = Lamb * (0.55 + 0.45 * (0.5 + 0.5 * vz));
			float3 c = alb * (amb * 0.9 + Lsun * sunl * 0.8 + fl);
			float a = 1.0 - exp(-rho * ds);
			col += T * a * c; fogS += T * a * s; fogW += T * a; T *= 1.0 - a;
			if (T < 0.01) break;
		}
	}
	fogS = fogW > 1e-5 ? fogS / fogW : 0.0;
	// chama: cone de gas quente saindo do bocal. Emissao volumetrica integrada pelo raio (8 passos dentro do cone),
	// nucleo branco e estreito, envelope amarelo-laranja que abre e esfria, turbulencia correndo para tras e cintilacao.
	float3 glow = 0;
	if (plR.w > 0.0)
	{
		float3 A = plR.xyz, e = plF.xyz; float Ln = plF.w;
		float b = dot(d, e), dW = dot(d, -A), eW = dot(e, -A);
		float den = max(1.0 - b * b, 1e-5);
		float tq = clamp((eW - b * dW) / den, 0.0, Ln);
		float sc = max(dot(d, A + e * tq), 0.0);
		float3 Qc = A + e * tq;
		float dc = length(d * sc - Qc);
		float Rmax = 1.3 + 6.0;
		if (dc < Rmax * 2.0)
		{
			float half = (Rmax * 1.5) / max(sqrt(den), 0.12);
			float sa = max(sc - half, 0.0), sb = sc + half;
			float dsf = (sb - sa) / 10.0;
			float fl_t = plA.z;
			float flick = 0.9 + 0.1 * sin(fl_t * 61.0) * sin(fl_t * 23.0 + 1.3);
			[loop] for (int q = 0; q < 10; q++)
			{
				float s = sa + (q + 0.5) * dsf;
				if (s > tmax) break;
				float3 x = d * s;
				float tt = dot(x - A, e);
				if (tt < 0.0 || tt > Ln) continue;	// comeca exatamente no bocal
				float f = saturate(tt / Ln);
				float rr = 0.55 + 6.5 * pow(f, 0.6);	// bocal do Scud ~0,9 m de diametro					// cone que abre
				float rad = length(x - (A + e * clamp(tt, 0.0, Ln))) / rr;
				float3 pw = plO.xyz + x;
				float tn = Noise.SampleLevel(LinWrap, pw * 0.09 - e * fl_t * 3.0, 0);	// turbulencia correndo para tras
				float env = exp(-rad * rad * 1.6) * pow(1.0 - f, 0.9) * (0.45 + 1.1 * tn);
				float core = exp(-rad * rad * 9.0) * exp(-f * 6.0) * 3.0;				// nucleo branco junto ao bocal
				float hot = saturate(1.0 - f * 1.4);
				float3 ec = lerp(float3(1.0, 0.38, 0.08), float3(1.0, 0.72, 0.30), hot);	// esfria: amarelo -> laranja -> vermelho
				float outer = exp(-rad * rad * 0.7) * pow(1.0 - f, 0.8) * (0.6 + 0.8 * tn);	// envelope laranja mais largo e transparente
				glow += (ec * env * 0.50 + float3(1.0, 0.95, 0.82) * core * 0.05 + float3(1.0, 0.45, 0.15) * outer * 0.10) * dsf * flick;
			}
		}
		// halo e ponto de longe (a chama continua visivel a muitos km)
		float ang = dc / max(sc, 1.0);
		float near = saturate(1.0 - tq / (Ln * 0.4));
		float tqu = (eW - b * dW) / den;	// sem limitar: negativo = na frente do bocal (em cima do missil)
		float behind = saturate(tqu / 3.0 + 0.15);
		glow += float3(1.0, 0.8, 0.5) * (0.25 * exp(-dc / 8.0) * (0.5 + 0.5 * (1.0 - day * 0.5)) * behind + 0.9 * exp(-(ang / 0.0025) * (ang / 0.0025)) * near) * saturate((tmax - sc) / 5.0 + 0.5);
		glow *= plR.w * max(T, 0.3);
	}
	// mesma saida da fumaca das explosoes: luz linear -> exposicao -> curva de tom -> gama, e nevoa atmosferica pela distancia
	float gl = saturate(max(glow.r, max(glow.g, glow.b)) * 0.9);
	T *= 1.0 - 0.45 * gl;	// gas quente denso: cobre um pouco o fundo (senao o laranja somado ao ceu azul vira branco)
	float cov = 1.0 - T;
	float3 ce = (col + glow) * scales2.z;
	float cv = max(max(cov, 1e-4), saturate(max(ce.r, max(ce.g, ce.b)) * 0.5));
	float3 c = tonemap(ce / cv);
	float fa = 1.0 - exp(-max(fogS, 0.0) / max(fog.w, 1.0));
	c = lerp(c, fog.rgb, fa) * cv;
	return float4(c, T);
}
