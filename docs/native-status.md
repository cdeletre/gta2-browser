# Port natif Linux aarch64 : statut

Mis à jour le 9 novembre 2026. **Jalons 1 à 5 atteints ; jalon 6 presque fini** : le jeu
tourne sur 8 consoles (tableau ci-dessous), de 640×480 à 1920×1080, par Wayland, KMS/DRM ou
fbdev, avec Mali (pilote propriétaire), Panfrost et Adreno ; 30,3 ticks/s en jeu ; manette,
écran Options, son. Le paquet PortMaster est dans `packaging/portmaster/` (script
`GTA 2.sh` et dossier `gta2/` du propriétaire, avec `README.md`, `port.json`,
`gameinfo.xml`, images et outils d'extraction de l'installateur). Reste pour le jalon 6 :
l'adresse du dépôt (`<FORK_URL>` dans `gta2/README.md`). Jalon 7 (multijoueur) pas
commencé.

| Console | Puce, GPU, pilote | Système | Écran |
|---|---|---|---|
| MANGMI Air X | SD662, Adreno 610, Mesa (freedreno) | ROCKNIX | 1920×1080, Wayland |
| RG353VS | RK3566, Mali-G52, Panfrost ou libmali | ROCKNIX (et dArkOS avant) | 640×480 |
| TrimUI Smart Pro S | A523, Mali-G57, Panfrost | REG Linux | 1280×720, Wayland |
| R36S | RK3326, Mali-G31, libmali | AmberELEC | 640×480, KMS |
| G350 | RK3326, Mali-G31, libmali | dArkOS | 640×480, KMS |
| RG CubeXX | H700, Mali-G31, libmali | muOS | 720×720, fbdev |
| RG34XX-H | H700, Mali-G31, libmali | muOS | 720×480, fbdev |
| RG40XX-H | H700, Mali-G31, libmali | muOS | 640×480, fbdev |

## Soak de nuit, 8 consoles (observé, 9 novembre 2026)

