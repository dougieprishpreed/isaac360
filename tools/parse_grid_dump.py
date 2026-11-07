"""Parse retail GRIDDUMP log lines into a normalized JSON/TSV packet."""
from pathlib import Path
import argparse
import json
import re

GRID = re.compile(
    r"GRIDDUMP (?P<tag>\w+) f=(?P<frame>\d+) safe=(?P<safe>-?\d+) "
    r"grid=(?P<grid>\d+) T=(?P<type>\d+) V=(?P<variant>\d+) "
    r"S=(?P<state>-?\d+) SC=(?P<count>\d+) SS=(?P<seed>\d+) "
    r"VD=(?P<var_data>-?\d+) I=(?P<initialized>true|false) "
    r"VS=(?P<variable_seed>\d+) CC=(?P<collision>-?\d+) "
    r"PATH=(?P<path>-?\d+) RT=(?P<room_type>-?\d+) "
    r"RV=(?P<room_variant>-?\d+) anm=(?P<anm>.*?) "
    r"anim=(?P<anim>.*?) fr=(?P<sprite_frame>-?\d+)$")
SPAWN = re.compile(
    r"GRIDDUMP SPAWN f=(?P<frame>\d+) T=(?P<type>\d+) V=(?P<variant>\d+) "
    r"ST=(?P<subtype>-?\d+) seed=(?P<seed>\d+) "
    r"pos=(?P<x>-?[\d.]+),(?P<y>-?[\d.]+)$")
PICKUP = re.compile(
    r"GRIDDUMP PICKUP f=(?P<frame>\d+) V=(?P<variant>\d+) "
    r"ST=(?P<subtype>-?\d+) seed=(?P<seed>\d+) "
    r"pos=(?P<x>-?[\d.]+),(?P<y>-?[\d.]+)$")

INT_FIELDS = {
    "tag": str, "frame": int, "safe": int, "grid": int, "type": int,
    "variant": int, "state": int, "count": int, "seed": int,
    "var_data": int, "variable_seed": int, "collision": int, "path": int,
    "room_type": int, "room_variant": int, "sprite_frame": int,
    "subtype": int, "x": float, "y": float,
}


def convert(match):
    values = match.groupdict()
    if values.get("initialized") is not None:
        values["initialized"] = values["initialized"] == "true"
    for key, kind in INT_FIELDS.items():
        if key in values and values[key] is not None:
            values[key] = kind(values[key])
    return values


def parse(path):
    records = []
    for raw in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if "GRIDDUMP " not in raw:
            continue
        line = raw[raw.index("GRIDDUMP "):]
        for pattern in (GRID, SPAWN, PICKUP):
            match = pattern.match(line)
            if match:
                records.append(convert(match))
                break
    return records


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--tsv", action="store_true")
    args = parser.parse_args()
    records = parse(args.log)
    if args.json:
        print(json.dumps(dict(format="retail-grid-dump-v1", records=records), indent=2))
    elif args.tsv:
        keys = ["tag", "frame", "safe", "grid", "type", "variant", "state",
                "count", "seed", "var_data", "initialized", "variable_seed",
                "collision", "path", "room_type", "room_variant", "anm",
                "anim", "sprite_frame", "subtype", "x", "y"]
        print("\t".join(keys))
        for record in records:
            print("\t".join(str(record.get(key, "")) for key in keys))
    else:
        print(f"parsed {len(records)} GRIDDUMP records")
        for record in records:
            print(json.dumps(record, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
