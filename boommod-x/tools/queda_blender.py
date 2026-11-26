# queda.py - Nightzim BoomMod X: estudo de queda de aeronave (bola de fogo de querosene + incendio pos-queda)
# fase "bake":   blender -b --factory-startup --python queda.py      (QD_VAR=A|B|C|D, QD_RES, QD_QUADROS, QD_TAG)
# fase "render": blender -b <cena>.blend --python queda.py           (QD_FASE=render, QD_QUADROS_R="15,30,..." ou QD_ANIM=1)
import bpy, bmesh, os, math, random, time
E = os.environ.get
VAR = E("QD_VAR", "A"); TAG = E("QD_TAG", VAR.lower()); FASE = E("QD_FASE", "bake")
base = r"C:\Users\fcire\Videos\nz_work\queda"
BLEND = os.path.join(base, "queda_%s.blend" % TAG)
sc = bpy.context.scene

def gpu():
    try:
        cp = bpy.context.preferences.addons["cycles"].preferences
        cp.compute_device_type = 'HIP'; cp.get_devices()
        for dv in cp.devices: dv.use = (dv.type == 'HIP' and '(TM) Graphics' not in dv.name)
        sc.cycles.device = 'GPU'; print("[render] HIP", [dv.name for dv in cp.devices if dv.use], flush=True)
    except Exception as e: print("GPU:", e, flush=True)

if FASE == "render":
    gpu()
    sc.cycles.samples = int(E("QD_AMOSTRAS", "32"))
    if E("QD_DADOS") == "1": bpy.data.objects["Dominio_Explosao"].modifiers["Fluid"].domain_settings.use_noise = False
    sc.cycles.volume_bounces = int(E("QD_SALTOS", "1")); sc.cycles.volume_step_rate = float(E("QD_PASSO_VOL", "2.0")); sc.cycles.volume_max_steps = 256
    sc.cycles.max_bounces = 4; sc.cycles.diffuse_bounces = 1; sc.cycles.glossy_bounces = 0; sc.cycles.transmission_bounces = 0; sc.cycles.transparent_max_bounces = 2
    for a_ in ("volume_preview_step_rate",):
        if hasattr(sc.cycles, a_): setattr(sc.cycles, a_, 2.0)
    sc.render.resolution_percentage = int(E("QD_PCT", "50"))
    for n_ in bpy.data.materials["Mat_Queda"].node_tree.nodes:
        if n_.bl_idname == "ShaderNodeTexNoise" and E("QD_DET"): n_.inputs["Detail"].default_value = float(E("QD_DET"))
    for n_ in bpy.data.materials["Mat_Queda"].node_tree.nodes:
        if n_.bl_idname == "ShaderNodeMapRange" and abs(n_.inputs["To Max"].default_value - 1.75) < 1e-3 and E("QD_SEM_RUIDO") == "1":
            for l_ in list(n_.inputs["Value"].links): bpy.data.materials["Mat_Queda"].node_tree.links.remove(l_)
            n_.inputs["To Min"].default_value = 1.0; n_.inputs["To Max"].default_value = 1.0
        if n_.bl_idname == "ShaderNodeVolumePrincipled" and E("QD_ALB_R"):
            a_ = float(E("QD_ALB_R")); n_.inputs["Color"].default_value = (a_, a_ * 0.94, a_ * 0.88, 1)
        if n_.bl_idname == "ShaderNodeMapRange" and E("QD_KMAX_R") and abs(n_.inputs["To Min"].default_value - 1000) < 1: n_.inputs["To Max"].default_value = float(E("QD_KMAX_R"))
        if n_.bl_idname == "ShaderNodeMath" and n_.operation == 'MULTIPLY' and E("QD_FOGO_R") and abs(n_.inputs[1].default_value - float(E("QD_FOGO_DE", "70"))) < 1e-3 and not n_.inputs[1].is_linked:
            n_.inputs[1].default_value = float(E("QD_FOGO_R"))
    if E("QD_RX"): sc.render.resolution_x = int(E("QD_RX")); sc.render.resolution_y = int(E("QD_RY"))
    if E("QD_VT"): sc.view_settings.view_transform = E("QD_VT")
    if E("QD_LOOK"):
        try: sc.view_settings.look = E("QD_LOOK")
        except Exception as e_: print("LOOK", e_, [i.identifier for i in sc.view_settings.bl_rna.properties["look"].enum_items], flush=True)
    if E("QD_EXP"): sc.view_settings.exposure = float(E("QD_EXP"))
    if E("QD_CAM"):
        cx_, cy_, cz_, ax_, ay_, az_, ln_ = [float(x) for x in E("QD_CAM").split(",")]
        sc.camera.location = (cx_, cy_, cz_); sc.camera.data.lens = ln_; bpy.data.objects["Alvo"].location = (ax_, ay_, az_)
    outd = os.path.join(base, "r_" + TAG + E("QD_SUF", "")); os.makedirs(outd, exist_ok=True)
    im = sc.render.image_settings
    if hasattr(im, "media_type"): im.media_type = 'IMAGE'
    im.file_format = 'PNG'; im.color_mode = 'RGB'; im.compression = 30
    if E("QD_ANIM") == "1":
        if E("QD_FIM"): sc.frame_end = int(E("QD_FIM"))
        sc.frame_step = int(E("QD_PASSO", "2")); sc.render.use_overwrite = False; sc.render.use_placeholder = True
        sc.render.filepath = os.path.join(outd, "q_")
        t = time.time(); bpy.ops.render.render(animation=True); print("ANIM_FIM %.0fs" % (time.time() - t), flush=True)
    else:
        for f in [int(x) for x in E("QD_QUADROS_R", "8,20,45,90,150").split(",") if int(x) <= sc.frame_end]:
            t = time.time(); sc.frame_set(f); sc.render.filepath = os.path.join(outd, "s%03d.png" % f)
            bpy.ops.render.render(write_still=True); print("RENDER", f, "%.1fs" % (time.time() - t), flush=True)
        print("PREVIA_OK", flush=True)
    raise SystemExit

