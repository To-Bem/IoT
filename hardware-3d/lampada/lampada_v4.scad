// =====================================================================
//  Tô Bem — lâmpada v4: sem tampa, com entrada deslizante para a placa difusora
//  (2 anéis de LED + radar no centro + ESP32 + DHT22)
//  Abra no OpenSCAD, ajuste as medidas abaixo, F6 e exporte STL.
//  Medidas em milímetros. O fundo (z = 0) é o lado do TETO;
//  a placa difusora fica do lado virado para o cômodo.
// =====================================================================

// O que mostrar: "base" | "tampa_esp" | "tampa_alim" | "presilha" (peças já na posição de imprimir)
//                "montagem" (placa entrando) | "fechada" | "layout"
parte = "montagem";

// ---------- PLACA DIFUSORA: meça a espessura antes de imprimir ----------
placa_e  = 2.0;   // espessura da placa difusora ("teflon" / acrílico leitoso)

// ---------- Medidas dos componentes ----------
anel_de  = 85.8;  // anel de LED: diâmetro externo
anel_e   = 2.8;   // anel de LED: espessura (placa + LED)
anel_furo_dist = 0;    // anel de LED: distância entre dois furos OPOSTOS, centro a centro (0 = sem pinos, apoio pela borda)
anel_furo_d    = 2.0;  // anel de LED: diâmetro dos furos de encaixe
anel_furo_ang  = 45;   // anel de LED: ângulo do primeiro furo
pcb_c    = 55.0;  // ESP32: comprimento
pcb_l    = 28.0;  // ESP32: largura
pcb_e    = 1.6;   // ESP32: espessura só da placa
radar_c  = 22.0;  // radar: comprimento
radar_l  = 16.0;  // radar: largura
dht_c    = 38.0;  // compartimento do DHT22: comprimento
dht_e    = 8.0;   // compartimento do DHT22: profundidade

// ---------- Ajustes da caixa ----------
parede   = 3.4;   // paredes externas (mais grossas, para caber a canaleta da placa)
fundo    = 2.0;   // fundo (lado do teto)
h_int    = 22.0;  // altura interna, do fundo até a placa difusora
raio     = 12.0;  // arredondamento dos cantos
div_e    = 1.6;   // divisórias das duas baias
folga    = 0.4;   // folga em volta do ESP32
fol_anel = 2.1;   // folga entre o anel e a parede
vao_radar = 20.0; // espaço entre os dois anéis, no centro, onde fica o radar
sob      = 11.0;  // espaço sob o ESP32 (pinos + fios soldados)
anel_h   = 3.0;   // altura dos apoios dos anéis
usb_l    = 12.5;  // janela do cabo USB
canto_x  = 6.8;   // distância do ESP32 até a parede do USB
apoio_c  = 3.5;
apoio_l  = 5.0;
furo_cab = 4.5;   // furo de fixação: corpo do parafuso
furo_cabeca = 9.0;//                  cabeça do parafuso
furo_curso  = 8.0;//                  quanto a lâmpada desliza para travar

// ---------- Entrada deslizante da placa ----------
canal_p  = 1.8;   // profundidade da canaleta nas paredes
canal_f  = 0.4;   // folga da placa na altura
aro_h    = 1.6;   // altura do aro que segura a placa
entrada_y = 9.0;  // os trilhos começam a esta distância da frente (cantos arredondados)
placa_fl = 0.3;   // folga lateral da placa
placa_r  = 10.0;  // raio dos cantos da placa
pres_l   = 14.0;  // presilha: largura

// ---------- Medidas calculadas ----------
ro     = anel_de/2;
cc     = ro + fol_anel;
baia_c = canto_x + pcb_c + folga + div_e;
baia_l = 1.0 + pcb_l + folga + div_e;
ci     = ceil(max(baia_c + cc + sqrt(pow(ro + 2.1, 2) - pow(cc - baia_l, 2)),
                  2*cc + (anel_de + vao_radar)/sqrt(2)));
