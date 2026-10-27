"""Prepared compiled-animation adapter for explicitly selected presentation.

Raw cache words stay untouched. This translates the selected record to the
existing offline sprite compiler's in-memory XML model; it never mixes a DLC
animation with a base sheet or guesses package precedence.
"""
import hashlib,math,struct
from pathlib import PurePosixPath
import xml.etree.ElementTree as ET
from animation_cache import decode,encode
from extract_resources import filename_hash


def scalar(word):
    value=struct.unpack('<f',struct.pack('<I',word))[0]
    if not math.isfinite(value):raise ValueError('Nonfinite compiled presentation field')
    return value

def frame_node(frame,layer):
    words=frame['words'];expected=11 if layer else 5
    if len(words)!=expected or frame['visible_byte'] not in (0,1) or frame['interpolation_byte'] not in (0,1) or not words[-1]:
        raise ValueError('Invalid compiled presentation frame')
    keys=('XCrop','YCrop','Width','Height','XPosition','YPosition','XScale','YScale','XPivot','YPivot') if layer else ('XPosition','YPosition','XScale','YScale')
    attrs={k:format(scalar(v)*(100 if k in ('XScale','YScale') else 1),'.17g') for k,v in zip(keys,words[:-1])}
    for key,word in zip(('RedTint','GreenTint','BlueTint','AlphaTint','RedOffset','GreenOffset','BlueOffset'),frame['color_words']):
        attrs[key]=format(scalar(word)*255,'.17g')
    attrs.update(Delay=str(words[-1]),Visible='true' if frame['visible_byte'] else 'false',
                 Rotation=format(scalar(frame['rotation_word']),'.17g'),Interpolated='true' if frame['interpolation_byte'] else 'false')
    return ET.Element('Frame',attrs)

def presentation_tree(entry):
    root=ET.Element('AnimatedActor');content=ET.SubElement(root,'Content')
    sheets=ET.SubElement(content,'Spritesheets')
    for s in entry['spritesheets']:ET.SubElement(sheets,'Spritesheet',Id=str(s['id']),Path=s['path']['text'])
    layers=ET.SubElement(content,'Layers')
    for layer in entry['layers']:ET.SubElement(layers,'Layer',Id=str(layer['id']),SpritesheetId=str(layer['spritesheet']),Name=layer['name']['text'])
    nulls=ET.SubElement(content,'Nulls')
    for n in entry['nulls']:ET.SubElement(nulls,'Null',Id=str(n['id']),Name=n['name']['text'])
    animations=ET.SubElement(root,'Animations',DefaultAnimation=entry['default_animation']['text'])
    for source in entry['animations']:
        anim=ET.SubElement(animations,'Animation',Name=source['name']['text'],FrameNum=str(source['frame_count']),Loop='true' if source['loop_byte'] else 'false')
        track=ET.SubElement(anim,'RootAnimation')
        for f in source['root_frames']:track.append(frame_node(f,False))
        group=ET.SubElement(anim,'LayerAnimations')
        for source_track in source['layer_tracks']:
            track=ET.SubElement(group,'LayerAnimation',LayerId=str(source_track['id']),Visible='true' if source_track['visible_byte'] else 'false')
            for f in source_track['frames']:track.append(frame_node(f,True))
        group=ET.SubElement(anim,'NullAnimations')
        for source_track in source['null_tracks']:
            track=ET.SubElement(group,'NullAnimation',NullId=str(source_track['id']),Visible='true' if source_track['visible_byte'] else 'false')
            for f in source_track['frames']:track.append(frame_node(f,False))
    return root

def select_compiled(raw,path):
    key=filename_hash(path)[0]
    if isinstance(key,str):key=int(key,16)
    entries=decode(raw);found=[e for e in entries if int(e['lookup_hash'],16)==key]
    if len(found)!=1:raise ValueError('Missing/ambiguous compiled presentation: '+path)
    entry=found[0];record=raw[entry['file_offset']:entry['file_offset']+entry['file_bytes']]
    if encode([entry])[4:]!=record:raise ValueError('Compiled presentation record does not roundtrip')
    directory=entry['base_directory']['text'].replace('\\','/')
    if not directory.lower().startswith('resources/'):directory='resources/'+directory
    if str(PurePosixPath(directory)).lower()!=str(PurePosixPath(path).parent).lower():
        raise ValueError('Compiled presentation directory mismatch')
    provenance=dict(lookup_hash=entry['lookup_hash'],index=entry['index'],offset=entry['file_offset'],bytes=entry['file_bytes'],
                    cache_sha256=hashlib.sha256(raw).hexdigest(),record_sha256=hashlib.sha256(record).hexdigest(),roundtrip_exact=True)
    return presentation_tree(entry),provenance
