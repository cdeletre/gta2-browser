# Transfert d'état (option 6) : analyse du code

Branche `native-multiplayer-experimental`, 10 novembre. Objectif de l'option 6 : garder le
lockstep, mais savoir envoyer l'état complet d'une partie. Usages : rattraper une
désynchronisation au lieu de finir la partie ; faire revenir un joueur parti. C'est aussi
la base d'un éventuel rollback (option 3). Rien n'est implémenté ici : ce document dit où
est l'état, ce qu'il pèse, et comment s'y prendre.

Les sondes utilisées (hors dépôt, sous `build/probe/`) : un programme qui affiche le `sizeof`
des pools et des gestionnaires sur aarch64, avec les drapeaux du build natif ; une bibliothèque
`LD_PRELOAD` qui compte les allocations du binaire pendant une partie gta2-lan dans le conteneur.

## Où est l'état d'une partie

| Partie | Quoi | Taille aarch64 (mesurée) | Créé quand |
|---|---|---|---|
| Pools | 33 pools à taille fixe (`Pool.hpp` : tableaux et listes chaînées libres/utilisés) : piétons, voitures, physique, IA, objets, sprites, particules, armes, grille de collision… | **1,93 Mo** : Sprite_4C 443 Ko, Sprite 402 Ko, Object_2C 245 Ko, Ped 155 Ko, GridCell 144 Ko, Char_B4 83 Ko, Car 81 Ko… | une fois, au début de la partie (`new XPool()`) |
| Gestionnaires | ~35 objets uniques créés par le constructeur de `Game_0x40` : scripts, lumières, HUD, police, transports, pathfinding, joueurs… | **≈ 1,15 Mo** : ScriptManager 794 Ko, Light 168 Ko, RouteFinder 57 Ko, ObjectDefinitions 37 Ko, Player 2,4 Ko × 6 | une fois, au début de la partie |
| Globales du moteur | ~1 650 `DEFINE_GLOBAL*`, plus 111 `static` dans les fichiers du moteur | dans le `.bss` (2 Mo en tout, port compris) | au chargement du binaire |
| Carte | `Map_0x370` et ses blocs chargés du `.gmp` (fichier de 1,1 Mo). **Modifiable en jeu** : portes (`Door_4D4` → `ChangeBlock`), scripts (`CHANGE_BLOCK`, `LOWER_LEVEL`) | quelques Mo (à mesurer) | au chargement de la carte |
| Style | sprites, palettes, définitions (`.sty`, 6,5 Mo) | lecture seule pendant la partie | au chargement |
| État du port | `net_web.cpp` (file des messages, séquences, entrées livrées), statics de `winmain.cpp` (anneau du délai d'entrée, horaire du tick), `port_coop.cpp` (équipes, monter ensemble), `coop_web.cpp` (scores), `dev_hooks.cpp` (historique de synchro), `rand` du port | petit | au début du match |
| Local, non simulé | caméra, HUD et messages, interpolation, textures GPU, sons, touches, index du joueur local | — | reconstruit ou gardé localement |

Ordre de grandeur : **environ 3 Mo bruts plus la carte**, très compressibles (les pools
sont surtout des cases libres). À mesurer : la taille compressée réelle d'un état de Downtown à
6 joueurs.

## Ce que le code montre d'utile

- **Le moteur alloue tout au début.** Pools et gestionnaires sont créés une seule fois, quand
  la partie commence. Pendant la partie, la sonde voit ~1 000 allocations par seconde, compensées
  par autant de libérations (mémoire stable). Elles passent par `operator new` → `crt::malloc`,
  le seul allocateur (`crt_stubs.cpp`, `new_delete_native.cpp`). Mais ce chemin sert aussi aux
  conteneurs C++ du port (audio, réseau, JSON), qui en font l'essentiel. La part propre au moteur
  n'est pas encore isolée (la remontée de pile de la sonde s'arrête dans les modèles `std`).
  À faire en premier.
- **Le binaire n'est pas en PIE** (chargé à `0x400000`). Avec le même binaire, les globales,
  les fonctions et les vtables ont la même adresse dans tous les processus. Seules les adresses
  du tas changent d'une console à l'autre.
- **Des pointeurs partout.** Listes des pools (`mpNext`), piéton → voiture, sprite → propriétaire,
  scripts → données du script, objets → style. Un état ne peut pas être copié tel quel d'un
  processus à l'autre sans traiter ces pointeurs.
- **Le web sait déjà le faire, sur une seule machine.** La sauvegarde rapide du navigateur
  (`savestate_web.cpp`, `web/savestate.js`) copie toute la mémoire WebAssembly entre deux images.
  Au chargement, elle reconstruit l'horloge, les textures (`Gta2Port_ReuploadTextures`), l'audio
  (`Gta2Port_AudioAfterRestore`), les touches et le minuteur d'images. Ça prouve qu'un
  « image mémoire + reconstruction de l'extérieur » redonne une partie qui marche. Le natif n'a
  pas encore ces deux fonctions de reconstruction.
