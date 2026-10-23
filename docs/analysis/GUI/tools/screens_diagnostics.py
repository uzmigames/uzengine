"""VFX and diagnostic workspace studies; simulations remain illustrative."""
from visual_system import *

def curve(d,points,c,width=2):
    pts=[]
    p0,p1,p2,p3=[np.array(p,dtype=float) for p in points]
    for t in np.linspace(0,1,60):
        p=(1-t)**3*p0+3*(1-t)**2*t*p1+3*(1-t)*t*t*p2+t**3*p3
        pts.append(tuple(p))
    d.line(pts,fill=c,width=width)

def node(d,x,y,w,name,inputs,output,color):
    rect(d,(x,y,x+w,y+115),C["panel"],C["line"],5)
    rect(d,(x,y,x+w,y+29),C["surface"],None,5)
    rect(d,(x,y,x+3,y+115),color)
    text(d,x+13,y+8,name,13,bold=True)
    for i,s in enumerate(inputs):
        yy=y+49+i*25
        d.ellipse((x-4,yy,x+4,yy+8),fill=color)
        text(d,x+13,yy-3,s,12,C["muted"])
    d.ellipse((x+w-4,y+53,x+w+4,y+61),fill=color)
    text(d,x+w-70,y+50,output,12,color)

def vfx_screen():
    im,d=shell("VFX","EnergyRibbon.uzvfx")
    panel(d,(0,113,284,1173),"System stack")
    search(d,12,157,258,"Search modules")
    row(d,0,204,284,"EnergyRibbon","waypoints",selected=True)
    text(d,16,246,"EMITTER 01",11,C["faint"],True)
    modules=[("Emitter settings","settings-2"),("Spawn rate","plus"),("Initialize particle","circle-dot"),
             ("Velocity","move-up-right"),("Curl force","wind"),("Color over life","palette"),
             ("Size over life","scaling"),("Ribbon renderer","waves")]
    for i,(name,ico) in enumerate(modules):
        yy=280+i*53
        row(d,0,yy,284,name,ico,1,name=="Color over life")
        if i<7:
            line(d,(26,yy+28,26,yy+46),C["line"])
            text(d,50,yy+25,["System","Spawn","Spawn","Update","Update","Update","Update"][i],10,C["faint"])
    button(d,14,742,"Add module",253,name="plus")
    section(d,0,803,284,"Dependencies")
    row(d,0,846,284,"M_EnergyRibbon","circle-dot")
    row(d,0,878,284,"T_SoftTrail","image")
    row(d,0,910,284,"ColorGradient","palette")
    section(d,0,968,284,"Preview")
    field(d,14,1010,255,"Simulation","Local preview","chevron-down")
    field(d,14,1047,255,"Playback","Loop","chevron-down")
    text(d,14,1128,"Synthetic effect illustration",11,C["faint"])
    panel(d,(286,113,1588,603),"Preview")
    rect(d,(286,147,1588,603),"#121212")
    for j in range(10):
        yy=360+j*25
        line(d,(286,yy,1588,yy),"#242424")
    for xx in range(286,1589,98): line(d,(935,258,xx,603),"#252525")
    glow=Image.new("RGB",(1302,456),"black");gd=ImageDraw.Draw(glow)
    rng=np.random.default_rng(61)
    for j in range(8):
        pts=[]
        for t in np.linspace(0,1,160):
            xx=190+940*t
            yy=214+85*math.sin(t*math.pi*2.1+j*.08)+(j-3)*5*(1-t)
            pts.append((xx,yy))
        gd.line(pts,fill=(80,210,218) if j<5 else (210,226,138),width=3 if j<5 else 1)
        d.line([(x+286,y+147) for x,y in pts],fill=C["cyan"] if j<5 else C["accent"],width=2)
    gl=np.array(glow.filter(ImageFilter.GaussianBlur(15)),dtype=np.uint16)
    bg=np.array(im.crop((286,147,1588,603)),dtype=np.uint16)
    im.paste(Image.fromarray(np.minimum(bg+gl*.8,255).astype(np.uint8)),(286,147))
    d=ImageDraw.Draw(im)
    for i in range(175):
        t=rng.uniform(0,1);xx=476+940*t
        yy=361+85*math.sin(t*math.pi*2.1)+rng.normal(0,22)
        rr=rng.uniform(.7,2.3)
        d.ellipse((xx-rr,yy-rr,xx+rr,yy+rr),fill=C["cyan"] if i%3 else C["accent"])
    rect(d,(306,164,524,193),C["panel"],None,4)
    icon(d,318,172,"box",14);text(d,342,172,"Perspective",12)
    icon(d,444,173,"chevron-down",12);icon(d,477,171,"maximize",15)
    text(d,309,571,"Preview illustration",11,C["faint"])
    panel(d,(286,605,1588,1087),"Particle update graph")
    rect(d,(286,640,1588,1087),"#171717")
    for xx in range(300,1590,22):
        for yy in range(655,1085,22): d.point((xx,yy),fill="#343434")
    for x,n in [(305,"mouse-pointer-2"),(339,"hand"),(373,"plus"),(407,"zoom-out"),(441,"maximize")]: ib(d,x,651,n)
    for points,col in [
        ([(524,814),(589,814),(590,793),(656,793)],C["cyan"]),
        ([(867,793),(925,793),(924,814),(984,814)],C["accent"]),
        ([(1195,814),(1244,814),(1239,814),(1292,814)],C["cyan"]),
        ([(866,981),(924,981),(925,839),(984,839)],C["gold"])]:
        curve(d,points,col,2)
    node(d,315,757,208,"Particle age",["Normalized age"],"float",C["cyan"])
    node(d,657,736,209,"Sample gradient",["Time","Gradient"],"color",C["accent"])
    node(d,657,924,209,"Intensity",["Value   2.40"],"float",C["gold"])
    node(d,985,757,209,"Multiply",["Color","Intensity"],"color",C["cyan"])
    node(d,1293,757,251,"Set particle color",["Color","Alpha"],"particle",C["accent"])
    d.rectangle((649,728,874,859),outline=C["accent"],width=1)
    panel(d,(1590,113,1919,1087),"Module properties")
    icon(d,1608,166,"palette",24,C["accent"]);text(d,1645,166,"Color over life",18,bold=True)
    text(d,1608,211,"Particle update",12,C["muted"])
    section(d,1590,250,329,"Gradient")
    gradient=np.zeros((27,290,3),dtype=np.uint8)
    stops=[np.array([218,238,137]),np.array([114,220,213]),np.array([52,96,111])]
    for xx in range(290):
        t=xx/289*2;i=min(int(t),1);gradient[:,xx]=(stops[i]*(1-(t-i))+stops[i+1]*(t-i)).astype(np.uint8)
    im.paste(Image.fromarray(gradient),(1608,296))
    for xx in [1608,1752,1897]:d.polygon([(xx-5,328),(xx+5,328),(xx,336)],fill=C["text"])
    field(d,1608,358,290,"Interpolation","Linear","chevron-down")
    section(d,1590,413,329,"Alpha curve")
    rect(d,(1608,457,1899,691),C["field"])
    for k in range(5):
        line(d,(1608,457+k*57,1899,457+k*57))
        line(d,(1608+k*72,457,1608+k*72,691))
    curve(d,[(1617,674),(1664,446),(1790,419),(1890,674)],C["accent"],3)
    for xx,yy in [(1617,674),(1694,486),(1814,521),(1890,674)]:
        rect(d,(xx-4,yy-4,xx+4,yy+4),C["accent"])
    text(d,1608,703,"0.0",11,C["faint"]);text(d,1865,703,"1.0",11,C["faint"])
    field(d,1608,747,290,"Key time","0.25")
    field(d,1608,786,290,"Value","0.90")
    field(d,1608,825,290,"Tangents","Auto","chevron-down")
    section(d,1590,881,329,"Binding")
    row(d,1590,924,329,"Particle.Color","circle-dot")
    row(d,1590,957,329,"NormalizedAge","timer")
    text(d,1608,1022,"1 module selected",12,C["muted"])
    rect(d,(286,1089,1919,1173),C["panel"])
    for i,n in enumerate(["skip-back","play","pause","skip-forward","repeat"]):ib(d,304+i*34,1101,n,i==1)
    text(d,491,1110,"01:12",13,mono=True)
    for i in range(17):
        xx=594+i*76;line(d,(xx,1104,xx,1150));text(d,xx,1154,str(i*15),10,C["faint"],mono=True)
    rect(d,(594,1127,1820,1143),"#3d3d3d",None,2)
    line(d,(946,1095,946,1171),C["accent"],2)
    return im

