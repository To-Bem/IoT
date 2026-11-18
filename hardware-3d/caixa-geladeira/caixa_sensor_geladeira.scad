// =====================================================================
//  Tô Bem — caixa do sensor da geladeira (ESP32 DevKit V1 + MC-38)
//  Abra no OpenSCAD (gratuito), ajuste as medidas abaixo, F6 e exporte STL.
//  Todas as medidas em milímetros.
// =====================================================================

// O que mostrar: "base" | "tampa" (já virada para imprimir) | "montagem" | "geladeira"
parte = "montagem";

// ---------- MEÇA A SUA PLACA COM PAQUÍMETRO/RÉGUA E AJUSTE AQUI ----------
pcb_c = 52.0;   // comprimento da placa do ESP32 (lado do USB até a antena)
pcb_l = 28.5;   // largura da placa
pcb_e = 1.6;    // espessura da placa

// ---------- Ajustes da caixa ----------
folga    = 0.4;   // folga entre a placa e a parede (cada lado)
parede   = 2.0;   // espessura das paredes
fundo    = 2.0;   // espessura do fundo (lado que cola na geladeira)
sob      = 20.0;  // espaço embaixo da placa: 20 = cabe jumper fêmea nos pinos
                  //                          11 = caixa mais fina, fios SOLDADOS nos pinos
sobre    = 6.5;   // espaço acima da placa (módulo Wi-Fi + conector USB)
tampa_e  = 2.0;   // espessura da tampa
raio     = 3.0;   // arredondamento dos cantos

usb_l    = 12.5;  // largura do rasgo do cabo USB (plugue micro-USB comum ~11 mm)
fio_l    = 5.0;   // largura do rasgo dos fios do MC-38
fio_h    = 3.5;   // altura da passagem dos fios
fio_lado = 1;     // em qual parede longa sai o fio do MC-38: 0 ou 1 (escolha a que fica virada para a porta)
apoio_c  = 3.5;   // pilares de apoio nos 4 cantos da placa (sentido do comprimento)
apoio_l  = 5.0;   //   "       "    (sentido da largura)

folga_tampa = 0.2; // folga do encaixe da tampa (aumente para 0.3 se ficar dura)
aba_h    = 4.0;   // altura da aba da tampa que entra na caixa
aba_e    = 1.5;   // espessura da aba
texto    = "Tô Bem";

// ---------- Medidas calculadas ----------
ci_c  = pcb_c + 2*folga;          // cavidade interna
ci_l  = pcb_l + 2*folga;
ext_c = ci_c + 2*parede;          // externo
ext_l = ci_l + 2*parede;
z_pcb = fundo + sob;              // altura onde a placa apoia
h_base = z_pcb + pcb_e + sobre;   // altura da base
x_fio  = ext_c*0.72;              // posição do rasgo dos fios
x_trava = ext_c*0.30;             // posição das travas laterais
z_trava = h_base - 2.5;
z_fio   = fundo + 14;             // fundo do rasgo dos fios
y_fio   = fio_lado == 0 ? 0 : ext_l - parede;   // parede onde fica o rasgo
$fn = 48;

echo(str("Tamanho externo (mm): ", ext_c, " x ", ext_l, " x ", h_base + tampa_e));

module contorno(c, l, r) {
  translate([r, r]) offset(r) square([c - 2*r, l - 2*r]);
}

// travas: 2 nas paredes longas + 1 na parede da antena
module travas(r, y_off) {
  translate([x_trava, parede + y_off, z_trava]) rotate([0, 90, 0]) cylinder(r = r, h = 14, center = true);
  translate([x_trava, ext_l - parede - y_off, z_trava]) rotate([0, 90, 0]) cylinder(r = r, h = 14, center = true);
  translate([ext_c - parede - y_off, ext_l/2, z_trava]) rotate([90, 0, 0]) cylinder(r = r, h = 12, center = true);
}

module base() {
  difference() {
    linear_extrude(h_base) contorno(ext_c, ext_l, raio);
    // cavidade
    translate([parede, parede, fundo]) cube([ci_c, ci_l, h_base]);
    // rasgo do cabo USB (aberto em cima: o cabo entra com a tampa fora)
    translate([-1, ext_l/2 - usb_l/2, z_pcb - 1]) cube([parede + 2, usb_l, h_base]);
    // rasgo dos fios do MC-38
    translate([x_fio - fio_l/2, y_fio - 1, z_fio]) cube([fio_l, parede + 2, h_base]);
    // canaletas das travas
    travas(0.7, 0.15);
  }
  // pilares que apoiam os 4 cantos da placa (os pinos ficam livres no meio)
  for (x = [parede, parede + ci_c - apoio_c])
    for (y = [parede, parede + ci_l - apoio_l])
      translate([x, y, fundo - 0.01]) cube([apoio_c, apoio_l, sob + 0.01]);
}