# ------------------------------------------------------------------ variantes
# dom = (xmin, xmax, meia largura y, altura). nevoa = combustivel pulverizado no impacto (particulas com a velocidade do aviao)
V = dict(
 A=dict(nome="Rasante", dom=(-45, 215, 70, 230), carga=0.55, nucleo=(34, 0, 5, 8),
        nevoa=dict(n=300, vel=(110, 0, 13), normal=14, rnd=30, q=(1, 5), vida=30, arrasto=1.0, tam=3.0, comb=2.4),
        est=dict(n=70, vel=(60, 0, 22), normal=20, rnd=22, vida=75),
        pocas=[(8, 0, 5, 3.5), (40, 5, 6, 3.5), (78, -4, 3.5, 2.5), (118, 3, 2.5, 2)], cam=(75, -430, 10, 70, 30)),
 B=dict(nome="Mergulho", dom=(-85, 100, 85, 250), carga=1.25, nucleo=(0, 0, 3.5, 3),
        nevoa=dict(n=220, vel=(8, 0, 34), normal=30, rnd=22, q=(1, 4), vida=24, arrasto=1.2, tam=3.0, comb=2.4),
        est=dict(n=120, vel=(4, 0, 30), normal=34, rnd=20, vida=70),
        pocas=[(0, 0, 8, 8), (13, 7, 3, 2.5), (-10, -9, 2.5, 2.5), (9, -13, 2, 2)], cam=(5, -430, 10, 70, 30)),
 C=dict(nome="Baixa energia", dom=(-50, 150, 70, 230), carga=0.45, nucleo=0, atraso=5,
        nevoa=dict(n=150, vel=(42, 0, 7), normal=9, rnd=12, q=(1, 12), vida=26, arrasto=1.2, tam=2.8, comb=2.0),
        est=dict(n=20, vel=(25, 0, 14), normal=12, rnd=10, vida=60),
        pocas=[(26, 0, 15, 6.5), (6, 3, 5, 3.5), (50, -4, 5, 3)], poca_comb=1.5, cam=(45, -430, 10, 70, 30)),
 D=dict(nome="Cinema", dom=(-60, 190, 75, 240), carga=0.9, nucleo=(14, 0, 4, 4),
        nevoa=dict(n=240, vel=(72, 0, 30), normal=22, rnd=28, q=(1, 4), vida=28, arrasto=1.0, tam=3.0, comb=2.4),
        est=dict(n=150, vel=(40, 0, 30), normal=30, rnd=24, vida=85),
        sec=dict(pos=(40, -5, 3), q=42, esc=0.6),
        pocas=[(6, 0, 7, 5), (30, 5, 5, 3.5), (55, -6, 4, 3), (82, 3, 3, 2.5)], cam=(60, -430, 10, 70, 30)),
)[VAR]
NQ = int(E("QD_QUADROS", "360")); VOX = float(E("QD_VOXEL", "1.0")); GX = float(E("QD_GX", "300"))
PRETO = (0.025, 0.022, 0.020)
rnd = random.Random(7)
for ob in list(bpy.data.objects): bpy.data.objects.remove(ob, do_unlink=True)
sc.render.fps = 30; sc.frame_start, sc.frame_end = 1, NQ
sc.use_gravity = True; sc.gravity = (0, 0, -9.81 * GX)
def ligar(n, me): ob = bpy.data.objects.new(n, me); sc.collection.objects.link(ob); return ob
def oculto(ob): ob.hide_render = True; ob.display_type = 'WIRE'
def chave_const(ob, pares):   # liga/desliga um emissor tirando-o do dominio (inflow animado nao vale no bake)
    pe = bpy.context.preferences.edit; ip0 = pe.keyframe_new_interpolation_type; pe.keyframe_new_interpolation_type = 'CONSTANT'
    for q, p in pares: ob.location = p; ob.keyframe_insert("location", frame=q)
    pe.keyframe_new_interpolation_type = ip0

