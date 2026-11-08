# Port natif aarch64 : journal des corrections 64 bits

wasm32 et Win32 ont des pointeurs et des `long` de 4 octets. Linux aarch64 est LP64 :
pointeurs **et `long`** de 8 octets. Chaque correction ci-dessous indique la structure
ou la fonction, le symptôme (observé ou mesuré, sinon dit comme tel) et le correctif.

Règle pour les asserts de taille : voir « Stratégie 64 bits » dans `CLAUDE.md`.

## 1. Types Win32 à base de `long` (en-têtes de compat)

- **Où :** `port/core/compat/windows.h` (`DWORD`, `LONG`, `ULONG`, `HRESULT`),
  `port/core/compat/io.h` (`_finddata_t::size`), et `U32`/`S32`/`REALFAR` de
  `Source/3rdParty/mss.h` (Miles), qui valent `unsigned long`/`signed long` sauf
  s'ils sont déjà définis.
- **Symptôme (mesuré avec clang 10 dans `gta2-pm`, avant le jeu) :** `DWORD`, `LONG` et
  `HRESULT` faisaient 8 octets ; `RECT` 32 au lieu de 16, `GUID` 24 au lieu de 16,
  `_finddata_t::size` 8. Tout ce qui est construit dessus changeait de disposition :
  structures DirectInput/DirectDraw, valeurs `REG_DWORD` du registre (tampons de
  4 octets), champs `DWORD` des structures du jeu, signatures Miles.
- **Correctif :** sous `__LP64__` seulement, ces types passent à `int`/`unsigned int`
  (4 octets, comme sous Win32 et Win64). `port_prefix.h` prédéfinit `U32`, `S32` et
  `REALFAR` pour que `mss.h` garde ses propres `#ifndef`. Des `static_assert`
  (`GTA2_COMPAT_SIZE_CHECK`) dans `windows.h` vérifient `DWORD`, `LONG`, `ULONG`,
  `HRESULT`, `BOOL` (4) et `WCHAR` (2) en LP64.
- **Build web :** inchangé. La sortie du préprocesseur pour wasm32 de `port_prefix.h`,
  `windows.h` et `io.h` est identique octet pour octet avant et après (vérifié par
  `clang -E --target=wasm32-unknown-emscripten`). emsdk n'étant pas installé, le build
  web complet n'a pas été relancé.
- **Après :** `DWORD`/`LONG`/`HRESULT` 4, `RECT` 16, `GUID` 16, `_finddata_t::size` 4
  sur aarch64.
- **Reste à surveiller :** les 42 `long` du moteur lui-même (`Source/*.cpp`, `*.hpp`),
  qui ne passent pas par ces en-têtes.

## 2. Asserts de taille des structures en mémoire seulement (patch natif 0001)

- **Où :** `Camera_0xBC`, `Car_BC`, `Car_Door_10`, `CarInfo_808`, `Ped`, `Ped_List_4`,
  `ScriptStackFrame_C`, `ScriptStackFramePool_25C`, `miss2_0x11C`, `miss2_8`,
  `Sprite_Pool`.
- **Symptôme (compilation) :** leur `GTA2_ASSERT_SIZEOF_ALWAYS` échoue sur aarch64 :
  toutes contiennent des pointeurs, directement ou via des structures membres.
- **Pourquoi on peut les laisser grandir :** aucune n'est lue ni écrite telle quelle
  (fichier, sauvegarde, réseau), et aucune taille n'apparaît en dur dans le code
  (recherche de `0x294`, `0xBC`, `0x49B28`, `0x808`, `0x11C`, `0x25C` hors asserts :
  les seuls `0xBC` sont un identifiant d'objet, un commentaire et une valeur d'enum).
  `CarInfo_808` contient deux tableaux de 256 pointeurs ; l'enregistrement lu dans le
  fichier `.gci`, `ModelPhysics_48`, garde son assert (0x48) et il passe.
- **Correctif :** ces 11 asserts sont sous `#if !defined(GTA2_PORT_NATIVE)`.
- **Ajouté :** asserts natifs sur les deux structures écrites sur disque telles quelles,
  `SaveData_748` (sauvegarde `.svg`) et `PlyDat_2BC0` (données du joueur). Ni l'une ni
  l'autre ne contient de pointeur ; les deux passent.

## 3. Interface du renderer (`port/core/compat/3rdParty/GTA2Hax`)

- **Où :** `STexture` (`d3ddll.hpp`), `SVideo`, `SDevice`, `SDisplayMode` (`DmaVideo.hpp`).
- **Symptôme (compilation) :** leurs `static_assert(... "layout")` échouent : elles
  contiennent des pointeurs.
- **Pourquoi on peut les laisser grandir :** elles ne servent qu'entre le jeu et le
  renderer du port, compilés ensemble ; le jeu y accède par nom de champ. Rien ne les
  lit dans un fichier.
- **Correctif :** asserts sous `#if !defined(GTA2_PORT_NATIVE)`.

## 4. Miles : position des flux (patch natif 0002)

- **Où :** `cSampleManager::GetVocalPosMs_58E770` et `GetVocalLengthMs_58E7A0`.
- **Symptôme (compilation) :** le paramètre `long stream_idx` est réutilisé comme sortie
  de `AIL_stream_ms_position`, qui attend un `S32*` (4 octets) ; `long*` est refusé.
  Sans le correctif de l'entrée 1, Miles aurait écrit 4 octets dans un `long` de 8.
- **Correctif :** en natif, la position passe par une variable locale `S32`.

## 5. Pointeurs dans les commandes de script `.scr` (patch natif 0003)

