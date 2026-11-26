# exp_nzv.py - Nightzim BoomMod X: simulacao de queda (Mantaflow, ja assada) -> volume .nzv do add-on
# blender -b queda_kA.blend --python exp_nzv.py      (QD_VAR=A|B|C|D, QD_OUT=pasta)
import bpy, numpy as np, os, math, struct, time
E = os.environ.get
VAR = E("QD_VAR", "A"); OUT = E("QD_OUT", r"C:\Users\fcire\Videos\nz_work\queda")
DX = float(E("QD_DX", "2.0")); KSOOT = float(E("QD_KSOOT", "0.6")); KS_R = 22.0
# caixa do volume no mundo da simulacao (x = direcao do voo, z = cima): x0, celulas x, celulas y, celulas z
CX = dict(A=(-45.0, 132, 72, 76), B=(-85.0, 92, 84, 80), C=(-50.0, 100, 72, 76), D=(-60.0, 124, 76, 76))[VAR]
x0, NX, NY, NZ = CX; y0 = -NY * DX / 2; z0 = 0.0
ts = [round(0.1 * k, 2) for k in range(1, 21)] + [2.0 + 0.25 * k for k in range(1, 17)] + [6.0 + 0.3 * k for k in range(1, 16)]   # ate 10,5 s
sc = bpy.context.scene; dom = bpy.data.objects["Dominio_Explosao"]
SUN_AZ, SUN_EL = 120.0, 52.0
az, el = math.radians(SUN_AZ), math.radians(SUN_EL)
sun = np.array([math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)]); sd = sun / sun[2]

def lin_axis(a, axis, src0, dsrc, dst0, ddst, n):
    """reamostra linearmente o eixo 'axis' de a (centros em src0 + (i+0.5)*dsrc) para n celulas (centros em dst0 + (j+0.5)*ddst)"""
    c = (dst0 + (np.arange(n) + 0.5) * ddst - src0) / dsrc - 0.5
    i0 = np.floor(c).astype(np.int64); w = (c - i0).astype(np.float32)
    m = a.shape[axis]
    ok0 = ((i0 >= 0) & (i0 < m)).astype(np.float32); ok1 = ((i0 + 1 >= 0) & (i0 + 1 < m)).astype(np.float32)
    sh = [1] * a.ndim; sh[axis] = n
    A0 = np.take(a, np.clip(i0, 0, m - 1), axis=axis) * ((1 - w) * ok0).reshape(sh)
    A1 = np.take(a, np.clip(i0 + 1, 0, m - 1), axis=axis) * (w * ok1).reshape(sh)
    return A0 + A1
def box(a, r, axis):
    if r < 1: return a
    n = a.shape[axis]; pad = [(0, 0)] * a.ndim; pad[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pad, mode="edge"), axis=axis, dtype=np.float64)
    hi = [slice(None)] * a.ndim; lo = [slice(None)] * a.ndim
    hi[axis] = slice(2 * r + 1, 2 * r + 1 + n); lo[axis] = slice(0, n)
    return ((c[tuple(hi)] - c[tuple(lo)]) / (2 * r + 1)).astype(np.float32)
def gauss(a, sigma):
    r = max(1, int(round(sigma * 0.5773 * 1.0)))   # 3 passadas de caixa ~ gaussiana (var = 3 * ((2r+1)^2-1)/12)
    r = max(1, int(round((math.sqrt(4 * sigma * sigma + 1) - 1) / 2)))
    for _ in range(3):
        for ax in range(3): a = box(a, r, ax)
    return a
def shift2(a, fx, fy):
    """a(x + fx, y + fy) com interpolacao bilinear (fora = 0)"""
    ix, iy = int(math.floor(fx)), int(math.floor(fy)); wx, wy = fx - ix, fy - iy
    def sh(a, dx, dy):
        o = np.zeros_like(a); nx, ny = a.shape
        xs0, xs1 = max(0, dx), min(nx, nx + dx); ys0, ys1 = max(0, dy), min(ny, ny + dy)
        if xs1 > xs0 and ys1 > ys0: o[xs0 - dx:xs1 - dx, ys0 - dy:ys1 - dy] = a[xs0:xs1, ys0:ys1]
        return o
    return (sh(a, ix, iy) * (1 - wx) * (1 - wy) + sh(a, ix + 1, iy) * wx * (1 - wy) + sh(a, ix, iy + 1) * (1 - wx) * wy + sh(a, ix + 1, iy + 1) * wx * wy)
def sun_tau(sig):
    tau = np.zeros_like(sig); acc = np.zeros(sig.shape[:2], np.float32); L = float(np.linalg.norm(sd))
    for k in range(sig.shape[2] - 2, -1, -1):
        acc = shift2(acc + 0.5 * sig[:, :, k + 1] * L, sd[0], sd[1]) + 0.5 * sig[:, :, k] * L
        tau[:, :, k] = acc
    tau[:, :, -1] = 0.5 * sig[:, :, -1] * L
    return tau
