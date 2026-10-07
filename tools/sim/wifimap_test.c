// wifimap_test.c — prova della Mappa Wi-Fi sul PC: edificio simulato 30x15 m con 7 reti
// fisse, un furgone che passa e un hotspot che segue; una camminata di 2 m a scansione con
// circa 4 dB di rumore. Stampa le reti con il tipo riconosciuto e l errore medio di
// posizione dopo aver ruotato la mappa su quella vera.
// gcc -O1 -Imain -Itools/sim/inc tools/sim/wifimap_test.c main/wifimap.c -lm && SEED=3 ./a.out
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "wifimap.h"
static double gauss(void){ double u=(rand()+1.0)/(RAND_MAX+2.0), v=(rand()+1.0)/(RAND_MAX+2.0); return sqrt(-2*log(u))*cos(2*M_PI*v);}
typedef struct { const char *name; double x,y; int mobile; uint8_t b0; } ap_t;
static ap_t AP[] = {{"Casa",2,2,0,0x10},{"Casa_5G",3,2.5,0,0x10},{"Ufficio",25,3,0,0x20},{"Vicino",14,13,0,0x30},
 {"Stampante",28,13,0,0x40},{"Sala",12,4,0,0x50},{"Garage",-2,12,0,0x60},{"Furgone",0,0,1,0x70},{"iPhone di Ugo",0,0,2,0x72}};
#define NA (int)(sizeof(AP)/sizeof(AP[0]))
int main(){
  srand(getenv("SEED") ? atoi(getenv("SEED")) : 1); wm_reset();
  // percorso: giro lungo i corridoi dell'edificio 30x15, avanti e indietro
  double px[]={0,28,28,0,0,14,14,28}, py[]={1,1,13,13,1,1,13,13};
  int seg=7; double pos=0;
  for (int s=0;s<400;s++){
    // camminata: 2 m per scansione lungo il percorso
    double L[8], tot=0; for (int q=0;q<seg;q++){ L[q]=hypot(px[q+1]-px[q],py[q+1]-py[q]); tot+=L[q]; }
    double u=fmod(s*2.0, tot); int k=0; while (u>L[k]) { u-=L[k]; k++; } double f=u/L[k];
    double x=px[k]+(px[k+1]-px[k])*f, y=py[k]+(py[k+1]-py[k])*f;
    // mobili: il furgone va avanti e indietro lungo y=20; l'iPhone segue a 3 m di distanza, poi va via
    AP[7].x = 15+12*sin(s*0.05); AP[7].y=18;
    AP[8].x = x+3; AP[8].y=y+1;
    wifi_ap_t sc[16]; int n=0;
    for (int a=0;a<NA;a++){
      double d=sqrt((x-AP[a].x)*(x-AP[a].x)+(y-AP[a].y)*(y-AP[a].y)); if (d<0.5) d=0.5;
      double rssi=-40-10*2.7*log10(d)+4*gauss();
      if (rssi<-90) continue;
      memset(&sc[n],0,sizeof(sc[n])); snprintf(sc[n].ssid,33,"%s",AP[a].name); sc[n].rssi=rssi; sc[n].bssid[0]=AP[a].b0; sc[n].bssid[5]=a; n++;
    }
    wm_add_scan(sc,n);
  }
  // confronto con la verità (Procrustes: traslazione, rotazione, riflessione, scala 1)
  int idx[16], m=0; double tx[16],ty[16];
  for (int i=0;i<wm_count();i++){ const wm_node_t *p=wm_node(i); int a=p->bssid[5];
    printf("%-14s tipo=%d viste=%d deriva=%.2f pos=(%.1f,%.1f)\n",p->ssid,p->kind,p->seen,p->drift,p->x,p->y);
    if (!AP[a].mobile && p->kind==0){ idx[m]=i; tx[m]=AP[a].x; ty[m]=AP[a].y; m++; } }
  double best=1e9;
  for (int refl=0;refl<2;refl++) for (int ang=0;ang<360;ang+=2){
    double c=cos(ang*M_PI/180), s=sin(ang*M_PI/180), mx=0,my=0,qx=0,qy=0;
    double X[16],Y[16];
    for (int k=0;k<m;k++){ const wm_node_t *p=wm_node(idx[k]); double yy=refl?-p->y:p->y; X[k]=c*p->x-s*yy; Y[k]=s*p->x+c*yy; mx+=X[k]; my+=Y[k]; qx+=tx[k]; qy+=ty[k]; }
    mx/=m;my/=m;qx/=m;qy/=m; double e=0;
    for (int k=0;k<m;k++){ double dx=X[k]-mx-(tx[k]-qx), dy=Y[k]-my-(ty[k]-qy); e+=dx*dx+dy*dy; }
    e=sqrt(e/m); if (e<best) best=e; }
  // scala reale fra coppie
  printf("fisse: %d, errore medio di posizione (dopo rotazione): %.1f m su un edificio 30x15\n", m, best);
  float L; int c; printf("filo Casa-Ufficio: ");
  for (int i=0;i<wm_count();i++) for (int j=0;j<wm_count();j++) if (!strcmp(wm_node(i)->ssid,"Casa")&&!strcmp(wm_node(j)->ssid,"Ufficio")&&wm_edge(i,j,&L,&c)) printf("%.1f m (vero 23.0) visto %d volte\n",L,c);
}
