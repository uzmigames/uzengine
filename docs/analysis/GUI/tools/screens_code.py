"""Code and agent workspaces: static design studies, not runtime contracts."""
from visual_system import *
import re


def code_line(d, x, y, source, n=17):
    pattern = r'("[^"\n]*"|\'[^\'\n]*\'|--.*|\b(?:local|function|end|if|then|return|for|in|do|nil|true|false|or|and)\b|\b\d+(?:\.\d+)?\b)'
    for token in re.split(pattern, source):
        color = C['muted'] if token.startswith('--') else C['gold'] if token.startswith(('"', "'")) else C['cyan'] if re.fullmatch(r'\d+(?:\.\d+)?', token) else C['blue'] if token in ('local','function','end','if','then','return','for','in','do','nil','true','false','or','and') else C['text']
        text(d,x,y,token,n,color,mono=True)
        x += d.textlength(token,font=font(n,mono=True))


def small_label(d,x,y,label):
    text(d,x,y,label,11,C['faint'],True)


def code_screen():
    im,d=shell('Code','scripts / orbit_preview.lua')
    rect(d,(0,113,46,1173),C['panel'])
    for i,name in enumerate(['files','search','git-branch','bug','blocks']): ib(d,8,127+i*47,name,i==0)
    ib(d,8,1128,'settings-2')
    panel(d,(48,113,298,1173),'EXPLORER')
    search(d,61,158,222,'Filter files')
    section(d,49,201,248,'STUDIO')
    files=[('content','folder-open',0),('scenes','folder',1),('Sponza.uzscene','file',2),('materials','folder',1),('meshes','folder',1),('scripts','folder-open',0),('orbit_preview.lua','file-code',1),('asset_batch.py','file-code',1),('player.lua','file-code',1),('runtime','folder-open',0),('camera_controller.cpp','file-code',1),('camera_controller.h','file-code',1),('tests','folder',0),('studio.uzproj','file-cog',0)]
    for i,(s,n,dep) in enumerate(files): row(d,49,239+i*29,248,s,n,dep,s=='orbit_preview.lua')
    section(d,49,718,248,'OUTLINE')
    for i,s in enumerate(['OrbitPreview','new(options)','step(delta_time)','reset()']): row(d,50,756+i*28,245,s,'braces',i>0)
    section(d,49,914,248,'WORKSPACE')
    for i,(s,v) in enumerate([('Lua language server','Not connected'),('Interpreter','Not selected'),('Build profile','Debug / Windows')]):
        text(d,64,960+i*59,s,13,C['muted']);text(d,64,980+i*59,v,13,C['gold'] if i<2 else C['text'])
    rect(d,(300,113,1539,1173),'#191919')
    rect(d,(300,113,1539,153),C['panel'])
    ib(d,311,118,'save');ib(d,346,118,'undo-2');ib(d,381,118,'redo-2')
    line(d,(424,123,424,143))
    icon(d,441,127,'braces',16);text(d,468,127,'OrbitPreview.step',14)
    icon(d,605,129,'chevron-down',12)
    line(d,(636,123,636,143))
    ib(d,651,118,'play');ib(d,687,118,'bug')
    text(d,734,127,'Launch target: not configured',13,C['muted'])
    icon(d,952,129,'chevron-down',12)
    ib(d,1305,118,'list-filter');ib(d,1341,118,'wrap-text');ib(d,1377,118,'search')
    line(d,(1424,123,1424,143))
    ib(d,1460,118,'columns-2');ib(d,1500,118,'ellipsis')
    text(d,322,167,'scripts',13,C['muted']);icon(d,370,169,'chevron-right',12);text(d,394,167,'orbit_preview.lua',13);icon(d,516,169,'chevron-right',12);text(d,540,167,'OrbitPreview.step',13,C['cyan'])
    line(d,(300,193,1539,193),C['subtle'])
    lines=['-- Preview-only orbit controller; illustrative editor document.','-- Independent Lua example, not an engine scripting API.','','local OrbitPreview = {}','OrbitPreview.__index = OrbitPreview','','function OrbitPreview.new(options)','    local self = setmetatable({}, OrbitPreview)','    self.radius = options.radius or 4.0','    self.speed = options.speed or 0.25','    self.angle = 0.0','    self.height = options.height or 1.5','    return self','end','','function OrbitPreview:step(delta_time)','    if delta_time < 0.0 then','        return nil, "delta_time must be non-negative"','    end','','    self.angle = (self.angle + delta_time * self.speed)','        % (2.0 * math.pi)','','    return {','        x = math.cos(self.angle) * self.radius,','        y = self.height,','        z = math.sin(self.angle) * self.radius,','    }','end','','function OrbitPreview:reset()','    self.angle = 0.0','end','','return OrbitPreview']
    for i,s in enumerate(lines):
        yy=207+i*19
        if i==20: rect(d,(301,yy-2,1484,yy+18),'#2c2c2c')
        text(d,323,yy,str(i+1).rjust(2),14,C['faint'],mono=True);code_line(d,375,yy,s,16)
        if i>6 and i<28: line(d,(393,yy+1,393,yy+17),C['subtle'])
        for j in range(min(len(s)//6,15)):
            rect(d,(1496+j*2,207+i*3,1497+j*2,208+i*3),C['muted'] if s.startswith('--') else '#626262')
    d.rectangle((1488,202,1530,872),outline='#353535')
    d.rectangle((1488,202,1530,321),outline='#777777')
    rect(d,(1534,202,1538,872),'#242424');rect(d,(1534,202,1538,512),'#555555')
    for yy in [319,338,585,604]:rect(d,(302,yy,305,yy+17),'#aaaaaa')
    line(d,(862,607,862,624),C['accent'],2)
    rect(d,(300,887,1539,1173),C['field'])
    rect(d,(300,887,1539,924),C['panel'])
    for x,s,active in [(320,'Problems',True),(417,'Terminal',False),(520,'Output',False),(601,'Debug console',False)]:
        text(d,x,900,s,13,C['text'] if active else C['muted'])
        if active: line(d,(x,923,x+65,923),C['accent'],2)
    icon(d,1350,900,'list-filter',15);text(d,1374,900,'All sources',12,C['muted']);ib(d,1460,891,'refresh-cw');ib(d,1500,891,'chevron-up')
    search(d,322,936,514,'Filter diagnostics');text(d,864,943,'Workspace',13,C['muted']);icon(d,950,944,'chevron-down',13)
    rect(d,(301,976,1539,1006),C['panel'])
    for xx,label in [(323,'SOURCE'),(564,'STATE'),(806,'DETAIL'),(1352,'ACTION')]:small_label(d,xx,985,label)
    for i,(name,state,detail,action) in enumerate([('LuaLS','Disconnected','No language diagnostics received','Connect'),('clangd','Not configured','Compilation database not selected','Configure'),('Python','No environment','Interpreter selection required','Select')]):
        yy=1018+i*33;icon(d,323,yy,'circle-alert',15);text(d,348,yy,name,14);text(d,564,yy,state,13,C['muted']);text(d,806,yy,detail,13,C['muted']);text(d,1352,yy,action,13);icon(d,1437,yy,'arrow-up-right',14)
    line(d,(301,1129,1539,1129),C['subtle']);text(d,321,1143,'Lua',12,C['cyan']);text(d,379,1143,'UTF-8',12,C['muted']);text(d,451,1143,'LF',12,C['muted']);text(d,503,1143,'4 spaces',12,C['muted']);icon(d,615,1141,'git-branch',14);text(d,638,1143,'main / 1 modified buffer',12,C['muted']);text(d,1322,1143,'Ln 21, Col 58',12,C['muted'])
    panel(d,(1542,113,1919,1173),'CONTEXT',subtitle='Current file')
    text(d,1561,172,'Orbit preview',20,bold=True);text(d,1561,203,'Document context',13,C['muted'])
    section(d,1543,237,376,'ATTACHED TO NEXT REQUEST')
    row(d,1544,280,370,'orbit_preview.lua','file-code',selected=True)
    row(d,1544,313,370,'Selection: lines 16-29','text-select')
    row(d,1544,346,370,'Workspace instructions','book-open')
    text(d,1561,383,'Buffer revision 12 / unsaved',12,C['muted'])
    button(d,1560,407,'Add context',140,name='plus');ib(d,1830,407,'refresh-cw');ib(d,1870,407,'trash-2')
    line(d,(1561,450,1899,450));small_label(d,1561,473,'ASSISTANT')
    field(d,1561,500,338,'Provider','Codex','chevron-down')
    field(d,1561,537,338,'Model','Not selected','chevron-down')
    field(d,1561,574,338,'Scope','Read-only','chevron-down')
    rect(d,(1560,622,1900,748),C['field'],C['line'],4)
    text(d,1574,637,'Review angle wrapping and',15);text(d,1574,661,'invalid delta-time behavior.',15)
    icon(d,1574,715,'paperclip',16);text(d,1599,717,'3 context items',12,C['muted'])
    button(d,1767,762,'Ask agent',133,True,'arrow-up')
    text(d,1561,818,'Session',13,C['muted']);text(d,1735,818,'Not started',13)
    text(d,1561,849,'Cost estimate',13,C['muted']);text(d,1735,849,'Unavailable',13)
    line(d,(1561,886,1899,886));text(d,1561,910,'Permissions',14,bold=True)
    for i,(s,n) in enumerate([('Read attached files','check'),('Write files: approval required','lock-keyhole'),('Run commands: approval required','lock-keyhole')]):
        icon(d,1562,950+i*36,n,15,C['muted']);text(d,1589,950+i*36,s,13,C['muted'])
    text(d,1561,1112,'No request sent',13,C['faint']);icon(d,1878,1138,'panel-right-close',18)
    return im


def agent_screen():
    im,d=shell('Agents','scripts / orbit_preview.lua')
    panel(d,(0,113,297,1173),'DOCUMENT SESSIONS')
    button(d,16,161,'New session',264,name='plus');search(d,16,209,264,'Search sessions')
    small_label(d,17,264,'ORBIT_PREVIEW.LUA')
    rect(d,(9,291,287,389),C['selection'],r=4)
    icon(d,23,308,'git-pull-request',18,C['accent']);text(d,52,307,'Orbit preview',15,bold=True)
    text(d,52,336,'Awaiting review',13,C['accent']);text(d,52,361,'Codex  /  proposed change',12,C['muted'])
    icon(d,23,418,'message-square',17);text(d,52,416,'Material import notes',14);text(d,52,442,'Claude Agent / not started',12,C['muted'])
    section(d,1,506,295,'CHANGE SET')
    row(d,1,552,295,'orbit_preview.lua','file-code',selected=True,detail='M')
    row(d,1,586,295,'1 file / +8 -2','git-compare-arrows')
    section(d,1,650,295,'CONTEXT')
    for i,(s,n) in enumerate([('Workspace instructions','book-open'),('orbit_preview.lua','file-code'),('Selection: lines 16-29','text-select')]):row(d,1,695+i*32,295,s,n)
    text(d,17,801,'Context revision: 11 (example)',12,C['muted'])
    line(d,(17,846,278,846));small_label(d,17,869,'SESSION SETTINGS')
    for i,(label,value) in enumerate([('Provider','Codex'),('Model','Not selected'),('Cost','Unavailable'),('Mode','Review only')]):
        field(d,17,898+i*42,260,label,value,'chevron-down' if i<2 else None)
    button(d,16,1109,'Session settings',264,name='settings-2')
    rect(d,(300,113,1504,1173),'#191919')
    text(d,322,137,'Change review / orbit_preview.lua',22,bold=True);text(d,322,171,'Document assistant  /  Codex  /  Proposed change',13,C['muted'])
    button(d,1280,133,'Open in Code',200,name='external-link')
    rect(d,(301,211,1504,252),C['panel']);icon(d,321,224,'file-code',16,C['cyan']);text(d,347,223,'orbit_preview.lua',14);text(d,1189,224,'Unified',13,C['muted']);icon(d,1251,226,'chevron-down',12);text(d,1301,224,'+8',13,C['accent']);text(d,1340,224,'-2',13,C['red']);ib(d,1408,216,'chevron-up');ib(d,1445,216,'chevron-down')
    rect(d,(301,253,1504,289),'#292929');text(d,323,263,'@@ -16,9 +16,15 @@  OrbitPreview:step(delta_time)',14,C['muted'],mono=True)
    text(d,1224,264,'Hunk 1 of 1',12,C['muted']);ib(d,1407,255,'check');ib(d,1445,255,'x')
    diff=[(' ','16','16','function OrbitPreview:step(delta_time)'),('-','17','', '    self.angle = self.angle + delta_time * self.speed'),('-','18','', '    return self.angle'),('+','','17','    if delta_time < 0.0 then'),('+','','18','        return nil, "delta_time must be non-negative"'),('+','','19','    end'),('+','','20',''),('+','','21','    self.angle = (self.angle + delta_time * self.speed)'),('+','','22','        % (2.0 * math.pi)'),(' ','19','23',''),('+','','24','    return { angle = self.angle, radius = self.radius }'),('+','','25','    -- Preview data only; caller owns scene updates.'),(' ','20','26','end'),(' ','21','27',''),(' ','22','28','function OrbitPreview:reset()'),(' ','23','29','    self.angle = 0.0'),(' ','24','30','end')]
    for i,(mark,old,new,s) in enumerate(diff):
        yy=304+i*27
        if mark!=' ':
            rect(d,(301,yy-3,1504,yy+24),'#303030' if mark=='+' else '#242424')
            rect(d,(301,yy-3,303,yy+24),'#aaaaaa' if mark=='+' else '#666666')
        text(d,318,yy,old.rjust(2),14,C['faint'],mono=True);text(d,355,yy,new.rjust(2),14,C['faint'],mono=True);text(d,396,yy,mark,16,C['accent'] if mark=='+' else C['red'],mono=True);code_line(d,429,yy,s,17)
    line(d,(300,789,1504,789));text(d,323,810,'Review notes',16,bold=True)
    icon(d,324,847,'message-square',17,C['muted']);text(d,354,846,'Negative time is rejected before state changes.',15)
    text(d,354,872,'Return type changes from number to table. Caller compatibility needs review.',14,C['muted'])
    icon(d,324,908,'triangle-alert',15);text(d,349,907,'Conflict check pending / base: buffer 11 / current: buffer 12 / example revisions',13,C['muted'])
    rect(d,(323,934,1479,1023),C['field'],C['line'],4);text(d,338,950,'Add review comment...',14,C['faint']);icon(d,1440,991,'arrow-up',17)
    line(d,(301,1052,1504,1052));icon(d,323,1076,'info',16,C['muted']);text(d,349,1076,'Illustrative diff. No repository changes applied.',13,C['muted'])
    button(d,323,1113,'Request changes',172,name='message-square');button(d,510,1113,'Discard proposal',170,name='x')
    panel(d,(1507,113,1919,1173),'REVIEW GATE')
    text(d,1528,171,'Awaiting your review',20,bold=True)
    text(d,1528,204,'1 file / 1 hunk / 0 accepted',14,C['muted'])
    section(d,1508,246,410,'EVIDENCE')
    for i,(label,status,col) in enumerate([('Diff reviewed','Pending',C['gold']),('Unit tests','Not run',C['muted']),('Build','Not run',C['muted']),('Formatting','Not checked',C['muted']),('Runtime behavior','Not checked',C['muted'])]):
        yy=294+i*43;text(d,1528,yy,label,14);text(d,1777,yy,status,13,col);line(d,(1528,yy+29,1896,yy+29),C['subtle'])
    section(d,1508,537,410,'PERMISSIONS')
    for i,(label,value) in enumerate([('Read','Attached files'),('Write','This change set only'),('Execute','Approval required'),('Network','Not requested')]):field(d,1528,583+i*39,368,label,value)
    section(d,1508,770,410,'VALIDATION')
    text(d,1528,816,'No validation command configured.',14,C['muted'])
    button(d,1528,855,'Configure checks',180,name='settings-2')
    text(d,1528,909,'Run checks',14,C['faint']);icon(d,1632,909,'lock-keyhole',14,C['faint'])
    text(d,1528,937,'Base revision must be reconciled.',13,C['muted'])
    line(d,(1528,971,1896,971));check(d,1528,997,'I reviewed this proposed change',False)
    button(d,1528,1044,'Review & apply',368,False,'lock-keyhole')
    text(d,1528,1094,'Blocked: revision check and review pending.',13,C['muted'])
    text(d,1528,1122,'No command runs when applying files.',13,C['muted'])
    return im
