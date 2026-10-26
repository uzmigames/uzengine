"""Asset, generation and paint workspace design studies."""
from visual_system import *


def _preview(im, bounds, angle=28):
    paste(im, helmet(angle, 700), bounds, False)


def _table(d,x,y,width,head,rows,columns):
    rect(d,(x,y,x+width,y+28),C["surface"])
    for dx,s in zip(columns,head): text(d,x+dx,y+8,s,12,C["muted"],True)
    for i,values in enumerate(rows):
        yy=y+29+i*31
        if i%2==0: rect(d,(x,yy,x+width,yy+30),C["panel"])
        for dx,s in zip(columns,values): text(d,x+dx,yy+8,s,12,C["muted"])
        line(d,(x,yy+30,x+width,yy+30),C["subtle"])


def _locked(d,x,y,w,label,value):
    field(d,x,y,w,label,value)
    icon(d,x+w-20,y+7,"lock-keyhole",12,C["faint"])


def _tab(d, x, y, label, selected=False):
    text(d, x, y, label, 13, C["text"] if selected else C["muted"])
    if selected:
        line(d, (x, y+24, x+d.textlength(label, font=font(13)), y+24), C["accent"], 2)


def _thumb(im, d, xy, pic, name, kind, selected=False):
    x,y,x2,y2=xy
    paste(im,pic,(x,y,x2,y2-47),"mesh" not in kind.lower())
    rect(d,(x,y2-47,x2,y2),C["selection"] if selected else C["panel"])
    text(d,x+11,y2-38,name,14)
    text(d,x+11,y2-18,kind,12,C["muted"])
    if selected: d.rectangle(xy,outline=C["accent"],width=2)


