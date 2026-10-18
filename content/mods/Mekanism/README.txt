Mekanism for Hellish Sight
==========================
A port of Mekanism (https://github.com/mekanism/Mekanism, MIT License, Copyright (c) 2017-2025 Aidan C. Brady)
and Mekanism Tools to the Hellish Sight mod hub (Minecraft Legacy Console Edition).
Textures come from Mekanism 26.3; copper (which 1.8 does not have) uses recoloured tin textures.

Install: copy this "Mekanism" folder into the game's "mods" folder and restart the game.
Machines open a furnace-style screen: top slot = input, bottom slot = power (redstone, coal,
bio fuel, energy tablet...) or the infusion/extra item, right slot = output.

Guia rapido (pt-BR)
-------------------
* Agachar + clique direito com a mao vazia: mostra no chat energia, tanques, progresso,
  upgrades e estado da estrutura. Canos/cabos/transportadores mostram o status sem agachar.
* Configurator: clique numa face para alternar padrao / entrada / saida / desligada.
  Na frente da maquina liga/desliga o auto-eject. Teleporter: muda a frequencia (0-15).
  Digital Miner (face de cima): alterna entre so minerios e todos os blocos.
  Logistical Transporter: a face marcada como "puxar itens" puxa de baus/fornalhas.
* Upgrades (velocidade/energia/silenciador): clique direito na maquina com o upgrade (max. 8).
* Baldes de agua/lava enchem tanques; balde vazio retira agua/lava.
* Quimicos (hidrogenio, oxigenio, acidos, polpas...) saem automaticamente para canos e tanques
  vizinhos; para mandar direto a outra maquina, marque a face como saida.
* Multiblocos: monte uma caixa oca (minimo 3x3x3) com as carcacas e o controlador em uma
  das faces: Induction Matrix (Induction Port + casing + cells/providers dentro),
  Dynamic Tank (Dynamic Valve), Thermal Evaporation (Controller, quanto mais alto mais rapido),
  Thermoelectric Boiler (Superheating Elements dentro), Industrial Turbine (Turbine Valve,
  rotores e bobinas dentro), Fission Reactor (Fuel Assemblies dentro), Fusion Reactor (5x5x5 a 9x9x9)
  e SPS (5x5x5).
  Canos, cabos e tubos de um multibloco ligam no controlador (Port/Valve/Controller).
* Teleporter: monte a moldura 4x5 de Teleporter Frames com o Teleporter no meio da fileira de
  baixo (buraco 2x3). Com energia aparece o portal; entre nele para ir ao proximo teleporter da
  mesma frequencia. Portable Teleporter: clique num teleporter para gravar a frequencia e use no ar.
* A tela de cada maquina mostra barras de energia, tanques e temperatura ao lado e o status escrito.
* Calor: Resistive Heater (FE) e Fuelwood Heater (combustivel) esquentam blocos vizinhos e o
  Thermoelectric Boiler encostado neles; Thermodynamic Conductors levam o calor. O boiler tambem
  aceita Sodio Superaquecido do reator de fissao. Agua pesada: Electric Pump com Filter Upgrade.
* Reator de fissao: Configurator na frente liga/desliga, nas outras faces muda a taxa de queima.
  Redstone no controlador = SCRAM. Sem refrigerante (agua ou sodio) ele esquenta, ganha dano e
  DERRETE (explode) em 100%.
* Reator de fusao: carregue 50M FE no controlador e encha de combustivel D-T para acender o plasma.
* QIO: Drive Array com drives + Dashboard (abre como bau), Importer e Exporter (item de filtro no
  slot de cima); tudo pela mesma frequencia. Quantum Entangloporter: mesma frequencia, faces de saida
  marcadas com o Configurator. Seismic Vibrator + Seismic Reader mostram os blocos abaixo.
* Modification Station: peca da MekaSuit em cima, modulo embaixo; a peca pronta sai na direita.
* Jetpack: segure pular; recarregue com hidrogenio num Chemical Tank (slot de baixo).
  Energy Tablet, MekaSuit, Free Runners e Atomic Disassembler recarregam num Energy Cube.
