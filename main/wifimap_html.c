// wifimap_html.c — pagina della Mappa Wi-Fi: un file HTML autonomo (niente da internet)
// con i dati dentro. Si apre dal telefono collegato all'hotspot del Gadget e si può salvare
// e condividere: funziona anche da file. Disegna reti, fili, percorso; toccando un punto
// mostra i dettagli; "Scarica immagine" salva un PNG (con titolo e legenda): su iPhone Safari
// non scarica le immagini fatte dalla pagina, quindi la mostra e si salva tenendola premuta.
// Leggibilità: le reti dello stesso apparecchio (2,4/5 GHz, ospiti: indirizzi quasi uguali)
// diventano un punto solo; i punti troppo vicini si allargano quanto basta (una linea
// tratteggiata porta alla posizione stimata) e i nomi si sistemano dove non si coprono.
#include "wifimap.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char HEAD[] =
"<!doctype html><html lang=it><head><meta charset=utf-8>"
"<meta name=viewport content=\"width=device-width,initial-scale=1\"><title>Mappa Wi-Fi</title><style>"
"body{margin:0;background:#000;color:#ededed;font-family:system-ui,-apple-system,sans-serif}"
"header{padding:16px 16px 8px}h1{margin:0;font-size:22px}p{margin:4px 0;color:#9aa0a6;font-size:14px}"
".l span{display:inline-block;width:10px;height:10px;border-radius:5px;margin:0 5px 0 12px;vertical-align:middle}"
".l span:first-child{margin-left:0}"
".b{display:inline-block;margin:10px 8px 0 0;padding:11px 16px;border-radius:9px;background:#ffb020;color:#000;"
"font-weight:600;font-size:15px;text-decoration:none;border:0;font-family:inherit}"
".b.g{background:#24282d;color:#ededed}"
"canvas{display:block;width:100%;touch-action:manipulation;background:#0b0e12}"
"#info{padding:12px 16px;font-size:15px;min-height:44px}#info b{color:#ffb020}#info div{margin:2px 0}#info .n{color:#9aa0a6;font-size:13px}"
"#ov{display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.94);overflow:auto;padding:16px;z-index:9}"
"#ov p{color:#ededed;font-size:16px}#ov img{display:block;width:100%;margin:12px 0;-webkit-touch-callout:default}"
"footer{padding:8px 16px 24px;color:#6e747c;font-size:12px}"
"</style></head><body><header><h1>Mappa Wi-Fi</h1><p id=sub></p>"
"<p class=l><span style=background:#ffb020></span>fissa<span style=background:#ff6b57></span>mobile"
"<span style=background:#ff4fd8></span>hotspot<span style=background:#fff></span>Gadget</p>"
"<button class=b id=png>Scarica immagine</button><a class=\"b g\" id=save href=/mappa-wifi.html download=mappa-wifi.html>Salva la pagina</a>"
"<p id=cna>Se i pulsanti non scaricano nulla sei nella finestra di accesso alla rete: chiudila restando "
"collegato all'hotspot e apri <b>192.168.4.1</b> nel browser.</p>"
"</header><div id=ov><p>Tieni premuto sull'immagine e scegli <b>Salva in Foto</b> (o <b>Aggiungi a Foto</b>).</p>"
"<img id=ovi alt=\"Mappa Wi-Fi\"><button class=b id=ovx>Chiudi</button></div><canvas id=c></canvas><div id=info>Tocca un punto per i dettagli.</div>"
"<footer>Mappa relativa: le distanze sono stimate dalla potenza dei segnali (qualche metro di errore) "
"e l'orientamento è arbitrario. I fili collegano le reti viste nella stessa scansione.</footer>"
"<script>var D=";

