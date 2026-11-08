# CLAUDE.md — Port natif GTA2 (Linux aarch64, SDL2 + GLES) pour PortMaster

## Objectif

Produire un binaire Linux **aarch64** de GTA2, basé sur le moteur `gta2_re` déjà porté vers clang
par ce dépôt (gta2-browser), avec **SDL2** (fenêtre, entrées, audio) et **OpenGL ES 2.0** (rendu),
empaquetable pour **PortMaster** (consoles portables ARM, écran typique 640×480 mais existe aussi dans d'autres résolutions tel que 720x480, 720x720, 960x544, 1280x720, 1920x1152, 1920x1680 entre autre).

Le build web (WebAssembly) doit continuer à fonctionner. Le code natif vit à côté, il ne le remplace pas.

## À lire avant toute modification

1. `README.md` et `AGENTS.md`.
2. `docs/browser-build-status.md`, en particulier « How it is put together » et
   « Findings worth keeping » : la plupart des pièges du moteur y sont déjà documentés
   (tables initialisées à zéro, `wcslen` sur 2 octets, retours manquants, constructeurs globaux
   qui lisent des fichiers trop tôt…).
3. `port/render/` (interface `gbh_*` de 62 fonctions + `vid_*`), `web/renderer.js`, `web/audio.js`,
   `port/platform/`, `scripts/build_port.py` (comment les patches sont appliqués).

## Disposition

```
research/gta2_re/            upstream, épinglé (02de8be). NE JAMAIS MODIFIER.
research/port-work/core/     copie patchée du build web (gérée par build_port.py)
research/native-work/core/   copie patchée du build natif (gérée par build_native.sh)
port/patches/                patches moteur partagés web + natif (sous GTA2_PORT)
port/core/compat/            en-têtes de remplacement Windows/DirectX (partagés)
port/native/                 TOUT le code natif :
  CMakeLists.txt             point d'entrée CMake du build natif
  patches/                   patches moteur spécifiques au natif (sous GTA2_PORT_NATIVE)
  platform/                  Win32/DirectInput/registre/fichiers → SDL2 + POSIX
  render/                    gbh_*/vid_* → GLES 2.0
  audio/                     Miles → SDL2 audio
  main.cpp                   boucle principale SDL
scripts/build_native.sh      build natif (relance automatiquement dans Docker)
packaging/portmaster/        script de lancement, port.json, etc.
docs/native-status.md        statut du port natif (à tenir à jour)
docs/native-64bit.md         journal des corrections 64 bits
```

## Build

Toujours compiler dans le conteneur `gta2-pm` (aarch64 natif sur Apple Silicon).
Le script se relance tout seul dans le conteneur s'il est appelé depuis macOS :

```bash
./scripts/build_native.sh                     # debug
./scripts/build_native.sh --preset release
./scripts/build_native.sh --sanitize          # ASan + UBSan (debug)
./scripts/build_native.sh --prepare           # recrée research/native-work/core
./scripts/build_native.sh --clean             # efface build/native/<preset>
```

- Sortie : `build/native/<preset>/gta2`.
- `compile_commands.json` est lié à la racine pour clangd.
- Le script **échoue** si un drapeau obligatoire (voir plus bas) manque dans `compile_commands.json`,
  ou si le binaire importe une fonction `wcs*` de la libc.
- Il affiche la version GLIBC maximale requise par le binaire : comparez-la à celle de la console cible.

Si `scripts/build_port.py` fait plus qu'appliquer les patches (par exemple
`scripts/port/constify_temporaries.py` pour le patch 0001), reproduire cette étape dans
`scripts/native/prepare_extra.sh`, que `build_native.sh` exécute après les patches.

## Règles

- **Ne jamais modifier `research/gta2_re`.** Tout changement moteur passe par un patch :
  - dans `port/patches/` s'il sert aussi au web (garde `GTA2_PORT`) ;
  - dans `port/native/patches/` s'il est propre au natif (garde `GTA2_PORT_NATIVE`).
- Un patch = une intention. Petits patches numérotés, message clair en tête de fichier.
- Ne pas casser le build web. Si un patch partagé change, vérifier que
  `python3 scripts/build_port.py --preset debug --prepare` passe encore (si emsdk est installé).
- **Ne jamais committer ni copier de fichiers du jeu** (`game/`, `data/`, `.gxt`, `.sty`, `.gmp`,
  `.raw`, `.sdt`, installateur). Ils sont fournis par l'utilisateur, sur la console comme en local.
- Garder la logique de simulation à 30 ticks/s, comme l'original. Ne rien changer au gameplay
  sans le documenter comme déviation (voir le style de `docs/browser-build-status.md`).
- Après chaque jalon, mettre à jour `docs/native-status.md` : ce qui marche, ce qui a été
  **observé** (pas supposé), ce qui reste.
- Commits petits et autonomes, sur une branche, jamais directement sur `main`.

## Drapeaux de compilation obligatoires