ext    = ci + 2*parede;
z0     = fundo + h_int;                 // nível onde a placa apoia
canal_h = placa_e + canal_f;
z_topo = z0 + canal_h + aro_h;          // altura total
c1     = [cc, cc];
c2     = [ci - cc, ci - cc];
esp_x  = ci - canto_x - pcb_c;
esp_y  = 1.0;
z_pcb  = fundo + sob;
x_dht  = 8.0;
y_alim = ci - 20;
k1     = [cc - furo_curso/2, cc];
k2     = [ci - cc - furo_curso/2, ci - cc];
zc     = z0 - 3.4;                      // altura das travas das tampinhas
placa_c = ext - 2*(parede - canal_p) - 2*placa_fl;        // placa: largura
placa_l = ext - (parede - canal_p) - 2*placa_fl;          // placa: comprimento (sentido de entrada)
zg     = z0 - 10.5;                     // canaleta das presilhas na face da frente
$fn = 72;

echo(str("Tamanho externo (mm): ", ext, " x ", ext, " x ", z_topo));
echo(str("PLACA DIFUSORA (mm): ", placa_c, " de largura x ", placa_l, " no sentido de entrada, cantos com raio ", placa_r, ", espessura ate ", placa_e));
echo(str("Parafusos no teto: ", k2[0] - k1[0], " mm num sentido e ", k2[1] - k1[1], " mm no outro"));

module ret(c, l, r) { translate([r, r]) offset(r) square([c - 2*r, l - 2*r]); }
module contorno() { ret(ext, ext, raio); }
module interior2d() { translate([parede, parede]) ret(ci, ci, raio - parede); }
module I() { translate([parede, parede, 0]) children(); }                 // coordenadas internas
module sim() { translate([ci, ci, 0]) rotate([0, 0, 180]) children(); }   // canto oposto
module placa2d() { translate([parede - canal_p + placa_fl, placa_fl]) ret(placa_c, placa_l, placa_r); }

module tx(x, y, r, l) { translate([x, y, zc]) rotate([0, 90, 0]) cylinder(r = r, h = l, center = true, $fn = 24); }
module ty(x, y, r, l) { translate([x, y, zc]) rotate([90, 0, 0]) cylinder(r = r, h = l, center = true, $fn = 24); }
// travas das duas tampinhas (o = distância do centro até a face da parede)
module travas_esp(r, o)  { tx(ci - baia_c/2 + 1, o, r, 12); tx(ci - baia_c/2 + 1, baia_l - div_e - o, r, 12); }
module travas_alim(r, o) { ty(o, ci - 19, r, 10); ty(baia_c - div_e - o, ci - 19, r, 10); }

module baia_paredes() {
  translate([ci - baia_c, -0.1, fundo - 0.01]) cube([div_e, baia_l + 0.1, h_int + 0.01]);
  translate([ci - baia_c, baia_l - div_e, fundo - 0.01]) cube([baia_c + 0.1, div_e, h_int + 0.01]);
}
module baia_passagens() {
  translate([ci - baia_c - 1, 11, fundo]) cube([div_e + 2, 8, 5]);
  translate([ci - 34, baia_l - div_e - 1, fundo]) cube([8, div_e + 2, 5]);
}

module apoio_anel() {
  if (anel_furo_dist > 0) {
    for (a = [0, 90, 180, 270]) rotate(anel_furo_ang + a) translate([anel_furo_dist/2, 0, 0]) {
      cylinder(d = 6, h = anel_h);
      cylinder(d = anel_furo_d - 0.3, h = anel_h + anel_e + 1.5, $fn = 24);
    }
  } else {
    for (a = [0, 90, 180, 270]) rotate(a) {
      translate([ro - 6, -1.5, 0]) cube([7.6, 3, anel_h]);
      translate([ro + 0.4, -1.5, 0]) cube([1.2, 3, anel_h + anel_e]);
    }
  }
}

