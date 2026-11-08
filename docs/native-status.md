# Port natif Linux aarch64 : statut

Mis à jour le 7 novembre 2026. Jalon en cours : **1** (build headless qui entre dans
Downtown sous ASan/UBSan). Le binaire se construit et démarre ; il n'atteint pas encore
le menu.

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
- **Lancement sans fenêtre** (`GTA2_DATA_DIR=/workspace/game GTA2_AUTOSTART=1`,
  dans le conteneur) : les tables statiques, le registre, le son (muet) et les fichiers
  de langue se chargent, puis le jeu crée le menu.

## Ce qui ne marche pas encore (observé)

- **Sous ASan/UBSan, le jeu s'arrête à la création du menu :** débordement de tas à la
  lecture des clés du `.gxt` (`docs/native-64bit.md`, entrée 7). Sans sanitizer, le jeu
  fait un segfault juste après le chargement des langues ; l'endroit exact n'a pas été
  vérifié (le débordement du `.gxt` est le premier suspect).
- **Rapports UBSan non bloquants, vus avant :**
  - `Frontend.cpp:1554` et `:1529` : méthodes appelées sur un `Frontend` nul
    (`intro_bik_exists_4B5FF0`, depuis `WinMain`), à analyser ;
  - `fix16.hpp:243` : conversion `double` → `int` hors bornes dans
    `arc_tan_table_init_4052D0` (−7,99e13). Pour une valeur négative, x86 et aarch64
    donnent tous les deux `INT_MIN` ; pour une valeur positive hors bornes, x86 donne
    `INT_MIN` et aarch64 `INT_MAX`. À vérifier si le cas se présente.
- **47 troncatures de pointeurs** à analyser (`docs/native-64bit.md`, entrée 6).
- **Compilation :** 117 avertissements de troncature d'entiers, pas encore triés.

## Corrigé en route

- Le dossier `player/` est créé au démarrage (le jeu y écrit `plyslot0.dat` ; sans lui,
  « Fatal Error 16 »). La redirection vers `$GTA2_SAVE_DIR` reste à faire.
- `AIL_release_sample_handle(NULL)` (patch web absent ; correctif natif dans
  `port/audio/miles_web.cpp`) : `cSampleManager::Terminate_58DAE0` libère des cases
  encore vides. Miles l'ignore ; le port déréférençait le pointeur. **Le build web a
  le même défaut** : en wasm, l'accès nul tombe dans la mémoire linéaire (écriture
  silencieuse à l'adresse 0x30). Il n'est pas corrigé côté web, parce que changer le
  code du jeu web invalide les sauvegardes du navigateur (règle des sauvegardes).
- `-shared-libasan` sous `--sanitize` : le moteur définit ses propres `operator new`
  et `delete` (`crt_stubs.cpp`, qui remettent à zéro via `calloc`, comme le jeu
  l'attend), et ceux de la bibliothèque ASan statique entraient en conflit.

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

## Reste à faire pour le jalon 1

1. ~~Les asserts de taille et l'appel Miles~~ : fait.
2. ~~Le lien~~ : fait (ponts `gta2_js_*` vides, `net_web`/`map_web`/`coop_web`/
   `dev_hooks`/`options_web` compilés tels quels, `--gc-sections` comme le web).
2b. Les textes `.gxt` (entrée 7), puis les 47 troncatures (entrée 6).
3. Recherche insensible à la casse des fichiers du jeu (aujourd'hui `crt_compat` met
   les chemins en minuscules, comme le stockage du shell web).
4. Registre persisté dans un fichier texte (`$GTA2_SAVE_DIR`, sinon `./player/`).
5. Lancement sous `--sanitize` jusqu'à Downtown.