def planck(T, lam): return 1.0 / (lam ** 5 * (np.exp(14388.0 / (lam * T)) - 1.0))
def quadro(f):
    sc.frame_set(f)
    dg = bpy.context.evaluated_depsgraph_get(); ob = dom.evaluated_get(dg); m = ob.modifiers["Fluid"].domain_settings
    res = tuple(m.domain_resolution); k = int(m.noise_scale) if m.use_noise else 1
    n = (res[0] * k, res[1] * k, res[2] * k); tot = n[0] * n[1] * n[2]
    bb0 = np.array(ob.bound_box[0]) + np.array(ob.location); cs = np.array(m.cell_size) / k
    out = []
    for nome in ("density_grid", "flame_grid"):
        g = getattr(m, nome)
        if len(g) != tot: out.append(np.zeros((NX, NY, NZ), np.float32)); continue
        a = np.empty(tot, np.float32); g.foreach_get(a); a = a.reshape((n[2], n[1], n[0])).transpose(2, 1, 0)   # -> [x, y, z]
        a = lin_axis(a, 0, bb0[0], cs[0], x0, DX, NX); a = lin_axis(a, 1, bb0[1], cs[1], y0, DX, NY); a = lin_axis(a, 2, bb0[2], cs[2], z0, DX, NZ)
        out.append(np.maximum(a, 0).astype(np.float32))
    return out
def sstep(a, b, x): t = np.clip((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t)
# a fumaca some antes de tocar as faces da caixa (sem "tampa" nem paredes visiveis): topo de 45% a 96% da altura, lados nos ultimos 14%
ux = (np.arange(NX) + 0.5) / NX; uy = (np.arange(NY) + 0.5) / NY; uz = (np.arange(NZ) + 0.5) / NZ
bx = sstep(0.0, 0.14, ux) * sstep(0.0, 0.14, 1 - ux); by = sstep(0.0, 0.14, uy) * sstep(0.0, 0.14, 1 - uy); bz = 1 - sstep(0.70, 0.98, uz)
BORDA = (bx[:, None, None] * by[None, :, None] * bz[None, None, :]).astype(np.float32)
# erosao: o piso de densidade sobe com a altura e perto das faces, entao a nuvem se desfaz em fiapos (as partes ralas somem primeiro) em vez de acabar num plano
lat = (bx[:, None, None] * by[None, :, None])
PISO = (float(E('QD_PISO', '0.14')) + 0.34 * sstep(0.58, 0.97, uz)[None, None, :] + 0.5 * (1 - lat)).astype(np.float32)
vols = []; t0 = time.time()
for i, t in enumerate(ts):
    dens, fl = quadro(int(round(t * 30)) + 1)
    fl = np.clip(fl, 0, 1) ** 2.4 * BORDA; dens = np.maximum(dens - PISO, 0.0) ** 1.6 * BORDA; s = dens * KSOOT
    T = 300.0 + 1500.0 * np.sqrt(fl) + 250.0 * np.clip(dens, 0, 1) * (fl > 0.02)
    sig = s * KS_R * DX
    tauS = sun_tau(sig); tauU = np.cumsum(sig[:, :, ::-1], 2)[:, :, ::-1] - 0.5 * sig; tauD = np.cumsum(sig, 2) - 0.5 * sig
    j = np.minimum(s * KS_R, 0.6) * (T > 800) * planck(np.maximum(T, 600.0), 0.61) * 0.7e8 * 0.84 + fl * planck(2000.0, 0.61) * 0.2e8
    L = np.zeros_like(j)
    for sg, wt in ((3.0, 0.7), (7.0, 0.5), (16.0, 0.22), (32.0, 0.06)): L += wt * gauss(j.astype(np.float32), sg / DX)
    L *= DX * 0.10
    vols.append((s.astype(np.float16), T.astype(np.float16), fl.astype(np.float16), tauS.astype(np.float16), tauU.astype(np.float16), tauD.astype(np.float16), L.astype(np.float32)))
    print("QUADRO %d/%d t=%.2f dens %.3f chama %.3f L %.1f (%.0fs)" % (i + 1, len(ts), t, float(dens.max()), float(fl.max()), float(L.max()), time.time() - t0), flush=True)
smax = max(float(v[0].max()) for v in vols) or 1.0; fmax = max(float(v[2].max()) for v in vols) or 1.0
taumax = 250.0; lmax = max(float(v[6].max()) for v in vols) or 1.0
def u8(x): return np.clip(np.round(np.asarray(x, np.float32) * 255.0), 0, 255).astype(np.uint8)
out = os.path.join(OUT, "queda_%s.nzv" % VAR); tmp = out + ".tmp"
with open(tmp, "wb") as fo:
    hdr = struct.pack("<4s4if3f5f2ff", b"NZV1", NX, NY, NZ, len(ts), DX, -x0, -y0, 0.0, smax, 0.0, fmax, taumax, lmax, SUN_AZ, SUN_EL, float(ts[-1]))
    fo.write(hdr + b"\0" * (128 - len(hdr))); fo.write(np.asarray(ts, np.float32).tobytes())
    lt = math.log1p(taumax); ll = math.log1p(lmax); z8 = np.zeros((NX, NY, NZ), np.uint8)
    for (s, T, fl, tauS, tauU, tauD, L) in vols:
        A = np.stack([u8(np.sqrt(s.astype(np.float32) / smax)), z8, u8((T.astype(np.float32) - 300.0) / 3000.0), u8(np.sqrt(fl.astype(np.float32) / fmax))], -1)
        B = np.stack([u8(np.log1p(tauS.astype(np.float32)) / lt), u8(np.log1p(tauU.astype(np.float32)) / lt), u8(np.log1p(tauD.astype(np.float32)) / lt), u8(np.log1p(L) / ll)], -1)
        fo.write(np.ascontiguousarray(A.transpose(2, 1, 0, 3)).tobytes()); fo.write(np.ascontiguousarray(B.transpose(2, 1, 0, 3)).tobytes())
os.replace(tmp, out)
print("NZV_OK %s %dx%dx%d %d quadros dx %.2f %.0f MB soot %.4f chama %.3f fogo %.1f" % (out, NX, NY, NZ, len(ts), DX, os.path.getsize(out) / 1e6, smax, fmax, lmax), flush=True)
