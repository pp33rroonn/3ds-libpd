import sys
O=[];C=[]
def o(t,x,y,kind="obj"):
    O.append(f"#X {kind} {x} {y} {t};"); return len(O)-1
def c(a,b,ao=0,bi=0): C.append(f"#X connect {a} {ao} {b} {bi};")
o("Patch 11: Circle-Pad = Tonhoehe (Ton solange ausgelenkt) \\, Steuerkreuz = 4 Noten \\, C-Stick = Pan + Lautstaerke",10,5,"text")
# --- Circle-Pad: x = Tonhoehe, Ton an solange ausgelenkt
px=o("r pad-x",10,40); py=o("r pad-y",120,40)
p1=o("* 12",10,65); p2=o("+ 57",10,90); p3=o("mtof",10,115); p4=o("pack f 20",10,140); p5=o("line~",10,165)
osc1=o("osc~",10,190)
# Betrag |x|+|y| > 0.05 -> 0.6 sonst 0
ax=o("abs",230,65); ay=o("abs",290,65)
sum_=o("+",230,115); lastx=o("f",230,90); tb=o("t b f",290,90)
gt=o("> 0.05",230,140); g2=o("* 0.6",230,165); g3=o("pack f 30",230,190); g4=o("line~",230,215)
amp1=o("*~",10,240)
c(px,p1);c(p1,p2);c(p2,p3);c(p3,p4);c(p4,p5);c(p5,osc1)
c(px,ax);c(py,ay)
c(ax,lastx,0,1); c(ax,sum_,0,0)          # |x| merken + heiss ins +
c(ay,tb); c(tb,sum_,1,1); c(tb,lastx,0,0); c(lastx,sum_,0,0)  # |y| kalt, dann |x| nachschieben
c(sum_,gt);c(gt,g2);c(g2,g3);c(g3,g4);c(osc1,amp1);c(g4,amp1,0,1)
# --- Steuerkreuz: 4 Noten
lb=o("loadbang",450,40); z=o("0",450,65,"msg"); env=o("line~",450,240)
c(lb,z);c(z,env)
notes=[("btn-up",72,"UP"),("btn-down",60,"DOWN"),("btn-left",65,"LEFT"),("btn-right",67,"RIGHT")]
mt=o("mtof",450,140); att=o("0.7 5",560,140,"msg"); dl=o("delay 400",560,165); rel=o("0 200",560,190,"msg")
osc2=o("osc~",450,190); amp2=o("*~",450,265)
pr=o("print DPAD",650,140)
for i,(r,n,nm) in enumerate(notes):
    rr=o(f"r {r}",450+i*70,90 if i<2 else 90); m=o(str(n),450+i*70,115,"msg"); lab=o(nm,650,90+i*0,"msg")
    c(rr,m);c(m,mt);c(rr,att);c(rr,dl);c(rr,lab);c(lab,pr)
c(mt,osc2);c(att,env);c(dl,rel);c(rel,env);c(osc2,amp2);c(env,amp2,0,1)
# --- Mischen, C-Stick Lautstaerke + Pan (mit line~ geglaettet -> kein Knistern)
vol=o("*~",10,320)
c(amp1,vol);c(amp2,vol)
cy=o("r cstick-y",150,270); y1=o("+ 1",150,290); y2=o("* 0.4",150,310)
yp=o("pack f 30",150,330); yl=o("line~",150,350)
c(cy,y1);c(y1,y2);c(y2,yp);c(yp,yl);c(yl,vol,0,1)
cx=o("r cstick-x",300,270); l1=o("* -0.5",300,290); l2=o("+ 0.5",300,310); r1=o("* 0.5",380,290); r2=o("+ 0.5",380,310)
lp=o("pack f 30",300,330); ll=o("line~",300,350); rp=o("pack f 30",380,330); rl=o("line~",380,350)
c(cx,l1);c(l1,l2);c(l2,lp);c(lp,ll);c(cx,r1);c(r1,r2);c(r2,rp);c(rp,rl)
# Startwerte (auch fuer Old 3DS ohne C-Stick): Mitte = Lautstaerke 0.4, Pan Mitte
lb2=o("loadbang",500,270); z2=o("0",500,290,"msg"); c(lb2,z2); c(z2,y1); c(z2,l1); c(z2,r1)
L=o("*~",10,380); R=o("*~",110,380); dac=o("dac~",10,410)
c(vol,L);c(vol,R);c(ll,L,0,1);c(rl,R,0,1);c(L,dac);c(R,dac,0,1)
# --- Log
for nm,rcv,x in (("PADX","pad-x",600),("PADY","pad-y",680),("CSX","cstick-x",760),("CSY","cstick-y",840)):
    a=o(f"r {rcv}",x,300); b=o(f"print {nm}",x,330); c(a,b)
open(sys.argv[1],"w").write("#N canvas 0 0 950 450 10;\n"+"\n".join(O+C)+"\n")