# ------------------------------------------------------------------ dominio (chao em z=0: laje de colisao por baixo)
x0, x1, hy, hz = V["dom"]; ZB = -3.0
dim = (x1 - x0, 2 * hy, hz - ZB)
me = bpy.data.meshes.new("Dominio"); bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
for v in bm.verts: v.co.x *= dim[0]; v.co.y *= dim[1]; v.co.z *= dim[2]
bm.to_mesh(me); bm.free()
dom = ligar("Dominio_Explosao", me); dom.location = ((x0 + x1) / 2, 0, (hz + ZB) / 2)
ds = dom.modifiers.new("Fluid", 'FLUID'); ds.fluid_type = 'DOMAIN'; ds = ds.domain_settings
ds.domain_type = 'GAS'; ds.resolution_max = int(max(dim) / VOX)
ds.use_adaptive_domain = True; ds.additional_res = 0; ds.adapt_margin = 4; ds.adapt_threshold = 0.002
ds.alpha = 0.0; ds.vorticity = 0.6
ds.burning_rate = float(E("QD_QUEIMA", "0.2")); ds.flame_smoke = float(E("QD_FULIGEM", "2.2")); ds.flame_vorticity = 1.0
ds.flame_ignition = 1.25; ds.flame_max_temp = 4.0; ds.flame_smoke_color = PRETO
ds.use_dissolve_smoke = True; ds.dissolve_speed = int(E("QD_DISSOLVE", "125")); ds.use_dissolve_smoke_log = True
ds.cache_directory = r"F:\DCS_Explosao_cache\queda_" + TAG; ds.cache_type = E('QD_CACHE', 'MODULAR'); ds.cache_data_format = 'OPENVDB'
ds.cache_frame_start = 1; ds.cache_frame_end = NQ
ds.use_noise = E("QD_RUIDO", "1") == "1"
if ds.use_noise: ds.noise_scale = 2; ds.noise_strength = 2.0; ds.noise_pos_scale = 2.0; ds.noise_time_anim = 0.1
B0 = float(E("QD_BETA", "6.5"))
for q, b in ((1, B0), (30, B0), (90, B0 * 0.9), (150, B0 * 0.75), (NQ, B0 * 0.7)): ds.beta = b; ds.keyframe_insert("beta", frame=q)
# laje de colisao
me = bpy.data.meshes.new("Laje"); bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
for v in bm.verts: v.co.x *= dim[0] + 20; v.co.y *= dim[1] + 20; v.co.z *= 6.0
bm.to_mesh(me); bm.free()
laje = ligar("Laje", me); laje.location = ((x0 + x1) / 2, 0, -3.0); oculto(laje)
ef = laje.modifiers.new("Fluid", 'FLUID'); ef.fluid_type = 'EFFECTOR'; ef.effector_settings.effector_type = 'COLLISION'
laje.modifiers.new("Colisao", 'COLLISION'); laje.collision.damping_factor = 0.75; laje.collision.friction_factor = 0.35

