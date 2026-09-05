#!/usr/bin/env python3
"""Compare generated resources without modifying either input directory."""
import argparse
import json
from pathlib import Path

import texture_generator as tg


def compare(before, after, output):
    output.mkdir(parents=True, exist_ok=True)
    names=("grass_top", "dirt", "stone", "sand", "oak_planks", "deepslate", "obsidian")
    report={"generator_version":tg.GENERATOR_VERSION,"textures":{}}
    width=680
    canvas=[(33,36,39,255)]*(width*len(names)*165)
    for row,name in enumerate(names):
        y=row*165
        tg.draw_text(canvas,width,8,y+5,name)
        report["textures"][name]={}
        for column,(label,directory) in enumerate((("before",before),("after",after))):
            pixels=tg.read_generated_png(directory/(name+".png"))[2]
            report["textures"][name][label]=tg._visual_color_stats(pixels)
            x=column*340
            tg.draw_text(canvas,width,x+8,y+20,label)
            tg.blit(canvas,width,x+8,y+35,pixels,16,16,4)
            tg.blit(canvas,width,x+85,y+35,pixels,16,16)
            for dy in range(3):
                for dx in range(3):
                    tg.blit(canvas,width,x+175+dx*48,y+20+dy*48,pixels,16,16,3)
    tg.write_png(output/"comparison.png",width,len(names)*165,canvas)
    (output/"comparison.json").write_text(json.dumps(report,indent=2)+"\n")

    width=540; height=len(tg.FUNCTIONAL)*85
    canvas=[(33,36,39,255)]*(width*height)
    for row,name in enumerate(tg.FUNCTIONAL):
        tg.draw_text(canvas,width,4,row*85+10,name)
        for col,suffix in enumerate(("","_top","_side","_bottom")):
            pixels=tg.read_generated_png(after/(name+suffix+".png"))[2]
            x=160+col*90
            tg.draw_text(canvas,width,x,row*85+3,suffix[1:] or "front")
            tg.blit(canvas,width,x,row*85+14,pixels,16,16,4)
    tg.write_png(output/"functional_faces.png",width,height,canvas)
    # Full native-resolution atlas and readable contact sheets for each version.
    for label,directory in (("before",before),("after",after)):
        for folder in ("items","entity_skins"):
            files=sorted((directory/folder).glob("*.png"))
            columns=8 if folder=="items" else 4
            cell=88 if folder=="items" else 150
            width=columns*cell; height=max(1,(len(files)+columns-1)//columns)*cell
            canvas=[(33,36,39,255)]*(width*height)
            for i,path in enumerate(files):
                w,h,pixels=tg.read_generated_png(path)
                x=(i%columns)*cell; y=(i//columns)*cell
                tg.draw_text(canvas,width,x+2,y+2,path.stem[:(cell-4)//4])
                tg.blit(canvas,width,x+4,y+14,pixels,w,h,4 if folder=="items" else 2)
            tg.write_png(output/(label+"_"+folder+".png"),width,height,canvas)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before",type=Path,required=True)
    parser.add_argument("--after",type=Path,default=Path("assets/textures/generated"))
    parser.add_argument("--output",type=Path,default=Path("build-local/texture-review"))
    args=parser.parse_args()
    compare(args.before,args.after,args.output)


if __name__=="__main__":
    main()