module berco_radar() {
  f = 0.3; h = anel_h + 1;
  for (s = [0, 1]) {
    translate([-radar_c/2, s == 0 ? -radar_l/2 - f - 1.2 : radar_l/2 - 1.5, 0]) cube([radar_c - 5, 2.7 + f, h]);
    translate([-radar_c/2, s == 0 ? -radar_l/2 - f - 1.2 : radar_l/2 + f, 0]) cube([radar_c - 5, 1.2, h + 2.5]);
  }
  translate([-radar_c/2 - f - 1.2, -radar_l/2 - f - 1.2, 0]) cube([1.2, radar_l + 2*f + 2.4, h + 2.5]);
}

module esp_apoios() {
  hg = sob + pcb_e + 0.6;
  for (ix = [0, 1]) {
    x0 = esp_x + ix*(pcb_c - apoio_c) - (ix == 0 ? folga : 0);
    translate([x0, -0.1, fundo - 0.01]) cube([apoio_c + folga, esp_y - folga + 0.1, hg]);
    translate([x0, esp_y - folga, fundo - 0.01]) cube([apoio_c + folga, apoio_l + folga, sob]);
    translate([x0, esp_y + pcb_l - apoio_l, fundo - 0.01]) cube([apoio_c + folga, apoio_l + folga + 0.1, sob]);
  }
  for (iy = [0, 1])
    translate([esp_x + pcb_c + folga, esp_y + iy*(pcb_l - apoio_l), fundo - 0.01]) cube([1.5, apoio_l, hg]);
}

module dht_paredes() {
  translate([x_dht - 1.2, ci - dht_e - 1.2, fundo - 0.01]) cube([dht_c + 2.4, 1.2, h_int + 0.01]);
  for (x = [x_dht - 1.2, x_dht + dht_c])
    translate([x, ci - dht_e - 1.2, fundo - 0.01]) cube([1.2, dht_e + 1.3, h_int + 0.01]);
}

module fechadura() {
  cylinder(d = furo_cabeca, h = fundo + 2);
  hull() { cylinder(d = furo_cab, h = fundo + 2); translate([furo_curso, 0, 0]) cylinder(d = furo_cab, h = fundo + 2); }
}

// moldura de cima: canaleta em 3 lados + aro; a frente (y = 0) fica aberta para a placa entrar
module moldura() {
  difference() {
    union() {
      translate([0, 0, z0 - 0.01]) linear_extrude(canal_h + 0.01) difference() { contorno(); offset(r = canal_p) interior2d(); }
      translate([0, 0, z0 + canal_h - 0.01]) linear_extrude(aro_h + 0.01) difference() { contorno(); interior2d(); }
    }
    translate([-1, -1, z0]) cube([ext + 2, entrada_y + 1, canal_h + aro_h + 1]);          // entrada
    for (x = [parede - canal_p, ext - parede - 0.1])                                     // boca alargada, para a placa entrar fácil
      hull() {
        translate([x, entrada_y - 0.1, z0]) cube([canal_p + 0.1, 0.1, canal_h + 1.0]);
        translate([x, entrada_y + 8, z0]) cube([canal_p + 0.1, 0.1, canal_h]);
      }
  }
}

module base() {
  difference() {
    union() {
      difference() {
        linear_extrude(z0) contorno();
        translate([0, 0, fundo]) linear_extrude(z0) interior2d();
      }
      moldura();
      I() {
        baia_paredes();
        sim() baia_paredes();
        for (c = [c1, c2]) translate([c[0], c[1], fundo - 0.01]) apoio_anel();
        translate([ci/2, ci/2, fundo - 0.01]) rotate(-45) berco_radar();
        esp_apoios();
        dht_paredes();
      }
    }
    I() {
      baia_passagens();
      sim() baia_passagens();
      // janela do USB do ESP32 (parede direita)
      translate([ci - 1, esp_y + pcb_l/2 - usb_l/2, z_pcb - 1]) cube([parede + 2, usb_l, 8]);
      // entrada do cabo de 5V: janela lateral ou furo no fundo (vindo do teto)
      translate([-parede - 1, y_alim - 3, fundo + 6]) cube([parede + 2, 6, 8]);
      translate([14, y_alim, -1]) hull() { cylinder(d = 8, h = fundo + 2); translate([furo_curso, 0, 0]) cylinder(d = 8, h = fundo + 2); }
      // furos de fixação no teto
      translate([k1[0], k1[1], -1]) fechadura();
      translate([k2[0], k2[1], -1]) fechadura();
      // grelha do DHT22 na parede de fora + passagem dos fios para dentro
      for (i = [0 : floor((dht_c - 6)/5)])
        translate([x_dht + 3 + i*5, ci - 1, 5]) cube([2.2, parede + 2, z0 - 9]);
      translate([x_dht + dht_c - 6, ci - dht_e - 2.2, z0 - 3.5]) cube([4, 3.2, 4]);
      // canaletas das travas das tampinhas
      travas_esp(0.7, 0.15);
      travas_alim(0.7, 0.15);
    }
    // canaleta na face da frente, onde as presilhas engatam
    translate([raio + 2, -0.1, zg - 0.6]) cube([ext - 2*raio - 4, 1.0, 2.1]);
  }
}

