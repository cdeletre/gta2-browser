# Port natif Linux aarch64 : statut

Mis à jour le 8 novembre 2026. **Jalon 1 atteint en headless** : le binaire charge les
données et entre dans Downtown sous ASan/UBSan, sans erreur ASan. Jalon suivant : 2
(soak de plusieurs minutes, tri des rapports UBSan et des avertissements).

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
- Une ligne d'état toutes les 5 s (`port/native/platform/status_native.cpp`) : carte,
  position du joueur, véhicule.

## Ce qui ne marche pas encore, ou n'est pas vérifié

- **Pas vérifié :** que la simulation avance à 30 ticks/s. Le joueur ne bouge pas (aucune
  entrée), et environ 180 images par seconde sont présentées (le renderer headless ne
  limite rien ; l'interpolation du patch 0015 dessine entre les ticks). Il faut une
  mesure du nombre de ticks.
- **Pas vérifié :** que (159,5 ; 139,5 ; 2) est bien le départ à l'église du build web.
- **21 rapports UBSan distincts, non bloquants** (60 s dans Downtown) :
  - 17 « index out of bounds » sur des tableaux de taille fixe utilisés comme tableaux de
    longueur variable (`map_0x370.cpp` ×12, `gtx_0x106C.cpp` ×2, `PublicTransport.cpp`,
    `Gang.cpp`, `RollingDigitCounter_38.cpp`) : idiome du code d'origine, ASan ne voit
    aucun accès hors du bloc alloué ;
  - à regarder de près : `MapRenderer.cpp:3515` (index −1 dans `Nanobotz_8[1500]`) et
    `sound_obj.cpp:7152`/`7154` (index 5 dans `sound_7[5]`) ;
  - `Frontend.cpp:1529`/`1554` : méthodes appelées sur un `Frontend` nul
    (`intro_bik_exists_4B5FF0`, depuis `WinMain`) ;
  - `fix16.hpp:243` : conversion `double` → `int` hors bornes dans
    `arc_tan_table_init_4052D0` (−7,99e13). Pour une valeur négative, x86 et aarch64
    donnent tous les deux `INT_MIN` ; pour une valeur positive hors bornes, x86 donne
    `INT_MIN` et aarch64 `INT_MAX`.
- **Troncatures de pointeurs restantes :** toutes classées (`docs/native-64bit.md`,
  entrée 6) ; à revoir : `sound_obj.cpp:1398`, `KanjiFont_8EC.cpp` (japonais).
- **Compilation :** 117 avertissements de troncature d'entiers, pas encore triés.

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

## À reprendre plus tard (64 bits, hors jalon 1)

- `miles_web.cpp` `Id()` et `gbh_web.cpp:173` tronquent un pointeur en `int` pour
  nommer un son ou une texture côté JS. Sans conséquence en headless ; à revoir pour
  l'audio (jalon 5) et le renderer (jalon 3).

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

## Reste à faire

Points 1 à 5 : le jalon 1. Les points 3 et 4 ne le bloquent pas dans le conteneur (montage
insensible à la casse), mais ils sont nécessaires sur la console.

1. ~~Les asserts de taille et l'appel Miles~~ : fait.
2. ~~Le lien~~ : fait (ponts `gta2_js_*` vides, `net_web`/`map_web`/`coop_web`/
   `dev_hooks`/`options_web` compilés tels quels, `--gc-sections` comme le web).
2b. ~~Les textes `.gxt`, le style, la carte et les troncatures~~ : faits (entrées 6 à 11).
3. Recherche insensible à la casse des fichiers du jeu (aujourd'hui `crt_compat` met
   les chemins en minuscules, comme le stockage du shell web).
4. Registre persisté dans un fichier texte (`$GTA2_SAVE_DIR`, sinon `./player/`).
5. ~~Lancement sous `--sanitize` jusqu'à Downtown~~ : fait (60 s, 0 erreur ASan).
