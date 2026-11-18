from pathlib import Path
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1]
source=root/"docs"/"logo.png"
target=root/"res"/"app.ico"
picture=Image.open(source).convert("RGBA")
n=min(picture.size)
if picture.width != picture.height:
    x=(picture.width-n)//2;y=(picture.height-n)//2
    picture=picture.crop((x,y,x+n,y+n))
big=Image.new("L",(n*2,n*2),0)
draw=ImageDraw.Draw(big)
draw.rounded_rectangle((0,0,n*2-1,n*2-1),radius=round(n*0.27),fill=255)
picture.putalpha(big.resize((n,n),Image.Resampling.LANCZOS))
picture.save(target,format="ICO",sizes=[(a,a) for a in (16,24,32,48,64,128,256)])
print("Generated:",target,"from:",source)
