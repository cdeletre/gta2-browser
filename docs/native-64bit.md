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