- **Où :** `Source/miss2_0x11C.hpp`. Le script est chargé comme des octets
  (`ScriptManager_C1EA8::field_334C_script_data`), et ses commandes sont lues en place
  par les structures `SCR_*`. Dix d'entre elles gardent un pointeur d'exécution dans une
  case de 4 octets de la commande : `SCR_POINTER` (union de 10 types), `SCR_PLAYER_PED`,
  `SCR_CHAR_DATA_DEC`, `SCR_OBJ_DATA`, `SCR_THREAD`, `SCR_CHAR_AREA_ANY`,
  `SCR_CRANE_TARGET_DEC`, `SCR_CRANE_BASIC_DEC`, `SCR_DOOR_DATA_DEC`, `SCR_ARROW_ENTITY`.
- **Symptôme (déduit du code, aucun assert ne le couvrait) :** avec des pointeurs de
  8 octets alignés sur 8, tous les champs suivants se décalent. Par exemple,
  `SCR_PLAYER_PED::field_C_pos` serait lu à 0x10 au lieu de 0xC. Chaque écriture du
  pointeur écraserait aussi 4 octets de la commande suivante. Cela aurait compilé sans
  erreur, avec une corruption silencieuse des scripts.
- **Correctif :** ces cases deviennent `GTA2_SCR_PTR(T)` (`Source/port_scr_ptr.hpp`).
  C'est un simple `T*` hors du natif LP64, et dans le natif un handle de 4 octets vers
  une table de pointeurs (`port/native/platform/scr_handles.cpp`). Le handle 0 vaut
  `nullptr`, donc une case à zéro (comme dans le fichier) se lit toujours « pas
  d'objet ». Un handle inconnu arrête le jeu avec un message plutôt que de suivre un
  pointeur invalide. Des asserts natifs `offsetof` vérifient chaque champ contre
  l'offset donné par son nom, et `sizeof(SCR_POINTER) == 0xC`.
  - Environ 590 accès compilent sans changement grâce aux conversions implicites. Un
    seul cast C (`ScriptManager_C1EA8::AddCranePowerupTrigger_511B10`) devait passer par
    le type de la case avant de devenir `SCR_THREAD*`.
- **Pas encore observé à l'exécution :** à vérifier au chargement de Downtown. Si la
  table « handle inconnu » se déclenche, le jeu lit une case via un membre de l'union
  qui n'y a jamais été écrit comme pointeur.
- **Reste possible :** d'autres valeurs écrites dans le tampon du script par un autre
  chemin que les structures `SCR_*` ; aucune n'a été trouvée par recherche dans le code.
  *Correction (entrée 6) :* j'avais d'abord écrit que clang refuserait de toute façon un
  cast d'un pointeur vers un entier de 4 octets. C'est faux avec `-fms-extensions`.

## 6. Troncatures de pointeurs invisibles avec `-fms-extensions` (méthode)

- **Constat (clang 10, `gta2-pm`) :** avec `-fms-extensions`, nécessaire au moteur,
  `(int)p` et `reinterpret_cast<int>(p)` compilent **sans aucun diagnostic**, même avec
  `-Weverything`. Sans l'extension, ce sont des erreurs. Les avertissements
  `-Wpointer-to-int-cast` du CMake ne les voient donc pas, et `-Wmicrosoft-cast` non plus.
- **Outil :** `scripts/native/scan_pointer_truncation.py build/native/debug` (dans le
  conteneur). Il relance chaque commande de `compile_commands.json` en `-fsyntax-only`
  sans `-fms-extensions`, et ne garde que les erreurs « cast from pointer to smaller type ».
- **Premier passage (7 novembre 2026) : 47 troncatures** dans 17 fichiers. S'y ajoutent
  14 avertissements `-Wint-to-pointer-cast` dans le sens inverse (un pointeur reconstruit
  depuis un `s32`/`u32`) : `Car_BC.cpp` (11, `Car_18_Cmd*` et `Generator_2C*`),
  `Police_38.cpp:1058` (`Ped*`), `gtx_0x106C.cpp:440` (`delta_store_entry*`),
  `sprite.hpp:46` (`Sprite_4C*`).

  | Fichier | Troncatures | Première lecture |
  |---|---|---|
  | `error.cpp` | 25 | pointeurs passés aux messages d'erreur (affichage), à confirmer |
  | `char.cpp` | 6 | à analyser |
  | `Bink.cpp`, `KanjiFont_8EC.cpp`, `sound_obj.cpp` | 2, 2, 3 | à analyser |
  | `text_0x14.cpp:107` | 1 | **critique, voir l'entrée 7** |
  | `sprite.hpp:51`, `gtx_0x106C.cpp:479` | 1, 1 | pointeur rangé dans un `u32`, probablement critique |
  | `Car_BC`, `Draw`, `Frontend`, `Police_38`, `memory`, `registry`, `winmain` (×2) | 1 chacun | à analyser |

## 7. Textes `.gxt` : clés lues directement dans une structure à pointeurs (observé)

- **Où :** `text_0x14::TKEY_Load_5B5960` et `TDAT_Offsets2Strings_5B59B0`.
- **Symptôme (ASan, au démarrage du menu) :** `heap-buffer-overflow`, un `fread` de
  31 140 octets dans un bloc de 31 128. La section TKEY est un tableau d'enregistrements
  de 12 octets (offset de la chaîne sur 4 octets, clé sur 8). Le jeu la lit
  directement dans un tableau de `text_0xC { wchar_t*; const char*; s32; }`, puis
  remplace chaque offset par un pointeur sur place. En LP64, `text_0xC` fait 24 octets :
  le nombre d'entrées calculé est faux, la lecture déborde, et les champs sont décalés.
- **Correctif :** pas encore fait (prochaine étape).
