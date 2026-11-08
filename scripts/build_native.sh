#!/usr/bin/env bash
# build_native.sh — build natif Linux aarch64 (SDL2 + GLES2) du port GTA2.
#
# Appelé depuis macOS, il se relance tout seul dans le conteneur Docker
# ($GTA2_CONTAINER, par défaut gta2-pm), le dépôt étant monté sur /workspace.
#
#   ./scripts/build_native.sh [--preset debug|release] [--sanitize] [--headless]
#                             [--prepare] [--clean] [--jobs N] [--allow-unpinned]
set -euo pipefail

UPSTREAM_PIN="02de8be"
CONTAINER="${GTA2_CONTAINER:-gta2-pm}"
CONTAINER_ROOT="${GTA2_CONTAINER_ROOT:-/workspace}"

# Drapeaux qui DOIVENT apparaître dans chaque commande de compilation (voir CLAUDE.md).
REQUIRED_FLAGS=(
  -fshort-wchar
  -mms-bitfields
  -fno-builtin-wcslen
  -fno-strict-return
  -fsigned-char
  -fwrapv
  -fno-strict-aliasing
)

# ---------------------------------------------------------------------------
# Relance dans le conteneur si on est sur l'hôte
# ---------------------------------------------------------------------------
if [[ ! -f /.dockerenv && -z "${GTA2_IN_CONTAINER:-}" ]]; then
  command -v docker >/dev/null || { echo "docker introuvable" >&2; exit 1; }
  if [[ "$(docker inspect -f '{{.State.Running}}' "$CONTAINER" 2>/dev/null)" != "true" ]]; then
    echo "Le conteneur '$CONTAINER' n'est pas démarré : docker start $CONTAINER" >&2
    exit 1
  fi
  tty_flag=(); [[ -t 1 ]] && tty_flag=(-t)
  # ${a[@]+...} : le bash 3.2 de macOS refuse un tableau vide sous set -u.
  exec docker exec -i ${tty_flag[@]+"${tty_flag[@]}"} -e GTA2_IN_CONTAINER=1 -w "$CONTAINER_ROOT" \
       "$CONTAINER" ./scripts/build_native.sh "$@"
fi

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
PRESET="debug"; SANITIZE=0; HEADLESS=0; FORCE_PREPARE=0; CLEAN=0; ALLOW_UNPINNED=0
JOBS="$(nproc 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)          PRESET="$2"; shift 2 ;;
    --sanitize)        SANITIZE=1; shift ;;
    --headless)        HEADLESS=1; shift ;;
    --prepare)         FORCE_PREPARE=1; shift ;;
    --clean)           CLEAN=1; shift ;;
    --jobs|-j)         JOBS="$2"; shift 2 ;;
    --allow-unpinned)  ALLOW_UNPINNED=1; shift ;;
    -h|--help)         sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "Option inconnue : $1" >&2; exit 2 ;;
  esac
done

case "$PRESET" in
  debug)   BUILD_TYPE="Debug" ;;
  release) BUILD_TYPE="Release" ;;
  *) echo "--preset doit être debug ou release" >&2; exit 2 ;;
esac
if [[ $SANITIZE -eq 1 && "$PRESET" == "release" ]]; then
  echo "Note : --sanitize avec release, les traces seront moins lisibles." >&2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

