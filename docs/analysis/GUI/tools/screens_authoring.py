"""Material, rig and animation editor proposals, not implemented tool surfaces."""
from visual_system import *
from screens_diagnostics import curve


def port(d,x,y,color=C["cyan"],square=False):
    if square:
        d.polygon([(x-4,y-5),(x+4,y),(x-4,y+5)],fill=color)
    else:
        d.ellipse((x-4,y-4,x+4,y+4),fill=color)


def wire(d,a,b,color=C["cyan"],bend=None):
    span=max(40,abs(b[0]-a[0])*.52) if bend is None else bend
    curve(d,[a,(a[0]+span,a[1]),(b[0]-span,b[1]),b],color,2)


def graph_background(d,box):
    x,y,x2,y2=box
    rect(d,box,C["field"])
    for xx in range(x+12,x2,24):
        for yy in range(y+12,y2,24):d.point((xx,yy),fill=C["line"])


def graph_node(d,x,y,w,title,rows,selected=False):
    h=47+28*len(rows)
    rect(d,(x,y,x+w,y+h),C["panel"],C["text"] if selected else C["line"],4)
    rect(d,(x+1,y+1,x+w-1,y+30),C["surface"],r=3)
    text(d,x+12,y+8,title,13,bold=True)
    icon(d,x+w-22,y+10,"ellipsis",12)
    for i,(left,right,color) in enumerate(rows):
        yy=y+52+i*28
        if left:port(d,x,yy,color);text(d,x+12,yy-6,left,12,C["muted"])
        if right:
            port(d,x+w,yy,color)
            text(d,x+w-12-d.textlength(right,font=font(12)),yy-6,right,12,color)
    return h


def texture_node(im,d,x,y,w,title,channel="RGB",kind="BaseColor"):
    rect(d,(x,y,x+w,y+160),C["panel"],C["line"],4)
    rect(d,(x+1,y+1,x+w-1,y+30),C["surface"],r=3)
    icon(d,x+10,y+9,"image",14);text(d,x+34,y+8,title,13,bold=True)
    paste(im,texture(kind),(x+12,y+43,x+83,y+114))
    text(d,x+95,y+49,"UV 0",12,C["muted"])
    text(d,x+95,y+74,"sRGB" if kind in ("BaseColor","Emissive") else "Linear",12,C["muted"])
    text(d,x+12,y+137,kind,11,C["faint"])
    if kind=="ORM":
        for offset,label,col in [(99,"R / AO",C["gold"]),(127,"G / rough",C["gold"]),(155,"B / metal",C["blue"])]:
            text(d,x+w-79,y+offset-7,label,11,col);port(d,x+w,y+offset,col)
    else:
        text(d,x+w-52,y+120,channel,12,C["cyan"]);port(d,x+w,y+127)


def document_bar(d,x,y,w,title,items):
    rect(d,(x,y,x+w,y+36),C["panel"])
    icon(d,x+12,y+10,"file-code",15)
    text(d,x+38,y+10,title,13,bold=True)
    xx=x+305
    for label,ico in items:
        icon(d,xx,y+10,ico,15);text(d,xx+23,y+10,label,12,C["muted"])
        xx+=110