// ---------- tampinhas das duas baias (ficam por baixo da placa difusora) ----------
module forma_esp()  { intersection() { offset(delta = -0.25) interior2d();
    translate([parede + ci - baia_c + div_e + 0.25, -5]) square([baia_c + 10, parede + baia_l - div_e - 0.25 + 5]); } }
module forma_alim() { difference() {
    intersection() { offset(delta = -0.25) interior2d();
      translate([-5, parede + ci - baia_l + div_e + 0.25]) square([parede + baia_c - div_e - 0.25 + 5, baia_l + 10]); }
    translate([parede + x_dht - 1.45, parede + ci - dht_e - 1.45]) square([dht_c + 2.9, dht_e + 10]); } }

module tampinha(esp) {
  difference() {
    union() {
      translate([0, 0, z0 - 1.4]) linear_extrude(1.2) { if (esp) forma_esp(); else forma_alim(); }
      translate([0, 0, z0 - 5.4]) linear_extrude(4.01) difference() {
        if (esp) forma_esp(); else forma_alim();
        offset(delta = -1.5) { if (esp) forma_esp(); else forma_alim(); }
      }
      I() intersection() {
        if (esp) travas_esp(0.6, 0.35); else travas_alim(0.6, 0.35);
        translate([-parede, -parede, zc - 1]) cube([ext, ext, 2]);
      }
      if (esp) {                                  // pinos que seguram o ESP32 pelos cantos
        zp = z_pcb + pcb_e + 0.3;
        I() for (iy = [0, 1]) {
          translate([esp_x + 0.2, esp_y + 0.2 + iy*(pcb_l - 3.4), zp]) cube([6, 3, z0 - 1.4 - zp + 0.01]);
          translate([esp_x + pcb_c - 3.2, esp_y + 0.2 + iy*(pcb_l - 3.4), zp]) cube([3, 3, z0 - 1.4 - zp + 0.01]);
        }
      }
    }
    if (esp)                                      // a aba não pode bater no plugue USB
      I() translate([ci - 3, esp_y + pcb_l/2 - usb_l/2 - 0.5, z0 - 6.4]) cube([4, usb_l + 1, 5]);
    else                                          // passagem dos fios do DHT22
      I() translate([x_dht + dht_c - 7, ci - dht_e - 1.2 - 4.5, z0 - 6.4]) cube([6, 4.6, 7]);
  }
}

// ---------- presilha: trava a placa na entrada ----------
// engata na canaleta da frente; para soltar, puxe a aba de baixo para fora
module presilha2d() {
  polygon([[0, canal_h - 0.1], [0, zg - z0 + 1.35], [0.6, zg - z0 + 1.35], [0.6, zg - z0 + 0.6], [0, zg - z0 - 0.4],
           [0, zg - z0 - 3.5], [-2.6, zg - z0 - 3.5], [-2.6, zg - z0 - 2.3], [-1.2, zg - z0 - 2.3],
           [-1.2, canal_h + aro_h - 0.1], [5, canal_h + aro_h - 0.1], [5, canal_h - 0.1]]);
}
module presilha() { linear_extrude(pres_l) presilha2d(); }
module presilha_montada(x) { translate([x, 0, z0]) rotate([90, 0, 90]) presilha(); }
module presilhas_montadas() { for (x = [raio + 4, ext - raio - 4 - pres_l]) presilha_montada(x); }