def esfera(nome, raio, sub=3):
    me = bpy.data.meshes.new(nome); bm = bmesh.new(); bmesh.ops.create_icosphere(bm, subdivisions=sub, radius=raio); bm.to_mesh(me); bm.free()
    ob = ligar(nome, me); oculto(ob); return ob
def fluxo(ob, tipo='BOTH'):
    mf = ob.modifiers.new("Fluid", 'FLUID'); mf.fluid_type = 'FLOW'; fs = mf.flow_settings
    fs.flow_type = tipo; fs.flow_behavior = 'INFLOW'; fs.smoke_color = PRETO; return fs

# ------------------------------------------------------------------ carga central (bola de fogo do impacto)
def carga(nome, pos, esc0, q0):
    ob = esfera(nome, 2.0, 4); fs = fluxo(ob); fs.flow_source = 'MESH'; fs.surface_distance = 1.0; fs.volume_density = 1.0
    fs.density = 0.3; fs.temperature = 4.0; fs.use_initial_velocity = True; fs.velocity_factor = 0.0
    FASES = ((1, 4.0, 40.0, 0.6), (2, 4.0, 34.0, 1.5), (3, 3.6, 26.0, 2.1), (4, 3.0, 18.0, 2.5), (6, 2.4, 9.0, 2.8), (10, 1.8, 4.0, 2.9), (16, 1.2, 2.0, 2.9), (22, 0.5, 1.0, 2.9))
    for q, comb, vel, esc in FASES:
        fs.fuel_amount = comb; fs.velocity_normal = vel * 0.5
        fs.keyframe_insert("fuel_amount", frame=q0 + q - 1); fs.keyframe_insert("velocity_normal", frame=q0 + q - 1)
        e = esc * esc0; ob.scale = (e, e, e); ob.keyframe_insert("scale", frame=q0 + q - 1)
    tex = bpy.data.textures.new("R_" + nome, 'CLOUDS'); tex.noise_scale = 0.9
    d = ob.modifiers.new("Irregular", 'DISPLACE'); d.texture = tex; d.mid_level = 0.5; d.strength = 1.2
    ob.modifiers.move(len(ob.modifiers) - 1, 0)
    fora = (pos[0], pos[1], -1000.0)
    chave_const(ob, ([(1, fora)] if q0 > 1 else []) + [(q0, pos), (q0 + 23, fora)])
    return ob