Binaire release `4a8c9e1e` (commit 4caf9cb : textures 16 bits, libération des textures,
pages d'atlas vidées). Démos du menu sans entrée pendant **4 h (14 400 s)** sur les 8
consoles en même temps, lancées par SSH frontend arrêté (`night.sh` : un relevé toutes les
30 s du CPU du jeu, de la charge, de la mémoire du jeu, de la mémoire GPU lue dans
`fdinfo` pour Mesa, de la mémoire libre, de la température et de la fréquence ; une ligne
d'état du jeu par minute). Les trois H700 en gouverneur `schedutil` (`FRONTEND stop` les
laisse en `powersave` à 288 MHz), `CAFFEINE on`.

**Résultat : les 8 vont au bout (arrêt par `GTA2_RUN_SECONDS`, code 0) ; aucun plantage,
blocage ou arrêt du noyau faute de mémoire.** 69 démos par console, chacune avec un
chargement du menu et un du district.

| Console | CPU du jeu moy. / max (% d'un cœur) | Charge moy. | Mémoire du jeu h1 → h4 | Mémoire GPU moy. (pic) | Mémoire libre min. | Temp. max |
|---|---|---|---|---|---|---|
| Air X | 52 / 100 | 1,99 | 96 → 99 Mo | 184 Mo (189) | 2 325 Mo | 62 °C |
| RG353VS (Panfrost) | 60 / 94 | 2,23 | 97 → 97 Mo | 117 Mo (130) | 439 Mo | 68 °C |
| TSP S | 56 / 98 | 1,66 | 90 → 91 Mo | 124 Mo (156) | 403 Mo | 55 °C |
| R36S | 70 / 97 | 1,39 | 181 → 183 Mo | (dans la mémoire du jeu) | 640 Mo | 61 °C |
| G350 | 66 / 95 | 1,39 | 182 → 186 Mo | (idem) | 649 Mo | 57 °C |
| CubeXX | 78 / 106 | 2,20 | 192 → 196 Mo | (idem) | 713 Mo | 56 °C |
| RG34XX-H | 85 / 108 | 2,13 | 192 → 197 Mo | (idem) | 715 Mo | 58 °C |
| RG40XX-H | 79 / 106 | 2,23 | 208 → 214 Mo | (idem) | 698 Mo | 66 °C |

- **Plus de fuite :** la mémoire prend 2 à 5 Mo la première heure puis reste plate (avant
  la correction, plus de 470 Mo en 15 min sur Mali).
- **En jeu, 30,3 ticks/s partout** ; les minutes sous 29,5 sont toutes celles d'un
  chargement.
- **Chargements** (médiane, et max) :

| Console | Envoi des textures | Menu | District |
|---|---|---|---|
| Air X | direct | 0,95 s (1,07) | 2,09 s (2,25) |
| RG353VS | par pages | 0,75 s (1,40) | 2,77 s (3,23) |
| TSP S | par pages | 0,75 s (1,67) | 2,30 s (2,55) |
| R36S (816 MHz, voir plus bas) | direct | 2,15 s (2,22) | 5,19 s (5,70) |
| G350 | direct | 1,58 s (1,62) | 3,63 s (4,13) |
| CubeXX | direct | 0,95 s (4,75) | 2,04 s (2,84) |
| RG34XX-H | direct | 0,94 s (4,06) | 2,05 s (2,78) |
| RG40XX-H | par pages | 0,73 s (3,47) | 1,48 s (2,24) |

- Chaque chargement de district renvoie environ 37 500 textures au GPU, même pour un
  district déjà vu : le cache ne garde que 16 Mo de textures non affichées. C'est le coût
  des chargements ci-dessus (optimisation possible, pas un défaut).
- Quelques chargements du menu bien plus longs que la médiane, sur les trois muOS
  seulement (jusqu'à 4,75 s). Cause pas cherchée.
- Un à-coup de 100 à 170 ms à la fin de chaque démo (le district est déchargé avant le
  retour au menu), jamais en jeu.
- G350 : 4 « underrun » ALSA en 4 h. Messages sans effet : « muOS-Keys has no controller
  mapping » (muOS), « xkbcommon Compose file » (TSP S).
- Le blocage sans son vu une fois sur la RG34XX-H (plus bas) ne s'est pas reproduit.

## Build release (jalon 6, préparation)

`./scripts/build_native.sh --preset release` produit, en plus de la sortie du linker,
**`build/native/release/dist/`** :
- `gta2.aarch64` : le binaire à distribuer, sans symboles (1,4 Mo), au nom que lance le
  script PortMaster (`./gta2.${DEVICE_ARCH}`) ;
- `gta2.aarch64.debug` : ses symboles (14 Mo), liés par `.gnu_debuglink`, pour lire une trace de
  plantage ; à garder, pas à distribuer.

Options : `-O2 -g` sans `NDEBUG`, comme le build web release (les options Release par
défaut de CMake étaient `-O3 -DNDEBUG`) ; `-g` ne change pas le code. `libstdc++` et
`libgcc` sont liées en statique (Release seulement ; debug et ASan restent dynamiques).
SDL2 est lié par son chemin plutôt qu'avec les options de `sdl2.pc`, qui ajoutait
`-Wl,-rpath,/usr/local/lib`.

- **Dépendances observées :** `libc`, `libm`, `libdl`, `libpthread` (glibc **2.17** au
  plus), `libSDL2-2.0.so.0`, `libGLESv2.so.2`. Aucun `RPATH`/`RUNPATH`.
  `build_native.sh` refuse un binaire `dist/` qui aurait un `RUNPATH` ou dépendrait de
  `libstdc++`/`libgcc_s`.
- **Sur la console** (8 novembre 2026, le binaire de `dist/` à la place du binaire debug) : soak de
  300 s à la manette simulée (`GTA2_SOAK_INPUT=pad`, graine 4), Wayland + PulseAudio :
  sortie propre (code 0), 8 810 ticks, 298 actions, 6 voitures ; cadence de **30,1 à
  30,5 ticks/s sur toutes les tranches de 5 s** après le chargement (49 sur 57 à 30,3).
  Le soak a joué sur le profil 0 de `saves/` de la console.

## TrimUI Smart Pro S (REG Linux) et chargements (observé, 8 novembre 2026)

Console : TrimUI Smart Pro S, 8 cœurs Cortex-A55, 959 Mo de RAM, Mali-G57 MC1 avec
**Panfrost** (Mesa 26.2), REG Linux 26.10-dev (noyau 6.18), Wayland (sway) en 1280×720,
son PipeWire, glibc 2.41. Port installé et lancé par l'utilisateur avec son lanceur ;
essais mesurés par SSH, binaire dans `/tmp`, cache disque vidé avant chaque essai.

- **Chargements longs, cause mesurée :** pas la carte (15,4 Mo des fichiers de Downtown lus
  à froid en 0,7 s), mais l'envoi des textures. Avec Panfrost, chaque petit envoi dans une
  page d'atlas coûte environ 0,28 ms : 15 s sur les 21 s jusqu'à Downtown (menu à 6,0 s).
- **Corrigé par l'envoi groupé des pages** (`gles_renderer.cpp`) : les pages récentes
  gardent une copie en mémoire (au plus 8, 32 Mo), les lignes modifiées partent au GPU en un
  appel par page avant l'image suivante. Sur le pilote Mali de la CubeXX, c'est l'inverse
  (petits envois à 0,04 ms, Downtown 8 s en direct, 17 s groupé) : les 1 000 premiers envois
  sont chronométrés et l'envoi groupé n'est choisi que si leur médiane dépasse 0,15 ms
  (`GTA2_ATLAS_BATCH=0/1` force le choix).
- **Blocages en jeu de 0,3 à 1,4 s, environ toutes les 30 s en voiture :** l'ouverture d'une
  piste de radio lisait tout le WAV (jusqu'à 32 Mo) dans le fil du jeu. Corrigé : le fichier
  est projeté en mémoire, la suite est lue d'avance en arrière-plan, et quand les données à
  décoder ne sont pas encore en mémoire (une station reprend au milieu d'une piste), le
  décodeur attend jusqu'à 300 ms au lieu de bloquer le jeu.
- **Après correction, cache vidé, soak avec conduite :**

| Console | Choix de l'atlas (médiane) | Downtown atteint | Blocages en jeu |
|---|---|---|---|
| TrimUI Smart Pro S (Panfrost) | groupé (0,217 ms) | 7,0 s (21,7 s avant) | aucun sur 2 min 20 |
| RG CubeXX (Mali r20p0) | direct (0,041 ms) | 9,6 s | aucun |
| R36S (Mali r13p0) | direct (0,050 ms) | 12,7 s | aucun |
| MANGMI Air X (freedreno) | direct (0,024 ms) | 6,0 s | aucun |
| RG353VS (Mali-G52 g29p1) | groupé (0,153 à 0,222 ms) | 6 s (18 s en direct) | aucun sur 4 min |
| G350 (Mali-G31 r13p0, dArkOS) | direct (0,044 ms) | 9 s (7 s groupé forcé) | aucun sur 3 min |

  Sur la R36S, l'envoi groupé forcé donnait 10,0 s : le seuil de 0,15 ms la laisse en direct,
  au prix de 2,7 s, pour ne pas risquer le cas de la CubeXX (0,041 ms) ; à revoir si besoin.
  Le seuil est passé de 0,15 à **0,10 ms** après le RG353VS : sa médiane varie de 0,153 à
  0,222 ms selon l'essai (7 essais), si près de 0,15 qu'un essai sur deux aurait pu tomber en
  direct et mettre 18 s au lieu de 6. Les autres consoles gardent le même choix.
- `GTA2_LOG_TEXTURES=1` écrit aussi les images de plus de 100 ms (envois de textures, envois
  de pages, attente de l'affichage) et les tours de boucle de plus de 100 ms avec le temps de
  chaque étape (événements, son, jeu). Soak de 90 s sous ASan, son actif et envoi groupé
  forcé : aucune erreur.
- **MANGMI Air X, installation neuve par le paquet de l'utilisateur :** fonctionne
  (observé par l'utilisateur).
- Par SSH, `get_controls` de PortMaster sur REG Linux met toute la base de manettes dans
  `SDL_GAMECONTROLLERCONFIG` (« Argument list too long ») : depuis l'interface, `regmsgd`
  fournit seulement les manettes branchées. Les essais par SSH passent la base par
  `SDL_GAMECONTROLLERCONFIG_FILE`.
- **Après une mise à jour de REG Linux (8 novembre au soir) :** l'utilisateur a réinstallé le
  port (build `1ca5259a`). Avec la correspondance du lanceur, A et B sortent inversés en
  `face_buttons = auto` : l'utilisateur a mis `nintendo` dans l'écran Options. Sur ce système,
  la correspondance exportée par PortMaster semble nommer les positions (convention SDL) et
  non les étiquettes, contrairement à la R36S. Non vérifié bouton par bouton.

## Fuite des textures et RG34XX-H figée (observé, 8 novembre 2026)

- **Vu par l'utilisateur :** la RG34XX-H, laissée sur les démos du menu, finit figée ; SSH
  ne répond plus (bannière jamais reçue) alors que le ping passe. Mémoire saturée, sans swap.
- **Mesuré avant le correctif :** sur les 5 consoles à pilote Mali propriétaire, la mémoire
  du jeu grossit de 70 à 90 Mo par quart d'heure (avec ce pilote, la mémoire graphique est
  comptée dans celle du jeu ; sous Mesa elle ne s'y voit pas). RG34XX-H, 15 min de démos :
  349 Mo à 2 min, 470 Mo à 15 min, 58 000 textures toutes vivantes, atlas de 51 à 62 pages.
- **Cause :** le cache des textures par contenu (`port/render/gbh_web.cpp`) ne libérait
  jamais une texture GPU. `gbh_FreeTexture` ne faisait que l'oublier côté jeu.
- **Correctif (natif seulement) :**
  - chaque texture GPU compte les textures du jeu qui l'affichent ;
  - celles qui ne sont plus affichées restent en cache jusqu'à 16 Mo au total, puis les plus
    anciennes sont libérées, jamais avant que l'image qui les a dessinées en dernier ait été
    affichée (les dessins attendent `gta2_frame_present`) ;
  - une page d'atlas vide repart de zéro.
- **La ligne d'état du log** donne le nombre de textures, de pages d'atlas et leurs Mo.
- **Observé après correctif :**
  - RG34XX-H, 30 min de démos : 352 Mo à 2 min, 354 à 359 Mo ensuite, jusqu'à 30 min ;
    52 pages d'atlas (221 à 223 Mo de textures), environ 37 800 textures en jeu ;
  - Air X, build ASan, 12 min de démos : aucune erreur, 51 pages, 216 à 220 Mo.
- **Ne libérer que ce qu'aucun dessin n'utilise :** une texture non affichée n'est libérée
  que si ni les dessins en attente de l'image en cours, ni le HUD gardé pour les images
  intermédiaires de l'interpolation (active par défaut) ne s'en servent
  (`Gta2Port_TexturesInUse`). La première version attendait l'image suivante, ce qui gardait
  l'ancien district pendant un chargement, et pouvait libérer une texture du HUD encore
  rejouée. TSP S, 9 min de démos : 211 à 221 Mo de mémoire GPU hors chargement (248 avant),
  43 pages d'atlas au lieu de 52. Air X, build ASan, 10 min : aucune erreur.
- **Pics de Panfrost aux changements de démo (TSP S), cause non trouvée :** la mémoire GPU
  du pilote monte à 400–435 Mo pendant environ 70 s, alors que le port ne compte que 188 Mo
  de textures (43 pages). Il reste alors 34 à 62 Mo libres sur la console. Ce ne sont pas
  les copies temporaires des envois de pages : un `glFinish` tous les 32 Mo n'y change rien.
  Avant ce correctif, le jeu a été tué une fois par le noyau (OOM) à 8 min.
- **Textures en 16 bits (RGB5_A1) :** 2 octets par pixel au lieu de 4. La transparence sur
  1 bit correspond exactement à la couleur 0 des palettes. `GTA2_TEXTURE_BITS=32` revient à
  RGBA8, et le log indique le format au démarrage. TSP S, 9 min de démos, envoi groupé :
  - mémoire GPU hors chargement : 114 à 131 Mo (211 à 222 Mo en 32 bits) ;
  - pics : 274 à 351 Mo (400 à 435 Mo en 32 bits) ;
  - mémoire libre au plus bas : 125 Mo (34 à 81 Mo en 32 bits) ;
  - 92 à 94 Mo de textures dans l'atlas ;
  - deux captures en jeu correctes (couleurs, transparence, HUD).
- **Pics de 200 Mo et chargements lents après le premier : trouvé et corrigé.** Depuis que les
  pages vidées sont réutilisées, elles n'avaient plus la copie en mémoire de l'envoi groupé
  (donnée seulement aux pages neuves) : sous Panfrost, chaque chargement après le premier
  envoyait ses textures une à une (37 566 en 10 à 11 s), et ces envois en rafale faisaient
  monter la mémoire du pilote. Maintenant l'atlas remplit d'abord les pages vides, et une
  page vide reçoit sa copie quand on commence à la remplir, comme une page neuve (seule une
  page vide le peut : GLES 2 ne relit pas une texture, une copie à zéro effacerait celles
  d'une page occupée). TSP S, 9 min de démos, cache chaud :

| | Avant | Après |
|---|---|---|
| District après le premier | 11,7 à 12,4 s | 1,8 à 2,1 s |
| Retour au menu entre deux démos | 4,6 s | moins de 1 s |
| Mémoire GPU au maximum | 332 Mo | 145 Mo |
| Mémoire libre au plus bas | 178 Mo | 415 Mo |

- **Session de l'utilisateur sur la TSP S** (binaire `a274d863`, environ 18 min dans Downtown) :
  30,3 ticks/s tout du long, 37 800 à 38 400 textures, 43 à 45 pages, 92 à 94 Mo ; pas de
  ralentissement ressenti.
- **Point ouvert :** pendant un premier essai sur la RG34XX-H (le son n'avait pas pu s'ouvrir,
  « ALSA: Host is down »), le jeu est resté bloqué dans le menu, le fil principal à 100 % dans
  l'horloge (`emscripten_get_now`), avant la première ligne d'état. Pas reproduit en deux
  essais suivants sans son (90 s, puis 30 min). Sans gdb sur la console, la pile s'arrête là.

## Plantage pendant les démos du menu (observé, 8 novembre 2026)

- **Vu par l'utilisateur :** le port laissé sur le menu, qui enchaîne ses 3 démos
  (`data\attract\attr1..3.rep`), plante par segfault au bout de 20 à 30 min, sur la Air X,
  la R36S, le RG353VS et la G350, toujours pendant la démo d'Industrial (`bil.gmp`).
- **Rapport de plantage** (`platform/crash_native.cpp`) : le log reçoit le signal, l'adresse,
  pc et lr, la ligne d'état du jeu et la pile (module + décalage, et l'adresse à passer à
  `addr2line` avec le `.debug` de la même release). Installé après SDL, puis le gestionnaire
  de SDL s'exécute. Pas installé sous ASan.
- **Cause, d'après le rapport sur la R36S :** lecture à l'adresse 0x3 dans
  `PublicTransport_181C::GetBusStopOnScreen_5799B0`. `gStationCount_6FF1CC` n'était jamais
  remis à zéro (`SetupTrainAndBusStops_5794B0`, fonction identique à l'original) : il grossit
  à chaque district chargé. Au-delà des 100 stations de la table, la boucle lit la mémoire qui
  suit comme des stations. Corrigé par le patch natif 0016, qui le remet à 0 au début de
  chaque partie. Même défaut dans le build web (« Correctifs à reporter »).

## Écran Options (observé, 8 novembre 2026)

L'entrée « Options » du menu principal lançait GTA2 Manager, un programme Windows séparé. Le
web montre à la place son écran Contrôles (patch 0024). En natif, elle ouvre un écran simple
dessiné par-dessus le menu (`platform/options_native.cpp`), avec une police 5×7 intégrée
(`render/overlay_font.h`) et des rectangles dessinés après l'image du jeu
(`gles_renderer.cpp`). Il n'y a pas de thème GTA2.

| Ligne | Registre / fichier | Appliqué |
|---|---|---|
| Musique et radio | `sound\cdvol` (0 à 127, pas de 10 %) | tout de suite, musique du menu comprise |
| Effets sonores | `sound\sfxvol` | tout de suite |
| Vitesse du texte | `option\text_speed` (1 à 5, 3 = normal) | à la partie suivante (`Hud_2B00::GetTextSpeed_5D6A90`) |
| Points qui explosent | `screen\exploding_on` | tout de suite (`Init_FrameRateLightAndUnknown_5D8EB0`) |
| Éclairage | `screen\lighting` | tout de suite (même fonction) |
| Boutons A/B | `controls.ini`, ligne `face_buttons` seulement | tout de suite |

- **Commandes :** haut et bas choisissent une ligne, gauche et droite changent la valeur (A
  aussi), B ou Start ferment. Au clavier : les flèches, Entrée, Échap. Tant que l'écran est
  ouvert, le jeu ne reçoit aucune touche. Les boutons encore enfoncés à la fermeture restent
  bloqués jusqu'à leur relâchement : le B qui ferme ne fait pas « retour » dans le menu du jeu.
- **Observé, piloté par des événements manette (`GTA2_SOAK_INPUT=pad`) :**
  - R36S (640×480), Air X (1920×1080), CubeXX (720×720) : texte lisible, et chaque
    changement se retrouve dans `registry.txt` et `controls.ini` ;
  - fermeture par A sur « Back » comme par B : le menu du jeu répond ensuite normalement ;
  - build ASan/UBSan sur la Air X, de Options jusqu'à 30 s dans Downtown : aucune erreur
    mémoire. Les rapports UBSan sont ceux déjà connus du moteur (noms de zone, tableaux de
    taille 1 ou 2), et les fuites de fin (4,7 Ko) sont les mêmes avec ou sans Options.
- **En ville :** Start (l'invite de sortie du jeu) puis Select, ou F8 au clavier (la touche
  de GTA2 Manager dans l'original). L'invite de sortie d'origine ne met pas le jeu en pause
  (`Player.cpp`, `DIK_ESCAPE`) : l'écran Options met donc en pause une partie solo tant
  qu'il est ouvert (`Game_0x40::Pause_4B96B0`, la pause de F6) ; une partie en réseau
  continue. Observé sur la Air X : Start, Select, la musique à 70 %, B ferme, B reprend.
- **Musique du menu :** son volume n'est fixé qu'à l'ouverture du flux (`sound_obj.cpp`,
  6/10 de `cdvol`). Vu par l'utilisateur : le réglage ne changeait la musique du menu qu'après
  une partie. Corrigé : hors de la ville, le flux en cours reçoit aussi le nouveau volume.
- **Pas encore vérifié :** l'effet de l'éclairage à l'écran.

## RG353VS (dArkOS) (observé, 8 novembre 2026)

Console : Anbernic RG353VS, RK3566 (4 Cortex-A55 à 1,99 GHz, gouverneur `performance`),
966 Mo de RAM sans swap, dArkOS sur Debian 13 (noyau 5.10, glibc 2.41, SDL 2.32.10),
Mali-G52 avec le **pilote Mali propriétaire** g29p1 (pas Panfrost), KMSDRM en 640×480,
son ALSA, manette « retrogame_joypad ». Port installé dans `/roms/ports/gta2` (exFAT) et
joué brièvement par l'utilisateur. Essais par SSH (`ark`, `sudo` sans mot de passe),
EmulationStation arrêté (`sudo systemctl stop emulationstation.service`), cache disque vidé.

- Downtown atteint vers 6 s en envoi groupé, 18 s en direct (`GTA2_ATLAS_BATCH=0`) : voir
  le seuil de l'atlas plus haut.
- Soak de 4 min avec conduite : 30,3 ticks/s, 60 images/s, aucune boucle lente après le
  chargement, 364 Mo au plus, sortie propre.

## RG34XX-H et RG40XX-H (H700, muOS) (observé, 8 novembre 2026)

Deux autres consoles H700 sous muOS 2606, avec le même pilote Mali r20p0 que la CubeXX. Port
installé et lancé par l'utilisateur sur les deux.

- **RG34XX-H, 720×480 (3:2) :** vue de jeu 720×480, HUD aux bords, menus en 4:3 avec deux
  bandes de 40 px, écran Options lisible (captures).
- **Calibration de l'atlas et gouverneur CPU :** avec ce pilote, le coût d'un envoi suit la
  vitesse du CPU. RG34XX-H en `schedutil` : médiane 0,021 ms sur 4 essais. RG40XX-H en
  `powersave` : 0,129 à 0,162 ms, au-dessus du seuil, donc envoi groupé. muOS change le
  gouverneur selon ce qui tourne (en `powersave` après `FRONTEND stop`).
- **Le choix compte peu ici :** RG40XX-H, cache vidé, Downtown à 7 s en direct et 6 s en
  envoi groupé. Les 17 s mesurées en envoi groupé sur la CubeXX venaient d'une version
  antérieure de l'envoi groupé.
- Le log de calibration donne maintenant aussi les déciles et quartiles (10, 25, 75, 90 %).

## G350 (dArkOS) et RG353VS sous ROCKNIX (observé, 8 novembre 2026)

- **G350 :** la console s'identifie comme « BatleXP G350 ». 4 cœurs à 1,51 GHz, 895 Mo de RAM,
  noyau 4.4.189 (famille RK3326, comme la R36S), pilote Mali G31 r13p0, KMSDRM 640×480.
  dArkOS sur Debian 13. `/roms` en exFAT, 4,2 Go seulement. Port installé par
  l'utilisateur.
  - Cache vidé : l'atlas choisit l'envoi direct (médiane 0,044 ms), Downtown atteint vers 9 s ;
    7 s en envoi groupé forcé, comme sur la R36S (même pilote).
  - Soak de 3 min avec conduite : 30,3 ticks/s, 60 images/s, aucune boucle lente après le
    chargement, 317 Mo au plus.
- **RG353VS :** repassée de dArkOS à ROCKNIX (noyau 7.2, sway, PulseAudio). L'utilisateur y a
  fait tourner le port avec libmali et avec Panfrost. Sous Panfrost, l'atlas choisit l'envoi
  groupé (médiane 0,291 ms).

## RG CubeXX (H700, muOS) (observé, 8 novembre 2026)

Console : Anbernic RG CubeXX, Allwinner H700 (4 Cortex-A53, jusqu'à 1,8 GHz), 973 Mo de
RAM sans swap, Mali-G31 (pilote propriétaire r20p0), noyau 4.9, muOS 2606.0 ANDROMEDA,
glibc 2.44, SDL 2.28.5. **Écran 720×720 sur le framebuffer (`/dev/fb0`), sans KMS/DRM** :
SDL passe par son pilote vidéo `mali`. Son ALSA (PipeWire derrière).

- **Premier lancement par SSH** (menu de muOS arrêté par `FRONTEND stop`, relancé par
  `FRONTEND start`, fonctions de `/root/.profile`) : pilote `mali`, `drawable 720x720`,
  **vue de jeu 640×640**, Downtown en moins de 10 s, 30,3 ticks/s et 60 images/s, 330 Mo
  stables. L'utilisateur a regardé l'écran : menu en 4:3 avec des bandes en haut et en bas,
  jeu en plein écran, son correct.
- **Partie à la main, lancée depuis le menu de muOS** par un lanceur de test au format
  PortMaster (`control.txt`, `get_controls`, `SDL_GAMECONTROLLERCONFIG`,
  `pm_platform_helper`) : 4 min dans Downtown, à pied et en voiture, 30,3 ticks/s hors
  chargement, 330 à 351 Mo. Manette « Deeplay-keys » (la correspondance de PortMaster sur
  muOS), `face_buttons = auto` : boutons corrects, Start puis A A quitte. Processeur en
  `schedutil` (1,2 à 1,4 GHz pendant le jeu), GPU en `simple_ondemand` à 360 MHz.
- **Vue haute sur un vrai écran carré :** pas de voiture, de passant ni de barrage de
  police qui apparaisse ou disparaisse à vue ; brief de mission en bas, argent, vies et
  barres de respect en haut, comme prévu. Fluidité jugée bonne pour une première version.
- Par SSH, sans lancement par muOS, le jeu ne voit que « muOS-Keys » (sans correspondance) :
  la manette virtuelle « Deeplay-keys » n'existe que quand muOS lance un contenu.

## R36S (RK3326, AmberELEC) (observé, 8 novembre 2026)

Console : R36S (arbre de périphériques « Anbernic RG351MP »), 4 Cortex-A35 à 1,3 GHz,
926 Mo de RAM, Mali-G31 avec le pilote propriétaire (libmali r13p0, GBM), noyau 4.4,
AmberELEC prerelease-20250515, glibc 2.38, SDL 2.32.4. Écran 640×480 par KMS/DRM, sans
compositeur ; son ALSA (rk817). Lancé par SSH, EmulationStation arrêté pendant l'essai.

- **KMS/DRM, ALSA et la manette intégrée marchent du premier coup** : `SDL KMSDRM video
  driver, drawable 640x480`, `GL_RENDERER Mali-G31`, `SDL alsa audio driver, 44100 Hz`,
  manette « GO-Super Gamepad » reconnue.
- **Premier essai : 80 s pour charger Downtown, et la mémoire montait à 800 Mo** (il ne
  restait que 47 Mo de libres). Cause mesurée (`GTA2_LOG_TEXTURES=1`) : le jeu enregistre
  environ 52 000 textures pendant le chargement, une par sprite et par palette, la plupart
  d'environ 20×20 ; avec ce pilote, chaque texture GL coûte environ 1 ms et une vingtaine
  de Ko. Sur la Air X (Mesa), le même chargement prend 4 s et 212 Mo.
- **Corrigé par un atlas** (`port/native/render/gles_renderer.cpp`) : les images jusqu'à
  254 pixels de côté vont dans des pages de 1024×1024 (50 pages pour Downtown), avec une
  bordure d'un pixel recopiée des bords ; les UV sont remappées à l'affichage et les lots
  consécutifs sur la même page sont dessinés en un appel. **Après : Downtown en 11 s,
  323 Mo stables, 30,3 ticks/s et 60 images/s**, menu et jeu affichés correctement (captures
  sur la console). Dans le conteneur, image identique à l'ancienne hors animations ; soak de
  60 s sans erreur ASan.
- Le menu met environ 10 s à s'afficher (contre 1 s sur la Air X). Les appuis automatiques
  des tests (`GTA2_AUTOSTART`, `GTA2_MENU_KEYS`) partent maintenant une fois le menu affiché.
- **Pas un défaut :** au départ, joueur immobile devant l'église, une vue de 640 de large
  ne fait apparaître aucun passant (2 « peds » : le joueur et un autre), une vue de 720 ou
  plus en fait apparaître une dizaine. Les passants naissent sur le pourtour des limites de
  la caméra, là où il y a un trottoir ; en 640, ce pourtour ne tombe que sur des toits tant
  que le joueur ne bouge pas. Dans les soaks en 640, ils sont 10 à 17 dès que le joueur
  avance. Que l'original fasse pareil à l'arrêt n'est pas vérifié.
- **Écran noir pendant les tests par SSH :** l'économiseur d'écran d'EmulationStation
  coupe le rétroéclairage (`/sys/class/backlight/backlight/bl_power` = 1) quand la console
  reste sans qu'on la touche ; arrêter EmulationStation à ce moment le laisse éteint, et le
  jeu dessine sur un écran noir (les captures, lues dans le GPU, ne le montrent pas). Les
  tests remettent `bl_power` à 0. Un lancement depuis EmulationStation n'est pas concerné.
- **Partie jouée à la main (13 min, Downtown et Industrial) :** 30,3 ticks/s hors
  chargements, 60 images/s en moyenne, 408 Mo, son correct. L'utilisateur trouve le jeu
  fluide ; de légères saccades quand des voitures se percutent avec les points qui
  s'affichent, jugées acceptables pour une première version de test. Les essais étaient en
  `powersave` (processeur à 816 MHz, GPU 400 MHz, mémoire 450 MHz), sauf quelques minutes
  en `performance`. **Corrigé le 9 novembre :** EmulationStation elle-même tourne en
  `powersave` (`powersave_es=1`), mais la config de cette console a
  `ports["GTA 2.sh"].maxperf=1` : un lancement depuis EmulationStation passe en
  `performance` (1 368 MHz) par `runemu.py`, qui remet `powersave` en sortant. Un test
  lancé par SSH, EmulationStation arrêtée, reste à 816 MHz : faire `. /etc/profile;
  performance` d'abord.
- **En `performance` (9 novembre, 10 min de démos) :** menu en 1,38 à 1,45 s (2,15 s à
  816 MHz), district en 3,26 à 3,77 s (5,19 s), 30,3 ticks/s en jeu, 181 Mo.
- **Manette :** sans correspondance fournie par le lanceur, SDL prend pour la manette
  intégrée une correspondance de repli sans Start, Select ni boutons de sticks
  (`crc:3ebb`) : Start ne faisait rien. Celle de PortMaster (`SDL_GAMECONTROLLERCONFIG`)
  a tous les boutons, mais nomme les boutons par leur étiquette (son « a » est le bouton
  marqué A), alors que les correspondances de SDL nomment des positions. Corrigé :
  `face_buttons = auto` (par défaut) garde les noms de la correspondance quand le lanceur
  en fournit une, et échange A/B et X/Y sinon, comme avant. Vérifié sur la console avec la
  correspondance de PortMaster : A valide et tire, B revient, Y monte en voiture.
- **Quitter à la manette :** pendant la question « quitter ? » (Start), la manette passe
  sur la disposition des menus : A deux fois quitte (« ARE YOU SURE? »), B ou Start
  reprend. Vérifié sur la console. `GTA2_LOG_INPUT=1` écrit la correspondance, chaque
  bouton et les touches envoyées.
- Reste à faire : saccades lors des chocs (pas mesurées depuis les textures 16 bits).

## Résolutions d'écran (observé, 8 novembre 2026)

Dix tailles de fenêtre dans le conteneur (`GTA2_WINDOW`, binaire release, llvmpipe), avec
une capture du menu et deux en jeu : 640×480, 720×480, 720×720, 960×544, 1280×720,
1920×1080, 1920×1152, 1920×1680, 480×320 et 320×240. **Toutes démarrent, entrent dans
Downtown et sortent proprement (code 0).**

| Écran | Vue de jeu | Bandes noires en jeu |
|---|---|---|
| 640×480, 320×240 (4:3) | 640×480 | aucune |
| 720×480, 480×320 (3:2) | 720×480 | aucune |
| 960×544 | 846×480 | aucune |
| 1280×720, 1920×1080 (16:9) | 852×480 | aucune |
| 1920×1152 (5:3) | 800×480 | aucune |
| 720×720 (1:1) | 640×640 | aucune |
| 1920×1680 (8:7) | 640×560 | aucune |

- **La vue de jeu remplit tout l'écran** (patch natif 0015, 8 novembre 2026). Plus large que
  4:3, elle fait 480 de haut et sa largeur suit la forme de l'écran, bornée entre 640 et
  1280 (`gta2_port_set_view_width`, comme le web). Plus haute que 4:3, elle fait 640 de
  large et sa hauteur suit la forme de l'écran, bornée entre 480 et 960
  (`gta2_port_set_view_height`, natif seulement). Dans les deux cas la boîte 4:3 garde son
  échelle : on voit plus de ville sur les côtés, ou au-dessus et en dessous. Quand la forme
  arrondie (largeur paire) diffère de l'écran de moins de 2 %, le renderer étire l'image
  pour combler le pixel restant. Vérifié : aucune ligne ni colonne noire au bord des
  captures en 960×544, 720×720 et 1920×1680, tuiles et sprites jusqu'en bas.
- Les menus restent en 640×480, centrés avec des bandes (ce sont des images 4:3).
- Le HUD est à la même place qu'en 640×480 à toutes les tailles (le dernier cœur touche le
  bord droit, comme dans l'original). En vue haute, les parties du haut (barres de respect,
  argent, vies, cœurs, recherche, pager) restent collées en haut, le brief de mission en
  bas, le reste (gros textes, flèches, pause) au centre.
- L'image est nette même à une échelle non entière (×1,5 en 1280×720) : la géométrie est
  dessinée directement à la taille de l'écran, seules les textures sont agrandies (NEAREST).
- En 320×240 (moitié de 640×480), les textes des menus restent lisibles.
- Pas vérifié sur une vraie console autre que 1920×1080 (vue 852×480, écran rempli,
  30,4 ticks/s avec le binaire du 8 novembre).

Le binaire release cherche `libSDL2` dans les chemins du système ; dans le conteneur, celle
de `/usr/local/lib` (qui a le pilote `offscreen`) demande `LD_LIBRARY_PATH=/usr/local/lib`.

## Missions, sauvegardes et districts (observé, 8 novembre 2026)

Sous ASan + UBSan dans le conteneur, avec les scénarios de `platform/scenario_native.cpp`
(`GTA2_TEST`, voir l'en-tête du fichier) et `GTA2_MENU_KEYS` pour choisir le district ou
reprendre une sauvegarde. **78 lancements sous ASan ; les 4 erreurs sont toutes le
cas provoqué ci-dessous** (téléphone 11 décroché à 3 s).

| Quoi | Lancements | Résultat |
|---|---|---|
| Sauvegarde à l'église, Downtown, Residential, Industrial | 3 | payée (−50 000 points), `plyslot0.svg` écrit (3 232 octets à Downtown) |
| Reprise de ces sauvegardes (« Resume saved status ») | 3 | même district, 10 000 points, `bLoadedFromSave` levé |
| Reprise avec armes (9 novembre, `GTA2_TEST_WEAPONS=0:50,1:80,4:20`, Downtown, build debug) | 1 + 1 | `plyslot0.svg` garde 10 000 points et les munitions ; la reprise rend 10 000 points, les trois armes avec leurs munitions, l'arme en main, 5 vies, santé 100 |
| Téléphones de mission de Downtown (18), décrochés à 20 s, puis soak 100 s | 18 | 7 lancent une mission qui tourne jusqu'à la fin (96 s) ; les autres ne lèvent pas le drapeau de mission (cause pas vérifiée : sans doute pas encore disponibles en début de partie) |
| Les mêmes, décrochés à 3 s | 18 | 17 sans erreur, 1 plantage provoqué (ci-dessous) |
| Residential et Industrial : soak 300 s, téléphones 0 à 4 | 2 + 10 | 4 missions lancées (jusqu'à 95 s) |

- Les districts 2 et 3 demandent un profil qui les a débloqués : `GTA2_TEST_UNLOCK=1` le
  fait depuis les menus (comme la triche UKGAMER). Avec lui, `ok,left,ok` démarre
  Residential (`ste.gmp`) ; `ok,right,ok` démarre Industrial (`bil.gmp`), mais `ok,ok` a
  aussi donné Industrial une fois : vérifier la carte dans le log.
- **Plantage provoqué, pas un défaut du port :** décrocher un deuxième téléphone pendant
  que la mission du premier démarre (le soak entre dans celle du téléphone près de l'église
  dès les premières secondes) charge la deuxième mission par-dessus la première avant que
  celle-ci ait levé son drapeau « en mission » ; le fil de la première lit alors une ligne
  de script effacée (`miss2_0x11C::PoolUpdate`, adresse nulle). Le moteur ferait pareil sur
  Windows. Un joueur ne peut pas toucher deux téléphones aussi vite ; à 20 s, le même
  téléphone (11) lance sa mission sans erreur. Trouvé avec `GTA2_TEST_TRACE=1`.
- **Rapports UBSan :** 30 distincts sur ces lancements, tous dans les catégories de
  l'entrée 15 de `docs/native-64bit.md`. Nouveaux dans Residential et Industrial : les
  mêmes tableaux de longueur variable lus dans les fichiers (`Car_BC.cpp:2152` remaps,
  `map_0x370.cpp` colonnes et zones), le nom de gang (`Hud.cpp:158`), et des écritures
  non alignées dans `PlyDat_2BC0.cpp:430` (permises en mode utilisateur sur aarch64, comme
  sur x86).
- **Profil et sauvegarde du jeu Windows** (un dossier `player\` « 100 % » téléchargé,
  copié dans un dossier de sauvegardes de test, sous ASan) : le menu affiche le nom
  (ILUMINAT), le score 999 999 999, la zone 3 et le stage bonus débloqués. « Resume saved
  status » reprend `plyslot0.svg` à Downtown, devant l'église, à la position du fichier
  (159,9 ; 137,6 ; 2,0), avec l'argent du fichier (0) et `bLoadedFromSave` levé ; 90 s de
  soak sans erreur ASan. La zone 3 démarre Industrial, le stage bonus 1 (`mike2h.gmp`) se
  joue jusqu'à l'écran de résultats, qui affiche les meilleurs scores de `hiscores.hsc`.
  Seul nouveau rapport UBSan : `map_0x370.cpp:3429` (`FindRailwayBelowZAtCoord`), le même
  tableau de colonne de longueur variable que les autres lignes de `map_0x370.cpp`.
- **Pas vérifié :** une mission jouée jusqu'au bout (le soak ne réussit pas les objectifs) ;
  une sauvegarde du build web reprise en natif.

## Log de la console

Ce que le jeu écrit sur stdout/stderr (le `log.txt` du lanceur) :
- au démarrage, une ligne par réglage (dossiers, vidéo, manette, audio), les quatre lignes
  d'initialisation du moteur et le GPU ;
- puis une ligne d'état toutes les 60 s (ticks/s, carte, peds, voitures, position) ;
  `GTA2_STATUS_SECONDS` change l'intervalle, `GTA2_STATUS_SECONDS=0` la supprime ;
- les ouvertures de fichiers qui échouent, sauf celles que le jeu fait exprès : la
  recherche du CD de musique sur chaque lecteur (`X:\GTAUDIO\1.wav`, dont le premier passage
  a lieu dans un constructeur global, avant que `main()` ait choisi le dossier des données)
  et la détection des langues (`data\?.gxt`). `GTA2_LOG_FILES=1` les remet ;
- plus les quatre bannières « Coop vX.Y / date » que les journaux du moteur écrivaient à la
  sortie (patch natif 0014).

## Jalon 5 : son

- **Mixeur** (`port/native/audio/audio_sdl.cpp`), derrière les mêmes ponts que
  `web/audio.js` (`port/audio/miles_web.cpp` inchangé) : callback SDL audio, 44100 Hz
  stéréo 16 bits.
  - **Effets :** PCM de la banque de sons (8 bits non signé ou 16 bits, mono ou stéréo),
    joué à la fréquence de l'échantillon (interpolation linéaire), volume /127,
    panoramique à puissance constante (les formules du `StereoPannerNode` de Web Audio),
    boucles avec points de boucle ; mêmes conversions et même cache (adresse, longueur,
    format, somme de contrôle partielle) que le web.
  - **Flux (voix, radio, musique) :** WAV IMA ADPCM ou PCM 16 bits, lus en mémoire à
    l'ouverture, décodés par blocs de 0,5 s jusqu'à 1 s d'avance **dans le thread du jeu**
    (`Gta2Native_AudioPump`, à chaque itération), le callback ne faisant que mixer. Statut
    et position suivent l'horloge murale (comme le web) ; après un manque de données, le
    décodeur se recale sur l'horloge.
  - `GTA2_AUDIO=0` coupe le son. Le build `--headless` garde des ponts vides
    (`audio_null.cpp`).
- **Décodeur vérifié échantillon par échantillon contre le JS :** `scripts/native/check_ima.sh`
  décode chaque fichier avec `wav_ima.cpp` et avec les fonctions de `web/audio.js`
  exécutées telles quelles sous Node (`scripts/native/wav_dump_js.js`), puis compare les
  sorties octet par octet. **Les 119 WAV du jeu sont identiques**, dont la station `1.wav`
  entière (31,7 millions d'échantillons stéréo). Node est ajouté au `Dockerfile`.
- **Dans le conteneur** (`SDL_AUDIODRIVER=disk`, 40 s de soak) : la sortie n'est jamais
  silencieuse (RMS de 1 600 à 13 800 par seconde), gauche et droite diffèrent quand des
  sons sont panoramiqués. La crête atteint souvent la saturation (32 767) quand le jeu
  superpose radio et effets ; le web fait la même somme (Web Audio écrête aussi à la
  sortie). Pas encore comparé à l'oreille avec le web.
- **Sur la console :** SDL choisit PulseAudio (44100 Hz, 2 canaux, tampon de 1024 images) ;
  la cadence reste à 30,2–30,3 ticks/s. **Écouté par le propriétaire (8 novembre 2026),
  environ 4 minutes de jeu à la manette : tous les sons fonctionnent** (musique du menu,
  effets, voix, radio). Fin de partie normale depuis le jeu (code 0).
- **Pas fait :** la touche R du web (couper la radio en voiture) ; les identifiants de sons
  restent le pointeur tronqué en `int` de `miles_web.cpp` (`Id()`), collision très
  improbable mais possible en 64 bits.

## Jalon 4 : clavier et manette

- **Clavier** (`port/native/platform/input_sdl.cpp`) : événements SDL, même table de
  touches que `web/shell.js` (scancode DirectInput + touche virtuelle), caractère tapé
  pour les touches imprimables, répétitions ignorées. Les touches du jeu sont celles de
  l'original : flèches, Ctrl gauche (tirer), Entrée (voiture), Espace (saut/frein à main),
  Z/X (armes), Tab et Alt gauche (spécial 1/2), Maj droite (statut).
- **Manette** (`SDL_GameController`) : disposition lue dans **`conf/controls.ini`**, écrit
  avec les valeurs par défaut et ses commentaires au premier lancement. Format INI, en
  anglais : `[options]` (`face_buttons = nintendo|xbox`, `left_stick = dpad|off`,
  `stick_deadzone`), `[city]` et `[menu]` avec une ligne `bouton = action` par bouton
  (`a b x y l1 r1 l2 r2 l3 r3 up down left right start select`). Les actions de `[city]`
  sont les 12 commandes du jeu (`forward backward left right attack enter_exit jump
  prev_weapon next_weapon special1 special2 status`, plus `pause`) : la touche envoyée est
  celle que le registre du jeu associe à la commande (`control\0` à `11`, relu à chaque
  entrée en ville), si bien qu'une touche changée dans les réglages du jeu suit son
  bouton. `[menu]` : `up down left right confirm back`. Les deux acceptent `none` et
  `key:<nom SDL>`. Une ligne invalide est signalée dans le journal avec son numéro, puis
  ignorée ; une ligne absente garde la valeur par défaut.
  - Valeurs par défaut (10 novembre, d'après la version PlayStation : ✕ = A, □ = B,
    △ = Y, ○ = X) : en ville, croix/stick = déplacement (haut/bas : avancer/reculer),
    A/B = accélérer/freiner (avancer/reculer à pied), R2 = tirer, Y = voiture,
    R1 = saut/frein à main, X = spécial 1, L1/L2 = arme suivante/précédente,
    L3 = spécial 2, R3 = statut, Start = pause, Select = radio. Avant : R2/L2 =
    accélérer/freiner, A = tirer, X = saut, B = spécial 1, L1/R1 = armes ; dans les menus, A = valider, B et
    Start = retour. Les étiquettes a, b, x, y suivent `face_buttons` : SDL ne connaît que
    les positions, et les portables sont en général étiquetés à la Nintendo (A à droite,
    B en bas ; c'est le cas de la MANGMI Air X). `GTA2_FACE_BUTTONS` remplace le fichier.
  - Les boutons tenus au changement de contexte sont ignorés jusqu'au relâchement.
  - `conf/gamecontrollerdb.txt` (s'il existe) et `SDL_GAMECONTROLLERCONFIG` ajoutent des
    correspondances SDL.
- **Quitter** est l'affaire du script de lancement (gptokeyb arrête le jeu) : le jeu ne
  détecte plus Select + Start. `SIGTERM`/`SIGINT` deviennent `SDL_QUIT` ; le jeu s'arrête
  proprement et enregistre le registre (observé : `kill -TERM` à 15 s, code 0,
  `conf/registry.txt` écrit). **Pour le script :** gptokeyb doit seulement gérer la sortie,
  sans traduire les boutons en touches, sinon chaque appui arriverait deux fois.
- **Vérifié dans le conteneur** (`GTA2_SOAK_INPUT=keyboard` ou `pad` : le pilote de soak
  injecte de vrais événements SDL) : marche, tirs et montée en voiture au clavier et à
  la manette ; démarrage validé avec A. Avec `y = jump` et `x = enter_exit` dans
  `controls.ini`, le soak (qui monte en voiture avec Y) ne monte plus dans aucune voiture
  (0 ligne d'état « in a car » en 40 s, contre 4 avec le fichier par défaut).
- **Sur la console :** la manette intégrée est exposée par InputPlumber comme une
  DualSense virtuelle (« PS5 Controller », ouverte deux fois, `js1` et `js2`, qui envoient
  les mêmes boutons). **Essayé à la main** (8 novembre 2026) : marcher, prendre une voiture,
  conduire 20 à 30 s. Au premier essai, la lecture par position faisait du bouton marqué
  A un Échap (il quittait le menu) ; corrigé avec les étiquettes Nintendo par défaut.
- **Limite connue :** l'état des manettes ouvertes est fusionné. Deux vraies manettes en même
  temps se mélangeraient (relâcher sur l'une libère la touche tenue sur l'autre).

## Dossiers du portage (`roms/ports/gta2/`)

| Dossier | Contenu | Variable |
|---|---|---|
| `gta2` | le binaire | |
| `gamedata/` | les fichiers du jeu installés (le contenu de `game/`) ; le jeu y tourne | `GTA2_DATA_DIR` |
| `saves/` | ce que le jeu écrit dans `player\` : profils `plyslot*.dat`, parties à l'église `plyslot*.svg`, `hiscores.hsc` | `GTA2_SAVE_DIR` |
| `conf/` | `registry.txt` (le registre du jeu), `controls.ini`, `gamecontrollerdb.txt` | `GTA2_CONF_DIR` |

- `saves/` et `conf/` sont créés s'ils manquent. Les profils de départ (`game/player/`)
  sont copiés dans `saves/` par le script d'installation, jamais écrasés ensuite.
- `conf/registry.txt` : une valeur par ligne (`chemin|nom|d|nombre` ou `chemin|nom|s|texte`),
  lu après les valeurs par défaut, réécrit quand il change (toutes les 5 s et à la sortie,
  via un fichier temporaire). Observé : 37 valeurs écrites au premier lancement, toutes
  relues au suivant ; `game/player/` intact quand `saves/` est un autre dossier.

## Jalon 3 : fenêtre SDL2 et renderer GLES 2 (observé)

- **Renderer** (`port/native/render/gles_renderer.cpp`) : traduction de `web/renderer.js`.
  Même liste de dessins (`port/render/frame.cpp`), 3 modes de mélange, UV correctes en
  perspective (w passé au GPU), textures RGBA venues de `gbh_web.cpp`. Spécifique à
  GLES 2.0 : GLSL `#version 100`, pas de VAO, textures NPOT en `CLAMP_TO_EDGE`/`NEAREST`
  sans mipmaps.
- **Fenêtre** (`port/native/platform/window_sdl.cpp`) : SDL2, contexte GLES 2.0, plein
  écran à la taille du bureau (`GTA2_WINDOW=LxH` pour une fenêtre), vsync
  (`GTA2_VSYNC=0` pour l'enlever). La largeur de vue suit l'écran, comme le web :
  852×480 en 16:9 ; menus en 640×480 avec des bandes noires.
- **Captures :** `GTA2_SCREENSHOT_AT=3,12,25` enregistre des PNG (`GTA2_SCREENSHOT_DIR`).
- **Dans le conteneur** (`SDL_VIDEODRIVER=offscreen`, Mesa llvmpipe) : menu et Downtown
  rendus comme dans le navigateur (départ à l'église, enseigne « JESUS SAVES », HUD
  ancré aux bords en 16:9). Soak de 120 s sous ASan avec le renderer : aucune erreur.
- **Sur la console** (`SDL_VIDEODRIVER=wayland`, `XDG_RUNTIME_DIR=/run/0-runtime-dir`,
  `WAYLAND_DISPLAY=wayland-1`, EmulationStation toujours lancé) : `GL_RENDERER FD610`,
  Mesa 26.2.4 ; captures identiques à celles du conteneur ; 60 images/s.
- **Cadence :** 27,3 à 28,7 ticks/s sur la console au premier essai (debug comme release),
  30,1 à 30,3 ensuite, voir « Choix par défaut » ci-dessous.
- **KMS/DRM non disponible sur cette console :** la SDL2 système (2.32.10) est compilée sans
  le pilote `kmsdrm` (« kmsdrm not available », `essway` arrêté pendant l'essai puis
  relancé). La commande de test de `CLAUDE.md` (`SDL_VIDEODRIVER=kmsdrm`) ne s'applique
  donc pas ici ; Wayland marche.
- **Noms de fichiers :** les fichiers du jeu gardent leur casse d'origine sur l'ext4 de la
  console (`data/Keyboard/eng_kb.cfg`), alors que `crt_compat.cpp` met les chemins en
  minuscules (Fatal Error 151 au premier essai). `port/native/platform/fs_native.cpp`
  retrouve chaque composant sans tenir compte de la casse (listes de dossiers en cache),
  et `_findfirst` compare avec `FNM_CASEFOLD`. Après : le jeu, la musique
  (`data/GTAudio`) et le clavier se chargent.

## Choix par défaut (natif)

- **`screen\min_frame_rate = 1`** (registre, `port/native/main.cpp`), l'option
  « fréquence minimale » de l'original, que le jeu impose lui-même aux parties en réseau
  (`Init_FrameRateLightAndUnknown_5D8EB0`). Chaque tick est dû une image après le
  précédent (et non après le moment où il a tourné), et la boucle rattrape jusqu'à
  8 ticks par image. Sans elle, avec la vsync à 60 Hz et une horloge en millisecondes,
  environ un tick sur quinze glissait d'une image : 27,3 à 28,7 ticks/s mesurés sur la
  console, puis 30,1 à 30,3 avec.

## Ce qui marche (observé)

- **Préparation de la copie patchée** (`./scripts/build_native.sh --prepare`, conteneur
  `gta2-pm`, clang 10.0, glibc 2.31) : clone de l'upstream épinglé `02de8be` puis
  `git am` des 25 patches web, un commit chacun ; 52 fichiers modifiés, dont 4 créés
  (`Source/port_*.cpp`). Un second lancement réutilise la copie.
- **Configuration CMake** (`port/native/CMakeLists.txt`) : la vérification des
  drapeaux obligatoires de `build_native.sh` passe pour toutes les unités.
- **En-têtes de compat en LP64** : `DWORD`/`LONG`/`HRESULT` à 4 octets, `RECT` 16,
  `GUID` 16 (voir `docs/native-64bit.md`, entrée 1).
- **Premier binaire** (`./scripts/build_native.sh`) : ELF aarch64, GLIBC 2.17 au plus,
  aucune fonction wchar de la glibc ; 12 Mo en debug, 24 Mo sous `--sanitize`.
- **Downtown sous ASan/UBSan** (`./scripts/build_native.sh --sanitize --prepare --clean`,
  25 patches web + 8 natifs, puis
  `GTA2_DATA_DIR=/workspace/game GTA2_AUTOSTART=1 GTA2_RUN_SECONDS=60` dans le conteneur) :
  - le menu se crée, l'autostart choisit la zone 1, et dès 5 s le journal indique
    `map "data\wil.gmp", player at (159.50, 139.50, 2.00) blocks` ;
  - 60 s sans erreur ASan, sortie propre (code 0) ;
  - le build sans sanitizer tourne aussi jusqu'au bout (20 s essayées).
- Une ligne d'état périodique (`port/native/platform/status_native.cpp`) : carte,
  position du joueur, véhicule. Toutes les 5 s à l'époque, 60 s par défaut depuis le
  8 novembre (`GTA2_STATUS_SECONDS`).

- **Cœurs de santé** (9 novembre, `GTA2_TEST=damage`, `scenario_native.cpp`) : le scénario
  frappe le joueur par `Ped::TakeDamage` (10 points toutes les 3-4 s) et journalise la
  santé ; la ligne d'état l'affiche aussi (`health N`).
  - Docker sans écran : 100 → 90 → … → 10 → 0, mort, réapparition à 100 avec
    50 ticks d'invulnérabilité (le coup suivant est ignoré), trois fois de suite.
  - TSP S, vrai rendu, captures toutes les 2 s : les cœurs affichés suivent la santé
    (5 cœurs à 100 et 90, puis un demi-cœur de moins tous les 10 points ; une vie de
    moins à la mort). Aucun écart avec l'original : à pied, il faut plus de 10 points de
    dégâts pour perdre le premier demi-cœur (un coup de poing en fait 2, une balle 10,
    une balle de pistolet 20) ; le gilet absorbe les balles ; en voiture, c'est la
    voiture qui prend les coups.

## Soaks (jalon 2, observé)

`GTA2_SOAK=1` (`port/native/platform/soak_native.cpp`) joue au hasard avec les touches par
défaut de l'original : marche et conduite avec virages, marche arrière, tirs en rafale,
saut et frein à main, changement d'arme, Entrée. Il donne une arme au départ, et toutes
les 30 s, s'il est à pied, fait apparaître une voiture à côté du joueur et appuie sur
Entrée pour y monter (aides de la sonde web, `dev_hooks.cpp`). `GTA2_SOAK_SEED` choisit la
séquence, pour rejouer un échec.

Build `--sanitize` (25 patches web + 13 natifs), `GTA2_AUTOSTART=1`, dans `gta2-pm` :

| Graine | Durée | Ticks | Actions | Voitures | Résultat |
|---|---|---|---|---|---|
| 1, 2, 3 | 300 s | ~8 810 chacune | 294 à 307 | 6 à 8 | 0 erreur ASan, sortie 0 |
| 4, 5 | 900 s | 27 014 chacune | 871, 917 | 24, 23 | 0 erreur ASan, sortie 0 |

- **Cadence de simulation mesurée :** 30,2 à 30,4 ticks/s sur chaque tranche de 5 s, et
  27 014 ticks en environ 892 s de jeu (30,3 ticks/s), comme l'original (33 ms par tick).
  Le compteur vient de `Gta2Port_SyncRecord`, appelé après chaque tick (patch natif 0010).
- Le joueur se déplace dans tout Downtown (positions de 116 à 227 en x, de 69 à 197 en y),
  monte en voiture, et réapparaît ailleurs après une mort ou une arrestation ; jusqu'à
  24 piétons et 73 voitures autour de lui.
- **Ce que le soak ne couvre pas :** les missions (aucune n'est démarrée volontairement),
  les sauvegardes, les deux autres districts, le réseau. Les trois premiers sont couverts
  depuis par les scénarios (« Missions, sauvegardes et districts » plus haut) ; il reste le
  réseau.
- **Rapports UBSan restants, non bloquants :** 29 distincts sur les cinq soaks, tous
  classés sans lien avec le 64 bits (`docs/native-64bit.md`, entrée 15).
- **Avertissements de compilation :** 40 distincts, tous classés sans conséquence (même
  entrée).

Avant d'en arriver là, les soaks ont trouvé cinq défauts, corrigés par les patches natifs
0009, 0011, 0012, 0013 et par `-fno-sanitize=return` (entrées 12 à 14 du journal 64 bits,
et « Écarts par rapport à l'original » ci-dessous).

## Écarts par rapport à l'original (natif seulement)

- **`rand()` de la bibliothèque C** (`platform/rand_native.cpp`, 9 novembre) : le natif
  remplace celui de la glibc par celui d'Emscripten (musl), que le build web a. Les nombres
  aléatoires « du monde » du moteur (`gRng_6F6784` : où naissent les passants, etc.) viennent
  directement de `rand()` ; avec la suite de la glibc, un joueur natif et un navigateur ne
  tiraient pas les mêmes après le même `srand(1)`. Aucune des deux n'est celle de l'original
  (MSVC, `RAND_MAX` 0x7FFF) : le solo natif tire donc désormais la même suite que le
  navigateur, et toujours pas celle du jeu Windows.
- **Vue haute** (patch natif 0015) : sur un écran plus haut que 4:3, on voit plus de ville
  au-dessus et en dessous du joueur, comme le widescreen du web en montre plus sur les
  côtés (D07). Avec la marge d'activation (active par défaut, comme dans le web), les
  limites de la caméra qui décident de l'apparition et de la disparition des peds et des
  voitures couvrent aussi cette hauteur. En 4:3 et plus large, rien ne change.
- **Rythme des ticks en multijoueur** (patch natif 0018, branche `native-multiplayer-experimental`) :
  une console en avance sur les autres attend 1 ms de plus un tick sur deux (1,5 % plus lente),
  une console en retard 1 ms de moins, au plus 30 ms par tranche de 2 s, jusqu'à être à moins
  d'un demi-tick des autres. Le jeu reste à 30 ticks/s en moyenne. Ce que fait un tick ne change
  pas ; la partie reste identique sur toutes les machines.
- **Délai d'entrée variable en multijoueur** (patch natif 0017, même branche) : le délai entre
  l'appui et l'action de son propre joueur monte ou descend d'un tick en cours de partie (1 à 8),
  au lieu de rester celui choisi au lancement. Les entrées de chaque joueur forment un flux joué
  une entrée par tick sur toutes les machines : la partie reste identique partout, un
  navigateur compris.

Les deux suivants remplacent une lecture hors de son objet, dont le résultat dépendait de ce qui
suivait en mémoire, par une valeur fixe :

- **Éclairage des chiffres du score qui explose** (patch natif 0012) : `gQuadVerts_706B88`
  a quatre positions monde de plus, à zéro ; le renderer les lisait dans les globaux
  suivants. Purement visuel.
- **Radio des véhicules sans radio** (patch natif 0013) : en quittant un véhicule de police
  ou de service, la voiture reçoit 0 (« pas d'émetteur ») au lieu d'une valeur lue 2,5 Ko
  après l'objet son. Effet attendu : si le joueur y remonte, la radio choisit une station
  comme pour une voiture neuve. Non observé à l'oreille (pas encore de son en natif).

## Ce qui n'est pas encore vérifié

- Que (159,5 ; 139,5 ; 2) est bien le départ à l'église du build web.
- Le paquet PortMaster complet (`packaging/portmaster/`) installé tel quel depuis
  PortMaster : jusqu'ici, le propriétaire installe le port avec son script et les tests
  remplacent le binaire par SSH. L'extraction de `GTA2INSTALLER.exe` par le script n'a
  pas été observée par Claude.

## Corrigé en route

- Le dossier `player/` est créé au démarrage (le jeu y écrit `plyslot0.dat` ; sans lui,
  « Fatal Error 16 »). La redirection vers `$GTA2_SAVE_DIR` reste à faire.
- `AIL_release_sample_handle(NULL)` est ignoré en natif, comme dans Miles (voir
  « Correctifs à reporter dans le build web »).
- `-shared-libasan` sous `--sanitize` : le moteur définit ses propres `operator new`
  et `delete` (`crt_stubs.cpp`, qui remettent à zéro via `calloc`, comme le jeu
  l'attend), et ceux de la bibliothèque ASan statique entraient en conflit.

## Correctifs à reporter dans le build web

**Reportés dans le web le 9 novembre** (multijoueur entre navigateur et natif, choix du
propriétaire ; ils changent `gta2.wasm`) : les entrées et départs à 3 joueurs et plus
(`net_web.cpp`, plus de garde `GTA2_PORT_NATIVE`), et les émetteurs radio hors de 1..5
(`port/patches/0026`, la même borne que le patch natif 0013) ; puis la mort des occupants
d'une voiture détruite avec le tir ami coupé (`port/patches/0027`).

Défauts trouvés en natif qui existent aussi dans le build web, corrigés sous
`GTA2_PORT_NATIVE` seulement. Ils ne sont pas reportés pour l'instant, à la demande du
propriétaire (7 novembre 2026) : changer le code du jeu web rend « older version »
les sauvegardes du navigateur (règle des sauvegardes, `docs/CONTRIBUTING.md`). Pour
reporter l'un d'eux, il suffit de retirer sa garde `#if defined(GTA2_PORT_NATIVE)`.

| Où | Défaut | Effet dans le build web |
|---|---|---|
| `port/audio/miles_web.cpp`, `AIL_release_sample_handle` | handle nul déréférencé (`cSampleManager::Terminate_58DAE0` libère des cases encore vides au premier `AllocSamples_58D9F0`) | écriture silencieuse à l'adresse 0x30 de la mémoire wasm, à chaque initialisation du son |
| `port/render/gbh_web.cpp` (`Draw`, lumières) avec `gQuadVerts_706B88` (`Draw.cpp`) | un dessin éclairé lit les positions monde après les 4 sommets ; `DrawFigure_5D7EC0` n'en remplit que 4 | les chiffres du score qui explose sont éclairés d'après des globaux sans rapport (lecture hors du tableau) |
| `PublicTransport_181C::SetupTrainAndBusStops_5794B0` (patch natif 0016) | `gStationCount_6FF1CC` jamais remis à zéro : il grossit à chaque district chargé, et au-delà des 100 stations de `field_0_stations` la boucle des arrêts de bus lit hors du tableau | plantage (ou lecture hors de l'objet) après plusieurs chargements de districts dans la même session, par exemple les démos du menu |
| `port/render/gbh_web.cpp`, cache des textures par contenu | une texture GPU n'est jamais libérée, même quand plus aucune texture du jeu ne l'affiche ; chaque nouveau texte, chiffre ou dégât de voiture et chaque district chargé en ajoute | la mémoire WebGL grossit pendant toute la session |

## À reprendre plus tard (64 bits, hors jalon 1)

- `miles_web.cpp` `Id()` tronque un pointeur en `int` pour nommer un son côté JS. Sans
  conséquence en headless ; à revoir pour l'audio (jalon 5). (`gbh_web.cpp:173`, que je
  citais aussi, range un petit identifiant dans un champ pointeur : sans problème.)

## Comment c'est construit

| Partie | Où |
|---|---|
| Script de build (relance dans Docker) | `scripts/build_native.sh` |
| Copie patchée | `research/native-work/core` (clone git, branche `native-port`) |
| Alias de casse des `#include` | `scripts/native/case_aliases.py` → `build/native/case-aliases` (reprend `make_case_aliases` de `build_port.py`) |
| CMake | `port/native/CMakeLists.txt` : `gta2_core` (les 110 fichiers de `port/core/sources.txt`) + exécutable `gta2` |
| Faux `<emscripten.h>` | `port/native/compat/emscripten.h` : `EM_JS` devient une déclaration implémentée en natif, `EM_ASM` est ignoré |
| Fichiers web réutilisés tels quels | `crt_compat`, `win32_shim`, `dinput_web`, `lang_web`, `exports`, `frame`, `gbh_web`, `vid_web`, `bink_web`, `miles_web` |
| Boucle principale headless | `port/native/main.cpp` (`GTA2_DATA_DIR`, `GTA2_AUTOSTART=1`, `GTA2_RUN_SECONDS`) |

## Différences de build avec le web

- **libstdc++ au lieu de libc++** : `port_prefix.h` redéfinit `vswprintf`, ce qui
  renommait aussi l'appel `std::vswprintf` de `std::to_wstring` dans `<string>`
  (417 erreurs). Sous libstdc++ seulement, `<string>` est maintenant inclus avant les
  redirections.
- **Avertissements** : `-Wno-everything` puis les quatre avertissements de troncature
  de pointeurs et d'entiers, au lieu du `-w` du web.
- Le montage Docker Desktop sur macOS est **insensible à la casse** (`Fix16.hpp` et
  `fix16.hpp` s'ouvrent tous les deux) : les alias ne sont donc pas créés ici, mais le
  seraient sur un système de fichiers sensible à la casse.

## Multijoueur (jalon 7, en cours, branche `native-multiplayer`)

Plan : option A d'abord (client WebSocket vers `/relay` de `server/server.mjs`, compatible
avec le build web), puis D (partie directe en réseau local, sans serveur). Le multijoueur
est un **binaire à part, `gta2-online`**, avec son propre lanceur : le binaire solo `gta2`
ne contient aucun code réseau (vérifié : aucun symbole `gta2net::`, `dist/gta2.aarch64`
toujours 1,5 Mo). Comme dans le navigateur, le lobby a lieu avant le démarrage du jeu, qui
entre ensuite en partie réseau par `-N`.

**A0, le socle (fait, 9 novembre)**, dans `port/native/net/` (compilé seulement dans
`gta2-online` et `gta2-net-test`, option CMake `GTA2_MULTIPLAYER`, activée par défaut) :
- `json.*` : JSON (clés dans l'ordre, nombres comme `JSON.stringify`, `\u` et UTF-8) ;
- `transport.h` : l'interface des transports de `web/net.js` (rejoindre, envoyer à un joueur
  ou à tous, ping, arrivées et départs), sans fil d'exécution : `Poll()` à chaque tour de
  boucle ; `memory_transport.*` : un transport en mémoire pour les tests ;
- `lobby.*` : `CoopLobby` porté (mêmes messages `hello`, `crew`, `side`, `roster`, `start`,
  `loaded`, `go`, `chat`, `sync`, `wp`, mêmes règles : roster et réglages de l'hôte, équipes
  et gangs de Gang Wars, délai d'entrée `inputDelayFor`, messages arrivés avant que le jeu
  soit prêt). Joueurs et réglages gardés en JSON, pour qu'un roster venu d'un navigateur
  revienne intact ;
- `coop_native.*` : `setupCoop` et `watchCoop` de `web/shell.js` (session du moteur, règles,
  équipes, carte multijoueur du district, noms, marge d'activation à 0 ; contrôle toutes
  les 90 ticks, joueurs attendus) ; remplace le `gta2_js_net_send` vide du binaire solo.
- `gta2-net-test` : 98 vérifications (JSON ; règles ; une salle hôte + 2 joueurs en mémoire,
  avec latence simulée, jusqu'au départ d'un joueur ; un roster et un départ au format d'un
  hôte navigateur) : toutes passent, aussi sous ASan + UBSan. Elles ont trouvé une référence
  pendante (`for (… : Crews().Items())` sur un temporaire) dans `ApplySide`.

**A1, client WebSocket (fait, 9 novembre)** : `ws_transport.*`, le `ServerTransport` de
`web/net.js` (RFC 6455 : trames texte et binaires, fragments, ping/pong, fermeture ; socket
non bloquant ; `ws://` seulement), avec la connexion d'un serveur à mot de passe
(`POST /login` ou code d'invitation, pour le cookie `gta2_session` que le relais demande).
`net_main.cpp` déroule le lobby avant `WinMain` (puis `-N`) et lit le transport à chaque
tour de boucle ; réglé pour l'instant par l'environnement (`GTA2_NET_SERVER`, `_PASSWORD`,
`_ROOM`, `_NAME`, `_HOST`, `_PLAYERS`, `_MODE`, `_DISTRICT`, `_CREW`, `_SIDE`…, en-tête du
fichier). La ligne d'état de `gta2-online` ajoute le dernier contrôle et les messages.

**A2, natif contre natif (observé, 9 novembre)** : le serveur dans son propre conteneur
(`docker build -t gta2-server server/`, puis `docker run -d --name gta2-relay -p 8790:8790
-e GTA2_PASSWORD=test -e DATA_DIR=/tmp/db gta2-server`) ; deux `gta2-online` sans écran dans
`gta2-pm`, par `ws://host.docker.internal:8790`, avec le soak des deux côtés
(`GTA2_SOAK=1 GTA2_SOAK_AIDS=0`, graines différentes) :

| Mode | Durée | Ticks | Contrôles | Divergence |
|---|---|---|---|---|
| Free roam | 300 s | 8 175 | identiques jusqu'à 8 100 | aucune |
| Free-for-all | 120 s | 2 720 | jusqu'à 2 700 | aucune |
| Gang Wars (équipes Reds/Loonies, Blues/Yakuza) | 120 s | 2 720 | jusqu'à 2 700 | aucune |
| Free roam sous ASan + UBSan | 60 s | 820 | jusqu'à 810 | aucune ; 0 erreur ASan, 0 rapport UBSan dans `net/` |

30,0 à 30,3 ticks/s, ping par le relais 11 ms, délai d'entrée 1 tick.
- **Faux départ :** avec le soak tel quel, les jeux divergeaient au tick 90. Cause : le soak
  donne une arme et fait apparaître des voitures dans le jeu local par les aides de test
  (`gta2_dev_give_weapon`, `gta2_dev_spawn_car`), donc chez un seul joueur.
  `GTA2_SOAK_AIDS=0` le limite aux touches.
- **`-ffp-contract=off` ajouté aux drapeaux obligatoires** (CMake et `build_native.sh`),
  solo compris : sans lui, clang fusionne multiplications et additions en FMA sur aarch64,
  ce que wasm (et le x87 de l'original) ne fait pas ; nécessaire pour jouer avec un
  navigateur (A3). Les parties ci-dessus sont avec ce drapeau.

**A3, natif contre navigateur (observé, 9 novembre)** : le serveur de jeu, le build web
release (emsdk 6.0.12) et `game/` sur un conteneur Ubuntu 24.04 du propriétaire
(10.20.0.158, `/opt/gta2-browser`) ; un `gta2-online` natif sans écran comme hôte dans
`gta2-pm`, et Chromium sans fenêtre comme invité (`scripts/native/browser_peer.mjs`, image
`mcr.microsoft.com/playwright:v1.49.1-noble` sur le Mac), qui rejoint la salle par le lien
d'invitation et presse des touches au hasard.
- **Premier essai : divergence au tick 90, même sans aucune touche.** Les relevés tick par
  tick (`GTA2_NET_DUMP_HISTORY=n` en natif, `DUMP=n` pour le navigateur) différaient dès le
  tick 2, sur le hash des peds seulement ; la liste des peds à ce tick (`GTA2_NET_SYNC_DUMP`,
  `?syncdump` côté web) : le passant 9 né 4 608 (0,28 bloc) plus loin en x et en y côté
  navigateur, soit 8 × l'écart de son décalage aléatoire (`GetSpawnJitter`). Cause : le
  `rand()` de la bibliothèque C, dont le moteur tire ses nombres « du monde », n'a pas la
  même suite dans la glibc et dans musl (vérifié : après `srand(1)`, 1804289383… contre
  0, 740882966…). Corrigé côté natif seulement (`platform/rand_native.cpp`, la suite de
  musl ; voir les écarts plus bas).
- **Après la correction :**

| Essai | Ticks | Résultat |
|---|---|---|
| Sans touches, 40 s | 150 premiers relevés comparés un à un | identiques |
| Touches au hasard des deux côtés, 2 min | 3 629 | contrôles identiques jusqu'à 3 600 |
| Touches au hasard des deux côtés, 10 min (630 touches côté navigateur, 713 actions côté natif) | 18 147 | contrôles identiques jusqu'à 18 090, aucune alerte dans la page |

  Ping par le relais 21 à 71 ms, délai d'entrée 1 ou 2 ticks, 29,4 à 30,3 ticks/s côté natif.


**A4, lobby à la manette (fait, 9 novembre)** : `net/lobby_screen.cpp`, dans le style de
l'écran Options (même police, mêmes entrées : haut/bas, gauche/droite, A, B ; il prend la
main par des crochets faibles de `options_native.cpp`, absents du binaire solo). Avant le
jeu : nom et salle choisis dans les listes de `conf/net.ini` (pas de clavier à l'écran pour
l'instant), « Create room » ou « Join room » ; dans la salle, les joueurs avec leur aller-
retour, les réglages de l'hôte (mode, district, tir ami ou durée, limite de kills), en
Gang Wars l'équipe et le gang de chacun, « Start the match » ; puis l'attente de tous.
`conf/net.ini` (écrit avec des valeurs par défaut au premier lancement) : `server`,
`password` ou `invite`, `names`, `name`, `rooms`, `room`, `crews` ; les `GTA2_NET_*`
priment sans être réécrits dans le fichier. Le mode automatique des tests reste pour les
builds sans écran et quand `GTA2_NET_HOST` est défini. Après une partie terminée par le jeu,
`gta2-online` se relance (`execv`) et revient au lobby, comme la page du navigateur se recharge.
`GTA2_LOBBY_KEYS` joue des appuis pour les tests.
- **Observé dans le conteneur** (pilote SDL hors écran, captures) : hôte à l'écran (créer
  ROOM1, attendre, démarrer) contre client automatique ; client à l'écran (rejoindre ROOM2,
  équipe Reds, gang) contre hôte automatique en Gang Wars, qui attend l'équipe puis le gang
  avant de démarrer. Les deux parties démarrent.
- **Pas encore vérifié :** le retour au lobby après une partie, sur console et à la manette.

**3 joueurs (9 novembre) :** deux consoles (TSP S hôte, RP Mini) et un navigateur ont divergé
au tick 180, les deux consoles entre elles. Reproduit dans le conteneur avec trois `gta2-online`
et des touches au hasard : l'hôte divergeait dès le tick 2 (11 peds contre 13). Cause et
correction (natif) : tableau « Correctifs à reporter dans le build web » ci-dessous. Après :
3 joueurs pendant 2 min, 3 622 ticks, contrôles identiques, 600 premiers relevés identiques.
- **Régression de cette correction, sur consoles (TSP S, RP Mini, Air X) :** les parties à 3
  se défaisaient dès le début (le jeu abandonnait un joueur après ~6,5 s d'attente, l'hôte
  partait, la partie finissait). Reproduit dans le conteneur avec un réseau irrégulier
  (`GTA2_NET_LAG_MS=30,120` : 30 ms plus jusqu'à 120 ms par message, ordre gardé) et tracé
  (`GTA2_NET_TRACE=1`) : en cherchant le premier message livrable, la boucle jetait comme
  « renvoi » tout message d'un joueur dont le numéro n'était pas l'attendu, y compris ceux qui
  attendaient derrière le premier de ce joueur ; ses entrées des ticks suivants disparaissaient.
  Corrigé : seul le premier message de chaque joueur dans la file est examiné. Après : 3 joueurs,
  réseau irrégulier, 120 s, aucun blocage ni abandon, 600 premiers relevés identiques.
- Les lignes `co-op:` du log portent l'heure depuis le début de la partie et l'image réseau,
  et disent quand le jeu abandonne un joueur ou termine la partie.

**Voiture explosée avec un joueur dedans (9 novembre)** : les deux consoles clientes ont
planté (SIGSEGV à l'adresse 0x48, `CarPhysics_B0::HandleUserInputs_55A860` depuis
`Player::DoCarControlInputs_566C30`) quand le joueur du navigateur a fait exploser la voiture
d'un autre joueur en Free roam, tir ami coupé ; le navigateur continuait (en wasm l'adresse 0
est de la mémoire). Cause : `Ped::Kill_46F9D0` ignorait la mort des occupants imputée à un
autre joueur (patch 0021), alors que l'explosion retire le conducteur et libère la physique
de la voiture : le joueur restait vivant dans l'épave. Choix du propriétaire (option A) : un
piéton dans une voiture meurt toujours (patch partagé 0027, `COOP_VERSION` 3) ; les balles et
les coups restent bloqués par le tir ami. Reproduit et vérifié avec `GTA2_TEST=boom` sous ASan
(le joueur meurt puis réapparaît, plus de plantage).

**A7, en partie (en cours, 9 novembre)** : `net/hud_net.cpp`, par-dessus le jeu (crochets
faibles de `options_native.cpp`). En match, Select fait trois choses (`input_sdl.cpp`, seulement
dans `gta2-online` et en partie réseau ; le binaire solo garde Select tel quel) : un appui court
(relâché en moins de 0,3 s, sans croix) donne son action liée (la radio) au relâchement ;
maintenu, il affiche le tableau des scores (colonnes et ordre du navigateur : joueur, kills,
morts, points ; Free-for-all par kills, Free roam par points, Gang Wars par équipe) avec en
légende les quatre messages ; avec la croix, il envoie un message rapide (`conf/net.ini`
`chat_up/down/left/right`, par défaut « Follow me! », « Wait for me! », « Help! »,
« Nice one! »), et la croix ne va pas au jeu tant que Select est tenu. Tab au clavier : le
tableau. Les messages reçus (consoles et texte libre du navigateur) : les quatre derniers en
bas à gauche, 9 s, le nom dans la couleur du joueur. Options (gta2-online seulement) : « Chat
messages » et « Waypoints » On/Off (`show_chat`, `show_waypoints` dans `net.ini`). Fin de
match : le classement final jusqu'à A ou 10 s, puis le lobby. Vu dans le conteneur
(`GTA2_NET_TEST_HUD=1`, captures) : tableau, légende, messages envoyés et reçus par l'autre
joueur. Points de passage : Select + L1 pose le sien là où l'on est, Select + R1 l'efface ;
atteint (à 2 blocs, après s'en être éloigné), il s'efface comme dans le navigateur ; ceux des
autres (carte du navigateur ou console) et le sien s'affichent comme un repère dans la couleur
du joueur, sur place s'il est dans la vue, sinon sur le bord de l'écran dans sa direction avec
la distance en blocs (vu dans le conteneur : un repère dans la vue, un autre sur le bord).
Placés avec la projection du jeu (`ProjectWorldToScreen_4B90E0`, caméra telle que dessinée),
à la hauteur du sol (`FindGroundZForCoord_4E5B60`, ou celle du joueur pour son propre point) :
ils ne glissent plus avec la perspective (vu sur console : ils glissaient sur les toits).
Navigateur (`web/map.js`) : les points des autres, loin, sont maintenant sur le bord du radar
(losange) ; avant, ils n'y apparaissaient que dans son rayon.
Reste : le geste et les points de passage à la manette, sur console.

**Clavier à l'écran (9 novembre)** : `platform/osk_native.cpp`, dans les deux binaires.
Disposition QWERTY (chiffres, `-`, `=`, Back ; QWERTYUIOP ; ASDFGHJKL, OK ; Shift, ZXCVBNM) ;
Shift (touche ou Y) passe de minuscules à majuscule pour la touche suivante puis majuscules
verrouillées (fond vert puis rouge) ; décalée, la rangée du haut donne `! @ # $ % ? & * ( ) _ +`.
Croix pour se déplacer, A appuie, B efface, X espace, Start valide, Select annule ; un clavier
physique tape directement. La police de la surcouche a gagné les minuscules et `@ # & * _`
(dessinées pour le port ; seul le clavier affiche les minuscules, les autres écrans restent en
majuscules). Dans le
menu du jeu, il s'ouvre seul quand le jeu attend un nom de joueur ou un mot de passe (états 3
et 5 du `Frontend`) et tape comme un clavier (touche enfoncée puis relâchée, une par une, ce
que `HandlePlayerNameTyping_4B2F60` attend) ; le texte affiché est celui du jeu. Dans le lobby
de `gta2-online`, A sur « Name » ou « Room » l'ouvre (salle en majuscules, comme le navigateur) :
une console peut rejoindre une salle créée par un navigateur, dont le code est aléatoire ;
la valeur tapée rejoint la tête de sa liste dans `net.ini` (gauche/droite repartent d'elle) ;
X (Suppr au clavier) retire de sa liste l'entrée affichée (il en reste toujours une) ; après la
dernière entrée vient « DEFAULT », où A remet la liste d'un `net.ini` neuf (noms prédéfinis,
ROOM1 à ROOM6). Saisie du nom du jeu : sans `-` ni `=`.
Vu dans le conteneur (captures) : saisie et validation du nom et de la salle dans le lobby.
Pas encore vérifié : l'ouverture automatique dans le menu solo (les appuis scriptés n'y
arrivent pas de façon fiable) ; à tester sur console.

**Gang Wars, objectif de points (9 novembre)** : ligne « Points target » de l'hôte dans le
lobby (aucun, 10 000 $ à 1 000 000 $ ; la valeur d'un hôte navigateur s'affiche telle quelle).
Comme `watchCoop` du navigateur : toutes les 250 ms, les points de chaque équipe (argent gagné
de ses membres) ; l'équipe en tête qui a l'objectif gagne, une bannière l'annonce, et 6 s plus
tard le match se termine (classement final « … win the Gang War », puis le lobby). Vu dans le
conteneur (`GTA2_NET_POINTS=100`, soak) : victoire annoncée à 102 points, fin du match 6 s
après.

**Gang Wars, équipes dans le lobby (9 novembre)** : comme les cartes du navigateur. Une ligne
par équipe, dans la couleur de son gang, avec ses membres (« * » : la sienne) ; A la rejoint ou
la quitte ; gauche/droite changent son gang, pour ses membres ou, pour toute équipe, l'hôte
(`Lobby::HostSetSide`, mêmes refus que le choix d'un membre : gang du district, pas déjà pris) ;
un gang tenu par une autre équipe est sauté. « New crew » propose les noms de `net.ini` (`crews`)
non utilisés puis « Type a name… » (clavier à l'écran) ; A crée l'équipe et la rejoint. Une
ligne nomme les joueurs sans équipe ; une équipe vide disparaît (les équipes viennent des
joueurs). L'équipe rejointe en dernier est gardée (`crew` dans `net.ini`) et rejointe en entrant
dans une salle, comme le navigateur. La liste défile quand elle dépasse l'écran ; l'aide suit la
ligne choisie. Vu dans le conteneur (captures) : équipe d'un invité rejointe, créée depuis New
crew, gang imposé par l'hôte à l'équipe d'un invité, gang pris sauté ; tests unitaires (109).

**Départ par « quitter ? » (9 novembre)** : sur consoles, l'hôte parti par la question du jeu
était retiré à une image d'écart sur deux machines. Reproduit (`GTA2_TEST=quit` : Échap puis
Entrée deux fois ; trois natifs, réseau irrégulier ; `GTA2_NET_TRACE=1` trace les messages
standard livrés) : un joueur qui quitte envoie un keep-alive par tick au lieu d'entrées
(minuteur de sortie) ; le jeu prend le premier comme son départ (retiré au tick suivant), au
même tick partout. L'écart venait du give-up de la connexion, redondant, lu un ou deux ticks
après sur une machine. Correctif (`net_web.cpp`, les deux builds, `COOP_VERSION` 4) : un
keep-alive compte comme les entrées, et son joueur est parti dès le premier (plus d'attente des
autres joueurs jusqu'au give-up). Après : trois essais, départ au même tick sur les deux
restants, contrôles identiques, 30,3 ticks/s.

**A6, `wss://` (TLS) (10 novembre)** : `gta2-online` parle au serveur en `ws://` ou en `wss://`
(`https://` accepté, port 443 par défaut), login compris. TLS : **mbedTLS 3.6.6** (LTS) lié en
statique dans `gta2-online` seulement, car l'OpenSSL des consoles diffère (3 sur ROCKNIX, 1.1 sur
AmberELEC). `scripts/native/build_mbedtls.sh` télécharge l'archive une fois dans `downloads/`
(jamais committée), vérifie le sha256 publié avec la version, la compile à part (ses sources
n'entrent pas dans `compile_commands.json`) ; `build_native.sh` l'appelle. Code :
`port/native/net/tls_conn.*` (connexion TCP ou TLS non bloquante, utilisée par le login et le
WebSocket ; après `WANT_WRITE`, la même longueur est réécrite comme mbedTLS le demande).
- Certificat toujours vérifié (autorité, nom, dates) : autorités de `ca_file` (`net.ini`, dans
  `conf/` sauf chemin absolu ; `GTA2_NET_CA_FILE`), sinon `SSL_CERT_FILE`, sinon le premier
  fichier présent parmi `/etc/ssl/certs/ca-certificates.crt`, `/etc/ssl/cert.pem`,
  `/etc/pki/tls/certs/ca-bundle.crt` (vu : les trois sur Air X et RP Mini, les deux derniers sur
  la R36S). Ou `tls_fingerprint = sha256:…` (`GTA2_NET_TLS_FINGERPRINT`, deux-points permis) :
  ce certificat-là seulement, pour un serveur à certificat maison ; le log donne l'empreinte.
- Messages courts dans le lobby (une notice tient maintenant sur trois lignes avant d'entrer
  dans une salle) : autorité inconnue, autre nom, périmé ou pas encore valide (« ou l'horloge
  de la console est fausse », avec sa date), empreinte différente, poignée de main expirée
  (« le serveur est-il en TLS ? »). Le détail et l'empreinte vont dans le log.
- `explicit_bzero` : compilé contre une glibc ≥ 2.25, mbedTLS l'appelle, et `gta2-online`
  demandait GLIBC 2.25. Avec `MBEDTLS_PLATFORM_ZEROIZE_ALT`, `tls_conn.cpp` fournit la
  fonction : retour à GLIBC 2.17, comme `gta2`. `build_native.sh` vérifie maintenant que chaque
  binaire de `dist/` ne demande pas plus que `gta2`.
- Taille : `dist/gta2-online.aarch64` 2,6 Mo ; `gta2` inchangé (1,5 Mo, aucun symbole mbedTLS).

Vu dans le conteneur :
- `scripts/native/test_tls.sh` : certificats de test (autorité, serveur pour `localhost` et
  `127.0.0.1`, autre nom, périmé, pas encore valide, auto-signé, autre autorité) et
  `scripts/native/test_relay.py` (le relais et le login de `server.mjs` en Python 3.8, en clair
  ou en TLS, le Node du conteneur étant trop vieux) ; `gta2-net-test` : 203 vérifications, toutes
  passent, aussi sous ASan + UBSan et en release. Deux joueurs en `ws://` puis `wss://`
  (`ca_file`, invitation, `https://` vers une IP, empreinte avec et sans deux-points) : arrivée,
  message texte, binaire des 256 octets, message de 50 Ko (plusieurs enregistrements TLS), ping,
  départ. Refus : chaque cas ci-dessus avec son message, mauvais mot de passe, `wss://` vers un
  serveur en clair, `ws://` vers un serveur TLS, serveur muet (délai).
- Deux parties de 10 minutes en même temps, à deux joueurs sans tête, l'une en `wss://` (TLS 1.3,
  AES-256-GCM), l'autre en `ws://` : contrôles identiques jusqu'au tick 16 290, 30,3 ticks/s,
  aucune divergence (le CPU du build sans tête, qui tourne en boucle, ne compare rien).
- Capture du lobby : l'erreur d'autorité inconnue sur trois lignes.

Vu sur consoles (10 novembre) : `scripts/native/tls_front.mjs` (Node seul, rien d'installé)
termine TLS devant `server.mjs` sur le conteneur Proxmox (port 8791, autorité de test ;
`ca_file` sur les consoles). D'abord deux joueurs sans tête depuis Docker (90 s, contrôles
identiques). Puis Air X (hôte), RP Mini, R36S et un navigateur en `https://` (exception de
sécurité acceptée pour l'autorité de test) : partie libre à 4, environ 200 s en jouant,
TLS 1.3 (AES-256-GCM) sur les trois consoles, contrôles identiques jusqu'au tick 5 310,
30,3 ticks/s, aucune divergence ; départ de l'hôte vu à la même image (5 963) sur les deux
restants. CPU du jeu (3 relevés d'une minute) : Air X 39 %, RP Mini 16 %, R36S 37 %, du même
ordre que la nuit en `ws://` à 3 joueurs immobiles (26-42 %, 16 %, 33-52 %) : pas de coût du
TLS visible. Mémoire 86 Mo (Air X, RP Mini) et 165 Mo (R36S, GPU compris), comme en `ws://`.

**Serveur public (10 novembre)** : `game-server-01` (LXC, `scripts/deploy_lxc.sh`, service
systemd), derrière le nginx du propriétaire (certificat Let's Encrypt) : `wss://gta2.example.org`.
Vu :
- depuis Docker, deux `gta2-online` sans tête, 10 min, seulement les autorités du système (pas de
  `ca_file`) : TLS 1.3, contrôles identiques jusqu'au tick 16 290, 30,3 ticks/s, 21 ms ;
- propriétaire, Mac sur un VPN au Canada (100 à 150 ms dans le lobby) : Free-for-all avec Air X,
  RP Mini et le navigateur, « fonctionnait très bien » ;
- six joueurs en Free-for-all (Air X hôte, R36S, TSP S, CubeXX, RP Mini, navigateur), environ
  7 min : TLS 1.3 (ChaCha20-Poly1305) sur les cinq consoles, délai d'entrée 2 ticks, contrôles
  identiques jusqu'au tick 12 510, aucune divergence ; 28,5 ticks/s la première minute (attente
  du chargement de la R36S), puis 30,0 à 30,3. Départs un par un à la fin : les restants
  continuent, contrôles bons après chaque départ. CPU du jeu (moyenne/max) : Air X 29/33 %,
  RP Mini 12/13 %, R36S 37/42 % (67 °C), TSP S 32/36 %, CubeXX 31/37 % tant que le CPU est à
  1,8 GHz. Sur la CubeXX, muOS est passé en `powersave` (288 MHz) au milieu de la partie,
  probablement son économie d'énergie après un moment sans appui (non vérifié) : 77 à 114 %
  d'un cœur, mais toujours 30 ticks/s. Mémoire : 84 à 90 Mo (RP Mini, Air X, TSP S),
  166-176 Mo (R36S, CubeXX, GPU compris).

Pas encore vérifié : session longue (une heure ou plus) en `wss://` sur le serveur public.

**Plusieurs serveurs dans le lobby (10 novembre)** : `conf/net.ini` a une section `[server]`
par serveur (`name`, `url`, et en option `password`, `invite`, `ca_file`, `tls_fingerprint`),
dans l'ordre du lobby ; `[net] server` nomme celui connecté en dernier. Sans `name`, l'hôte de
l'`url` ; deux noms pareils : « Main 2 ». L'ancien format (`server = <url>` et le login dans
`[net]`) se lit comme un serveur et s'écrit au nouveau format. `GTA2_NET_SERVER` (tests) ajoute
un serveur à part, jamais écrit ; sans lui, `GTA2_NET_PASSWORD`… valent pour la partie seulement.
Lecture et écriture dans `net/net_config.cpp` (testées par `gta2-net-test`).
Dans le lobby : les serveurs en tête ; A s'y connecte (le précédent est quitté d'abord) ou s'en
déconnecte ; « connecté » = login fait et relais d'accord (`WsTransport::Check`, sans salle),
la session sert ensuite à chaque salle sans nouveau login. Un serveur qui veut un mot de passe
absent ou faux ouvre une fenêtre (clavier à l'écran) ; accepté, le mot de passe est écrit dans sa
section ; refusé, la fenêtre revient. Create room et Join room restent gris tant qu'on n'est pas
connecté. Après un match, le lobby (nouveau processus) se reconnecte au serveur du match.
Vu dans le conteneur : `gta2-net-test` 242 vérifications (format, ancien format, noms, environnement
jamais écrit ; `Check` sans mot de passe, faux, bon puis salle avec la session, invitation,
serveur ouvert, `wss://`, serveur absent), aussi sous ASan + UBSan ; captures : liste, connexion,
fenêtre du mot de passe, « pw » tapé, connecté, écrit dans `net.ini`, salle créée ; lobby
automatique à deux joueurs inchangé.

**Parties en cours et score sur le serveur (10 novembre)** : `GET /games` (page pour un navigateur,
JSON sinon et sur `/games.json`, `server/games-page.mjs`), lu dans les messages du lobby que le
relais fait passer. Score : tous les 150 ticks, `Gta2Port_CoopSnapshot` (partagé) relève kills,
morts et points de chaque slot (`gta2_port_coop_scores`) ; chaque joueur (`coop_native.cpp`,
`web/shell.js`) les envoie à `@server` (le relais ne les passe à personne) ; le serveur compare
les rapports d'un même tick (majorité, confirmé à deux, désaccord signalé). Vu (copie du serveur
sur 10.20.0.158) : trois natifs, score confirmé par 3 ; un natif en `GTA2_TEST=pvp` (sa partie
diverge) : désaccord signalé au tick 2550, version de la majorité (2 sur 3) affichée ; un natif
et un navigateur (build web reconstruit) : mêmes rapports dès le tick 150, partie synchronisée ;
fin de match dans « Recent matches ».

**Noms (10 novembre)** : `gta2-mp` s'appelle maintenant **`gta2-online`** (serveurs `ws://` et
`wss://`, lanceur « GTA 2 Online.sh », l'ancien « GTA 2 Multiplayer.sh ») ; `gta2-lan` le réseau
local ; `gta2` le solo. Les mentions plus haut de `gta2-online` désignaient `gta2-mp` à l'époque.

**gta2-lan : le réseau local sans serveur (10 novembre)** : un troisième binaire (`gta2-lan`, cible
CMake avec `GTA2_NET_LAN`, 2,2 Mo, sans mbedTLS ni code serveur). `net/lan_transport.*` :
chaque joueur d'une salle est relié en TCP à tous les autres (maillage, port 27901 ou les
suivants s'il est pris) ; le lobby, le lockstep, le chat, les points de passage tournent dessus
comme sur le relais. Découverte : chaque membre d'une salle s'annonce chaque seconde en broadcast
UDP (27900 ; `SO_REUSEPORT` pour plusieurs jeux sur une machine) ; une salle non entendue depuis
5 s disparaît. Rejoindre : connexion aux membres annoncés ; chacun répond avec ceux qu'il connaît
(le nouveau s'y connecte) ; deux connexions croisées : celle du plus petit id reste, sans faux
départ (un joueur n'est annoncé parti qu'une fois toute connexion vers lui fermée, y compris
celle qui s'ouvre). PIN de salle (4 chiffres, 0000 par défaut, `[lan] pin` dans `net.ini`) vérifié
à chaque connexion. Score : pas de serveur, `@server` n'est envoyé à personne.
Lobby : les jeux entendus (nom, hôte, joueurs, mode ; « playing » ou autre version en gris), Host
a game, Name, Room, PIN (A l'édite comme un cadenas : gauche/droite le chiffre, haut/bas sa
valeur, A valide, B annule), Join by address (clavier à l'écran, `ip` ou `ip:port`), Quit ; la
salle est l'écran du mode en ligne. Le curseur reste sur sa ligne quand des jeux apparaissent.
Vu dans le conteneur : `gta2-net-test` 259 vérifications (maillage à trois dont un trouvé par la
liste d'un autre, messages, ping, PIN faux refusé, départ, annonces, lobby hôte + invité sur le
maillage), aussi sous ASan + UBSan ; trois `gta2-lan` sans tête qui se trouvent par les annonces
(cinq essais : aucun faux départ) et quatre en Gang Wars : contrôles identiques jusqu'au tick 1 800,
30,3 ticks/s ; captures : liste, cadenas « 0 <1> 0 0 », PIN faux refusé, salle rejointe.
Essai du propriétaire : 6 consoles en LAN, « ça fonctionne, pas de lag ». Corrigé ensuite : la
touche « . » du clavier à l'écran (après M, « . » aussi avec Shift ; cachée dans la saisie du nom
du jeu, comme - et =), pour Join by address ; un joueur parti reste au tableau des scores (Select)
et sur l'écran de fin, en gris, « (left) », avec ses derniers chiffres (notés à chaque image du
match ; avant, sa place libérée le faisait disparaître), dans gta2-lan comme gta2-online. Vu dans
le conteneur (capture) : « Nina (left) 2 0 $0 » après son départ.
**Deux fichiers de réglages (10 novembre)** : `conf/online.ini` (gta2-online : `[net]` avec le
serveur choisi, les sections `[server]`) et `conf/lan.ini` (gta2-lan : `[net]` sans serveur,
`[lan] pin`) remplacent le `net.ini` partagé. Les deux ont les listes (noms, salles, équipes),
le chat et les options. La première fois, chaque binaire reprend sa part de `net.ini` (gta2-lan
ignore ses serveurs, gta2-online son PIN) ; une fois les deux fichiers écrits, `net.ini` devient
`net.ini.old`. Messages TLS et commentaires : `online.ini`. Vu : `gta2-net-test` 271 vérifications
(dont la migration en deux temps), aussi sous ASan + UBSan.
Pas encore fait : les départs brutaux (perte du Wi-Fi en pleine partie : un joueur peut avoir
reçu la dernière entrée du partant et pas un autre), consoles, lanceur et paquet.

Reste : A5 (consoles), A7 (scores, chat, points de passage), puis D1-D4 (transport TCP local,
découverte UDP, consoles, paquet).

### Lockstep amélioré (branche `native-multiplayer-experimental`)

Plan : 0 mesurer, 1 envoi redondant des entrées, 2 délai d'entrée adaptatif annoncé « au tick T »,
3 rythme lissé, 4 UDP en LAN, 5 validation ; ensuite le transfert d'état (option 6).

**Étape 0, les mesures (10 novembre)** : `net/net_stats.cpp`, dans gta2-online et gta2-lan. Pendant
un match, une ligne `gta2: net stats:` toutes les `GTA2_NET_STATS` secondes (5 par défaut, 0 :
aucune) :
- ticks joués et cadence (30/s quand rien ne retient le jeu) ;
- « held up » : le temps où le tick suivant était dû alors qu'une entrée manquait encore, et de
  qui (attendre une entrée avant l'heure du tick ne coûte rien et n'est pas compté) ;
- la boucle : itérations, plus grand écart entre deux, travail par itération (jeu et dessin,
  sans `SDL_GL_SwapWindow`, l'attente de la vsync), avec un tick ou non, temps du swap ;
- par joueur : entrées reçues, marge (de combien de ticks une entrée arrive avant d'être lue,
  min/moy/max ; 0 = juste à temps ou attendue), aller-retour mesuré pendant le match (un ping
  par période, par le même transport que les entrées : `Lobby::PingSlot`).
Vu dans le conteneur (`scripts/native/test_lan_stats.sh`, deux gta2-lan hors écran en jeu
aléatoire, `GTA2_SOAK_AIDS=0`, sinon les aides de test désynchronisent au tick 90) : lignes
cohérentes, aucune désynchronisation ; les chiffres eux-mêmes ne valent rien là (deux jeux en
rendu logiciel sur un processeur partagé : images jusqu'à 150 ms). Les consoles diront.

**Essais sur consoles (10 novembre, gta2-lan, free-for-all et free roam, 3 à 5 min)** :
- A, Air X + RP Mini en Wi-Fi, délai 2 : 30,2-30,3 ticks/s ; jeu retenu 0,1 % du temps hors
  démarrage ; travail par image 1 à 2,5 ms ; aller-retour 17 ms (médiane) ; marges cumulées
  3,5 ticks pour un délai de 2, donc 1 tick suffit.
- B, Air X (Wi-Fi) + R36S (Ethernet), délai 1 : 30 ticks/s, R36S à 4,6 ms par image (13 au
  pire), jamais retenue en cours de partie.
- B', Air X + G350 en Wi-Fi (dongle), délai 2 : même chose, plus un gel d'environ 1 s vers
  180 s, sur les deux consoles à la fois, avec un ping de 936 ms : une coupure du lien Wi-Fi.
  La G350 n'a jamais dépassé 7,7 ms par image.
- Dans les trois : 1,2 à 1,8 s de blocage au tout début (la console la plus lente finit de
  charger le niveau), et un délai choisi au lobby qui varie d'une partie à l'autre (2, 1, 2)
  sur le même réseau : le ping du lobby compte l'attente d'une image de chaque côté.
Conclusion : ni le processeur ni le réseau ne pèsent en temps normal ; le levier est le délai,
puis les coupures Wi-Fi (à confirmer par les retransmissions TCP).

**Étapes 2 et 3, le délai adaptatif et le rythme (10 novembre)** : `net/net_delay.cpp`, patches
natifs 0017 et 0018. Les entrées de chaque joueur forment un flux que toutes les machines
jouent une entrée par tick (0017 : une file au lieu d'un anneau de taille fixe) : envoyer deux
fois la même entrée allonge son délai d'un tick, n'en envoyer aucune le raccourcit, sans accord
entre machines ni changement chez les autres (un navigateur compris). Toutes les 2 s, chaque
machine dit à chaque autre comment ses entrées sont arrivées (temps retenu, marge mini et
moyenne, son propre délai : message de lobby `margin`, qu'un navigateur ignore). D'après les
marges dans les deux sens, chacune sait de combien de ticks elle est en avance ; plus d'un
demi-tick : elle ralentit ou accélère de 1,5 % (0018), pour la moitié de l'écart, au plus 30 ms
par tranche. Le délai ne change qu'une fois les machines à moins d'un tick l'une de l'autre :
+1 si une autre a été retenue plus de 50 ms en 2 s par ses entrées, -1 après 10 s de marge d'au
moins un tick partout et 15 s sans hausse ; rien dans les 10 premières secondes.
Trouvé en route : sans le rythme, le délai adaptatif empire les choses. Une machine retenue
parce que l'autre tourne en avance fait allonger le délai de l'autre, ce qui laisse l'avance
grandir d'autant (vu dans le conteneur : hôte à 1 tick, invité à 6, soit 200 ms de réactivité
perdue pour lui).
Les lignes `net stats` donnent aussi le délai, l'avance estimée et, par lien TCP, l'aller-retour
du noyau et les segments renvoyés (`TCP_INFO`). Le HUD affiche « Waiting for <nom>... » après
une demi-seconde d'attente. `GTA2_NET_ADAPT=0`, `GTA2_NET_PACE=0`, `GTA2_NET_DELAY_MIN/MAX`,
`GTA2_NET_START_DELAY` (tests).
Vu dans le conteneur (`PLAYERS=2|3 scripts/native/test_lan_stats.sh`, départ à 3 ou 4 ticks) :
délais qui descendent (4 → 2, 4 → 3), aucune désynchronisation à 2 et 3 joueurs avec des délais
différents par joueur, aussi sous ASan + UBSan (rapports UBSan : ceux déjà connus du moteur) ;
`gta2-net-test` 169 vérifications. Le rythme n'a pas pu être réglé là : les jeux en rendu
logiciel n'y tiennent pas 30 ticks/s sans à-coups. À régler sur les consoles.

**Essais sur consoles avec le délai adaptatif (10 novembre)** :
- A, Air X + RP Mini : délai 2 → 1 vers 12-15 s, puis stable ; jeu retenu 0,1 % ; avance
  estimée ±0,5 tick ; une retransmission TCP en tout. « Très fluide sur les deux consoles. »
- B', Air X + G350 : délai 2 → 1, G350 jamais retenue ; le « Waiting for Zed » vu sur la Air X
  était le chargement (3,0 à 4,3 s) ; pas de coupure Wi-Fi cette fois. « Très fluide. »
- C, TSP S + RG34XX-H + RG353VS + RG40XX-H + CubeXX : aucune désynchronisation, jeu retenu 0,3 à
  0,6 %, avance presque toujours sous ±0,5 tick ; mais délais montés à 4-7 ticks (130 à 230 ms),
  « un peu moins fluide ». 22 à 63 retransmissions TCP par console en 5 min.
  Cause : était compté comme fautif tout joueur dont l'entrée manquait pendant une attente ; à
  5, avec la règle qui retient aussi des entrées arrivées à temps (InputDue, 3 joueurs et plus),
  un seul retard faisait monter tout le monde. Corrigé : seule l'entrée arrivée après l'heure de
  son tick compte, pour son retard ; et il faut un retard de plus de 50 ms dans 2 des 3 derniers
  rapports d'un joueur (une coupure isolée ne fait plus monter : un tick de plus ne l'aurait pas
  évitée). Pas encore revu sur les consoles.

**Couleurs des joueurs (10 novembre)** : les marqueurs, le tableau des scores et les noms du
chat prenaient les couleurs du navigateur par place (`PLAYER_COLOURS`), qui ne sont pas celles du
jeu (flèches, scores en $). `platform/player_colour_native.cpp` lit celles du jeu dans le style
chargé : la palette « user remap » du joueur (`Player::field_790_hud_palette`, celle que
`ColorScoreFromRemap_569A10` donne à ses chiffres), comparée à la palette neutre (user remap 1) ;
elle en change une rampe de 5 teintes, et la teinte du milieu est celle des chiffres à l'écran.
Vu sur une capture à 3 joueurs : violet #5b439f, vert #87d700, bleu #4743cf, comme les scores du
jeu. Le navigateur a le même écart (non corrigé : `web/shell.js`).

## Reste à faire

Points 1 à 5 : le jalon 1. Les points 3 et 4 ne le bloquent pas dans le conteneur (montage
insensible à la casse), mais ils sont nécessaires sur la console.

1. ~~Les asserts de taille et l'appel Miles~~ : fait.
2. ~~Le lien~~ : fait (ponts `gta2_js_*` vides, `net_web`/`map_web`/`coop_web`/
   `dev_hooks`/`options_web` compilés tels quels, `--gc-sections` comme le web).
2b. ~~Les textes `.gxt`, le style, la carte et les troncatures~~ : faits (entrées 6 à 11).
3. ~~Recherche insensible à la casse des fichiers du jeu~~ : fait (`fs_native.cpp`,
   jalon 3).
4. ~~Registre persisté dans un fichier texte~~ : fait (`conf/registry.txt`).
5. ~~Lancement sous `--sanitize` jusqu'à Downtown~~ : fait (60 s, 0 erreur ASan).
