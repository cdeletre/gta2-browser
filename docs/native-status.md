# Port natif Linux aarch64 : statut

Mis à jour le 8 novembre 2026. **Jalons 1 à 5 atteints** ; jalon 6 en cours (paquet PortMaster de l'utilisateur, WIP). Le jeu s'affiche sur la console (MANGMI Air X, ROCKNIX 20261101)
en 1920×1080 par Wayland, avec le GPU (Adreno 610) : menu, puis Downtown, à 60 images/s et
30,3 ticks/s ; manette et clavier ; son par PulseAudio.

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

  Sur la R36S, l'envoi groupé forcé donnait 10,0 s : le seuil de 0,15 ms la laisse en direct,
  au prix de 2,7 s, pour ne pas risquer le cas de la CubeXX (0,041 ms) ; à revoir si besoin.
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
  en `performance` : la config d'AmberELEC n'a pas `maxperf` pour les ports
  (`global.maxperf=0`), un lancement depuis EmulationStation sera donc en `powersave`
  aussi, sauf réglage du joueur.
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
- Reste à faire : lancement par le script PortMaster de l'utilisateur depuis
  EmulationStation, temps de chargement des autres districts, saccades lors des chocs.

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
  - Valeurs par défaut : en ville, croix/stick = déplacement, R2/L2 = accélérer/freiner,
    A = tirer, Y = voiture, X = saut/frein à main, B = spécial 1, L1/R1 = armes,
    L3 = spécial 2, R3 = statut, Start = pause ; dans les menus, A = valider, B et
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

- **Vue haute** (patch natif 0015) : sur un écran plus haut que 4:3, on voit plus de ville
  au-dessus et en dessous du joueur, comme le widescreen du web en montre plus sur les
  côtés (D07). Avec la marge d'activation (active par défaut, comme dans le web), les
  limites de la caméra qui décident de l'apparition et de la disparition des peds et des
  voitures couvrent aussi cette hauteur. En 4:3 et plus large, rien ne change.

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
- Le paquet PortMaster (jalon 6) : pour l'instant, le binaire et les fichiers du jeu sont
  copiés à la main dans `/roms/ports/gta2/` et lancés par SSH.

## Corrigé en route

- Le dossier `player/` est créé au démarrage (le jeu y écrit `plyslot0.dat` ; sans lui,
  « Fatal Error 16 »). La redirection vers `$GTA2_SAVE_DIR` reste à faire.
- `AIL_release_sample_handle(NULL)` est ignoré en natif, comme dans Miles (voir
  « Correctifs à reporter dans le build web »).
- `-shared-libasan` sous `--sanitize` : le moteur définit ses propres `operator new`
  et `delete` (`crt_stubs.cpp`, qui remettent à zéro via `calloc`, comme le jeu
  l'attend), et ceux de la bibliothèque ASan statique entraient en conflit.

## Correctifs à reporter dans le build web

Défauts trouvés en natif qui existent aussi dans le build web, corrigés sous
`GTA2_PORT_NATIVE` seulement. Ils ne sont pas reportés pour l'instant, à la demande du
propriétaire (7 novembre 2026) : changer le code du jeu web rend « older version »
les sauvegardes du navigateur (règle des sauvegardes, `docs/CONTRIBUTING.md`). Pour
reporter l'un d'eux, il suffit de retirer sa garde `#if defined(GTA2_PORT_NATIVE)`.

| Où | Défaut | Effet dans le build web |
|---|---|---|
| `port/audio/miles_web.cpp`, `AIL_release_sample_handle` | handle nul déréférencé (`cSampleManager::Terminate_58DAE0` libère des cases encore vides au premier `AllocSamples_58D9F0`) | écriture silencieuse à l'adresse 0x30 de la mémoire wasm, à chaque initialisation du son |
| `port/render/gbh_web.cpp` (`Draw`, lumières) avec `gQuadVerts_706B88` (`Draw.cpp`) | un dessin éclairé lit les positions monde après les 4 sommets ; `DrawFigure_5D7EC0` n'en remplit que 4 | les chiffres du score qui explose sont éclairés d'après des globaux sans rapport (lecture hors du tableau) |
| `sound_obj::RadioEmitter` (`sound_obj.hpp`) | index 102, 103 ou 128 quand `field_54F7[0]` vaut 101, 0x66 ou 127 | lectures et écritures 2 à 3 Ko après `gSound_obj_66F680` ; la voiture de police quittée reçoit un « statut d'émetteur » quelconque |

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

## Multijoueur (jalon 7, à faire)

Pas encore commencé : les ponts réseau `gta2_js_net_*` sont vides en natif
(`js_bridge_null.cpp`), mais `net_web.cpp`, `coop_web.cpp` et les patches `port_coop.cpp`
sont déjà compilés. Le plan est dans `CLAUDE.md` (« Multijoueur ») : transport WebSocket vers
le relais de `server/` (compatible avec le build web), lobby à la manette, vérification du
déterminisme aarch64 ↔ wasm32, puis tests entre deux consoles et avec un navigateur.

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
