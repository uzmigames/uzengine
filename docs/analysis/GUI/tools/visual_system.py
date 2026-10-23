"""Shared visual language for the revised static editor proposals."""
from pathlib import Path
import json, math
from functools import lru_cache
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageOps, ImageFilter

BASE=Path(__file__).resolve().parents[1]
ROOT=BASE.parents[2]
W,H=1920,1200
C={"bg":"#141414","panel":"#202020","surface":"#292929","field":"#171717",
   "line":"#3b3b3b","subtle":"#303030","text":"#e5e5e5","muted":"#a0a0a0",
   "faint":"#737373","accent":"#eeeeee","selection":"#3a3a3a",
   "cyan":"#7acbd3","blue":"#89b9ef","gold":"#e4bb75","red":"#e7948e"}
ICON_MAP=json.loads((BASE/"tools/icons/info.json").read_text(encoding="utf-8"))

@lru_cache(maxsize=100)
def font(n=15,bold=False,mono=False):
    name="consola.ttf" if mono else "segoeuib.ttf" if bold else "bahnschrift.ttf"
    return ImageFont.truetype(str(Path("C:/Windows/Fonts")/name),n)

def text(d,x,y,s,n=15,c=None,bold=False,mono=False):
    d.text((x,y),str(s),font=font(n,bold,mono),fill=c or C["text"])

def rect(d,xy,c=None,edge=None,r=0):
    if r: d.rounded_rectangle(xy,r,fill=c or C["panel"],outline=edge)
    else: d.rectangle(xy,fill=c or C["panel"],outline=edge)

def line(d,xy,c=None,w=1):
    d.line(xy,fill=c or C["line"],width=w)

def icon(d,x,y,name,size=18,c=None):
    f=ImageFont.truetype(str(BASE/"tools/icons/lucide.ttf"),size)
    code=int(ICON_MAP[name]["encodedCode"][1:],16)
    d.text((x,y),chr(code),font=f,fill=c or C["muted"],anchor="lt")