q_ign = 1 + int(V.get("atraso", 0))
carga("Carga", (0, 0, 2.5), V["carga"], q_ign)
if V.get("sec"): carga("Carga_Sec", V["sec"]["pos"], V["sec"]["esc"], V["sec"]["q"])
if V["nucleo"]:
    nx, ny, nz_, nq = V["nucleo"]
    nuc = esfera("Nucleo_Quente", 6.0); nf = fluxo(nuc, 'SMOKE'); nf.flow_source = 'MESH'; nf.surface_distance = 1.0
    nf.temperature = float(E("QD_TEMP_NUC", "10")); nf.density = 0.1
    chave_const(nuc, [(1, (nx, ny, -1000)), (nq, (nx, ny, nz_)), (nq + 40, (nx, ny, -1000))])

# ------------------------------------------------------------------ particulas: nevoa de combustivel e estilhacos em chamas
def particulas(nome, c, comb, tam, temp, fator_vel, dens):
    ob = esfera(nome, 1.5, 2); ob.location = (0, 0, 2.5)
    ob.modifiers.new("Particulas", 'PARTICLE_SYSTEM'); ps = ob.particle_systems[0].settings
    ps.count = max(4, int(c["n"] * c.get("mult", 1.0))); ps.frame_start, ps.frame_end = c.get("q", (1, 4)); ps.frame_start += q_ign - 1; ps.frame_end += q_ign - 1
    ps.lifetime = c["vida"]; ps.lifetime_random = 0.4
    ps.emit_from = 'FACE'; ps.use_emit_random = True
    ps.normal_factor = c["normal"]; ps.factor_random = c["rnd"]
    ps.object_align_factor = c["vel"]
    ps.effector_weights.gravity = 1.0 / GX; ps.drag_factor = min(1.0, c.get("arrasto", 0.35)); ps.render_type = 'NONE'; ps.physics_type = 'NEWTON'
    fs = fluxo(ob); fs.flow_source = 'PARTICLES'; fs.particle_system = ob.particle_systems[0]
    fs.use_particle_size = False; fs.particle_size = tam; fs.density = dens; fs.temperature = temp; fs.fuel_amount = comb
    fs.use_initial_velocity = True; fs.velocity_factor = fator_vel
    return ob
nv = V["nevoa"]; particulas("Nevoa", nv, nv["comb"], nv["tam"], 4.5, 0.55, 0.12)
es = V["est"]; particulas("Estilhacos", dict(es, arrasto=0.3, mult=float(E("QD_EST", "0.22")), rnd=es["rnd"] * 1.6, normal=es["normal"] * 0.7), 1.2, 1.6, 2.0, 0.15, 0.5)

# ------------------------------------------------------------------ pocas de querosene (incendio pos-queda)
PC = float(V.get("poca_comb", 1.0)) * float(E("QD_POCA", "1.0"))
for i, (px, py, rx, ry) in enumerate(V["pocas"]):
    me = bpy.data.meshes.new("Poca%d" % i); bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=24, radius1=1.0, radius2=1.0, depth=1.0)
    for v in bm.verts:
        k = 1.0 + 0.3 * math.sin(3 * math.atan2(v.co.y, v.co.x) + i * 1.7)
        v.co.x *= rx * k; v.co.y *= ry * k; v.co.z *= 1.6
    bm.to_mesh(me); bm.free()
    ob = ligar("Poca%d" % i, me); oculto(ob); ob.rotation_euler = (0, 0, rnd.uniform(-0.5, 0.5))
    fs = fluxo(ob); fs.flow_source = 'MESH'; fs.surface_distance = 1.0; fs.volume_density = 1.0
    fs.density = 0.3; fs.temperature = 3.0; fs.fuel_amount = 1.4 * PC
    tx = bpy.data.textures.new("Lingua%d" % i, 'CLOUDS'); tx.noise_scale = 0.5; tx.noise_depth = 2; tx.contrast = 1.8
    fs.use_texture = True; fs.noise_texture = tx; fs.texture_map_type = 'AUTO'; fs.texture_size = 1.0
    fs.texture_offset = 0.0; fs.keyframe_insert("texture_offset", frame=1); fs.texture_offset = NQ / 40.0; fs.keyframe_insert("texture_offset", frame=NQ)
    q_on = q_ign + 3 + int(abs(px) / 6)      # a frente de chama alcanca a poca
    chave_const(ob, [(1, (px, py, -1000.0)), (q_on, (px, py, 0.9))])
