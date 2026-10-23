"""Detailed neutral scene-authoring workspace, design study 03."""
from visual_system import *

def value(d,x,y,w,s,color=None,dropdown=False):
    rect(d,(x,y,x+w,y+25),C["field"],C["line"],3)
    text(d,x+8,y+6,s,12,color or C["text"],mono=True)
    if dropdown: icon(d,x+w-19,y+7,"chevron-down",11)

def inspector(d):
    panel(d,(1554,113,1919,1173),"Details")
    search(d,1567,158,336,"Search properties")
    icon(d,1570,204,"sun",23,C["gold"]);text(d,1607,203,"Sunlight",18,bold=True)
    text(d,1607,231,"Sponza / Lighting / Sunlight",11,C["faint"])
    for x,s,active in [(1568,"Properties",True),(1660,"Components",False),(1780,"Overrides",False)]:
        text(d,x,263,s,12,C["text"] if active else C["muted"])
        if active: line(d,(x,283,x+72,283),C["text"],2)
    section(d,1554,297,365,"Transform")
    icon(d,1880,304,"rotate-ccw",13)
    for i,(label,vals) in enumerate([("Location",("0.0","0.0","0.0")),("Rotation",("-28.0","42.0","0.0")),("Scale",("1.0","1.0","1.0"))]):
        yy=336+i*31;text(d,1570,yy+6,label,12,C["muted"])
        for j,(axis,v) in enumerate(zip("XYZ",vals)):
            xx=1655+j*77
            value(d,xx,yy,70,axis+" "+v)
            line(d,(xx,yy+24,xx+70,yy+24),[C["red"],C["cyan"],C["blue"]][j])
    icon(d,1896,404,"lock-keyhole",12)
    text(d,1570,438,"Mobility",12,C["muted"])
    for x,s,on in [(1655,"Static",False),(1732,"Stationary",False),(1820,"Movable",True)]:
        rect(d,(x,431,x+75,456),C["selection"] if on else C["field"],C["line"],3)
        text(d,x+7,438,s,11,C["text"] if on else C["muted"])
    section(d,1554,473,365,"Directional light")
    rows=[("Intensity","75,000 lx"),("Temperature","6,500 K"),("Source angle","0.535 deg"),
          ("Indirect intensity","1.00"),("Volumetric scale","1.00")]
    for i,(a,b) in enumerate(rows):
        y=511+i*31;text(d,1570,y+6,a,13,C["muted"]);value(d,1731,y,164,b)
    text(d,1570,679,"Light color",12,C["muted"])
    rect(d,(1731,672,1860,696),"#f8f5e7",C["line"],3)
    ib(d,1867,669,"pipette",size=28)
    check(d,1570,713,"Use temperature")
    check(d,1741,713,"Affect world")
    section(d,1554,747,365,"Shadows")
    check(d,1570,786,"Cast shadows");check(d,1741,786,"Contact shadows")
    for i,(a,b) in enumerate([("Shadow method","Project default"),("Depth bias","0.50"),("Normal offset","0.005")]):
        y=815+i*31;text(d,1570,y+6,a,12,C["muted"]);value(d,1731,y,164,b,dropdown=i==0)
    section(d,1554,924,365,"Atmosphere and visibility")
    check(d,1570,962,"Atmosphere sun light")
    check(d,1570,992,"Affect volumetric fog")
    check(d,1570,1022,"Visible in editor")
    check(d,1741,1022,"Visible in game")
    line(d,(1568,1054,1901,1054))
    icon(d,1570,1074,"file-code",15);text(d,1594,1074,"Open component source",12)
    button(d,1569,1114,"Add component",333,name="plus")
    text(d,1569,1156,"Illustrative properties / not capture settings",10,C["faint"])

def outliner(d):
    panel(d,(0,113,260,1173),"Outliner")
    search(d,12,158,236,"Search name, type or tag")
    for i,n in enumerate(["plus","folder-plus","list-filter","eye","lock-keyhole"]): ib(d,12+i*33,195,n)
    text(d,14,235,"ENTITY",10,C["faint"],True)
    icon(d,213,235,"eye",11);icon(d,238,235,"lock-keyhole",11)
    entries=[
        ("Sponza","folder-open",0),("Architecture","folder-open",1),
        ("Arcade_North","box",2),("Arch_Entrance","box",2),
        ("Arch_Upper","box",2),("Columns","folder-open",2),
        ("Column_A_01","box",3),("Column_A_02","box",3),
        ("Column_B_01","box",3),("Column_B_02","box",3),
        ("Floor","box",2),("Walls","folder",2),("Windows","folder",2),
        ("Lighting","folder-open",1),("Sunlight","sun",2),
        ("SkyAtmosphere","cloud-sun",2),("SkyLight","sunrise",2),
        ("ReflectionProbe","scan",2),("Cameras","folder-open",1),
        ("Camera_A","camera",2),("Camera_C","camera",2),("Environment","folder",1)]
    for i,(s,n,depth) in enumerate(entries):
        yy=259+i*24
        row(d,12,yy,248,s,n,depth,s=="Sunlight")
        if n in ["folder","folder-open"]:
            icon(d,8+depth*14,yy+6,"chevron-down" if n=="folder-open" else "chevron-right",10)
        if n not in ["folder","folder-open"]:
            icon(d,214,yy+5,"eye",11,C["faint"])
        if "Camera" in s:icon(d,239,yy+5,"lock-keyhole",11,C["faint"])
    line(d,(255,251,255,797),C["field"],3)
    line(d,(255,310,255,454),C["faint"],3)
    text(d,14,806,"1 selected",11,C["muted"]);text(d,156,806,"Hierarchy view",11,C["faint"])
    section(d,0,838,260,"Collections")
    for i,(s,n) in enumerate([("Lighting rig","sun"),("Camera bookmarks","camera"),("Source assets","layers")]):
        row(d,0,875+i*29,260,s,n)
    section(d,0,977,260,"Selection context")
    field(d,14,1014,232,"Layer","Lighting")
    field(d,14,1048,232,"Type","Directional")
    row(d,0,1095,260,"Locate in scene","scan")
    row(d,0,1126,260,"Copy entity reference","link")