static const char TAIL[] =
";\n"
"var K=['fissa','mobile','hotspot'],COL=['#ffb020','#ff6b57','#ff4fd8'];"
"var c=document.getElementById('c'),info=document.getElementById('info');"
"document.getElementById('sub').textContent=D.t+' · '+D.n.length+' reti · '+D.s+' scansioni';"
"if(location.protocol=='file:'||location.hostname!='192.168.4.1'){document.getElementById('save').style.display='none';document.getElementById('cna').style.display='none';}"
"(function(){var P=[];D.n.forEach(function(n){P.push([n.x,n.y])});D.p.forEach(function(p){P.push(p)});if(P.length<2)return;"
"var mx=0,my=0;P.forEach(function(p){mx+=p[0];my+=p[1]});mx/=P.length;my/=P.length;var a=0,b=0,d=0;"
"P.forEach(function(p){var x=p[0]-mx,y=p[1]-my;a+=x*x;b+=x*y;d+=y*y});var t=Math.PI/2-0.5*Math.atan2(2*b,a-d),C=Math.cos(t),S=Math.sin(t);"
"function r(p){var x=p[0]-mx,y=p[1]-my;p[0]=x*C-y*S;p[1]=x*S+y*C}"
"function rn(n){var p=[n.x,n.y];r(p);n.x=p[0];n.y=p[1];if(n.t)n.t.forEach(r)}"
"D.n.forEach(rn);D.p.forEach(r);if(D.me)r(D.me);})();"
"function aspect(){var x0=1e9,x1=-1e9,y0=1e9,y1=-1e9;D.n.forEach(function(n){x0=Math.min(x0,n.x);x1=Math.max(x1,n.x);y0=Math.min(y0,n.y);y1=Math.max(y1,n.y)});"
"D.p.forEach(function(p){x0=Math.min(x0,p[0]);x1=Math.max(x1,p[0]);y0=Math.min(y0,p[1]);y1=Math.max(y1,p[1])});return x0>x1?1:Math.max(y1-y0,6)/Math.max(x1-x0,6);}"
"function mac(b){return b.split(':').map(function(h){return parseInt(h,16)})}"
"var G=[],GE=[],gi=[];"
"D.n.forEach(function(n,i){var m=mac(n.b),g=-1;"
"for(var j=0;j<G.length&&g<0;j++){var o=G[j],q=o.m;"
"if(n.k==0&&o.k==0&&q[1]==m[1]&&q[2]==m[2]&&q[3]==m[3]&&Math.abs(q[4]*256+q[5]-m[4]*256-m[5])<16&&Math.hypot(o.x-n.x,o.y-n.y)<6)g=j;}"
"if(g<0){g=G.length;G.push({k:n.k,m:m,x:0,y:0,a:[],v:0})}"
"var o=G[g];o.a.push(i);o.x+=(n.x-o.x)/o.a.length;o.y+=(n.y-o.y)/o.a.length;o.v=Math.max(o.v,n.v);gi[i]=g;});"
"G.forEach(function(o){var s=[];o.a.forEach(function(i){var t=D.n[i].s||'(nascosta)';if(s.indexOf(t)<0)s.push(t)});o.s=s.join(' · ');});"
"(function(){var seen={},A=G.map(function(){return[]});D.e.forEach(function(e){var a=gi[e[0]],b=gi[e[1]];if(a==b)return;var k=Math.min(a,b)+','+Math.max(a,b);"
"if(seen[k])return;seen[k]=1;var l=Math.hypot(G[a].x-G[b].x,G[a].y-G[b].y);A[a].push([l,a,b]);A[b].push([l,a,b]);});seen={};"
"A.forEach(function(L){L.sort(function(p,q){return p[0]-q[0]});L.slice(0,3).forEach(function(e){var k=e[1]+','+e[2];if(!seen[k]){seen[k]=1;GE.push([e[1],e[2]])}})});})();"
"var view;"
"function fit(w,h,m,top){var x0=1e9,x1=-1e9,y0=1e9,y1=-1e9;"
"function a(x,y){x0=Math.min(x0,x);x1=Math.max(x1,x);y0=Math.min(y0,y);y1=Math.max(y1,y);}"
"G.forEach(function(n){a(n.x,n.y)});D.p.forEach(function(p){a(p[0],p[1])});"
"if(x0>x1){x0=y0=-5;x1=y1=5}var W=Math.max(x1-x0,6),H=Math.max(y1-y0,6);"
"var s=Math.min((w-2*m)/W,(h-top-2*m)/H);"
"return{s:s,X:function(x){return w/2+(x-(x0+x1)/2)*s},Y:function(y){return top+(h-top)/2+(y-(y0+y1)/2)*s}};}"
"function spread(v,w,h,top,dpr){var P=G.map(function(o){return[v.X(o.x),v.Y(o.y)]}),O=P.map(function(p){return p.slice()});"
"var d=34*dpr,pad=12*dpr,N=P.length,me=D.me?[v.X(D.me[0]),v.Y(D.me[1])]:null;"
"for(var it=0;it<400;it++){"
"for(var i=0;i<N;i++)for(var j=i+1;j<N;j++){var dx=P[j][0]-P[i][0],dy=P[j][1]-P[i][1],l=Math.hypot(dx,dy);"
"if(l>=d)continue;if(l<0.01){dx=Math.cos(i+j*2.4);dy=Math.sin(i+j*2.4);l=1}var f=(d-l)/2/l;"
"P[i][0]-=dx*f;P[i][1]-=dy*f;P[j][0]+=dx*f;P[j][1]+=dy*f;}"
"if(me)for(var i=0;i<N;i++){var dx=P[i][0]-me[0],dy=P[i][1]-me[1],l=Math.hypot(dx,dy);if(l<d*0.7&&l>0.01){P[i][0]+=dx*(d*0.7-l)/l;P[i][1]+=dy*(d*0.7-l)/l}}"
"for(var i=0;i<N;i++){if(it<340){P[i][0]+=(O[i][0]-P[i][0])*0.03;P[i][1]+=(O[i][1]-P[i][1])*0.03;}"
"P[i][0]=Math.min(w-pad,Math.max(pad,P[i][0]));P[i][1]=Math.min(h-pad,Math.max(top+pad,P[i][1]));}}"
"return{P:P,O:O,me:me};}"
"function labels(g,L,w,h,top,dpr){var r=6*dpr,fs=13*dpr,B=L.P.map(function(p){return[p[0]-r-2*dpr,p[1]-r-2*dpr,p[0]+r+2*dpr,p[1]+r+2*dpr]}),out=[];if(L.me)B.push([L.me[0]-9*dpr,L.me[1]-9*dpr,L.me[0]+9*dpr,L.me[1]+9*dpr]);"
"g.font=fs+'px system-ui,sans-serif';"
"function free(a){if(a[0]<4*dpr||a[1]<top+2*dpr||a[2]>w-4*dpr||a[3]>h-26*dpr)return false;"
"for(var i=0;i<B.length;i++){var b=B[i];if(a[0]<b[2]&&b[0]<a[2]&&a[1]<b[3]&&b[1]<a[3])return false}return true}"
"var ord=G.map(function(o,i){return i}).sort(function(a,b){return G[b].v-G[a].v});"
"ord.forEach(function(i){var x=L.P[i][0],y=L.P[i][1],t=G[i].s,T=[t];if(t.length>16)T.push(t.slice(0,14)+'…');if(t.length>9)T.push(t.slice(0,8)+'…');"
"for(var q=0;q<T.length;q++){var tw=g.measureText(T[q]).width,th=fs,e=r+4*dpr;"
"var C=[[x+e,y-th/2],[x-e-tw,y-th/2],[x-tw/2,y-e-th],[x-tw/2,y+e],[x+e*0.7,y-e*0.7-th],[x+e*0.7,y+e*0.7],[x-e*0.7-tw,y-e*0.7-th],[x-e*0.7-tw,y+e*0.7]];"
"for(var k=0;k<C.length;k++){var a=[C[k][0],C[k][1],C[k][0]+tw,C[k][1]+th];if(free(a)){B.push([a[0]-2*dpr,a[1]-1*dpr,a[2]+2*dpr,a[3]+1*dpr]);out.push([T[q],a[0],a[1]+th*0.8]);return}}}});"
"return out;}"
"function draw(g,w,h,dpr,title){g.fillStyle='#0b0e12';g.fillRect(0,0,w,h);var top=0;"
"if(title){g.fillStyle='#ededed';g.font='600 '+22*dpr+'px system-ui,sans-serif';g.fillText('Mappa Wi-Fi',16*dpr,32*dpr);"
"g.fillStyle='#9aa0a6';g.font=13*dpr+'px system-ui,sans-serif';g.fillText(document.getElementById('sub').textContent,16*dpr,54*dpr);"
"var lx=16*dpr;[['fissa',COL[0]],['mobile',COL[1]],['hotspot',COL[2]],['Gadget','#fff']].forEach(function(l){"
"g.fillStyle=l[1];g.beginPath();g.arc(lx+5*dpr,72*dpr,5*dpr,0,7);g.fill();g.fillStyle='#9aa0a6';g.fillText(l[0],lx+14*dpr,77*dpr);"
"lx+=g.measureText(l[0]).width+34*dpr;});top=88*dpr;}"
"var v=fit(w,h,40*dpr,top),L=spread(v,w,h,top,dpr),P=L.P;v.P=P;"
"g.lineWidth=1*dpr;g.strokeStyle='#353d46';GE.forEach(function(e){g.beginPath();g.moveTo(P[e[0]][0],P[e[0]][1]);g.lineTo(P[e[1]][0],P[e[1]][1]);g.stroke();});"
"if(D.p.length>1){g.lineWidth=2*dpr;g.strokeStyle='#55606c';g.beginPath();D.p.forEach(function(p,i){"
"var sx=0,sy=0,k=0;for(var j=i-2;j<=i+2;j++)if(j>=0&&j<D.p.length){sx+=D.p[j][0];sy+=D.p[j][1];k++}"
"var X=v.X(sx/k),Y=v.Y(sy/k);i?g.lineTo(X,Y):g.moveTo(X,Y);});g.stroke();}"
"D.n.forEach(function(n){if(!n.t||n.t.length<2)return;g.strokeStyle=COL[n.k];g.lineWidth=1*dpr;g.beginPath();"
"n.t.forEach(function(p,i){i?g.lineTo(v.X(p[0]),v.Y(p[1])):g.moveTo(v.X(p[0]),v.Y(p[1]))});g.stroke();});"
"g.strokeStyle='#4a535d';g.lineWidth=1*dpr;g.setLineDash([3*dpr,3*dpr]);"
"G.forEach(function(o,i){if(Math.hypot(P[i][0]-L.O[i][0],P[i][1]-L.O[i][1])<8*dpr)return;g.beginPath();g.moveTo(L.O[i][0],L.O[i][1]);g.lineTo(P[i][0],P[i][1]);g.stroke();});"
"g.setLineDash([]);"
"if(D.me){var X=v.X(D.me[0]),Y=v.Y(D.me[1]);g.fillStyle='#fff';g.strokeStyle='#0b0e12';g.lineWidth=3*dpr;"
"g.beginPath();g.arc(X,Y,7*dpr,0,7);g.fill();g.stroke();}"
"G.forEach(function(o,i){g.fillStyle=COL[o.k];g.beginPath();g.arc(P[i][0],P[i][1],6*dpr,0,7);g.fill();"
"if(o.a.length>1){g.strokeStyle='#0b0e12';g.lineWidth=2*dpr;g.beginPath();g.arc(P[i][0],P[i][1],3*dpr,0,7);g.stroke();}});"
"labels(g,L,w,h,top,dpr).forEach(function(t){g.lineWidth=3*dpr;g.strokeStyle='#0b0e12';g.lineJoin='round';g.strokeText(t[0],t[1],t[2]);g.fillStyle='#cfd3d7';g.fillText(t[0],t[1],t[2]);});"
"var m=v.s*10>w/3?5:10;g.strokeStyle='#9aa0a6';g.fillStyle='#9aa0a6';g.lineWidth=2*dpr;g.font=12*dpr+'px system-ui,sans-serif';"
"g.beginPath();g.moveTo(w-16*dpr-m*v.s,h-12*dpr);g.lineTo(w-16*dpr,h-12*dpr);g.stroke();"
"g.fillText(m+' m (indicativi)',w-16*dpr-m*v.s,h-18*dpr);return v;}"
"function render(){var dpr=window.devicePixelRatio||1,w=c.clientWidth,h=Math.round(Math.min(w*1.7,Math.max(w*0.75,(w-80)*aspect()+80)));"
"c.width=w*dpr;c.height=h*dpr;c.style.height=h+'px';view=draw(c.getContext('2d'),c.width,c.height,dpr,false);view.dpr=dpr;}"
"render();window.addEventListener('resize',render);"
"c.addEventListener('click',function(ev){var r=c.getBoundingClientRect(),x=(ev.clientX-r.left)*view.dpr,y=(ev.clientY-r.top)*view.dpr,best=-1,bd=1e9;"
"view.P.forEach(function(p,i){var d=Math.hypot(p[0]-x,p[1]-y);if(d<bd){bd=d;best=i}});"
"info.innerHTML='';if(best<0||bd>30*view.dpr){info.textContent='Tocca un punto per i dettagli.';return;}"
"G[best].a.forEach(function(k,j){var n=D.n[k],f=0;D.e.forEach(function(e){if(e[0]==k||e[1]==k)f++});"
"var p=document.createElement('div'),b=document.createElement('b');b.textContent=n.s||'(nascosta)';p.appendChild(b);"
"p.appendChild(document.createTextNode(' · '+K[n.k]+' · '+n.r+' dBm · vista '+n.v+' volte · '+f+' fili · '+n.b));info.appendChild(p);});"
"if(G[best].a.length>1){var p=document.createElement('div');p.className='n';p.textContent='Stesso apparecchio (indirizzi quasi uguali): disegnate come un punto solo.';info.appendChild(p);}});"
"var ios=/iP(hone|ad|od)/.test(navigator.userAgent)||(navigator.platform=='MacIntel'&&navigator.maxTouchPoints>1);"
"document.getElementById('ovx').onclick=function(){document.getElementById('ov').style.display='none';};"
"document.getElementById('png').onclick=function(){var o=document.createElement('canvas'),dpr=2;o.width=1200;o.height=Math.round(Math.min(2000,Math.max(1000,(1200-160)*aspect()+160+176)));"
"draw(o.getContext('2d'),o.width,o.height,dpr,true);var u=o.toDataURL('image/png');"
"if(ios){document.getElementById('ovi').src=u;document.getElementById('ov').style.display='block';window.scrollTo(0,0);return;}"
"var a=document.createElement('a');a.download='mappa-wifi.png';a.href=u;document.body.appendChild(a);a.click();a.remove();};"
"</script></body></html>\n";