def material_screen():
    im,d=shell("Material","DamagedHelmet.uzmat / Material Graph")
    panel(d,(0,113,378,502),"Material preview")
    paste(im,helmet(28,700),(1,147,377,458))
    for x,n in [(12,"circle"),(44,"box"),(76,"cylinder"),(108,"person-standing")]:ib(d,x,463,n,x==108)
    text(d,179,474,"Lit",12);icon(d,206,475,"chevron-down",12)
    text(d,241,474,"Studio",12);icon(d,294,475,"chevron-down",12)
    ib(d,337,463,"rotate-ccw")
    panel(d,(0,504,378,1173),"Parameters")
    search(d,13,551,350,"Filter name or group")
    section(d,0,596,378,"Surface")
    field(d,15,637,347,"Base color","BaseColor", "chevron-down")
    text(d,15,682,"Tint",13,C["muted"])
    rect(d,(164,676,340,702),"#e5e5e5",C["line"],3);icon(d,346,681,"pipette",14)
    field(d,15,716,347,"Metallic","Packed B")
    field(d,15,753,347,"Roughness","Packed G")
    field(d,15,790,347,"Normal strength","1.00")
    field(d,15,827,347,"Occlusion","Packed R")
    field(d,15,860,347,"Emissive","Emissive RGB")
    section(d,0,899,378,"Functions")
    for i,(s,n) in enumerate([("Texture sample","image"),("Normal strength","layers"),("Scalar parameter","hash"),("Vector parameter","palette"),("Multiply","x")]):
        row(d,0,933+i*32,378,s,n)
    button(d,14,1119,"Add parameter",348,name="plus")
    document_bar(d,380,113,1159,"Material Graph",[("Save","save"),("Validate","check"),("Compile","play"),("Find","search")])
    rect(d,(380,150,1539,192),C["surface"])
    text(d,399,164,"DamagedHelmet.uzmat",14,bold=True);text(d,608,165,"Draft graph",12,C["muted"])
    for i,n in enumerate(["mouse-pointer-2","hand","plus","maximize","undo-2","redo-2"]):ib(d,1156+i*35,155,n,i==0)
    text(d,1412,165,"Zoom 80%",12,C["muted"])
    graph_background(d,(380,194,1539,1000))
    text(d,404,209,"Surface inputs",12,C["muted"])
    wire(d,(638,381),(760,319))
    wire(d,(638,542),(760,347),C["gold"])
    wire(d,(946,319),(1206,464))
    wire(d,(976,650),(1206,500),C["blue"])
    wire(d,(976,622),(1206,536),C["gold"])
    wire(d,(976,594),(1206,608),C["gold"])
    wire(d,(638,835),(760,816),C["blue"])
    wire(d,(976,816),(1206,572),C["blue"])
    wire(d,(1176,950),(1206,644),C["cyan"],70)
    texture_node(im,d,416,254,222,"Base color")
    graph_node(d,416,490,222,"Tint",[("","RGB",C["gold"]),("R 1.0  G 1.0  B 1.0","",C["gold"])])
    graph_node(d,760,267,186,"Multiply",[("A","RGB",C["cyan"]),("B","",C["gold"])])
    texture_node(im,d,740,495,236,"Packed ORM","R / G / B","ORM")
    texture_node(im,d,416,708,222,"Normal map","RGB","Normal")
    graph_node(d,760,764,216,"Normal strength",[("Normal","RGB",C["blue"]),("Strength 1.00","",C["gold"])],True)
    texture_node(im,d,1010,823,166,"Emissive","RGB","Emissive")
    rect(d,(1206,365,1515,787),C["panel"],C["line"],4)
    rect(d,(1207,366,1514,407),C["surface"],r=3)
    icon(d,1222,379,"box",17);text(d,1251,378,"Material output",16,bold=True)
    text(d,1225,426,"Surface / Default lit / Opaque",12,C["muted"])
    for i,(label,col) in enumerate([("Base color",C["cyan"]),("Metallic",C["blue"]),("Roughness",C["gold"]),("Normal",C["blue"]),("Ambient occlusion",C["gold"]),("Emissive color",C["cyan"]),("Opacity",C["gold"]),("World position offset",C["blue"]) ]):
        yy=464+i*36;port(d,1206,yy,col);text(d,1225,yy-7,label,13,C["text"] if i<6 else C["faint"])
    panel(d,(380,1002,1539,1173),"Compiler results")
    text(d,569,1012,"Generated HLSL",12,C["muted"]);text(d,704,1012,"Dependencies",12,C["muted"])
    for x,s in [(399,"TARGET"),(605,"STATE"),(850,"DETAIL")]:text(d,x,1053,s,11,C["faint"],True)
    line(d,(397,1074,1521,1074))
    text(d,399,1090,"DX12 / Shader Model",13);text(d,605,1090,"Not compiled",13,C["gold"])
    text(d,850,1090,"Draft graph / no compiler invocation",13,C["muted"])
    text(d,399,1134,"4 source textures",12,C["muted"]);text(d,605,1134,"Instructions: --",12,C["faint"])
    text(d,850,1134,"Runtime preview / graph not executed",12,C["muted"])
    panel(d,(1541,113,1919,1173),"Node details")
    icon(d,1558,167,"layers",22);text(d,1595,166,"Normal strength",18,bold=True)
    text(d,1558,204,"Function / selected node",12,C["muted"])
    section(d,1541,244,378,"Inputs")
    field(d,1557,287,344,"Normal","Normal map.RGB")
    field(d,1557,324,344,"Strength","1.00")
    check(d,1557,367,"Expose strength parameter")
    section(d,1541,407,378,"Parameter")
    for i,(a,b) in enumerate([("Name","NormalStrength"),("Group","Surface"),("Default","1.00"),("Minimum","0.00"),("Maximum","2.00")]):field(d,1557,447+i*38,344,a,b)
    section(d,1541,657,378,"Material settings")
    for i,(a,b) in enumerate([("Domain","Surface"),("Shading","Default lit"),("Blend mode","Opaque")]):field(d,1557,697+i*38,344,a,b,"chevron-down")
    check(d,1557,821,"Two sided",False);check(d,1557,854,"Tangent space normal")
    section(d,1541,899,378,"Source references")
    for i,name in enumerate(["DH_HQ_BaseColor.jpg","DH_HQ_Normal.jpg","DH_HQ_ORM.png","DH_HQ_Emissive.jpg"]):
        row(d,1541,939+i*30,378,name,"image")
    text(d,1557,1070,"Draft / no source files changed",12,C["muted"])
    button(d,1557,1098,"Open source material",344,name="external-link")
    return im


