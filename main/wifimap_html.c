// wifimap_html.c — pagina della Mappa Wi-Fi: un file HTML autonomo (niente da internet)
// con i dati dentro. Si apre dal telefono collegato all'hotspot del Gadget e si può salvare
// e condividere: funziona anche da file. Disegna reti, fili, percorso; toccando un punto
// mostra i dettagli; "Scarica immagine" salva un PNG (con titolo e legenda).
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
"#info{padding:12px 16px;font-size:15px;min-height:44px}#info b{color:#ffb020}"
"footer{padding:8px 16px 24px;color:#6e747c;font-size:12px}"
"</style></head><body><header><h1>Mappa Wi-Fi</h1><p id=sub></p>"
"<p class=l><span style=background:#ffb020></span>fissa<span style=background:#ff6b57></span>mobile"
"<span style=background:#ff4fd8></span>hotspot<span style=background:#fff></span>Gadget</p>"
"<button class=b id=png>Scarica immagine</button><a class=\"b g\" id=save href=/mappa-wifi.html download=mappa-wifi.html>Salva la pagina</a>"
"<p id=cna>Se i pulsanti non scaricano nulla sei nella finestra di accesso alla rete: chiudila restando "
"collegato all'hotspot e apri <b>192.168.4.1</b> nel browser.</p>"
"</header><canvas id=c></canvas><div id=info>Tocca un punto per i dettagli.</div>"
"<footer>Mappa relativa: le distanze sono stimate dalla potenza dei segnali (qualche metro di errore) "
"e l'orientamento è arbitrario. I fili collegano le reti viste nella stessa scansione.</footer>"
"<script>var D=";