module tampa() {
  f = folga_tampa;
  difference() {
    union() {
      // chapa
      translate([0, 0, h_base]) linear_extrude(tampa_e) contorno(ext_c, ext_l, raio);
      // aba que entra na caixa
      translate([parede + f, parede + f, h_base - aba_h]) difference() {
        cube([ci_c - 2*f, ci_l - 2*f, aba_h + 0.01]);
        translate([aba_e, aba_e, -1]) cube([ci_c - 2*f - 2*aba_e, ci_l - 2*f - 2*aba_e, aba_h + 2]);
      }
      // 4 pinos que seguram a placa pelos cantos
      zp = z_pcb + pcb_e + 0.3;
      for (x = [parede + f, parede + ci_c - f - 3])
        for (y = [parede + f, parede + ci_l - f - 3])
          translate([x, y, zp]) cube([3, 3, h_base - zp + 0.01]);
      // travas
      intersection() {
        travas(0.6, f + 0.1);
        translate([0, 0, z_trava - 1]) cube([ext_c, ext_l, 2]);
      }
      // lingueta que fecha o rasgo dos fios, deixando só a passagem
      translate([x_fio - fio_l/2 + 0.2, fio_lado == 0 ? 0 : ext_l - parede - f - 0.1, z_fio + fio_h])
        cube([fio_l - 0.4, parede + f + 0.1, h_base - z_fio - fio_h + 0.01]);
    }
    // abertura na aba para o conector USB
    translate([parede - 1, ext_l/2 - usb_l/2 - 0.5, h_base - aba_h - 1])
      cube([aba_e + f + 2, usb_l + 1, aba_h + 1]);
    // ventilação (lado da antena)
    for (i = [0:2])
      translate([ext_c - 9.5 - i*3.5, ext_l/2 - 7, h_base - 1]) cube([1.6, 14, tampa_e + 2]);
    // nome em baixo-relevo (fica em pé com a caixa montada na vertical, USB para baixo)
    translate([(ext_c - 14)/2, ext_l/2, h_base + tampa_e - 0.6]) rotate([0, 0, -90])
      linear_extrude(1) text(texto, size = 5, font = "DejaVu Sans:style=Bold", halign = "center", valign = "center");
  }
}

// ---------- só para visualização ----------
module esp32_falso() {
  translate([parede + folga, parede + folga, z_pcb]) {
    color("#1b5e20") cube([pcb_c, pcb_l, pcb_e]);
    color("silver") translate([pcb_c - 25.5, pcb_l/2 - 9, pcb_e]) cube([19, 18, 3.2]);     // módulo Wi-Fi
    color("#263238") translate([pcb_c - 6.5, pcb_l/2 - 9, pcb_e]) cube([6.5, 18, 0.8]);    // antena
    color("silver") translate([-0.5, pcb_l/2 - 3.9, pcb_e]) cube([5.8, 7.8, 2.8]);         // micro-USB
    color("#222") for (y = [0.2, pcb_l - 2.74]) translate([6.5, y, -2.5]) cube([38.1, 2.54, 2.5]); // barras de pinos
    color("gold") for (y = [1.47, pcb_l - 1.47]) for (i = [0:14])
      translate([6.5 + 1.27 + i*2.54, y, -8.5]) cylinder(d = 0.7, h = 6, $fn = 8);
  }
}

module cena_geladeira() {
  // canto de uma geladeira vista pela lateral: corpo + porta.
  // A caixa fica na LATERAL, fundo colado na geladeira, cabo USB saindo para baixo.
  color("#e8eaed") cube([220, 260, 420]);                                   // corpo
  color("#f7f8fa") translate([-2, -42, 0]) cube([224, 38, 420]);            // porta
  translate([220, 60 + ext_l, 290]) rotate([0, 90, 0]) rotate([0, 0, 180]) {
    color("#fafafa") base();
    color("#2f6f62") tampa();
  }
  // cabo USB descendo até o carregador na tomada
  color("#333") translate([220 + z_pcb + 3, 60 + ext_l/2, 0]) cylinder(d = 3.5, h = 290);
  // MC-38: peça com fio no corpo, ímã na porta (alinhados, a menos de 1 cm)
  color("white") translate([220, 3, 325]) cube([10, 27, 14]);
  color("white") translate([222, -32, 325]) cube([10, 27, 14]);
  // fio do sensor até a caixa
  color("#444") {
    hull() { translate([223, 30, 332]) sphere(d = 2.2); translate([223, 52, 290 + x_fio]) sphere(d = 2.2); }
    hull() { translate([223, 52, 290 + x_fio]) sphere(d = 2.2); translate([220 + z_fio + 2, 60, 290 + x_fio]) sphere(d = 2.2); }
  }
}

if (parte == "base") base();
else if (parte == "tampa") translate([0, ext_l, h_base + tampa_e]) rotate([180, 0, 0]) tampa();
else if (parte == "geladeira") cena_geladeira();
else if (parte == "tampa_montada") tampa();   // usado só para conferir o encaixe
else if (parte == "placa") esp32_falso();
else {
  color("#fafafa") base();
  esp32_falso();
  color("#2f6f62") translate([0, 0, 26]) tampa();
}