def diagnostics_screen():
    im,d=shell("Diagnostics","Capture review")
    panel(d,(0,113,275,1173),"Capture library")
    search(d,12,157,249,"Find run or capture")
    text(d,14,208,"CURRENT SESSION",11,C["faint"],True)
    row(d,0,244,275,"Sponza / Camera A","image",selected=True)
    text(d,37,276,"Frame 680  /  DX12",12,C["muted"])
    row(d,0,320,275,"Material showcase","image")
    text(d,37,352,"Frame 240  /  DX12",12,C["muted"])
    section(d,0,411,275,"Available output")
    row(d,0,456,275,"Final output","image",selected=True)
    row(d,0,490,275,"Run manifest","file-json")
    row(d,0,524,275,"Execution log","file-text")
    text(d,15,583,"PASS CAPTURE",11,C["faint"],True)
    for i,s in enumerate(["GBuffer","Lighting","Volumetrics","Post processing"]):
        row(d,0,618+i*32,275,s,"layers")
    text(d,15,765,"Not collected in this run",12,C["gold"])
    section(d,0,821,275,"Comparison")
    field(d,14,868,245,"Reference","Not attached")
    text(d,14,918,"Falcor match",12,C["muted"])
    text(d,14,944,"Pending",16,C["gold"])
    button(d,14,1010,"Attach reference",245,name="plus")
    text(d,14,1137,"7 runs / 8 retained frames",12,C["muted"])
    panel(d,(277,113,1556,886),"Sponza / Camera A","Final output")
    paste(im,source_scene(),(277,148,1556,868))
    rect(d,(298,169,559,198),C["panel"],None,4)
    icon(d,310,176,"image",15);text(d,336,177,"Frame 680",12)
    text(d,424,177,"1920 x 1080",12,C["muted"])
    panel(d,(1558,113,1919,1173),"Run details")
    icon(d,1577,164,"check",20,C["accent"]);text(d,1610,166,"Capture complete",18,bold=True)
    for i,(lab,val) in enumerate([("Backend","DX12"),("Adapter","RTX 4090"),("Frame","680"),("Assets ready","Yes"),("Preload capture","No"),("Process exit","0")]):
        field(d,1576,215+i*35,323,lab,val)
    section(d,1558,445,361,"Validation boundary")
    text(d,1576,486,"Source run captured",14,C["accent"])
    text(d,1576,519,"Matched benchmark not run",13,C["muted"])
    text(d,1576,552,"Falcor comparison pending",13,C["muted"])
    section(d,1558,604,361,"Warnings in source log")
    warnings=[("Shader warmup","SkyCubemapCapturePS"),
              ("VFX hot reload","Directory unavailable"),
              ("Font atlas","Missing glyph warnings")]
    for i,(head,desc) in enumerate(warnings):
        yy=650+i*91
        icon(d,1577,yy,"triangle-alert",16,C["gold"])
        text(d,1604,yy,head,14)
        text(d,1604,yy+27,desc,12,C["muted"])
    button(d,1577,967,"Open execution log",322,name="file-text")
    button(d,1577,1012,"Open run manifest",322,name="file-json")
    text(d,1577,1137,"Timing is not a matched baseline",11,C["faint"])
    panel(d,(277,888,1556,1173),"Jobs and artifacts")
    for x,s in [(296,"OPERATION"),(732,"STATE"),(953,"ARTIFACT"),(1321,"ACTION")]:
        text(d,x,937,s,11,C["faint"],True)
    jobrows=[("Capture / Sponza Camera A","Exit 0","sponza_680.png","Open"),
             ("Capture / DamagedHelmet A","Exit 0","damaged-quarter_240.png","Open"),
             ("Mushroom / shutdown leak","Exit 1","character-run.log","Inspect")]
    for i,(name,status,out,act) in enumerate(jobrows):
        y=975+i*55
        line(d,(294,y-10,1537,y-10),C["subtle"])
        icon(d,296,y+4,"check" if i<2 else "triangle-alert",16,C["accent"] if i<2 else C["gold"])
        text(d,324,y+4,name,14)
        text(d,732,y+4,status,13,C["accent"] if i<2 else C["gold"])
        text(d,953,y+4,out,12,C["muted"],mono=True)
        text(d,1321,y+4,act,13,C["cyan"])
        icon(d,1498,y+4,"arrow-up-right",16)
    return im