def character_view(im,d,box,view="character_120"):
    """Display an unmodified runtime capture; preserve the full frame and aspect."""
    with Image.open(BASE/"images/sources"/(view+".png")) as capture:
        source=capture.convert("RGB")
    paste(im,source,box,contain=True)


def rig_screen():
    im,d=shell("Rig","MushroomMonster.uzrig / Control Rig")
    panel(d,(0,113,290,1173),"Rig controls / draft")
    search(d,12,158,266,"Find bone, control or space")
    for x,label,active in [(14,"Controls",True),(106,"Skeleton",False),(202,"Spaces",False)]:
        text(d,x,205,label,12,C["text"] if active else C["muted"])
        if active:line(d,(x,226,x+61,226),C["text"],2)
    for i,(label,dep,n) in enumerate([
        ("MushroomMonster",0,"person-standing"),("root_ctrl",1,"circle-dot"),
        ("pelvis_ctrl",2,"circle-dot"),("body_ctrl",3,"circle-dot"),
        ("cap_ctrl",3,"circle-dot"),("cap_offset",4,"circle-dot"),
        ("cap_tip_ctrl",4,"circle-dot"),("Leg 01",2,"folder-open"),
        ("leg_01_root",3,"circle-dot"),("leg_01_ctrl",3,"circle-dot"),
        ("leg_01_pole",3,"diamond"),("Leg 02",2,"folder"),
        ("Leg 03",2,"folder-open"),("leg_03_ctrl",3,"circle-dot"),
        ("leg_03_pole",3,"diamond"),("Leg 04",2,"folder"),
        ("World",1,"axis-3d"),("Body space",1,"axis-3d")]):
        yy=247+i*27;row(d,0,yy,290,label,n,dep,label=="leg_01_ctrl")
        if n.startswith("folder"):icon(d,8+dep*14,yy+6,"chevron-down" if n=="folder-open" else "chevron-right",10)
    section(d,0,758,290,"Solver stack")
    for i,(label,n) in enumerate([("Forward solve","play"),("Leg 01 IK / draft","workflow"),("Leg 02 IK / draft","workflow"),("Leg IK","workflow"),("Constraints","link")]):row(d,0,798+i*31,290,label,n,selected=i==1)
    section(d,0,974,290,"Source skeleton")
    text(d,14,1014,"MushroomMonster_Low.fbx",13)
    text(d,14,1035,"22 source bones",12,C["muted"])
    text(d,14,1056,"Runtime source / fbx_test",12,C["muted"])
    text(d,14,1081,"Controls and solve graph: draft",12,C["muted"])
    button(d,14,1123,"Add control",261,name="plus")
    document_bar(d,292,113,1264,"Control Rig",[("Save","save"),("Validate","check"),("Reset pose","rotate-ccw"),("Bake","clapperboard")])
    rect(d,(292,151,1556,189),C["surface"])
    for x,s,on in [(310,"Viewport",True),(412,"Solve graph",False),(533,"Linked code",False)]:
        text(d,x,163,s,12,C["text"] if on else C["muted"])
        if on:line(d,(x,186,x+68,186),C["text"],2)
    text(d,1235,163,"Frame",12,C["muted"]);text(d,1296,163,"120",12)
    icon(d,1353,163,"chevron-down",12);ib(d,1444,153,"bone",False);ib(d,1480,153,"eye",True);ib(d,1516,153,"maximize")
    character_view(im,d,(292,191,1556,741),"character_120")
    rect(d,(311,210,505,242),C["panel"],C["line"],3)
    icon(d,322,219,"box",15);text(d,348,220,"Runtime / Frame 120",12)
    rect(d,(1370,210,1538,242),C["panel"],C["line"],3)
    text(d,1381,220,"World",12);icon(d,1434,220,"chevron-down",12)
    ib(d,1470,211,"move",True,size=28);ib(d,1505,211,"rotate-3d",size=28)
    text(d,313,693,"Leg 01 / draft",12,C["muted"])
    for x,s,on in [(313,"FK",False),(361,"IK",True)]:
        rect(d,(x,711,x+42,733),C["selection"] if on else C["panel"],C["line"],3)
        text(d,x+11,716,s,11,C["text"] if on else C["muted"])
    text(d,1350,714,"Solver: not evaluated",12,C["muted"])
    panel(d,(292,744,1556,1173),"Forward solve / draft Leg 01")
    text(d,624,755,"Variables",12,C["muted"]);text(d,718,755,"Compiler results",12,C["muted"])
    graph_background(d,(292,779,1556,1173))
    for i,n in enumerate(["mouse-pointer-2","hand","plus","maximize","undo-2"]):ib(d,306+i*34,790,n,i==0)
    text(d,1309,801,"Execution / Draft",12,C["muted"])
    wire(d,(492,902),(850,902),C["text"])
    wire(d,(1154,902),(1230,902),C["text"])
    wire(d,(766,978),(850,1014),C["cyan"])
    wire(d,(766,1112),(850,1042),C["gold"])
    wire(d,(1154,1070),(1230,986),C["cyan"])
    graph_node(d,312,850,180,"Forward solve",[("","Execute",C["text"])])
    graph_node(d,532,926,234,"Get control transform",[("leg_01_ctrl","Transform",C["cyan"]),("Space: World","",C["muted"])])
    graph_node(d,532,1060,234,"Get pole vector",[("leg_01_pole","Vector",C["gold"])])
    graph_node(d,850,850,304,"Two bone IK",[("Execute","Execute",C["text"]),("Root: MushLeg01","",C["muted"]),
        ("Mid: Bone014","",C["muted"]),("Tip: Bone015","",C["muted"]),("Effector","",C["cyan"]),
        ("Pole vector","",C["gold"]),("Weight: 1.00","Transform",C["cyan"])],True)
    graph_node(d,1230,850,301,"Set bone transform",[("Execute","Execute",C["text"]),("Bone: Bone015","",C["muted"]),
        ("Space: Global","",C["muted"]),("Transform","",C["cyan"]),("Propagate: Yes","",C["muted"])])
    panel(d,(1558,113,1919,1173),"Control details")
    icon(d,1575,168,"circle-dot",23,C["cyan"]);text(d,1611,167,"leg_01_ctrl",18,bold=True)
    text(d,1575,205,"Bone015 / draft transform control",12,C["muted"])
    section(d,1558,242,361,"Transform / draft control")
    for i,(label,vals) in enumerate([("Position",("0.0","0.0","0.0")),("Rotation",("0.0","0.0","0.0")),("Scale",("1.0","1.0","1.0"))]):
        yy=284+i*35;text(d,1575,yy+6,label,12,C["muted"])
        for j,(axis,v) in enumerate(zip("XYZ",vals)):
            xx=1657+j*77;rect(d,(xx,yy,xx+70,yy+26),C["field"],C["line"],3)
            text(d,xx+7,yy+7,axis+" "+v,11,mono=True)
    field(d,1575,402,327,"Space","World","chevron-down")
    section(d,1558,451,361,"IK / FK")
    field(d,1575,493,327,"IK blend","1.00")
    line(d,(1575,539,1896,539),C["line"],3);d.ellipse((1888,533,1900,545),fill=C["text"])
    text(d,1575,552,"FK",11,C["muted"]);text(d,1886,552,"IK",11,C["muted"])
    field(d,1575,582,327,"Pole control","leg_01_pole")
    check(d,1575,628,"Maintain end rotation")
    check(d,1575,657,"Allow stretch",False)
    section(d,1558,699,361,"Limits")
    field(d,1575,739,327,"Twist min","-90 deg")
    field(d,1575,776,327,"Twist max","90 deg")
    check(d,1575,821,"Draw limit cone",False)
    section(d,1558,866,361,"Control shape")
    field(d,1575,905,327,"Shape","Circle","chevron-down")
    field(d,1575,942,327,"Scale","1.00")
    text(d,1575,988,"Color",12,C["muted"]);rect(d,(1715,980,1870,1006),C["cyan"],C["line"],3)
    text(d,1575,1043,"Solve result: not evaluated",12,C["muted"])
    button(d,1575,1094,"Reset selected control",327,name="rotate-ccw")
    return im