def assets_screen():
    im,d=shell("Assets","Content / meshes / DamagedHelmetHQ")
    panel(d,(0,113,244,1173),"CONTENT")
    search(d,12,160,220,"Find folder")
    for i,(s,n,dep,sel) in enumerate([
        ("All assets","library",0,False),("Favorites","star",0,False),
        ("Recent","clock",0,False),("Content","folder-open",0,False),
        ("meshes","folder-open",1,False),("DamagedHelmetHQ","folder-open",2,True),
        ("sponza","folder",2,False),("materials","folder",1,False),
        ("textures","folder",1,False),("vfx","folder",1,False),
        ("scripts","folder",1,False),("scenes","folder",1,False)]):
        row(d,0,207+i*32,244,s,n,dep,sel)
    section(d,12,648,220,"COLLECTIONS")
    row(d,0,691,244,"Review queue","inbox")
    row(d,0,727,244,"Generated assets","wand-sparkles")
    row(d,0,763,244,"External sources","link")
    text(d,18,1119,"PROJECT CONTENT",11,C["faint"],True)
    text(d,18,1141,"Local source files",13,C["muted"])

    rect(d,(246,113,1488,1173),C["bg"])
    text(d,270,136,"DamagedHelmetHQ",22,bold=True)
    text(d,270,169,"1 mesh / 4 texture maps",13,C["muted"])
    button(d,1202,133,"Import files",132,True,"import")
    ib(d,1350,134,"folder-plus"); ib(d,1390,134,"ellipsis")
    search(d,270,204,560,"Search this folder")
    button(d,846,202,"All types",117,name="list-filter")
    text(d,1180,213,"Name",13,C["muted"]); icon(d,1233,213,"arrow-down-a-z",16)
    ib(d,1324,204,"layout-grid",True); ib(d,1360,204,"list")
    line(d,(246,248,1488,248))
    items=[
        (helmet(28,700),"DamagedHelmetHQ.uzmesh","Runtime mesh / .uzmesh",True),
        (texture(),"BaseColor","Texture / sRGB",False),
        (texture("MetallicRoughness"),"DH_HQ_ORM","R: AO / G: roughness / B: metal",False),
        (texture("Normal"),"Normal","Texture / linear",False),
        (texture("Emissive"),"DH_HQ_Emissive","Texture / sRGB",False)]
    for i,(pic,name,kind,sel) in enumerate(items):
        x=270+(i%3)*397; y=273+(i//3)*235
        _thumb(im,d,(x,y,x+373,y+214),pic,name,kind,sel)
    section(d,1064,508,373,"PACKED MATERIAL")
    field(d,1075,551,351,"Occlusion","ORM / R")
    field(d,1075,588,351,"Roughness","ORM / G")
    field(d,1075,625,351,"Metallic","ORM / B")
    text(d,1075,681,"4 source maps / 6 material channels",12,C["muted"])
    _tab(d,272,750,"Dependencies",True); _tab(d,400,750,"Import log")
    _tab(d,507,750,"References"); _tab(d,620,750,"Source control")
    folder=ROOT/"sample/content/textures"
    rows=[]
    for name,kind,space in [("DH_HQ_BaseColor.jpg","Base color","sRGB"),
                           ("DH_HQ_ORM.png","AO / roughness / metallic","Linear"),
                           ("DH_HQ_Normal.jpg","Normal","Linear"),
                           ("DH_HQ_Emissive.jpg","Emissive","sRGB")]:
        size=(folder/name).stat().st_size
        rows.append((name,kind,space,f"{size/1024:,.0f} KB","Local"))
    _table(d,270,792,1192,["SOURCE","ROLE","COLOR SPACE","SIZE","LOCATION"],rows,[10,460,750,930,1070])
    section(d,270,1000,1192,"IMPORT RECIPE")
    field(d,282,1042,365,"Scale","Source units")
    field(d,685,1042,365,"Normals","From source")
    field(d,1081,1042,365,"Destination","Project")
    check(d,283,1095,"Review material mapping")
    check(d,687,1095,"Keep source files")
    text(d,273,1144,"5 files  /  1 selected",12,C["muted"])
    text(d,1080,1144,"Import status: not evaluated",12,C["muted"])

    panel(d,(1490,113,1919,1173),"ASSET DETAILS")
    _preview(im,(1505,158,1904,471))
    text(d,1510,486,"DamagedHelmetHQ",21,bold=True)
    badge(d,1510,518,"LOCAL SOURCE",C["muted"])
    _tab(d,1510,558,"Overview",True); _tab(d,1615,558,"Import"); _tab(d,1688,558,"References")
    section(d,1506,605,397,"GEOMETRY")
    for i,(k,v) in enumerate([("Format",".uzmesh"),("Texture maps","4"),("Preview","Runtime capture")]):
        field(d,1515,647+i*38,380,k,v)
    section(d,1506,778,397,"SOURCE")
    field(d,1515,820,380,"Origin","Local project")
    field(d,1515,858,380,"License","Unverified")
    text(d,1515,911,"sample / content / meshes /",13,C["muted"])
    text(d,1515,935,"DamagedHelmetHQ.uzmesh",13,C["muted"])
    section(d,1506,975,397,"VALIDATION")
    text(d,1515,1017,"Source dependencies",13,C["muted"])
    text(d,1776,1017,"4 maps found",13)
    button(d,1510,1067,"Open asset",183,True,"box")
    button(d,1705,1067,"Reveal source",185,False,"folder-open")
    text(d,1510,1125,"Source preview",12,C["faint"])
    section(d,12,827,220,"FILTERS")
    for i,(s,v) in enumerate([("Runtime meshes","1"),("Textures","4")]):
        row(d,0,870+i*32,244,s,"filter",detail=v)
    check(d,18,993,"Include source files")
    check(d,18,1023,"Show dependencies")
    return im


def generate_screen():
    im,d=shell("Generate","Generation / New request")
    panel(d,(0,113,456,1173),"REQUEST / DRAFT")
    section(d,14,158,426,"PROVIDER")
    field(d,22,198,410,"Service","Meshy","chevron-down")
    _locked(d,22,232,410,"Model version","Unknown offline")
    _locked(d,22,266,410,"API capability","Not queried")
    section(d,14,311,426,"PROMPT")
    rect(d,(21,349,434,464),C["field"],C["line"],3)
    for i,s in enumerate(["Compact exploration helmet; ceramic shell,",
                           "graphite seals and amber visor.",
                           "Recessed fasteners, hard-surface prop."]):
        text(d,32,363+i*25,s,14)
    text(d,22,476,"Text to 3D",12,C["muted"]); text(d,306,476,"Local draft",12,C["muted"])
    section(d,14,508,426,"GENERATION PARAMETERS")
    for i,(k,v) in enumerate([("Stage","Preview"),("Seed","Unavailable"),
                              ("Complexity","Unavailable"),("Topology","Unavailable"),
                              ("Symmetry","Unavailable"),("Texture mode","After preview")]):
        _locked(d,22,548+i*34,410,k,v)
    text(d,23,761,"Provider capabilities not loaded",12,C["muted"])
    section(d,14,795,426,"DESTINATION")
    field(d,22,835,410,"Collection","Generated assets")
    field(d,22,869,410,"Format","Pending capability")
    check(d,23,910,"Stage before import")
    check(d,23,939,"Require review")
    line(d,(20,975,435,975))
    field(d,22,989,410,"Cost estimate","Unavailable")
    field(d,22,1023,410,"Remote task ID","Not submitted")
    button(d,22,1070,"Connect provider",410,True,"plug")
    button(d,22,1117,"Save draft",410,False,"save")

    panel(d,(458,113,1433,1173),"REFERENCE WORKSPACE")
    _tab(d,478,162,"3D reference",True)
    _tab(d,610,162,"Images"); _tab(d,705,162,"Candidates (0)")
    ib(d,1348,155,"rotate-3d"); ib(d,1387,155,"maximize")
    _preview(im,(478,201,1022,735))
    text(d,493,211,"LOCAL SOURCE",11,C["muted"])
    rect(d,(1040,201,1415,735),C["panel"])
    section(d,1052,213,351,"REFERENCE PROPERTIES")
    for i,(k,v) in enumerate([("Asset","DamagedHelmetHQ"),
                              ("Texture maps","4"),("Preview","Runtime capture"),
                              ("Source",".uzmesh"),("Usage","Visual reference"),
                              ("Upload","Not submitted")]):
        field(d,1060,255+i*35,337,k,v)
    section(d,1052,483,351,"SOURCE TEXTURE")
    paste(im,texture(),(1061,523,1209,671),True)
    text(d,1222,538,"Base color",13)
    text(d,1222,565,"sRGB",12,C["muted"])
    text(d,1222,592,"Local file",12,C["muted"])
    text(d,1061,696,"DH_HQ_BaseColor.jpg",12,C["muted"])
    for j,(ang,label) in enumerate([(28,"Camera A"),(0,"Camera B"),(90,"Camera C"),(180,"Camera D")]):
        x=478+j*237
        paste(im,helmet(ang,500),(x,754,x+218,898),False)
        if j==0: d.rectangle((x,754,x+218,898),outline=C["accent"])
        text(d,x+8,909,label,12,C["muted"])
    section(d,478,953,937,"REFERENCE MANIFEST")
    _table(d,478,991,937,["FILE","TYPE","ORIGIN","UPLOAD"],
           [("DamagedHelmetHQ.uzmesh","3D reference","Local project","Not uploaded"),
            ("DH_HQ_BaseColor.jpg","Texture","Local project","Not uploaded")],
           [10,420,570,745])
    button(d,479,1116,"Add reference",157,False,"plus")
    text(d,1066,1127,"Remote candidates: 0",13,C["muted"])

    panel(d,(1435,113,1919,1173),"JOB / NOT SUBMITTED")
    section(d,1450,158,454,"EXECUTION")
    steps=[("Draft","Saved locally"),("Submit","Connection required"),
           ("Preview","Not started"),("Refine","Not started"),
           ("Download","Not started"),("Review + import","Not started")]
    for i,(s,detail) in enumerate(steps):
        y=207+i*49
        d.ellipse((1466,y+3,1474,y+11),fill=C["text"] if i==0 else C["faint"])
        if i<5: line(d,(1470,y+15,1470,y+48))
        text(d,1490,y,s,14,C["text"] if i==0 else C["muted"])
        text(d,1691,y,detail,12,C["muted"])
    section(d,1450,519,454,"REMOTE STATE")
    for i,(k,v) in enumerate([("Account","Disconnected"),("Task ID","--"),
                              ("Status","No remote task"),("Credits","Unavailable"),
                              ("Elapsed","--"),("Result expiry","--")]):
        field(d,1459,562+i*34,437,k,v)
    section(d,1450,792,454,"IMPORT GATE")
    for i,s in enumerate(["Source + license","Geometry validation",
                          "Material inspection","Cost confirmation"]):
        check(d,1460,834+i*32,s,False)
    line(d,(1450,984,1904,984))
    icon(d,1462,1006,"lock-keyhole",16)
    text(d,1490,1006,"Submission locked / offline",13,C["muted"])
    button(d,1459,1069,"Inspect request",437,False,"file-code")
    button(d,1459,1116,"Open job log",437,False,"list")
    return im

def paint_screen():
    im,d=shell("Paint","DamagedHelmetHQ / Material roundtrip")
    rect(d,(0,113,54,1173),C["panel"])
    for i,n in enumerate(["mouse-pointer-2","paintbrush","eraser","pipette","stamp","move","rotate-3d"]):
        ib(d,11,132+i*42,n,i==1)
    rect(d,(56,113,1514,151),C["panel"])
    icon(d,73,124,"paintbrush",17)
    text(d,102,124,"Material preview",13)
    for x,s in [(306,"Size  32"),(430,"Opacity  100%"),(612,"Flow  100%")]:
        text(d,x,124,s,13,C["faint"])
    text(d,1230,125,"EZPainter",13)
    badge(d,1332,125,"DISCONNECTED",C["muted"])
    _preview(im,(57,152,988,732),28)
    rect(d,(990,152,1514,732),C["field"])
    rect(d,(990,152,1514,191),C["panel"])
    text(d,1005,165,"2D / BASE COLOR",12,C["muted"],True)
    for i,n in enumerate(["maximize","grid-2x2","scan","rotate-ccw"]):
        ib(d,1348+i*39,155,n)
    paste(im,texture(),(1040,205,1464,629),True)
    text(d,1007,650,"DH_HQ_BaseColor.jpg",13)
    text(d,1007,680,"UV 0",12,C["muted"])
    text(d,1110,680,"Source texture",12,C["muted"])
    text(d,1403,680,"100%",12,C["muted"])
    rect(d,(72,174,266,203),C["panel"],r=3)
    text(d,85,183,"PBR / Runtime capture",12,C["muted"])
    for i,(label,val) in enumerate([("Camera","Runtime"),("View","Capture A"),("Zoom","Fit")]):
        field(d,76+i*285,682,265,label,val)

    panel(d,(56,734,1514,1173),"EXPORT MAPS / DRAFT RECIPE")
    _tab(d,77,782,"Map output",True); _tab(d,196,782,"Material slots")
    _tab(d,339,782,"Import changes"); _tab(d,486,782,"History")
    text(d,1102,783,"Pending provider validation",12,C["muted"])
    maps=[("BaseColor","Base color","JPG","sRGB","RGB","BaseColor"),
          ("MetallicRoughness","AO / roughness / metallic","PNG","Linear","R / G / B","ORM"),
          ("Normal","Normal","JPG","Linear","RGB","Normal"),
          ("Emissive","Emissive","JPG","sRGB","RGB","Emissive")]
    rect(d,(76,825,1494,853),C["surface"])
    for x,label in [(89,"MAP"),(502,"FILE FORMAT"),(684,"COLOR SPACE"),(872,"CHANNELS"),(1054,"OUTPUT SUFFIX"),(1361,"STATUS")]:
        text(d,x,833,label,12,C["muted"],True)
    for i,(key,label,fmt,cs,ch,suffix) in enumerate(maps):
        yy=854+i*55
        if i%2==0: rect(d,(76,yy,1494,yy+54),C["panel"])
        paste(im,texture(key),(88,yy+5,132,yy+49),True)
        text(d,145,yy+20,label,13)
        for x,s in [(505,fmt),(688,cs),(875,ch),(1055,suffix),(1362,"Not exported")]:
            text(d,x,yy+20,s,12,C["muted"])
        line(d,(76,yy+54,1494,yy+54),C["subtle"])
    field(d,87,1091,470,"Material slot","0 / DamagedHelmetHQ")
    field(d,594,1091,425,"Resolution","Source")
    field(d,1057,1091,422,"Overwrite","Review first")
    text(d,88,1140,"Destination: project textures / selected material",12,C["muted"])
    text(d,1181,1140,"4 maps / export not started",12,C["muted"])

    panel(d,(1516,113,1919,1173),"MATERIAL SESSION")
    field(d,1535,159,365,"Material","DamagedHelmetHQ")
    field(d,1535,193,365,"Slot","0 / 1")
    section(d,1532,237,370,"CHANNELS")
    for i,(s,col,value) in enumerate([("Base color","#d6d6d6","sRGB"),("Metallic","#777777","B"),
                                     ("Roughness","#b2b2b2","G"),("Normal","#a1a1a1","RGB"),
                                     ("Ambient occlusion","#d4d4d4","R"),("Emissive","#dddddd","sRGB")]):
        y=273+i*25
        rect(d,(1543,y,1559,y+16),col,r=2)
        text(d,1575,y+1,s,13); text(d,1816,y+1,value,12,C["muted"])
        icon(d,1877,y+1,"eye",14)
    section(d,1532,436,370,"LAYERS / DISCONNECTED")
    rect(d,(1533,473,1901,500),C["field"])
    for x,s in [(1544,"NAME"),(1781,"BLEND"),(1850,"OPACITY")]:
        text(d,x,481,s,10,C["faint"])
    icon(d,1547,517,"layers",17,C["faint"])
    text(d,1577,518,"No remote stack",13,C["muted"])
    for i,n in enumerate(["plus","folder-plus","copy","trash-2"]):
        ib(d,1542+i*37,551,n)
    text(d,1710,560,"0 remote layers",12,C["faint"])
    section(d,1532,600,370,"BRUSH / OFFLINE")
    for i,(k,v) in enumerate([("Size","--"),("Opacity","--"),("Flow","--"),("Spacing","--")]):
        _locked(d,1541,640+i*33,351,k,v)
    section(d,1532,791,370,"ROUNDTRIP")
    field(d,1541,831,351,"Adapter","Not connected")
    field(d,1541,865,351,"Revision","Local source")
    check(d,1543,912,"Review maps before replacement")
    check(d,1543,943,"Keep source textures")
    line(d,(1536,980,1899,980))
    text(d,1543,1000,"Remote changes: unavailable",12,C["muted"])
    text(d,1543,1030,"Local sources: unchanged",12,C["muted"])
    button(d,1541,1070,"Connect EZPainter",351,True,"plug")
    button(d,1541,1117,"Review recipe",351,False,"scan-eye")
    return im