Compilateur : **clang/clang++** (pas GCC : `-mms-bitfields` n'existe que sur x86 chez GCC).

| Drapeau | Pourquoi |
|---|---|
| `-fshort-wchar` | Les chaînes larges du jeu font 2 octets (comme MSVC). |
| `-mms-bitfields` | Disposition des champs de bits comme MSVC. |
| `-fno-builtin-wcslen` | Sinon clang remplace les boucles « compter jusqu'à 0 » par `wcslen` de la libc (4 octets). Voir « Findings worth keeping ». |
| `-fno-strict-return` | Deux fonctions du moteur ne retournent pas de valeur sur tous les chemins ; MSVC tolérait. |
| `-fsigned-char` | **Spécifique ARM** : `char` est non signé par défaut sur aarch64, signé sous MSVC/x86/wasm. |
| `-fwrapv` | Débordements d'entiers signés définis, comme le code MSVC d'origine les supposait. |
| `-fno-strict-aliasing` | Le code décompilé fait beaucoup de type-punning. |

Avec `-fshort-wchar`, **aucune** fonction `wcs*`/`wprintf` de la glibc ne doit être appelée :
utiliser les versions du port (`crt_compat.cpp`). `build_native.sh` le vérifie.

## Stratégie 64 bits (le point dur)

wasm32 et x86 ont des pointeurs de 4 octets ; aarch64 en a 8. Attendu :
- `GTA2_ASSERT_SIZEOF` qui échouent sur des structures contenant des pointeurs ;
- pointeurs rangés dans des `u32`/`s32`, ou différences de pointeurs tronquées ;
- offsets codés en dur, `memcpy` de structures, lecture de fichiers directement dans des structures.

Méthode :
1. Compiler avec `-Wshorten-64-to-32 -Wint-to-pointer-cast -Wpointer-to-int-cast -Wvoid-pointer-to-int-cast`
   et traiter ces avertissements en priorité.
2. Les asserts de taille : sous `GTA2_PORT_NATIVE`, une structure **qui n'est jamais lue/écrite
   telle quelle depuis un fichier ou le réseau** peut changer de taille → désactiver l'assert de
   cette structure seulement, avec une ligne dans `docs/native-64bit.md` expliquant pourquoi.
   Une structure sérialisée (sauvegardes `.svg`, fichiers `.gmp`/`.sty`/`.sdt`, paquets réseau)
   doit garder sa taille exacte : remplacer les pointeurs par des index/handles ou sérialiser
   champ par champ.
3. Valider sous `--sanitize` (ASan + UBSan) jusqu'au chargement de Downtown avant d'attaquer le rendu.
4. Chaque correction 64 bits = une entrée dans `docs/native-64bit.md` (structure/fonction, symptôme, correctif).

## Rendu (GLES 2.0)

- Contexte : `SDL_GL_CONTEXT_PROFILE_ES`, version 2.0, shaders `#version 100`.
  Pas de VAO, pas d'instancing, pas de textures entières ; textures NPOT seulement en
  `CLAMP_TO_EDGE` sans mipmaps.
- Traduire `web/renderer.js` (WebGL2) en C++ : même liste de draws batchée, 3 modes de mélange,
  UV correctes en perspective (passer w, laisser l'interpolation matérielle).
- Reprendre la logique de `port/render/gbh_web.cpp` : textures adressées par contenu
  (hash pixels + taille + palette), pages de style à pas de ligne de 256 octets.
- La table d'exports du renderer doit `static_cast` chaque fonction vers le typedef du jeu, comme en web.
- Résolution : espace de référence 480 px de haut (comme le web) ; 640×480 sur la plupart des
  consoles = 4:3 natif. Le widescreen (patch 0010) reste possible si l'écran est plus large.
- Viser 30 i/s d'abord. L'interpolation (patch 0015) est un bonus, à mesurer sur la console.

## Audio

- `SDL_OpenAudioDevice`, mixage dans le callback (ou `SDL_QueueAudio`), 22050 ou 44100 Hz stéréo.
- Porter de `web/audio.js` : banques PCM (volume, pan, pitch, boucles), décodeur IMA ADPCM
  (le vérifier échantillon par échantillon contre la version JS), radio depuis `data/GTAudio`.
- Bink : pas de vidéo, comme le web (« cannot open »).

## Entrées

- Clavier via événements SDL → couche DirectInput du port (touches tamponnées + verrouillées).
- Manette : `SDL_GameController`, avec un mapping par défaut adapté aux consoles portables.
  En attendant, `gptokeyb` (PortMaster) peut traduire la manette en touches.
- Prévoir une combinaison pour quitter proprement (ex. Select+Start). À noter que dans le cas de l'utilisation de `gptokeyb` cette combinaison est supportée de base.

## Fichiers et chemins

- Le jeu utilise des chemins Windows (`\`, casse mélangée) ; le système de fichiers Linux est
  sensible à la casse. Normaliser `\` → `/` et faire une recherche insensible à la casse
  (avec cache) dans la couche plateforme.
- Racine des données : variable d'environnement `GTA2_DATA_DIR`, sinon le dossier du binaire.
- Registre Windows : réutiliser le registre en mémoire du port, persisté dans un fichier texte
  à côté des sauvegardes (`$GTA2_SAVE_DIR`, sinon `./player/`).

## Test sur la console

```bash
DEVICE=root@10.10.0.128          # Mangmi Air X sur Rocknix 20261101 build
rsync -av build/native/debug/gta2 "$DEVICE":/roms/ports/gta2/
ssh "$DEVICE" 'cd /roms/ports/gta2 && SDL_VIDEODRIVER=kmsdrm ./gta2 2>&1 | tee log.txt'
```

Toujours écrire les logs sur stdout/stderr pour pouvoir les lire par SSH.

## Jalons

1. Build natif headless (pas de fenêtre) qui charge les données et entre dans Downtown sans crash,
   sous ASan/UBSan.
2. Corrections 64 bits jusqu'à un soak de plusieurs minutes sans erreur.
3. Fenêtre SDL2 + renderer GLES 2 : menu, puis Downtown.
4. Clavier, puis manette.
5. Audio : effets, voix, radio.
6. Paquet PortMaster (`packaging/portmaster/`), test sur au moins une console.