def ib(d,x,y,name,active=False,size=30):
    if active: rect(d,(x,y,x+size,y+size),C["selection"],r=4)
    icon(d,x+(size-17)//2,y+(size-17)//2,name,17,C["accent"] if active else C["muted"])

def button(d,x,y,s,w=140,primary=False,name=None):
    rect(d,(x,y,x+w,y+32),C["accent"] if primary else C["surface"],None,4)
    c=C["bg"] if primary else C["text"]
    if name: icon(d,x+10,y+8,name,16,c)
    text(d,x+(34 if name else 12),y+8,s,14,c,bold=primary)

def search(d,x,y,w,placeholder="Search"):
    rect(d,(x,y,x+w,y+28),C["field"],C["subtle"],3)
    icon(d,x+8,y+7,"search",14)
    text(d,x+30,y+7,placeholder,13,C["faint"])

def panel(d,xy,title,subtitle=None):
    x,y,x2,y2=xy
    rect(d,xy,C["panel"])
    rect(d,(x,y,x2,y+33),C["surface"])
    text(d,x+14,y+9,title,14,bold=True)
    if subtitle: text(d,x2-125,y+10,subtitle,12,C["muted"])
    icon(d,x2-27,y+10,"ellipsis",15)
    line(d,(x,y2,x2,y2),C["bg"],2)

def section(d,x,y,w,title):
    rect(d,(x,y,x+w,y+28),C["surface"])
    icon(d,x+7,y+7,"chevron-down",13)
    text(d,x+27,y+7,title,13,bold=True)

def field(d,x,y,w,label,value,suffix=None):
    text(d,x,y+7,label,13,C["muted"])
    start=x+int(w*.43)
    rect(d,(start,y,x+w,y+27),C["field"],C["subtle"],3)
    text(d,start+9,y+7,value,13)
    if suffix: icon(d,x+w-21,y+7,suffix,13)

def check(d,x,y,label,checked=True):
    rect(d,(x,y,x+13,y+13),C["selection"] if checked else C["field"],C["line"],2)
    if checked: icon(d,x,y,"check",13,C["accent"])
    text(d,x+23,y-1,label,13)

def badge(d,x,y,s,c=None):
    text(d,x,y,s,12,c or C["accent"])
    return d.textlength(s,font=font(12))+14

def row(d,x,y,w,label,name="box",depth=0,selected=False,detail=None,c=None):
    if selected:
        rect(d,(x,y-3,x+w,y+25),C["selection"])
        rect(d,(x,y-3,x+2,y+25),C["accent"])
    icon(d,x+12+depth*14,y+3,name,15,c or (C["accent"] if selected else C["muted"]))
    text(d,x+36+depth*14,y+3,label,13,C["text"] if selected else C["muted"])
    if detail: text(d,x+w-48,y+4,detail,12,C["faint"])

def paste(im,pic,xy,contain=False):
    x,y,x2,y2=map(int,xy)
    resize=ImageOps.pad if contain else ImageOps.fit
    args={"color":C["bg"]} if contain else {}
    im.paste(resize(pic,(x2-x,y2-y),method=Image.Resampling.LANCZOS,**args),(x,y))

def source_scene():
    return Image.open(BASE/"images/sources/sponza_680.png").convert("RGB")

def source_reference():
    return Image.open(BASE/"images/sources/materials_240.png").convert("RGB")

def texture(name="BaseColor"):
    key="ORM" if name in ("MetallicRoughness","AmbientOcclusion") else name
    extension="png" if key=="ORM" else "jpg"
    pic=Image.open(ROOT/f"sample/content/textures/DH_HQ_{key}.{extension}").convert("RGB")
    return pic.getchannel("R").convert("RGB") if name=="AmbientOcclusion" else pic

@lru_cache(maxsize=12)
def helmet(angle=28,size=700):
    view={28:"quarter",0:"front",90:"side",180:"back"}[angle]
    pic=Image.open(BASE/f"images/sources/damaged-{view}_240.png").convert("RGB")
    return pic


@lru_cache(maxsize=8)
def brand_icon(size=24):
    with Image.open(ROOT/"icon.ico") as source:
        return source.convert("RGBA").resize((size,size),Image.Resampling.LANCZOS)

def viewport_tools(d,x,y,width):
    rect(d,(x,y,x+width,y+36),C["panel"])
    for i,(name,active) in enumerate([("mouse-pointer-2",False),("move",True),("rotate-3d",False),("maximize",False)]):
        ib(d,x+7+i*31,y+3,name,active)
    line(d,(x+140,y+8,x+140,y+28))
    text(d,x+154,y+11,"World",13,C["muted"]); icon(d,x+199,y+12,"chevron-down",12)
    for i,(name,label) in enumerate([("grid-2x2","10 cm"),("rotate-ccw","15 deg"),("scaling","0.1")]):
        xx=x+235+i*74; icon(d,xx,y+10,name,16); text(d,xx+25,y+11,label,12)
    text(d,x+width-264,y+11,"Perspective",13)
    icon(d,x+width-174,y+12,"chevron-down",12)
    text(d,x+width-144,y+11,"Lit",13)
    icon(d,x+width-119,y+12,"chevron-down",12)
    ib(d,x+width-86,y+3,"camera")
    ib(d,x+width-44,y+3,"maximize")

def shell(mode,document="Sponza.uzscene"):
    im=Image.new("RGB",(W,H),C["bg"]); d=ImageDraw.Draw(im)
    rect(d,(0,0,W,32),"#101010")
    mark=brand_icon(24);im.paste(mark,(17,4),mark)
    text(d,54,8,"ENGINE",11,C["muted"],True)
    for i,s in enumerate(["File","Edit","View","Tools","Window"]): text(d,140+i*57,9,s,12,C["muted"])
    text(d,701,8,"Studio  /  "+document,12,C["muted"])
    icon(d,1702,8,"git-branch",14); text(d,1724,9,"main",12,C["muted"])
    for x,n in [(1804,"minus"),(1844,"square"),(1884,"x")]: icon(d,x,9,n,13)
    rect(d,(0,33,W,76),C["panel"])
    active={"Scene":"Sponza.uzscene","Code":"orbit_preview.lua","Assets":"DamagedHelmetHQ.uzmesh",
            "Generate":"DamagedHelmetHQ.uzmesh","Paint":"DamagedHelmetHQ.uzmesh","VFX":"EnergyRibbon.uzvfx",
            "Agents":"orbit_preview.lua","Diagnostics":"sponza-manifest.json",
            "Material":"DamagedHelmet.uzmat","Rig":"MushroomMonster.uzrig","Animation":"Mushroom_Idle.uzanim"}[mode]
    tabs=[("Sponza.uzscene","box"),("DamagedHelmet.uzmat","workflow"),
          ("orbit_preview.lua","file-code"),("MushroomMonster.uzrig","bone"),("Mushroom_Idle.uzanim","clapperboard")]
    if active not in [name for name,_ in tabs]:
        tabs.append((active,"waypoints" if mode=="VFX" else "file-json" if mode=="Diagnostics" else "file-box"))
    x=16
    for label,name in tabs:
        w=int(d.textlength(label,font=font(13)))+84
        if label==active:
            rect(d,(x,34,x+w,75),C["surface"])
            rect(d,(x+12,73,x+w-12,75),C["accent"])
        icon(d,x+11,47,name,17,C["accent"] if label==active else C["muted"])
        text(d,x+36,48,label,13,C["text"] if label==active else C["muted"])
        if label.endswith((".uzscene",".uzmat",".lua",".uzrig",".uzanim",".uzvfx")):
            d.ellipse((x+w-40,52,x+w-35,57),fill=C["muted"])
        icon(d,x+w-21,50,"x",10)
        x+=w+4
    ib(d,x+3,40,"plus")
    search(d,1476,40,263,"Search files and commands")
    for xx,n in [(1754,"git-branch"),(1794,"bot"),(1834,"panels-top-left"),(1874,"settings-2")]:ib(d,xx,40,n)
    rect(d,(0,77,W,112),C["bg"])
    icon(d,16,88,"folder-open",15)
    text(d,42,88,"Studio",13,C["muted"]); icon(d,94,89,"chevron-right",13)
    text(d,116,88,document,13)
    text(d,1610,88,"Scene: Sponza",12,C["muted"])
    icon(d,1760,86,"play",17,C["accent"]); icon(d,1796,86,"pause",17); icon(d,1832,86,"square",16)
    icon(d,1880,86,"panels-top-left",17)
    line(d,(0,112,W,112),C["line"])
    rect(d,(0,1174,W,H),"#101010")
    icon(d,14,1181,"git-branch",13); text(d,35,1181,"main",12,C["muted"])
    icon(d,103,1181,"check",13,C["accent"]); text(d,124,1181,"Local workspace",12,C["muted"])
    text(d,1385,1181,"DESIGN STUDY 05  /  PROPOSED INTERFACE",11,C["muted"])
    text(d,1770,1181,"23 OCT 2026",11,C["faint"])
    return im,d
