# nzv_sweep.py - napalm: a faixa de fogo acende de uma ponta para a outra (o tambor rola e espalha o gel pelo caminho)
#   quadro_novo(t)[x] = quadro_original(t - atraso(x))[x];   atraso cresce ao longo do eixo x da grade (direcao do voo)
import numpy as np, struct, sys
src, dst = sys.argv[1], sys.argv[2]; SWEEP = float(sys.argv[3]) if len(sys.argv) > 3 else 0.75
raw = open(src, 'rb').read(128); nx, ny, nz, nf = struct.unpack('<4i', raw[4:20]); dx = struct.unpack('<f', raw[20:24])[0]
mm = np.memmap(src, np.uint8, 'r'); ts = np.frombuffer(mm[128:128 + 4 * nf].tobytes(), np.float32).astype(np.float64)
base = 128 + 4 * nf; cell = nx * ny * nz * 4
F = np.asarray(mm[base:base + 2 * nf * cell]).reshape(nf, 2, nz, ny, nx, 4)
fl = F[0, 0, ..., 3].astype(np.float64).sum((0, 1)); xs = np.nonzero(fl > fl.max() * 0.05)[0]; x0, x1 = int(xs[0]), int(xs[-1])
delay = np.clip((np.arange(nx) - x0) / max(x1 - x0, 1), 0, 1) ** 0.85 * SWEEP
print("faixa de fogo: x %d..%d (%.0f m); varredura %.2f s (%.0f m/s)" % (x0, x1, (x1 - x0) * dx, SWEEP, (x1 - x0) * dx / SWEEP))
out = np.zeros_like(F)
for k in range(nf):
    for x in range(nx):
        tau = ts[k] - delay[x]
        if tau <= 0: continue
        soft = min(1.0, tau / 0.14) ** 1.5      # frente macia: o fogo cresce em ~0,14 s em vez de aparecer como uma parede
        if tau <= ts[0]: out[k, :, :, :, x] = (F[0, :, :, :, x].astype(np.float32) * soft + 0.5).astype(np.uint8); continue
        if tau >= ts[-1]: out[k, :, :, :, x] = F[-1, :, :, :, x]; continue
        j = int(np.searchsorted(ts, tau)); i = j - 1; w = (tau - ts[i]) / (ts[j] - ts[i])
        out[k, :, :, :, x] = ((F[i, :, :, :, x].astype(np.float32) * (1 - w) + F[j, :, :, :, x].astype(np.float32) * w) * soft + 0.5).astype(np.uint8)
hdr = bytearray(raw)
bx = (x0 + 1.0) * dx                      # o ponto de impacto passa a ser a ponta onde o fogo comeca
hdr[24:28] = struct.pack('<f', bx)
with open(dst, 'wb') as f:
    f.write(bytes(hdr)); f.write(np.asarray(ts, np.float32).tobytes()); f.write(out.tobytes())
print("gravado", dst, "burst x", bx)
