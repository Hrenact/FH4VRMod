"""Read-only MSVC RTTI candidates for separating UI rendering; no hooks/writes."""
import json,struct,argparse
from pathlib import Path
import pefile
p=Path(r'F:\SteamLibrary\steamapps\common\ForzaHorizon4\ForzaHorizon4.exe')
d=p.read_bytes(); pe=pefile.PE(str(p)); base=pe.OPTIONAL_HEADER.ImageBase
parser=argparse.ArgumentParser();parser.add_argument('--names',nargs='+');parser.add_argument('--out',default='reports/ui-rtti-research.json');parser.add_argument('--methods',type=int,default=24);args=parser.parse_args()
rows=[]
for name in ([x.encode() for x in args.names] if args.names else (b'.?AVUIRenderer@@',b'.?AVIForzaUIRenderer@@')):
    pos=d.find(name+b'\0')
    if pos<0:continue
    td=pe.get_rva_from_offset(pos)-16
    needle=struct.pack('<I',td); cursor=0
    while True:
        at=d.find(needle,cursor)
        if at<0:break
        cursor=at+1
        if at<12:continue
        col=at-12
        sig,offset,cd,typ,cls,selfrva=struct.unpack_from('<6I',d,col)
        if sig!=1:continue
        try:rva=pe.get_rva_from_offset(col)
        except pefile.PEFormatError:continue
        if selfrva!=rva:continue
        ptr=struct.pack('<Q',base+rva); start=0
        while True:
            vt=d.find(ptr,start)
            if vt<0:break
            start=vt+1
            try: table=pe.get_rva_from_offset(vt)+8
            except pefile.PEFormatError:continue
            methods=[]
            for i in range(min(args.methods,128)):
                value=struct.unpack_from('<Q',d,vt+8+i*8)[0]-base
                sec=pe.get_section_by_rva(value)
                if not sec or not sec.Characteristics&0x20000000:break
                methods.append(hex(value))
            rows.append(dict(name=name.decode(),type=hex(td),col=hex(rva),object_offset=offset,vtable=hex(table),methods=methods))
Path(args.out).write_text(json.dumps(rows,indent=2),encoding='utf-8')
print(json.dumps(rows,indent=2))