def diamond(d,x,y,color,size=4):
    d.polygon([(x,y-size),(x+size,y),(x,y+size),(x-size,y)],fill=color)


def animation_screen():
    im,d=shell("Animation","Mushroom_Idle.uzanim / Animation Editor")
    panel(d,(0,113,290,1173),"Animation browser")
    search(d,12,158,265,"Search clips or skeleton")
    section(d,0,203,290,"Open documents")
    row(d,0,248,290,"Mushroom_Idle.uzanim","clapperboard",selected=True)
    text(d,37,281,"Draft / 30 fps",12,C["muted"])
    row(d,0,320,290,"MushroomMonster.uzrig","bone")
    row(d,0,355,290,"MushroomMonster_Low.fbx","person-standing")
    section(d,0,407,290,"Source skeleton / 22 bones")
    search(d,12,449,265,"Filter bones")
    entries=[("Point001",0),("MushroomPelvis",1),("Point005",2),("MushLeg04",3),
             ("Bone011",4),("Bone012",5),("Point004",2),("MushLeg03",3),
             ("Bone008",4),("Bone009",5),("Point002",2),("MushLeg01",3),
             ("Bone014",4),("Bone015",5)]
    for i,(s,dep) in enumerate(entries):row(d,0,491+i*27,290,s,"bone",min(dep,4),s=="Bone015")
    section(d,0,903,290,"Preview settings")
    text(d,13,945,"MushroomMonster",13)
    text(d,13,965,"Source Idle / 4.0 s / 30 fps",12,C["muted"])
    field(d,13,983,263,"Capture","Frame 240")
    field(d,13,1021,263,"Playback","Loop","chevron-down")
    check(d,13,1067,"Show root motion",False)
    check(d,13,1098,"Show skeleton",False)
    button(d,13,1134,"Import clip",263,name="download")
    document_bar(d,292,113,1264,"Animation Editor",[("Save","save"),("Add track","plus"),("Retarget","workflow"),("Export","upload")])
    rect(d,(292,151,1556,188),C["surface"])
    text(d,310,163,"Mushroom_Idle",14,bold=True);text(d,455,164,"Draft",12,C["muted"])
    text(d,1080,163,"Runtime: MushroomMonster",12,C["muted"])
    ib(d,1390,153,"bone");ib(d,1426,153,"move");ib(d,1462,153,"camera");ib(d,1500,153,"maximize")
    character_view(im,d,(292,190,1556,650),"character_240")
    rect(d,(310,208,511,238),C["panel"],C["line"],3)
    icon(d,320,216,"box",15);text(d,346,217,"Perspective / Lit",12)
    text(d,1325,216,"Runtime capture / 240",12,C["muted"])
    rect(d,(309,593,570,631),C["panel"],C["line"],3)
    text(d,322,605,"Mushroom_Idle",13);text(d,440,606,"Frame 240",12,mono=True)
    text(d,1303,617,"Draft keys / independent of capture",12,C["muted"])
    panel(d,(292,652,1556,921),"Dope sheet / draft keys")
    text(d,539,663,"Curves",12,C["muted"]);text(d,615,663,"Notifies",12,C["muted"])
    for i,n in enumerate(["skip-back","step-back","play","step-forward","skip-forward","repeat"]):ib(d,307+i*32,692,n,i==5)
    text(d,525,702,"00:00:00:00",13,mono=True)
    text(d,715,702,"Start 0",12,C["muted"]);text(d,806,702,"End 60",12,C["muted"])
    text(d,901,702,"30 fps",12);icon(d,951,703,"chevron-down",11)
    check(d,1036,702,"Auto key",False)
    icon(d,1248,702,"magnet",15);text(d,1273,702,"Frame",12,C["muted"])
    ib(d,1450,692,"zoom-out");ib(d,1487,692,"maximize")
    left,right,top=590,1534,735
    rect(d,(292,730,1556,760),C["surface"])
    text(d,309,740,"Track",12,C["muted"]);text(d,496,740,"Channels",11,C["muted"])
    for frame in range(0,61,5):
        x=left+(right-left)*frame/60
        line(d,(x,735,x,918),C["line"] if frame%15==0 else C["subtle"])
        text(d,x-6,740,str(frame),10,C["muted"],mono=True)
    tracks=[("root_ctrl",0,C["text"]),("leg_01_ctrl",0,C["text"]),("Location.X",1,C["red"]),
            ("Location.Y",1,C["cyan"]),("Location.Z",1,C["blue"]),("Notifies",0,C["gold"])]
    for i,(name,dep,col) in enumerate(tracks):
        yy=765+i*25
        if i==2:rect(d,(293,yy,1555,yy+24),C["selection"])
        icon(d,310+dep*18,yy+6,"diamond" if dep else "chevron-down",12,col)
        text(d,332+dep*18,yy+6,name,12,col if dep else C["text"])
        if i<5:
            icon(d,512,yy+6,"eye",11,C["faint"]);icon(d,548,yy+6,"lock-keyhole",11,C["faint"])
            for frame in ([0,60] if i==0 else [0,15,30,45,60]):
                diamond(d,left+(right-left)*frame/60,yy+12,col,5 if i<2 else 4)
        else:
            for frame,label in [(12,"idle_marker_a"),(48,"idle_marker_b")]:
                x=left+(right-left)*frame/60
                diamond(d,x,yy+12,C["gold"],4);text(d,x+9,yy+6,label,10,C["gold"])
        line(d,(293,yy+24,1555,yy+24),C["subtle"])
    line(d,(left,731,left,918),C["text"],2)
    d.polygon([(left,821),(left+6,827),(left,833),(left-6,827)],fill=C["red"],outline=C["text"],width=2)
    d.polygon([(left-5,731),(left+5,731),(left,737)],fill=C["text"])
    panel(d,(292,923,1556,1173),"Draft curves / leg_01_ctrl")
    text(d,551,939,"cm",11,C["muted"])
    for i,(s,col) in enumerate([("Location.X",C["red"]),("Location.Y",C["cyan"]),("Location.Z",C["blue"]) ]):
        yy=976+i*31;icon(d,308,yy,"eye",12,col)
        text(d,330,yy,s,12,col);text(d,468,yy,"0.0",12,C["muted"],mono=True)
    text(d,309,1090,"Translation offset",11,C["muted"])
    text(d,309,1113,"Auto tangents",11,C["muted"])
    x0,x1,y0,y1=588,1533,970,1135
    rect(d,(x0,y0,x1,y1),C["field"])
    for j in range(5):
        yy=y0+j*(y1-y0)/4;line(d,(x0,yy,x1,yy),C["subtle"])
        text(d,550,yy-6,str(40-j*10),10,C["faint"],mono=True)
        xx=x0+j*(x1-x0)/4;line(d,(xx,y0,xx,y1),C["line"])
        text(d,xx-4,1143,str(j*15),10,C["muted"],mono=True)
    for col,amp,offset in [(C["red"],131,0),(C["cyan"],62,0),(C["blue"],27,0)]:
        pts=[]
        for t in np.linspace(0,1,180):pts.append((x0+(x1-x0)*t,y1-amp*math.sin(math.pi*t)**2-offset))
        d.line(pts,fill=col,width=2)
        for t in [0,.25,.5,.75,1]:diamond(d,x0+(x1-x0)*t,y1-amp*math.sin(math.pi*t)**2-offset,col,3)
    line(d,(x0,965,x0,1159),C["text"],2)
    text(d,1475,1158,"frames",10,C["muted"])
    panel(d,(1558,113,1919,1173),"Animation details")
    icon(d,1574,168,"clapperboard",23);text(d,1611,167,"Mushroom_Idle",19,bold=True)
    text(d,1574,207,"Idle document / draft authoring state",12,C["muted"])
    section(d,1558,247,361,"Draft clip settings")
    for i,(a,b) in enumerate([("Frame rate","30 fps"),("Range","0 - 60"),("Duration","2.000 s"),("Skeleton","22 bones"),("Root motion","Disabled")]):field(d,1574,287+i*38,328,a,b)
    section(d,1558,500,361,"Selected key")
    field(d,1574,541,328,"Control","leg_01_ctrl")
    field(d,1574,578,328,"Channel","Location.X")
    field(d,1574,615,328,"Frame","0")
    field(d,1574,652,328,"Value","0.00 cm")
    field(d,1574,689,328,"Interpolation","Cubic","chevron-down")
    field(d,1574,726,328,"Tangents","Auto","chevron-down")
    check(d,1574,773,"Weighted tangents",False)
    section(d,1558,819,361,"Events")
    row(d,1558,860,361,"idle_marker_a","flag",detail="12")
    row(d,1558,895,361,"idle_marker_b","flag",detail="48")
    section(d,1558,943,361,"Evaluation")
    text(d,1574,982,"Preview: runtime frame 240",12,C["muted"])
    text(d,1574,1010,"Draft edits: not evaluated",12,C["muted"])
    text(d,1574,1038,"Draft keys are not capture metadata",12,C["muted"])
    button(d,1574,1094,"Export clip",328,name="upload")
    return im
