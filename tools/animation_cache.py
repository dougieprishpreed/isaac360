"""Lossless animation-cache reader reconstructed from saved native game code.

Container: main:00409a00. Root/null frames:00409730. Layer frames:00409870.
The frame word arrays preserve IEEE-754 bits and integer fields verbatim.
"""
import struct


class Reader:
    def __init__(self, data):
        self.data, self.offset = data, 0

    def read(self, fmt):
        size = struct.calcsize(fmt)
        if self.offset+size > len(self.data):
            raise ValueError(f'Truncated animation cache at {self.offset}')
        value = struct.unpack_from(fmt,self.data,self.offset)
        self.offset += size
        return value

    def count(self):
        value, = self.read('<I')
        if value > 100000:
            raise ValueError(f'Invalid animation cache count {value} at {self.offset-4}')
        return value

    def string(self):
        size, = self.read('<H')
        value, = self.read(f'<{size}s')
        return {'text':value.decode('utf-8',errors='replace'),'bytes_hex':value.hex()}


def frames(reader, count, layer=False):
    words = 11 if layer else 5
    result = []
    for _ in range(count):
        prefix = list(reader.read('<'+'I'*words))
        visible, = reader.read('<B')
        colors = list(reader.read('<7I'))
        rotation,interpolated = reader.read('<IB')
        result.append({'words':prefix,'visible_byte':visible,'color_words':colors,
                       'rotation_word':rotation,'interpolation_byte':interpolated})
    return result


def decode(data):
    reader = Reader(data)
    count = reader.count()
    entries = []
    for index in range(count):
        start = reader.offset
        key, = reader.read('<I')
        base = reader.string()
        sheets = []
        for _ in range(reader.count()):
            sheet_id, = reader.read('<I')
            sheets.append({'id':sheet_id,'path':reader.string()})
        layers = []
        for _ in range(reader.count()):
            layer_id,sheet_id = reader.read('<II')
            layers.append({'id':layer_id,'spritesheet':sheet_id,'name':reader.string()})
        definitions = []
        for _ in range(2):
            group = []
            for _ in range(reader.count()):
                item_id, = reader.read('<I')
                group.append({'id':item_id,'name':reader.string()})
            definitions.append(group)
        default = reader.string()
        animations = []
        for _ in range(reader.count()):
            name = reader.string()
            frame_count,loop = reader.read('<IB')
            root = frames(reader,reader.count())
            tracks = []
            for layer in (True,False):
                group = []
                for _ in range(reader.count()):
                    item_id,visible = reader.read('<IB')
                    group.append({'id':item_id,'visible_byte':visible,
                                  'frames':frames(reader,reader.count(),layer)})
                tracks.append(group)
            triggers = []
            for _ in range(reader.count()):
                event,frame = reader.read('<II')
                triggers.append({'event':event,'frame':frame})
            animations.append({'name':name,'frame_count':frame_count,'loop_byte':loop,
                               'root_frames':root,'layer_tracks':tracks[0],'null_tracks':tracks[1],
                               'triggers':triggers})
        entries.append({'index':index,'file_offset':start,'file_bytes':reader.offset-start,
                        'lookup_hash':f'{key:08x}','base_directory':base,'spritesheets':sheets,
                        'layers':layers,'nulls':definitions[0],'events':definitions[1],
                        'default_animation':default,'animations':animations})
    if reader.offset != len(data):
        raise ValueError(f'Animation cache has {len(data)-reader.offset} unparsed bytes')
    return entries


def encode(entries):
    out = bytearray(struct.pack('<I',len(entries)))
    def string(value):
        raw = bytes.fromhex(value['bytes_hex'])
        out.extend(struct.pack('<H',len(raw)))
        out.extend(raw)
    def frame_list(values, layer=False):
        out.extend(struct.pack('<I',len(values)))
        for frame in values:
            out.extend(struct.pack('<'+'I'*(11 if layer else 5),*frame['words']))
            out.extend(struct.pack('<B7IIB',frame['visible_byte'],*frame['color_words'],
                                   frame['rotation_word'],frame['interpolation_byte']))
    for entry in entries:
        out.extend(struct.pack('<I',int(entry['lookup_hash'],16)))
        string(entry['base_directory'])
        out.extend(struct.pack('<I',len(entry['spritesheets'])))
        for sheet in entry['spritesheets']:
            out.extend(struct.pack('<I',sheet['id'])); string(sheet['path'])
        out.extend(struct.pack('<I',len(entry['layers'])))
        for layer in entry['layers']:
            out.extend(struct.pack('<II',layer['id'],layer['spritesheet'])); string(layer['name'])
        for key in ('nulls','events'):
            out.extend(struct.pack('<I',len(entry[key])))
            for item in entry[key]:
                out.extend(struct.pack('<I',item['id'])); string(item['name'])
        string(entry['default_animation'])
        out.extend(struct.pack('<I',len(entry['animations'])))
        for animation in entry['animations']:
            string(animation['name'])
            out.extend(struct.pack('<IB',animation['frame_count'],animation['loop_byte']))
            frame_list(animation['root_frames'])
            for key,layer in [('layer_tracks',True),('null_tracks',False)]:
                out.extend(struct.pack('<I',len(animation[key])))
                for track in animation[key]:
                    out.extend(struct.pack('<IB',track['id'],track['visible_byte']))
                    frame_list(track['frames'],layer)
            out.extend(struct.pack('<I',len(animation['triggers'])))
            for trigger in animation['triggers']:
                out.extend(struct.pack('<II',trigger['event'],trigger['frame']))
    return bytes(out)