# vento fraco de traves (a coluna inclina)
W = float(E("QD_VENTO", "1.0"))
if W:
    bpy.ops.object.effector_add(type='WIND', location=(0, 0, 40), rotation=(0, math.radians(90), 0)); wd = bpy.context.object; wd.field.strength = W; wd.field.flow = 0

# ------------------------------------------------------------------ material (pyro: blackbody + up-res por ruido 4D)
mat = bpy.data.materials.new("Mat_Queda"); mat.use_nodes = True
nt = mat.node_tree; nt.nodes.clear(); N = nt.nodes.new; L = nt.links.new
out = N("ShaderNodeOutputMaterial")
def M(op, a=None, b=None, clamp=False):
    m = N("ShaderNodeMath"); m.operation = op; m.use_clamp = clamp
    for i, v in enumerate((a, b)):
        if v is None: continue
        if isinstance(v, (int, float)): m.inputs[i].default_value = v
        else: L(v, m.inputs[i])
    return m.outputs[0]
def MR(v, a0, a1, b0, b1):
    m = N("ShaderNodeMapRange"); m.clamp = True; L(v, m.inputs["Value"])
    for k, x in zip(("From Min", "From Max", "To Min", "To Max"), (a0, a1, b0, b1)): m.inputs[k].default_value = x
    return m.outputs["Result"]
info = N("ShaderNodeVolumeInfo"); vol = N("ShaderNodeVolumePrincipled"); vol.inputs["Density Attribute"].default_value = ""
tc = N("ShaderNodeTexCoord"); nz = N("ShaderNodeTexNoise"); nz.noise_dimensions = '4D'
nz.inputs["Scale"].default_value = 0.45; nz.inputs["Detail"].default_value = 8; nz.inputs["Roughness"].default_value = 0.6; nz.inputs["Distortion"].default_value = 0.3
L(tc.outputs["Object"], nz.inputs["Vector"])
drv = nz.inputs["W"].driver_add("default_value"); drv.driver.expression = "frame*0.0117"
nfac = MR(nz.outputs["Fac"], 0.3, 0.7, 0.25, 1.75)
dpiso = M('MAXIMUM', M('SUBTRACT', info.outputs["Density"], 0.05), 0.0)
abre = M('MAXIMUM', M('SUBTRACT', 1.0, M('MULTIPLY', info.outputs["Flame"], 3.0, clamp=True)), 0.15)
L(M('MULTIPLY', M('MULTIPLY', M('MULTIPLY', M('POWER', dpiso, 1.3), nfac), abre), float(E("QD_DENS", "28"))), vol.inputs["Density"])
vol.inputs["Color"].default_value = (0.24, 0.225, 0.21, 1); vol.inputs["Absorption Color"].default_value = (0, 0, 0, 1); vol.inputs["Anisotropy"].default_value = 0.4
chama = M('MULTIPLY', info.outputs["Flame"], nfac); bb = N("ShaderNodeBlackbody")
L(MR(chama, 0.0, 1.0, 1000, float(E("QD_KMAX", "1900"))), bb.inputs["Temperature"]); L(bb.outputs["Color"], vol.inputs["Emission Color"])
L(M('MULTIPLY', M('POWER', M('MAXIMUM', chama, 0.0), 2.6), float(E("QD_FOGO", "7"))), vol.inputs["Emission Strength"])
L(vol.outputs["Volume"], out.inputs["Volume"]); dom.data.materials.append(mat)