static const char TAIL[] =
";\n"
"var K=['fissa','mobile','hotspot'],COL=['#ffb020','#ff6b57','#ff4fd8'];"
"var c=document.getElementById('c'),info=document.getElementById('info');"
"document.getElementById('sub').textContent=D.t+' \\u00b7 '+D.n.length+' reti \\u00b7 '+D.s+' scansioni';"
"if(location.protocol=='file:'||location.hostname!='192.168.4.1'){document.getElementById('save').style.display='none';document.getElementById('cna').style.display='none';}"
"var view;"
// adatta la mappa al riquadro (larghezza w, altezza h, margine m, spazio in alto top)
"function fit(w,h,m,top){var x0=1e9,x1=-1e9,y0=1e9,y1=-1e9;"
"function a(x,y){x0=Math.min(x0,x);x1=Math.max(x1,x);y0=Math.min(y0,y);y1=Math.max(y1,y);}"
"D.n.forEach(function(n){a(n.x,n.y)});D.p.forEach(function(p){a(p[0],p[1])});"
"if(x0>x1){x0=y0=-5;x1=y1=5}var W=Math.max(x1-x0,6),H=Math.max(y1-y0,6);"
"var R=m*2.5,s=Math.min((w-2*m-R)/W,(h-top-2*m)/H);"   // R: spazio a destra per i nomi
"return{s:s,X:function(x){return (w-R)/2+(x-(x0+x1)/2)*s},Y:function(y){return top+(h-top)/2+(y-(y0+y1)/2)*s}};}"
"function draw(g,w,h,dpr,title){g.fillStyle='#0b0e12';g.fillRect(0,0,w,h);var top=0;"
"if(title){g.fillStyle='#ededed';g.font='600 '+22*dpr+'px system-ui,sans-serif';g.fillText('Mappa Wi-Fi',16*dpr,32*dpr);"
"g.fillStyle='#9aa0a6';g.font=13*dpr+'px system-ui,sans-serif';g.fillText(document.getElementById('sub').textContent,16*dpr,54*dpr);"
"var lx=16*dpr;[['fissa',COL[0]],['mobile',COL[1]],['hotspot',COL[2]],['Gadget','#fff']].forEach(function(l){"
"g.fillStyle=l[1];g.beginPath();g.arc(lx+5*dpr,72*dpr,5*dpr,0,7);g.fill();g.fillStyle='#9aa0a6';g.fillText(l[0],lx+14*dpr,77*dpr);"
"lx+=g.measureText(l[0]).width+34*dpr;});top=88*dpr;}"
"var v=fit(w,h,34*dpr,top);"
"g.lineWidth=1*dpr;g.strokeStyle='#2c333b';D.e.forEach(function(e){var a=D.n[e[0]],b=D.n[e[1]];"
"g.beginPath();g.moveTo(v.X(a.x),v.Y(a.y));g.lineTo(v.X(b.x),v.Y(b.y));g.stroke();});"
"if(D.p.length>1){g.lineWidth=2*dpr;g.strokeStyle='#55606c';g.beginPath();D.p.forEach(function(p,i){"
"var sx=0,sy=0,k=0;for(var j=i-2;j<=i+2;j++)if(j>=0&&j<D.p.length){sx+=D.p[j][0];sy+=D.p[j][1];k++}"
"var X=v.X(sx/k),Y=v.Y(sy/k);i?g.lineTo(X,Y):g.moveTo(X,Y);});g.stroke();}"
"D.n.forEach(function(n){if(!n.t||n.t.length<2)return;g.strokeStyle=COL[n.k];g.lineWidth=1*dpr;g.beginPath();"
"n.t.forEach(function(p,i){i?g.lineTo(v.X(p[0]),v.Y(p[1])):g.moveTo(v.X(p[0]),v.Y(p[1]))});g.stroke();});"
"g.font=12*dpr+'px system-ui,sans-serif';"
"D.n.forEach(function(n){var X=v.X(n.x),Y=v.Y(n.y);g.fillStyle=COL[n.k];g.beginPath();g.arc(X,Y,6*dpr,0,7);g.fill();"
"g.fillStyle='#cfd3d7';g.fillText(n.s,X+9*dpr,Y+4*dpr);});"
"if(D.me){var X=v.X(D.me[0]),Y=v.Y(D.me[1]);g.fillStyle='#fff';g.strokeStyle='#0b0e12';g.lineWidth=3*dpr;"
"g.beginPath();g.arc(X,Y,7*dpr,0,7);g.fill();g.stroke();}"
// scala: un segmento di 5 o 10 m in basso a destra
"var m=v.s*10>w/3?5:10;g.strokeStyle='#9aa0a6';g.fillStyle='#9aa0a6';g.lineWidth=2*dpr;"
"g.beginPath();g.moveTo(w-16*dpr-m*v.s,h-16*dpr);g.lineTo(w-16*dpr,h-16*dpr);g.stroke();"
"g.fillText(m+' m',w-16*dpr-m*v.s,h-22*dpr);return v;}"
"function render(){var dpr=window.devicePixelRatio||1,w=c.clientWidth,h=Math.round(w*0.8);"
"c.width=w*dpr;c.height=h*dpr;c.style.height=h+'px';view=draw(c.getContext('2d'),c.width,c.height,dpr,false);view.dpr=dpr;}"
"render();window.addEventListener('resize',render);"
"c.addEventListener('click',function(ev){var r=c.getBoundingClientRect(),x=(ev.clientX-r.left)*view.dpr,y=(ev.clientY-r.top)*view.dpr,best=-1,bd=1e9;"
"D.n.forEach(function(n,i){var d=Math.hypot(view.X(n.x)-x,view.Y(n.y)-y);if(d<bd){bd=d;best=i}});"
"if(best<0||bd>30*view.dpr){info.textContent='Tocca un punto per i dettagli.';return;}var n=D.n[best],f=0;"
"D.e.forEach(function(e){if(e[0]==best||e[1]==best)f++});"
"info.innerHTML='';var b=document.createElement('b');b.textContent=n.s;info.appendChild(b);"
"info.appendChild(document.createTextNode(' \\u00b7 '+K[n.k]+' \\u00b7 '+n.r+' dBm \\u00b7 vista '+n.v+' volte \\u00b7 '+f+' fili \\u00b7 '+n.b));});"
"document.getElementById('png').onclick=function(){var o=document.createElement('canvas'),dpr=2;o.width=1200;o.height=1000;"
"draw(o.getContext('2d'),o.width,o.height,dpr,true);"
"var a=document.createElement('a');a.download='mappa-wifi.png';a.href=o.toDataURL('image/png');document.body.appendChild(a);a.click();a.remove();};"
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
