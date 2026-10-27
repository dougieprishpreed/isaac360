"""Emit immutable native animation data; retain original frame bit patterns."""
import json,struct
def f32(n):return struct.unpack('<f',struct.pack('<f',n))[0]
def bits(n):return struct.unpack('<I',struct.pack('<f',n))[0]
def fold(h,n):
    for b in struct.pack('<I',n&0xffffffff):h=((h^b)*16777619)&0xffffffff
    return h
def string(h,s):
    for b in s.encode('ascii')+b'\0':h=((h^b)*16777619)&0xffffffff
    return h
def frame_words(frame,end):
    w=frame['words'];assert len(w) in (5,11) and w[-1]>0
    assert frame['visible_byte'] in (0,1) and frame['interpolation_byte'] in (0,1)
    return w[:-1]+[0]*(11-len(w)),w[-1],frame['color_words'],frame['rotation_word'],end,frame['visible_byte'],frame['interpolation_byte']
def track_hash(h,track,layer=False):
    for n in (track['id'],track['visible_byte'],len(track['frames'])):h=fold(h,n)
    end=0
    for frame in track['frames']:
        end+=frame['words'][-1];g,delay,color,rotation,total,v,i=frame_words(frame,end)
        for n in g+[delay]+color+[rotation,total,v,i]:h=fold(h,n)
    return h
def asset_hash(asset):
    h=string(string(2166136261,asset['canonical_path']),asset['default_animation']['text']);h=fold(h,len(asset['spritesheets']))
    for s in asset['spritesheets']:
        h=fold(h,s['id']);h=string(h,s['canonical_path']);h=fold(fold(h,s['width']),s['height'])
    h=fold(h,len(asset['layers']))
    for s in asset['layers']:h=string(fold(fold(h,s['id']),s['spritesheet']),s['name']['text'])
    for group in ('nulls','events'):
        h=fold(h,len(asset[group]))
        for n in asset[group]:h=string(fold(h,n['id']),n['name']['text'])
    h=fold(h,len(asset['animations']))
    for a in asset['animations']:
        h=fold(fold(string(h,a['name']['text']),a['frame_count']),a['loop_byte']);h=track_hash(h,dict(id=-1,visible_byte=1,frames=a['root_frames']))
        for group in ('layer_tracks','null_tracks'):
            h=fold(h,len(a[group]))
            for t in a[group]:h=track_hash(h,t)
        h=fold(h,len(a['triggers']))
        for event in a['triggers']:h=fold(fold(h,event['event']),event['frame'])
    return h
def emit_asset(lines,asset,prefix):
    def array(kind,name,rows):
        if rows:lines.extend(['static const '+kind+' '+name+'[] = {']+rows+['};'])
        return name if rows else '0'
    def track(name,t):
        end=0;rows=[]
        for frame in t['frames']:
            end+=frame['words'][-1];assert end<=0xffffffff
            g,d,c,r,total,v,i=frame_words(frame,end)
            rows.append('{{%s},%d,{%s},%d,%d,%s,%s},'%(','.join(str(x)+'U' for x in g),d,','.join(str(x)+'U' for x in c),r,total,'true' if v else 'false','true' if i else 'false'))
        return '{%d,%s,%s,%d}'%(t['id'],'true' if t['visible_byte'] else 'false',array('NativeSpriteFrame',name,rows),len(rows))
    clips=[]
    for i,a in enumerate(asset['animations']):
        assert a['frame_count']>0 and a['loop_byte'] in (0,1)
        p=prefix+'Anim%d'%i;root=track(p+'Root',dict(id=-1,visible_byte=1,frames=a['root_frames']));groups=[]
        for group,label in (('layer_tracks','Layers'),('null_tracks','Nulls')):
            rows=[track(p+label+str(j),t)+',' for j,t in enumerate(a[group])]
            groups.extend((array('NativeSpriteTrack',p+label,rows),len(rows)))
        events=a['triggers'];eventArray=array('NativeSpriteEvent',p+'Events',['{%d,%d},'%(e['event'],e['frame']) for e in events])
        clips.append('{%s,%d,%s,%s,%s,%d,%s,%d,%s,%d},'%(json.dumps(a['name']['text']),a['frame_count'],'true' if a['loop_byte'] else 'false',root,*groups,eventArray,len(events)))
    cl=array('NativeSpriteClip',prefix+'Clips',clips)
    sh=array('NativeSpriteSheet',prefix+'Sheets',['{%d,%s,%d,%d},'%(s['id'],json.dumps(s['canonical_path']),s['width'],s['height']) for s in asset['spritesheets']])
    la=array('NativeSpriteLayer',prefix+'Layers',['{%d,%d,%s},'%(s['id'],s['spritesheet'],json.dumps(s['name']['text'])) for s in asset['layers']])
    groups=[]
    for group,label in (('nulls','Nulls'),('events','Events')):
        groups.extend((array('NativeSpriteName',prefix+label,['{%d,%s},'%(n['id'],json.dumps(n['name']['text'])) for n in asset[group]]),len(asset[group])))
    lines.append('static const NativeSpriteAsset %s = {%s,%s,%s,%d,%s,%d,%s,%d,%s,%d,%s,%d};'%(prefix,json.dumps(asset['canonical_path']),json.dumps(asset['default_animation']['text']),sh,len(asset['spritesheets']),la,len(asset['layers']),*groups,cl,len(clips)))
