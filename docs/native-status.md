# Port natif Linux aarch64 : statut

Mis à jour le 8 novembre 2026. **Jalons 1 et 2 atteints, sans fenêtre** : le binaire entre
dans Downtown, et des parties jouées au hasard tiennent 15 minutes sous ASan/UBSan sans
erreur ASan. Jalon suivant : 3 (fenêtre SDL2 et renderer GLES 2).

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
  les sauvegardes, les deux autres districts, le réseau.
- **Rapports UBSan restants, non bloquants :** 29 distincts sur les cinq soaks, tous
  classés sans lien avec le 64 bits (`docs/native-64bit.md`, entrée 15).
- **Avertissements de compilation :** 40 distincts, tous classés sans conséquence (même
  entrée).

Avant d'en arriver là, les soaks ont trouvé cinq défauts, corrigés par les patches natifs
0009, 0011, 0012, 0013 et par `-fno-sanitize=return` (entrées 12 à 14 du journal 64 bits,
et « Écarts par rapport à l'original » ci-dessous).

## Écarts par rapport à l'original (natif seulement)

Les deux remplacent une lecture hors de son objet, dont le résultat dépendait de ce qui
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
- Le build release (seul le debug, avec et sans sanitizer, a tourné).
- Le binaire sur la console (jalon 6), dont les chemins sensibles à la casse.

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
