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

## Ce qui ne marche pas encore (observé)

- **Compilation : tout passe.** Après `--prepare --clean`, les 120 unités compilent
  (25 patches web + 3 patches natifs, voir `docs/native-64bit.md`, entrées 2 à 5).
  117 avertissements de troncature (`-Wshorten-64-to-32` et autres), pas encore triés.
- **Lien : 123 références indéfinies**, dans trois groupes :
  - les ponts `gta2_js_*` (textures, `present`, sons, fichiers) : à implémenter en natif,
    vides au jalon 1 ;
  - le réseau du jeu (`NetPlay::*`, `Network_20324::*`, `gNetPlay_7071E8`,
    `gGta2PortNetInputDelay`) et `gGta2PortHideHudArrows`, définis dans des fichiers
    web non compilés ;
  - quelques fonctions Win32 (`WideCharToMultiByte`, `lstrcatA`) et `ImGuiDebugDraw`.

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
2. Le lien : implémentations natives des `gta2_js_*` (no-op en headless) et stubs pour
   les symboles des fichiers web non compilés.
3. Recherche insensible à la casse des fichiers du jeu (aujourd'hui `crt_compat` met
   les chemins en minuscules, comme le stockage du shell web).
4. Registre persisté dans un fichier texte (`$GTA2_SAVE_DIR`, sinon `./player/`).
5. Lancement sous `--sanitize` jusqu'à Downtown.
