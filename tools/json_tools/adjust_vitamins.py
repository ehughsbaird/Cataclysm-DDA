#!/usr/bin/env python3

import argparse
import json
import os

args = argparse.ArgumentParser()
args.add_argument("dir", action="store", help="specify json directory")
args_dict = vars(args.parse_args())

mappings = { "iron": 188, "calcium": 10420, "vitC": 938 }

def format_mass(key, units):
    total = mappings[key] * units;
    if total < 1000:
        return f"{total} μg"
    mg = int(total / 1000)
    ug = total % 1000
    return f"{mg} mg {ug} μg"


def gen_new(path):
    change = False
    with open(path, "r", encoding="utf-8") as json_file:
        json_data = json.load(json_file)
        for jo in json_data:
            if not "type" in jo:
                continue
            if jo["type"] != "COMESTIBLE" or not "vitamins" in jo:
                continue
            for vit_data in jo["vitamins"]:
                if vit_data[0] not in mappings:
                    continue
                if type(vit_data[1]) is not int:
                    continue
                change = True
                vit_data[1] = format_mass(vit_data[0], vit_data[1]);

    return json_data if change else None


for root, directories, filenames in os.walk(args_dict["dir"]):
    for filename in filenames:
        path = os.path.join(root, filename)
        if path.endswith(".json"):
            new = gen_new(path)
            if new is not None:
                with open(path, "w", encoding="utf-8") as jf:
                    json.dump(new, jf, ensure_ascii=False)
                os.system(f"./tools/format/json_formatter.cgi {path}")