def content_browser(im,d):
    panel(d,(262,904,1550,1173),"Content browser")
    text(d,425,914,"Output log",12,C["muted"]);text(d,526,914,"Jobs",12,C["muted"])
    text(d,588,914,"Source control",12,C["muted"])
    text(d,278,955,"Content",12);icon(d,332,956,"chevron-right",12)
    text(d,354,955,"All assets",12,C["muted"])
    search(d,534,945,477,"Search assets")
    for i,n in enumerate(["list-filter","layout-grid","list","sliders-horizontal"]):ib(d,1028+i*33,944,n,i==1)
    button(d,1374,944,"Import assets",157,name="download")
    pics=[source_scene(),helmet(28,400),texture(),texture("Normal"),
          texture("ORM"),texture("Emissive"),source_reference(),None]
    labels=[("Sponza","Scene"),("DamagedHelmetHQ","Compiled mesh"),("BaseColor","Texture / sRGB"),
            ("Normal","Texture / Linear"),("ORM","Texture / Linear"),
            ("Emissive","Texture / sRGB"),("Material Showcase","Scene"),("MushroomMonster","Skeleton")]
    for i,(pic,(name,kind)) in enumerate(zip(pics,labels)):
        x=278+i*157
        rect(d,(x,989,x+143,1080),C["field"])
        if pic is not None:paste(im,pic,(x,989,x+143,1080))
        else:icon(d,x+55,1019,"file-box",31,C["faint"])
        if i==0:d.rectangle((x,989,x+143,1080),outline=C["text"],width=1)
        text(d,x,1091,name,12)
        text(d,x,1110,kind,11,C["muted"])
    line(d,(277,1136,1535,1136),C["subtle"])
    text(d,278,1149,"8 visible   /   1 selected",11,C["muted"])
    icon(d,516,1148,"link",14);text(d,538,1149,"Source references",11,C["muted"])
    text(d,1183,1149,"Thumbnail size",11,C["muted"])
    line(d,(1283,1157,1416,1157),C["faint"],2);d.ellipse((1340,1152,1349,1161),fill=C["text"])
    icon(d,1507,1148,"settings-2",14)

def scene_screen():
    im,d=shell("Scene","Sponza.uzscene")
    outliner(d);inspector(d)
    rect(d,(262,113,1550,143),C["panel"])
    rect(d,(270,116,353,140),C["selection"],r=3)
    icon(d,280,122,"box",13);text(d,302,122,"Scene",12)
    icon(d,368,122,"file-code",13);text(d,390,122,"Linked code",12,C["muted"])
    icon(d,490,121,"sun",14);text(d,514,121,"Lighting profile",12,C["muted"])
    icon(d,680,121,"link",13);text(d,703,121,"scene_setup.lua",12,C["muted"])
    icon(d,1485,121,"columns-2",14);icon(d,1521,121,"ellipsis",14)
    viewport_tools(d,262,144,1288)
    paste(im,source_scene(),(262,180,1550,903))
    rect(d,(280,858,469,888),"#242424",C["line"],4)
    icon(d,291,865,"camera",16);text(d,316,867,"Camera A",12)
    icon(d,434,866,"lock-keyhole",14)
    rect(d,(1444,200,1530,286),"#242424",C["line"],4)
    icon(d,1473,227,"box",25)
    text(d,1483,206,"Z",10,C["blue"]);text(d,1511,250,"X",10,C["red"]);text(d,1454,258,"Y",10,C["cyan"])
    rect(d,(1340,858,1531,888),"#242424",C["line"],4)
    icon(d,1350,866,"gauge",15);text(d,1374,867,"Camera speed",11,C["muted"]);text(d,1470,867,"1.0",12,mono=True)
    content_browser(im,d)
    return im
