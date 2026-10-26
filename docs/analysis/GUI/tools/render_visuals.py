"""Generate revised report images from current captures; no browser required."""
from pathlib import Path
import hashlib, json, os, tempfile, time
from visual_system import *
from screens_scene import scene_screen
from screens_code import code_screen, agent_screen
from screens_assets import assets_screen, generate_screen, paint_screen
from screens_diagnostics import vfx_screen, diagnostics_screen
from screens_authoring import material_screen, rig_screen, animation_screen

OUT=BASE/"images"
CAPTURE_RUNS=[
    {"name":"sponza","frames":[680],"exit_code":0},
    {"name":"materials","frames":[240],"exit_code":0},
    *[{"name":"damaged-"+view,"frames":[240],"exit_code":0}
      for view in ("quarter","front","side","back")],
    {"name":"character","frames":[120,240],"exit_code":1,
     "failure":"D3D12MA shutdown assertion: 3 live allocations, 6881280 bytes"}]

def architecture_screen():
    im=Image.new("RGB",(W,H),"#eeeeee"); d=ImageDraw.Draw(im)
    ink="#282828";muted="#6a6a6a";edge="#cccccc"
    mark=brand_icon(28);im.paste(mark,(62,39),mark)
    text(d,103,45,"UZENGINE",15,"#444444",True)
    text(d,62,88,"A native home for the engine.",39,ink,True)
    text(d,62,151,"Proposed architecture  /  Windows-first prototype  /  Decisions gated by measurement",18,muted)
    labels=[
        (62,242,1230,360,"01","Desktop shell","Qt Widgets, docking, focus, accessibility and project lifecycle"),
        (62,410,621,576,"02","Code surface","WebView2 / Monaco, LSP and DAP adapters"),
        (671,410,1230,576,"03","Editor services","Documents, revisions, transactions and jobs"),
        (62,626,1230,790,"04","Engine owner","Existing ECS, VFX, assets and renderer; bounded commands"),
        (62,840,1230,1003,"05","Direct presentation","One swapchain / native HWND. Capture stays an explicit operation.")]
    for x,y,x2,y2,num,head,desc in labels:
        rect(d,(x,y,x2,y2),"#fafafa",edge,5)
        text(d,x+20,y+20,num,14,"#777777",True)
        text(d,x+67,y+18,head,23,ink,True)
        text(d,x+67,y+66,desc,16,muted)
    for x,y,y2 in [(341,362,405),(950,362,405),(950,578,621),(640,792,834)]:
        line(d,(x,y,x,y2),"#777777",2)
        d.polygon([(x-5,y2-7),(x+5,y2-7),(x,y2)],fill="#777777")
    text(d,1297,242,"SUPERVISED PROCESSES",15,muted,True)
    for i,(ic,head,desc) in enumerate([
        ("code-xml","Language services","clangd / Pyright / LuaLS"),
        ("bot","Agent adapters","Codex app-server / Claude Agent"),
        ("paintbrush","External authoring","EZPainter / Blender / Painter"),
        ("workflow","Durable workers","Import / build / download / validate")]):
        yy=304+i*175
        line(d,(1297,yy+130,1854,yy+130),edge)
        icon(d,1297,yy,ic,24,"#666666")
        text(d,1340,yy,head,23,ink,True)
        text(d,1340,yy+48,desc,16,muted)
    text(d,62,1054,"Shared identity. Explicit approval. Recoverable work.",25,ink,True)
    text(d,62,1104,"VSCode remains a companion. This diagram is a proposal, not shipping topology or a performance result.",16,muted)
    text(d,62,1155,"DESIGN STUDY 05   /   23 OCT 2026",12,muted)
    return im

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def publish_png(im,path):
    """Keep the published image intact until its replacement is fully encoded."""
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(prefix='.'+path.stem+'-',suffix='.png',dir=path.parent,delete=False) as stream:
            temporary=Path(stream.name)
            im.save(stream,format='PNG')
            stream.flush()
            os.fsync(stream.fileno())
        for attempt in range(5):
            try:
                os.replace(temporary,path)
                temporary=None
                return
            except OSError as error:
                if attempt==4 or (error.errno not in (13,22) and getattr(error,'winerror',None) not in (5,32,33)):
                    raise
                time.sleep(0.2*(attempt+1))
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    with Image.open(ROOT/"icon.ico") as mark:
        mark.convert("RGBA").save(OUT/"sources/uzmi-icon.png")
    frames=[
        ("00-architecture.png",architecture_screen,"Proposed native host architecture; not shipping topology."),
        ("01-scene-workspace.png",scene_screen,"Scene workspace using sample Sponza frame 680."),
        ("02-code-workspace.png",code_screen,"Code workspace proposal; illustrative code and service states."),
        ("03-agent-review.png",agent_screen,"Agent review proposal; no provider session or approved patch."),
        ("04-asset-lab.png",assets_screen,"Asset library with DamagedHelmet runtime capture and its four real material maps."),
        ("05-model-generation.png",generate_screen,"Meshy workflow concept; local stand-in, no paid generation."),
        ("06-paint-roundtrip.png",paint_screen,"Paint workflow concept; no EZPainter connection or paint operation."),
        ("07-vfx-workspace.png",vfx_screen,"VFX authoring concept with synthetic trail illustration."),
        ("08-diagnostics-jobs.png",diagnostics_screen,"Diagnostic concept using current capture identities and observed warnings."),
        ("09-material-graph.png",material_screen,"Material document with source textures and draft graph; no shader compilation."),
        ("10-control-rig.png",rig_screen,"Rig proposal with captured MushroomMonster; draft controls, no solver evaluation or joint overlay."),
        ("11-animation-editor.png",animation_screen,"Animation proposal with runtime Idle capture; displayed keys are drafts, not extracted/evaluated tracks.")]
    outputs=[]
    for filename,fn,description in frames:
        im=fn()
        path=OUT/filename;publish_png(im,path)
        outputs.append({"file":filename,"description":description,"width":im.width,"height":im.height,"sha256":sha(path)})
        print("Rendered",filename,im.size)
    sheet=Image.new("RGB",(1440,336*((len(outputs)+2)//3)),"#eeeeee");sd=ImageDraw.Draw(sheet)
    for i,item in enumerate(outputs):
        x=(i%3)*480;y=(i//3)*336
        pic=Image.open(OUT/item["file"]).resize((456,285),Image.Resampling.LANCZOS)
        sheet.paste(pic,(x+12,y+12))
        text(sd,x+13,y+309,item["file"],13,"#353535")
    publish_png(sheet,OUT/"contact-sheet.png")
    outputs.append({"file":"contact-sheet.png","description":"Overview of document-oriented design studies.","width":sheet.width,"height":sheet.height,"sha256":sha(OUT/"contact-sheet.png")})
    inputs=[OUT/"sources/uzmi-icon.png",*sorted((ROOT/"sample/content/textures").glob("DH_HQ_*")),
            ROOT/"sample/content/meshes/DamagedHelmetHQ.uzmesh",
            BASE/"tools/icons/lucide.ttf",BASE/"tools/icons/info.json",BASE/"tools/icons/LICENSE",ROOT/"icon.ico"]
    for run in CAPTURE_RUNS:
        inputs.extend([OUT/f"sources/{run['name']}-manifest.json",OUT/f"sources/{run['name']}-run.log"])
        inputs.extend(OUT/f"sources/{run['name']}_{frame}.png" for frame in run["frames"])
    for name in ("sponza","material_showcase","damaged_helmet","fbx_test"):
        inputs.extend([ROOT/f"sample/projects/{name}.uzproject",ROOT/f"sample/content/scenes/{name}.uzscene"])
    inputs.extend(sorted((ROOT/"sample/content/meshes/mushroommonster").glob("*.uzskel")))
    inputs.extend([ROOT/"sample/content/meshes/mushroommonster/MushroomMonster_Low.uzmesh",
                   ROOT/"sample/content/meshes/mushroommonster/animations/AS_Mushroom_Idle.uzanim",
                   ROOT/"sample/content/meshes/mushroommonster/mushroom_monster.uzmat",
                   ROOT/"sample/content/scripts/mushroom_controller.lua"])
    sources=[{"path":p.relative_to(ROOT).as_posix(),"sha256":sha(p)} for p in inputs if p.is_file()]
    manifest={"created":"2026-10-23","revision":5,"kind":"Document-oriented neutral-gray static GUI design proposals",
              "navigation":"document-type",
              "generator":"tools/render_visuals.py","sources":sources,"outputs":outputs,
              "runtime_capture":{"binary_sha256":"2c866adb624599de524b14fd382c7a308a1f07bc87aae02202f97b01cadcbd40",
              "runs":CAPTURE_RUNS,"fresh_runs":True,"matched_benchmark":False},
              "icons":{"package":"lucide-static","version":"0.468.0","license":"tools/icons/LICENSE"},
              "notes":["Original root .screenshots directory deleted after explicit user confirmation.",
                       "New runtime captures replace historical images; PNGs are not color-retouched.",
                       "Source runs include warnings; successful capture does not certify renderer correctness.",
                       "GUI is static proposal; no native shell or agent/provider integration implemented.",
                       "DamagedHelmet mesh, maps and lighting come from sample/projects/damaged_helmet.uzproject; only CLI cameras changed. License not verified; not Meshy output.",
                       "VFX trail is synthetic. All authoring state is illustrative unless capture identity is explicit.",
                       "MushroomMonster is a runtime capture with Idle playback. Run exited 1 during shutdown; rig controls and timeline keys remain unevaluated proposals.",
                       "Sample PNGs are copied losslessly. Composition uses aspect-preserving resampling and background crops, without recoloring or fabricated object views.",
                       "Files choose their editor. Scene linked code requires an explicit association; agents and external authoring are contextual panels.",
                       "Generator itself does not launch an engine, browser, provider or external tool."]}
    (OUT/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")

if __name__=="__main__":
    main()