UPSTREAM="research/gta2_re"
WORK="research/native-work/core"
BUILD_DIR="build/native/$PRESET"
[[ $SANITIZE -eq 1 ]] && BUILD_DIR="${BUILD_DIR}-san"
[[ $HEADLESS -eq 1 ]] && BUILD_DIR="${BUILD_DIR}-headless"

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m!!\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mxx\033[0m %s\n' "$*" >&2; exit 1; }

for tool in clang clang++ cmake ninja git python3; do
  command -v "$tool" >/dev/null || die "$tool manquant dans le conteneur (voir docker/Dockerfile)"
done
[[ "$(uname -m)" == "aarch64" ]] || warn "Architecture $(uname -m), pas aarch64 : le binaire ne tournera pas sur la console."

# ---------------------------------------------------------------------------
# 1. Vérifier l'upstream épinglé
# ---------------------------------------------------------------------------
[[ -d "$UPSTREAM/.git" || -f "$UPSTREAM/.git" ]] \
  || die "$UPSTREAM absent : git clone https://github.com/CriminalRETeam/gta2_re $UPSTREAM && git -C $UPSTREAM checkout $UPSTREAM_PIN"

git config --global --add safe.directory "$ROOT/$UPSTREAM" 2>/dev/null || true
HEAD_SHA="$(git -C "$UPSTREAM" rev-parse HEAD)"
if [[ "$HEAD_SHA" != "$UPSTREAM_PIN"* ]]; then
  [[ $ALLOW_UNPINNED -eq 1 ]] || die "$UPSTREAM est à ${HEAD_SHA:0:7}, attendu $UPSTREAM_PIN (ou --allow-unpinned)"
  warn "upstream non épinglé (${HEAD_SHA:0:7}) : les patches peuvent ne pas s'appliquer"
fi

# ---------------------------------------------------------------------------
# 2. Copie patchée (research/native-work/core)
# ---------------------------------------------------------------------------
shopt -s nullglob
PATCHES=(port/patches/*.patch port/patches/*.diff port/native/patches/*.patch port/native/patches/*.diff)
shopt -u nullglob
IFS=$'\n' PATCHES=($(printf '%s\n' "${PATCHES[@]}" | sort -V)); unset IFS

# Les patches web d'abord, puis les natifs, chacun dans l'ordre de leur numéro.
ordered=()
for p in "${PATCHES[@]}"; do [[ "$p" == port/patches/* ]] && ordered+=("$p"); done
for p in "${PATCHES[@]}"; do [[ "$p" == port/native/patches/* ]] && ordered+=("$p"); done
PATCHES=("${ordered[@]}")

[[ ${#PATCHES[@]} -gt 0 ]] || warn "Aucun patch trouvé dans port/patches/ : vérifier comment build_port.py les stocke."

# Le tampon vit dans le .git de la copie : il disparaît avec elle et ne salit pas son arbre.
STAMP_FILE="$WORK/.git/native-stamp"
STAMP="$(
  { echo "$HEAD_SHA"
    for p in "${PATCHES[@]}"; do sha1sum "$p"; done
    # if, pas « && » : sans le fichier, le groupe renverrait 1 et set -e + pipefail
    # arrêterait le script sans message.
    if [[ -f scripts/native/prepare_extra.sh ]]; then sha1sum scripts/native/prepare_extra.sh; fi
  } | sha1sum | cut -d' ' -f1
)"

if [[ $FORCE_PREPARE -eq 1 || ! -f "$STAMP_FILE" || "$(cat "$STAMP_FILE")" != "$STAMP" ]]; then
  log "Préparation de $WORK (upstream ${HEAD_SHA:0:7} + ${#PATCHES[@]} patches)"
  # Comme build_port.py : la copie est un clone git à part entière. Un simple extrait
  # (git archive) dans research/ ne convient pas : il est dans l'arbre de travail de
  # gta2-browser, où « git apply » résout les chemins depuis la racine de ce dépôt,
  # ignore Source/... sans rien dire et renvoie 0 sans avoir rien appliqué.
  # Le clone permet aussi d'écrire les patches natifs avec « git format-patch ».
  rm -rf "$WORK"; mkdir -p "$(dirname "$WORK")"
  git clone -q --shared --no-checkout "$UPSTREAM" "$WORK"
  # --shared écrit un chemin absolu vers les objets de l'upstream, qui diffère entre le
  # conteneur (/workspace/...) et macOS : on le rend relatif pour que la copie
  # reste lisible des deux côtés.
  realpath --relative-to="$WORK/.git/objects" "$UPSTREAM/.git/objects" > "$WORK/.git/objects/info/alternates"
  git -C "$WORK" checkout -q -b native-port "$HEAD_SHA"

  am() { git -C "$WORK" -c user.name="GTA2 port" -c user.email=port@localhost "$@"; }
  for p in "${PATCHES[@]}"; do
    case "$p" in
      *.patch)
        # git format-patch (mbox) : un commit par patch, avec son message.
        if ! am am -q --whitespace=nowarn "$ROOT/$p"; then
          am am --abort >/dev/null 2>&1 || true
          die "Échec d'application : $p"
        fi ;;
      *.diff)
        # Diff brut, sans en-tête : appliqué puis commité sous le nom du fichier.
        git -C "$WORK" apply --index --whitespace=nowarn "$ROOT/$p" || die "Échec d'application : $p"
        am commit -q -m "$(basename "$p" .diff)" ;;
    esac
    echo "   ok  $p"
  done
  # Garde-fou : chaque patch doit avoir ajouté un commit.
  applied=$(git -C "$WORK" rev-list --count "$HEAD_SHA"..HEAD)
  [[ "$applied" -eq ${#PATCHES[@]} ]] \
    || die "$applied commits pour ${#PATCHES[@]} patches dans $WORK"

  if [[ -x scripts/native/prepare_extra.sh ]]; then
    log "scripts/native/prepare_extra.sh"
    GTA2_WORK="$ROOT/$WORK" scripts/native/prepare_extra.sh
  fi
  echo "$STAMP" > "$STAMP_FILE"
else
  log "$WORK à jour"
fi

# ---------------------------------------------------------------------------
# 3. Configuration CMake
# ---------------------------------------------------------------------------
[[ -f port/native/CMakeLists.txt ]] \
  || die "port/native/CMakeLists.txt n'existe pas encore (jalon 1 de CLAUDE.md)."

[[ $CLEAN -eq 1 ]] && { log "Nettoyage de $BUILD_DIR"; rm -rf "$BUILD_DIR"; }

# Alias de casse des #include, à chaque build comme build_port.py (les fichiers de
# port/ changent sans nouvelle préparation). Même ordre d'includes que le CMakeLists.
CASE_ALIASES="$ROOT/build/native/case-aliases"
python3 scripts/native/case_aliases.py "$CASE_ALIASES" \
  port/native/compat port/core/compat "$WORK/Source" "$WORK" port

log "Configuration ($BUILD_TYPE$([[ $SANITIZE -eq 1 ]] && echo ', ASan+UBSan'))"
cmake -S port/native -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGTA2_CORE_DIR="$ROOT/$WORK" \
  -DGTA2_PORT_ROOT="$ROOT/port" \
  -DGTA2_CASE_ALIASES="$CASE_ALIASES" \
  -DGTA2_SANITIZE="$([[ $SANITIZE -eq 1 ]] && echo ON || echo OFF)" \
  -DGTA2_HEADLESS="$([[ $HEADLESS -eq 1 ]] && echo ON || echo OFF)"

ln -sf "$BUILD_DIR/compile_commands.json" compile_commands.json

# Les drapeaux obligatoires doivent être présents partout.
CC_JSON="$BUILD_DIR/compile_commands.json"
missing=()
for f in "${REQUIRED_FLAGS[@]}"; do
  total=$(grep -c '"file"' "$CC_JSON" || true)
  with=$(grep -c -- "$f" "$CC_JSON" || true)
  [[ "$with" -ge "$total" && "$total" -gt 0 ]] || missing+=("$f ($with/$total)")
done
[[ ${#missing[@]} -eq 0 ]] || die "Drapeaux obligatoires absents de certaines unités : ${missing[*]}"

# ---------------------------------------------------------------------------
# 4. Compilation
# ---------------------------------------------------------------------------
log "Compilation (-j$JOBS)"
cmake --build "$BUILD_DIR" -j "$JOBS"

BIN="$BUILD_DIR/gta2"
[[ -x "$BIN" ]] || die "Binaire introuvable : $BIN (la cible CMake doit s'appeler gta2)"

# ---------------------------------------------------------------------------
# 5. Vérifications du binaire
# ---------------------------------------------------------------------------
log "Vérifications"
file "$BIN" | sed 's/^/   /'

# Avec -fshort-wchar, aucune fonction wcs*/w*printf de la glibc ne doit être importée.
bad_wide=$(nm -D --undefined-only "$BIN" 2>/dev/null \
            | awk '{print $NF}' | grep -E '^(wcs|wmem|wprintf|swprintf|vswprintf|fwprintf)' || true)
if [[ -n "$bad_wide" ]]; then
  die "Le binaire importe des fonctions wchar de la glibc (4 octets) : $(echo $bad_wide)"
fi
echo "   ok  aucune fonction wchar de la glibc"

glibc_max=$(objdump -T "$BIN" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 || true)
echo "   GLIBC requise : ${glibc_max:-inconnue} (à comparer avec la console cible)"
if [[ -n "${GTA2_MAX_GLIBC:-}" && -n "$glibc_max" ]]; then
  need="${glibc_max#GLIBC_}"
  if [[ "$(printf '%s\n%s\n' "$need" "$GTA2_MAX_GLIBC" | sort -V | tail -1)" != "$GTA2_MAX_GLIBC" ]]; then
    die "GLIBC $need > GTA2_MAX_GLIBC=$GTA2_MAX_GLIBC"
  fi
fi

sdl_dep=$(objdump -p "$BIN" | awk '/NEEDED/ {print $2}' | grep -i sdl || true)
echo "   SDL : ${sdl_dep:-non lié dynamiquement (attendu : libSDL2-2.0.so.0)}"

# Release : un binaire sans symboles à distribuer et ses symboles à part (pour lire une
# trace de plantage). Nommé gta2.<arch> (gta2.aarch64), comme le lance le script PortMaster
# (./gta2.${DEVICE_ARCH}). La sortie du linker reste intacte.
if [[ "$PRESET" == "release" && $SANITIZE -eq 0 ]]; then
  DIST="$BUILD_DIR/dist"
  NAME="gta2.$(uname -m)"
  mkdir -p "$DIST"
  objcopy --only-keep-debug "$BIN" "$DIST/$NAME.debug"
  objcopy --strip-all --add-gnu-debuglink="$DIST/$NAME.debug" "$BIN" "$DIST/$NAME"
  needed=$(objdump -p "$DIST/$NAME" | awk '/NEEDED/ {print $2}' | tr '\n' ' ')
  echo "   dist/$NAME : $(du -h "$DIST/$NAME" | cut -f1), bibliothèques : $needed"
  if objdump -p "$DIST/$NAME" | grep -qE 'RPATH|RUNPATH'; then
    die "dist/$NAME a un RPATH/RUNPATH : $(objdump -p "$DIST/$NAME" | grep -E 'RPATH|RUNPATH')"
  fi
  if echo "$needed" | grep -qE 'libstdc\+\+|libgcc_s'; then
    die "dist/$NAME dépend encore de libstdc++/libgcc_s (attendu : liés en statique)"
  fi
  echo "   ok  pas de RPATH, runtime C++ inclus"
fi

log "OK : $BIN ($(du -h "$BIN" | cut -f1))"