// testo JSON sicuro: anche "<" diventa < (un nome di rete non deve poter chiudere lo script)
static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') fprintf(f, "\\%c", ch);
        else if (ch < 0x20 || ch == '<' || ch == '>' || ch == '&') fprintf(f, "\\u%04x", ch);
        else fputc(ch, f);
    }
    fputc('"', f);
}

bool wm_export_html(const char *path, const char *when)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fputs(HEAD, f);
    fputs("{\"t\":", f);
    json_str(f, when);
    fprintf(f, ",\"s\":%lu,\"n\":[", (unsigned long)wm_scans());
    // solo le reti con una posizione (gli indici dei fili sono quelli di questo elenco)
    int map[WM_MAX], k = 0;
    for (int i = 0; i < wm_count(); i++) {
        const wm_node_t *n = wm_node(i);
        map[i] = -1;
        if (!n->placed) continue;
        map[i] = k++;
        fputs(k > 1 ? ",{\"s\":" : "{\"s\":", f);
        json_str(f, n->ssid);
        fprintf(f, ",\"b\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"x\":%.2f,\"y\":%.2f,\"k\":%d,\"r\":%.0f,\"v\":%u",
                n->bssid[0], n->bssid[1], n->bssid[2], n->bssid[3], n->bssid[4], n->bssid[5], n->x, n->y,
                n->kind % 3, n->rssi, n->seen);
        if (n->kind != WM_FIXED && n->tn > 1) {
            fputs(",\"t\":[", f);
            for (int t = 0; t < n->tn; t++) fprintf(f, "%s[%.2f,%.2f]", t ? "," : "", n->tx[t], n->ty[t]);
            fputc(']', f);
        }
        fputc('}', f);
    }
    fputs("],\"e\":[", f);
    int ne = 0;
    for (int a = 0; a < wm_count(); a++)
        for (int b = a + 1; b < wm_count(); b++) {
            int c;
            if (map[a] < 0 || map[b] < 0 || wm_node(a)->kind != WM_FIXED || wm_node(b)->kind != WM_FIXED) continue;
            if (!wm_edge(a, b, NULL, &c) || c < 2) continue;
            fprintf(f, "%s[%d,%d,%d]", ne++ ? "," : "", map[a], map[b], c);
        }
    fputs("],\"p\":[", f);
    float tp[WM_TRAIL][2];
    int tn = wm_trail(tp, WM_TRAIL);
    for (int i = 0; i < tn; i++) fprintf(f, "%s[%.2f,%.2f]", i ? "," : "", tp[i][0], tp[i][1]);
    fputc(']', f);
    float mx, my;
    if (wm_me(&mx, &my)) fprintf(f, ",\"me\":[%.2f,%.2f]", mx, my);
    fputc('}', f);
    fputs(TAIL, f);
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}
