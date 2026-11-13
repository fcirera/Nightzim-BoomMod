// ---------------------------------------------------------------- som das explosoes (atalhos; bomba real ja tem o som do DCS)
// waveOut do Windows: cada explosao abre a sua saida (o Windows mistura). Atraso pela distancia (343 m/s),
// volume pela distancia, panoramico pela direcao e filtro passa-baixa para as distantes.
#include <mmsystem.h>
struct Snd { std::vector<int16_t> pcm; int rate = 44100; };
static Snd g_snd[6];
struct Voice { HWAVEOUT h = nullptr; WAVEHDR hdr = {}; std::vector<int16_t> buf; };
static std::vector<Voice *> g_voices;
static float g_volume = 1.f;

static bool load_wav(const std::string &path, Snd &s)
{
	FILE *f = fopen(path.c_str(), "rb"); if (!f) return false;
	std::vector<unsigned char> d; unsigned char b[65536]; size_t r; while ((r = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + r); fclose(f);
	if (d.size() < 44 || memcmp(d.data(), "RIFF", 4) || memcmp(d.data() + 8, "WAVE", 4)) return false;
	size_t p = 12; int ch = 1, bits = 16;
	while (p + 8 <= d.size())
	{
		uint32_t sz; memcpy(&sz, d.data() + p + 4, 4);
		if (!memcmp(d.data() + p, "fmt ", 4)) { uint16_t c, bt; uint32_t sr; memcpy(&c, d.data() + p + 10, 2); memcpy(&sr, d.data() + p + 12, 4); memcpy(&bt, d.data() + p + 22, 2); ch = c; s.rate = sr; bits = bt; }
		else if (!memcmp(d.data() + p, "data", 4))
		{
			if (bits != 16) return false;
			size_t n = std::min<size_t>(sz, d.size() - p - 8) / 2 / ch;
			s.pcm.resize(n);
			for (size_t i = 0; i < n; i++) { int16_t v; memcpy(&v, d.data() + p + 8 + i * 2 * ch, 2); s.pcm[i] = v; }
			return true;
		}
		p += 8 + sz + (sz & 1);
	}
	return false;
}

static void load_sounds()
{
	const char *n[6] = {"jdam.wav", "aerea.wav", "nar.wav", "mk82.wav", "napalm.wav", "tank.wav"};
	for (int k = 0; k < 6; k++) if (!load_wav(g_dir + "snd\\" + n[k], g_snd[k])) logf("aviso: som %s ausente", n[k]);
}

// pan: -1 esquerda .. 1 direita
static void play_sound(int type, float dist, float pan)
{
	const Snd &s = g_snd[type];
	if (s.pcm.empty() || g_volume <= 0) return;
	Voice *v = new Voice();
	float vol = g_volume * std::min(1.f, 220.f / std::max(dist, 30.f));
	float a = (pan + 1.f) * 0.785398f, gl = cosf(a) * 1.41f, gr = sinf(a) * 1.41f;
	gl = std::min(gl, 1.f); gr = std::min(gr, 1.f);
	// passa-baixa de um polo: longe so sobra o ronco
	float fc = std::max(400.f, 16000.f * std::min(1.f, 300.f / std::max(dist, 1.f)));
	float k = 1.f - expf(-6.2831853f * fc / s.rate), y = 0;
	v->buf.resize(s.pcm.size() * 2);
	for (size_t i = 0; i < s.pcm.size(); i++)
	{
		y += k * (s.pcm[i] - y);
		float o = y * vol;
		v->buf[2 * i] = (int16_t)std::max(-32767.f, std::min(32767.f, o * gl));
		v->buf[2 * i + 1] = (int16_t)std::max(-32767.f, std::min(32767.f, o * gr));
	}
	WAVEFORMATEX wf = {}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = s.rate; wf.wBitsPerSample = 16;
	wf.nBlockAlign = 4; wf.nAvgBytesPerSec = s.rate * 4;
	if (waveOutOpen(&v->h, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { delete v; return; }
	v->hdr.lpData = (LPSTR)v->buf.data(); v->hdr.dwBufferLength = (DWORD)(v->buf.size() * 2);
	waveOutPrepareHeader(v->h, &v->hdr, sizeof v->hdr);
	waveOutWrite(v->h, &v->hdr, sizeof v->hdr);
	g_voices.push_back(v);
}

static void reap_sounds()
{
	for (size_t i = 0; i < g_voices.size();)
	{
		Voice *v = g_voices[i];
		if (v->hdr.dwFlags & WHDR_DONE)
		{
			waveOutUnprepareHeader(v->h, &v->hdr, sizeof v->hdr); waveOutClose(v->h); delete v;
			g_voices.erase(g_voices.begin() + i);
		}
		else i++;
	}
}