# ------------------------------------------------------------------ chao, ceu, sol, camera
me = bpy.data.meshes.new("Chao"); bm = bmesh.new(); bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=3000); bm.to_mesh(me); bm.free()
chao = ligar("Chao", me); chao.location = (0, 0, -0.02)
mg = bpy.data.materials.new("Mat_Chao"); mg.use_nodes = True; b = mg.node_tree.nodes.get("Principled BSDF")
b.inputs["Base Color"].default_value = (0.20, 0.19, 0.11, 1); b.inputs["Roughness"].default_value = 1.0
tn = mg.node_tree.nodes.new("ShaderNodeTexNoise"); tn.inputs["Scale"].default_value = 0.02; tn.inputs["Detail"].default_value = 6
mx = mg.node_tree.nodes.new("ShaderNodeMix"); mx.data_type = 'RGBA'
mx.inputs[6].default_value = (0.13, 0.15, 0.07, 1); mx.inputs[7].default_value = (0.30, 0.26, 0.16, 1)
mg.node_tree.links.new(tn.outputs["Fac"], mx.inputs[0]); mg.node_tree.links.new(mx.outputs[2], b.inputs["Base Color"])
chao.data.materials.append(mg)
# referencia de escala: caca de ~15 m (caixa cinza) parado ao lado, fora do fogo
me = bpy.data.meshes.new("Ref"); bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
for v in bm.verts: v.co.x *= 15.0; v.co.y *= 9.5; v.co.z *= 4.8
bm.to_mesh(me); bm.free(); ref = ligar("Ref_F16_15m", me); ref.location = (x0 + 20, -hy - 10, 2.4)
w = sc.world or bpy.data.worlds.new("Mundo"); sc.world = w; w.use_nodes = True
bg = w.node_tree.nodes.get("Background"); bg.inputs["Color"].default_value = (0.36, 0.55, 0.85, 1); bg.inputs["Strength"].default_value = 0.7
sol = ligar("Sol", bpy.data.lights.new("Sol", 'SUN')); sol.data.energy = 5.0; sol.data.color = (1.0, 0.96, 0.9); sol.data.angle = 0.02
sol.rotation_euler = (math.radians(52), 0, math.radians(-140))
cx, cy, cz, az, lens = V["cam"]
cam = ligar("Camera", bpy.data.cameras.new("Camera")); sc.camera = cam; cam.location = (cx, cy, cz); cam.data.lens = lens; cam.data.clip_end = 5000
alvo = bpy.data.objects.new("Alvo", None); sc.collection.objects.link(alvo); alvo.location = (cx, 0, az)
t = cam.constraints.new('TRACK_TO'); t.target = alvo; t.track_axis = 'TRACK_NEGATIVE_Z'; t.up_axis = 'UP_Y'
sc.render.engine = 'CYCLES'; gpu()
sc.cycles.samples = 32; sc.cycles.use_denoising = True; sc.cycles.volume_bounces = 3; sc.cycles.volume_step_rate = 0.5; sc.cycles.volume_max_steps = 512
sc.render.resolution_x, sc.render.resolution_y = 1280, 720; sc.view_settings.view_transform = "AgX"
os.makedirs(base, exist_ok=True); bpy.ops.wm.save_as_mainfile(filepath=BLEND)
print("[queda] montada", VAR, V["nome"], "dom", dim, "res", ds.resolution_max, "quadros", NQ, flush=True)
t = time.time()
if ds.cache_type == 'ALL':
    with bpy.context.temp_override(object=dom, active_object=dom, selected_objects=[dom]): bpy.ops.fluid.bake_all()
    print("BAKE_TUDO %.0fs" % (time.time() - t), flush=True)
else:
    with bpy.context.temp_override(object=dom, active_object=dom, selected_objects=[dom]): bpy.ops.fluid.bake_data()
    print("BAKE_DADOS %.0fs" % (time.time() - t), flush=True)
bpy.ops.wm.save_mainfile()
