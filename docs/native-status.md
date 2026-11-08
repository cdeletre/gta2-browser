# Port natif Linux aarch64 : statut

Mis à jour le 7 novembre 2026. Jalon en cours : **1** (build headless qui entre dans
Downtown sous ASan/UBSan). Pas encore de binaire.

## Ce qui marche (observé)

- **Préparation de la copie patchée** (`./scripts/build_native.sh --prepare`, conteneur
  `gta2-pm`, clang 10.0, glibc 2.31) : clone de l'upstream épinglé `02de8be` puis
  `git am` des 25 patches web, un commit chacun ; 52 fichiers modifiés, dont 4 créés
  (`Source/port_*.cpp`). Un second lancement réutilise la copie.
- **Configuration CMake** (`port/native/CMakeLists.txt`) : la vérification des
  drapeaux obligatoires de `build_native.sh` passe pour toutes les unités.
- **En-têtes de compat en LP64** : `DWORD`/`LONG`/`HRESULT` à 4 octets, `RECT` 16,
  `GUID` 16 (voir `docs/native-64bit.md`, entrée 1).

## Ce qui ne marche pas encore (observé au premier build complet, `ninja -k 0`)

82 fichiers sur 120 ne compilent pas, pour deux raisons :

- **Asserts de taille 64 bits** (structures contenant des pointeurs), à traiter une par
  une selon la règle de `CLAUDE.md` (sérialisée ou non) :
  - renderer : `STexture` (`d3ddll.hpp`) ; `SVideo`, `SDevice`, `SDisplayMode`
    (`DmaVideo.hpp`) ;
  - moteur : `Sprite_Pool`, `Ped`, `Ped_List_4`, `Car_Door_10`, `Car_BC`, `Camera_0xBC`,
    `CarInfo_808`, `miss2_0x11C`, `miss2_8`, `ScriptStackFrame_C`,
    `ScriptStackFramePool_25C`.
- **Miles** : `cSampleManager.cpp` (lignes 1037 et 1048) passe un `long*` à
  `AIL_stream_ms_position`, qui attend maintenant un `S32*` (4 octets).

Le build affiche aussi 120 avertissements de troncature (`-Wshorten-64-to-32` et
autres), pas encore triés. Le lien n'a pas encore été tenté : les symboles des fichiers
web non compilés (co-op, réseau, options, jobs) manqueront.

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

1. Les asserts de taille et l'appel Miles ci-dessus (une entrée chacun dans
   `docs/native-64bit.md`).
2. Le lien : implémentations natives des `gta2_js_*` (no-op en headless) et stubs pour
   les symboles des fichiers web non compilés.
3. Recherche insensible à la casse des fichiers du jeu (aujourd'hui `crt_compat` met
   les chemins en minuscules, comme le stockage du shell web).
4. Registre persisté dans un fichier texte (`$GTA2_SAVE_DIR`, sinon `./player/`).
5. Lancement sous `--sanitize` jusqu'à Downtown.