- **Une image n'est pas portable entre architectures.** wasm32 a des pointeurs de 4 octets,
  aarch64 de 8, et les dispositions diffèrent. Une partie mixte console + navigateur ne peut pas
  échanger d'image mémoire.

## Les approches

| | Approche | Avantages | Inconvénients | Complexité |
|---|---|---|---|---|
| A | **Image mémoire, adresses fixes** : les allocations du moteur vont dans une arène `mmap` à une adresse fixe, dans le même ordre partout. L'état = l'arène + la section des globales du moteur (`DEFINE_GLOBAL` placées dans une section à part, sous `GTA2_PORT_NATIVE`) + l'état du port. | Générique : couvre tout le moteur sans connaître ses structures. Aucun pointeur à traduire. C'est l'approche du web, éprouvée. | Natif ↔ natif seulement, et même binaire exactement. Il faut séparer les allocations du moteur de celles du port (le même `operator new`). L'état local (caméra, HUD, joueur local) est à remettre après chargement. | Moyenne : 2 à 4 semaines |
| A' | **Image mémoire + relocation** : on copie les régions connues (pools, gestionnaires, carte, globales) avec leurs adresses d'origine. À l'arrivée, chaque mot de 8 octets aligné qui tombe dans une région d'origine est traduit vers la région locale. Les régions en lecture seule (style, code du script) ne sont pas envoyées, seulement traduites. | Pas d'arène à imposer. | Un entier qui ressemblerait par hasard à une adresse serait faussé (peu probable en 64 bits, pas impossible). Il faut la liste complète des régions. | Moyenne |
| B | **Sérialisation champ par champ** : chaque structure est écrite champ par champ, avec les pointeurs remplacés par (pool, index). Générable depuis les en-têtes avec un outil clang. | Portable, y compris avec le navigateur. État petit. | Des centaines de structures. Chaque oubli est une désynchronisation. L'outil de génération est un projet en soi. | Élevée : plusieurs mois |
| C | **Hybride** : A entre consoles, l'image wasm du web entre navigateurs, rien dans une partie mixte (comportement actuel). | Couvre la plupart des parties pour le coût de A. | Pas de rattrapage dans une partie mixte. | A + peu |

## Recommandation et étapes

C'est A (ou A' si l'arène s'avère difficile), vers C. Le cœur de la démarche ne demande pas
de réseau.

1. **Inventaire précis** (2 à 3 jours) : isoler les allocations du moteur pendant la partie
   (marquer `crt::malloc` par un drapeau pendant les ticks du moteur) ; lister les régions ;
   ranger les globales du moteur dans une section (`__start_`/`__stop_`) ; lister les statics
   du port à inclure.
2. **Aller-retour dans un seul processus** (3 à 5 jours) : sauvegarder au tick T, jouer jusqu'à
   T+300 en notant les contrôles de synchronisation, restaurer T, rejouer les mêmes entrées,
   comparer. Tout état oublié se voit comme une divergence. C'est le test qui prouve que l'état
   est complet, et la base du rollback.
3. **Entre deux processus** (2 à 4 jours) : restaurer dans un second gta2-lan (arène fixe ou
   relocation), remettre l'état local (index du joueur, caméra, HUD), reconstruire textures et
   sons (équivalents natifs de `Gta2Port_ReuploadTextures` et `Gta2Port_AudioAfterRestore`),
   comparer les contrôles.
4. **Rattrapage d'une désynchronisation** (3 à 5 jours) : quand un contrôle diffère, l'hôte
   annonce « état du tick T » (le message « au tick T » de l'étape 2 du lockstep). Il envoie
   l'image compressée en morceaux. Les autres s'arrêtent, chargent, rejouent les entrées
   reçues depuis T et repartent.
5. **Retour d'un joueur parti** (1 à 2 semaines, risqué) : le même transfert, puis recréer son
   `Player` et son piéton dans sa place. Le moteur ne sait pas faire entrer un nouveau joueur en
   cours de partie : le script de la carte dépend du nombre de joueurs (`downtown-2p.scr`…) ;
   seul le retour dans une place existante paraît réaliste.

## Points ouverts

- La part des ~1 000 allocations par seconde qui vient du moteur, et de quelles fonctions.
- La taille réelle des blocs de carte modifiables, et celle d'un état compressé.
- Le temps de chargement d'un état sur RK3326 (R36S) : une pause de quelques centaines de ms
  serait acceptable pour un rattrapage, pas pour un rollback.
- Les pointeurs du moteur vers des données du port ou des bibliothèques (sons, textures,
  fichiers) : à recenser pendant l'étape 1.
