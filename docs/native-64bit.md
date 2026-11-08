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
  | `text_0x14.cpp:107` | 1 | **corrigé**, entrée 7 |
  | `gtx_0x106C.cpp:479` | 1 | **corrigé**, entrée 8 |
  | `memory.cpp:36`, `Car_BC.cpp:226`, `Police_38.cpp:1079` | 1 chacun | **corrigés**, entrée 10 |
  | `sprite.hpp:51` | 1 | sans effet : `mpNext` y sert d'identifiant 32 bits (`SetCollisionId_446920`), la troncature ne perd rien |
  | `Draw.cpp:127` | 1 | sans effet : l'adresse de la surface logicielle remplit `gScreenTable`, que rien ne relit (le renderer est sur GPU) |
  | `registry.cpp:471` | 1 | sans effet en little-endian : `lpValueName` sert de tampon de 4 octets, les 4 octets bas contiennent la valeur lue |
  | `Frontend.cpp:102` | 1 | sans effet : relaie une valeur de registre de 32 bits |
  | `winmain.cpp:952` | 1 | sans effet : `net_web.cpp` ignore ce callback |
  | `winmain.cpp:2278` | 1 | sans effet : code de retour de `ShellExecuteA` |
  | `sound_obj.cpp:5246`, `:5522` | 2 | sans effet : l'adresse sert de nombre pseudo-aléatoire (hauteur, parité) |
  | `sound_obj.cpp:1398` | 1 | à revoir : `Car_BC*` rangé dans `field_4` (`u32`), jamais relu comme pointeur dans `sound_obj.cpp` |
  | `Bink.cpp` | 2 | sans effet tant que Bink est désactivé (`bink_web.cpp` : « cannot open ») |
  | `KanjiFont_8EC.cpp` | 2 | à corriger avant d'activer le japonais (pointeur passé en `s32` à `OutlineGlyph_4D2150`) |

## 7. Textes `.gxt` : clés lues directement dans une structure à pointeurs (observé)

- **Où :** `text_0x14::TKEY_Load_5B5960` et `TDAT_Offsets2Strings_5B59B0`.
- **Symptôme (ASan, au démarrage du menu) :** `heap-buffer-overflow`, un `fread` de
  31 140 octets dans un bloc de 31 128. La section TKEY est un tableau d'enregistrements
  de 12 octets (offset de la chaîne sur 4 octets, clé sur 8). Le jeu la lit
  directement dans un tableau de `text_0xC { wchar_t*; const char*; s32; }`, puis
  remplace chaque offset par un pointeur sur place. En LP64, `text_0xC` fait 24 octets :
  le nombre d'entrées calculé est faux, la lecture déborde, et les champs sont décalés.
- **Correctif (patch natif 0004) :** en natif, `text_0xC` garde un pointeur de 8 octets
  suivi de la clé en ligne (`field_4_pKey[4]` puis `field_8[4]`, contigus, donc
  `TKeySearch_5B5A10` et son `strcmp` marchent sans changement). `TKEY_Load_5B5960` lit
  les enregistrements de 12 octets dans un tampon et les convertit un par un.
- **Après :** plus de débordement ; le menu se crée.

## 8. Style `.sty` : index des sprites et deltas (patch natif 0005)

- **Où :** `gtx_0x106C::load_sprite_index_5AAF80`, `SetSpriteIndexDataPtrs_5AAC40`,
  `load_delx_5AAB30`, `build_delta_container_5AAC70`, `SetDeltaDataPtrs_5AABF0`.
- **Symptôme (lu dans le code, après le correctif de l'entrée 10) :**
  - SPRX est une suite d'enregistrements de 8 octets (offset dans les graphismes,
    largeur, hauteur), lus directement dans `sprite_index`, dont le premier champ devient
    un pointeur : même problème que l'entrée 7.
  - Le tampon des deltas (`field_50_delta_buffer`), construit en mémoire, est parcouru
    avec la taille 32 bits « `8 * count + 4` », alors que ses entrées contiennent des
    pointeurs ; le conteneur alloue `4 * maxSprite + 8` pour un tableau de pointeurs ;
    `SetDeltaDataPtrs_5AABF0` ajoute la base par jeu de types sur des `u32`.
- **Correctif :** versions natives de ces cinq fonctions, avec `offsetof`/`sizeof`
  (`GTA2_NATIVE_DELTAS_SIZE`) et `uintptr_t`. Le côté fichier (`delta_entry`,
  `2 * count + 4`) ne change pas.

## 9. Carte `.gmp` : colonnes et zones (patch natif 0006)

- **Colonnes, critique et silencieux :** `field_40008_pColumn` contient les « column
  words » du fichier (4 octets chacun), mais il était typé `u16**` et indexé directement
  (`&field_40008_pColumn[base]`). Le pas de l'index était donc la taille d'un pointeur :
  en LP64, chaque colonne aurait été lue 2 fois trop loin, sans aucun diagnostic. En
  natif, le champ est `u32*`. Trouvé en passant en revue les quatre champs `T**` du
  moteur ; les trois autres sont sans problème (`STexture**` est alloué avec `sizeof`,
  `car_info**` n'est lu que comme des octets, `SpriteTreeNode_C**` est un pointeur
  simple).
- **Zones :** `build_zone_list_4E90E0` allouait `4 * n + 4` pour un tableau de pointeurs
  (ASan : `heap-buffer-overflow`).
- **Ajouté :** asserts natifs sur `Map_sub` (0x324) et `gmp_block_info` (0xC), écrites
  dans la sauvegarde `.svg` à la suite de `SaveData_748`. Elles passent.

## 10. Pointeurs rangés dans des entiers de 32 bits (patch natif 0007)

- `Memory::Aligned_malloc_4FE510` alignait l'adresse avec `(u32)pointeur` : les 32 bits
  hauts disparaissaient (ASan : SEGV à `0x82590000` au chargement du style). En natif,
  le calcul se fait en `uintptr_t`.
- `Car_18::field_0_pScriptCmd` (une commande de script ou un `Generator_2C*`) et
  `Police_38::field_30` (un `Ped*`) sont des `s32` qui contiennent un pointeur ; en natif,
  ce sont des `intptr_t`. Les 11 relectures de `Car_BC.cpp` et celle de `Police_38.cpp`
  (avertissements `-Wint-to-pointer-cast`) compilent sans changement.

## 11. Tableaux de pointeurs alloués en octets 32 bits (patch natif 0008)

- `LightGrid::AllocGrid_4D6E00` allouait `0x4000` octets pour 4 096 `Light_28*`
  (ASan : `heap-buffer-overflow`). En natif : `4096 * sizeof(Light_28*)`.
- Recherche des autres allocations `4 * n` dans le moteur : il n'y avait que celles des
  entrées 8 et 9.