// ---------- só para visualizar ----------
module placa(desl = 0) { translate([0, -desl, z0 + 0.2]) linear_extrude(placa_e) placa2d(); }
module anel_falso() {
  color("#222") difference() { cylinder(d = anel_de, h = 1.6); translate([0, 0, -1]) cylinder(d = anel_de - 20, h = 4); }
  color("#fff8e1") for (i = [0:15]) rotate(i*22.5 + 11.25) translate([ro - 7.5, -2.5, 1.6]) cube([5, 5, 1.2]);
}
module componentes() {
  I() {
    for (c = [c1, c2]) translate([c[0], c[1], fundo + anel_h]) anel_falso();
    translate([ci/2, ci/2, fundo + anel_h + 1]) rotate(-45) translate([-radar_c/2, -radar_l/2, 0]) {
      color("#1565c0") cube([radar_c, radar_l, 1.2]);
      color("#c9a227") translate([3, 3, 1.2]) cube([radar_c - 10, radar_l - 6, 0.3]);
    }
    translate([esp_x, esp_y, z_pcb]) {
      color("#1b5e20") cube([pcb_c, pcb_l, pcb_e]);
      color("silver") translate([6.5, pcb_l/2 - 9, pcb_e]) cube([19, 18, 3.2]);
      color("#263238") translate([0, pcb_l/2 - 9, pcb_e]) cube([6.5, 18, 0.8]);
      color("silver") translate([pcb_c - 5.5, pcb_l/2 - 3.9, pcb_e]) cube([5.8, 7.8, 2.8]);
      color("#222") for (y = [0.2, pcb_l - 2.74]) translate([9, y, -2.5]) cube([38.1, 2.54, 2.5]);
      color("gold") for (y = [1.47, pcb_l - 1.47]) for (i = [0:14])
        translate([9 + 1.27 + i*2.54, y, -8.5]) cylinder(d = 0.7, h = 6, $fn = 8);
    }
    translate([x_dht + 1, ci - dht_e + 0.5, fundo + 3]) {
      color("white") cube([25, 7, 15]);
      color("#222") translate([25, 2, 0]) cube([10, 1.6, 15]);
    }
    color("#2e7d32") translate([30, ci - baia_l + 3, fundo]) cube([28, 8, 10]);
    color("#37474f") translate([30, ci - 14.5, fundo + 5]) rotate([0, 90, 0]) cylinder(d = 10, h = 20);
  }
}

if (parte == "base") base();
else if (parte == "tampa_esp")  translate([0, ext, z0 - 0.2]) rotate([180, 0, 0]) tampinha(true);
else if (parte == "tampa_alim") translate([0, ext, z0 - 0.2]) rotate([180, 0, 0]) tampinha(false);
else if (parte == "presilha") presilha();
else if (parte == "tampinhas_montadas") { tampinha(true); tampinha(false); }      // daqui para baixo: só para conferir
else if (parte == "componentes") componentes();
else if (parte == "placa_montada") placa();
else if (parte == "presilhas_montadas") presilhas_montadas();
else if (parte == "layout") { color("#ffffff") base(); componentes(); }
else if (parte == "fechada") {
  color("#ffffff") base();
  color("#fbfbf8") { tampinha(true); tampinha(false); }
  color("#f4f4ee") placa();
  color("#ffffff") presilhas_montadas();
}
else {
  color("#ffffff") base();
  componentes();
  color("#fbfbf8") { tampinha(true); tampinha(false); }
  color("#f4f4ee", 0.75) placa(95);
  color("#2f6f62") for (x = [raio + 4, ext - raio - 4 - pres_l]) translate([0, -125, -6]) presilha_montada(x);
}
