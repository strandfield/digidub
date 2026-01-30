
# Tutoriel

Le but de ce tutoriel est de présenter les bases de l'utilisation du logiciel dans un cas simplifié.

## Prérequis

Il est nécessaire d'avoir les programmes `ffmpeg` et `mkvmerge` dans le `PATH` pour qu'ils puissent
être invoqués en ligne de commande.

Les fichiers fournis pour ce tutoriel sont les suivants.
- `1-eng.mp4`
- `1-fre.mp4`
- `1.txt` (fichier de projet digidub)

Les fichiers mp4 sont des extraits du premier épisode de la saison 2 de Digimon, respectivement
de la version anglaise et de la française.

Ces extraits ont été produits via les commandes `ffmpeg` suivantes:

```
ffmpeg -ss 1:35 -to 2:40 -i Digimon.S2.E01.mkv -c:v libx264 -profile:v main -level 3.1 -preset medium -crf 24 -x264-params ref=4 -c:a aac -movflags +faststart 1-eng.mp4
ffmpeg -ss 1:46 -to 3:16 -i Digi2x01.mkv -c:v libx264 -profile:v main -level 3.1 -preset medium -crf 24 -x264-params ref=4 -c:a aac -movflags +faststart 1-fre.mp4
```

L'objectif de ce tutoriel est de "recaler" l'audio de la vidéo en français sur la vidéo anglaise, 
pour ainsi obtenir une vidéo ayant 2 pistes audio : une piste "version originale" (anglais) et une 
piste "remix/redub" en français.

Avant de commencer, il y a quelques détails qu'il est intéressant de noter.

Premièrement, les deux vidéos n'ont pas les mêmes dimensions. 
La version anglaise a une image en 704x560 tandis que la version française est en 640x480.
C'est l'une des raisons pour lesquelles on veut recaler l'audio français sur la vidéo 
anglaise et non l'inverse : la version anglaise est *a priori* de meilleure qualité.

Deuxièmement, l'extrait français est dans cet exemple un peu plus long et recouvre entièrement
l'extrait anglais. 
On devrait donc pouvoir entièrement faire le doublage de la version anglaise.

On notera cependant en regardant les commandes `ffmpeg` ci-dessus que bien que l'extrait français
soit plus long, son timestamp dans la vidéo complète démarre à 1 minute 46, contre 1 minute 35 
pour l'extrait anglais.
Le premier épisode de la version française démarre en effet par un segment introductif 
récapitulant l'évolution des personnages depuis la fin de la première saison ; 
séquence qui est totalement absente de la version anglaise (qui démarre directement par le générique).
Inversement, on trouve parfois des séquences dans la version anglaise qui ne sont pas présentes dans 
la version française, et que l'on ne pourra donc pas doubler.
Ce cas de figure n'est pas traité dans ce tutoriel, mais est bien supporté par le logiciel.

## Création d'un projet

Nous allons dans un premier temps voir comment créer un fichier de projet, c'est-à-dire comment 
produire le fichier `1.txt`.

Le contenu de ce fichier devrait ressembler à ça:

```
DIGIDUB PROJECT
VERSION 1
TITLE Digimon S2E01 - Enter Flamedramon
VIDEO 1-eng.mp4
AUDIO 1-fre.mp4
OUTPUT 1-out.mkv
BEGIN MATCHLIST (1)
0:00.000-1:05.000~0:14.040-1:21.763
END MATCHLIST
```

La création d'un fichier de projet se fait pour l'instant via le programme en ligne de commande 
`digidub-cli`.

Dans le cas présent, la commande suivante a été utilisée:

```
digidub-cli.exe create --detect-matches --output 1-out.mkv -i 1-eng.mp4 -i 1-fre.mp4
```

Explications:
- la commande "create" permet de créer un projet
- l'option "--output" permet de spécifier le nom du fichier de sortie lors de l'export
- les deux vidéos sont données en entrées avec "-i" (l'ordre est important)
- l'option "--detect-matches" permet de lancer une recherche de scènes correspondantes dans les deux vidéos.

Cette commande devrait produire une sortie similaire à la suivante:

```
ffprobe -v 0 -select_streams v:0 -count_packets -show_entries stream=r_frame_rate,nb_read_packets -show_entries format_tags -show_entries format=duration 1-eng.mp4
ffprobe -v 0 -select_streams v:0 -count_packets -show_entries stream=r_frame_rate,nb_read_packets -show_entries format_tags -show_entries format=duration 1-fre.mp4
Extracting frames for 1-eng.mp4...
Extracting frames for 1-fre.mp4...
Detecting silences on  1-eng.mp4...
ffmpeg -nostats -hide_banner -i 1-eng.mp4 -map 0:1 -af silencedetect=n=-35dB:d=0.4 -f null -
detecting silences...
Detecting black frames on  1-eng.mp4...
ffmpeg -nostats -hide_banner -i 1-eng.mp4 -map 0:0 -vf blackdetect=d=0.4:pix_th=0.05 -f null -
detecting back frames...
Detecting scene changes on  1-eng.mp4...
ffmpeg -nostats -hide_banner -i 1-eng.mp4 -map 0:0 -vf scdet -f null -
detecting scene changes...
S: 1-eng.mp4[0:00-1:05] A:1-fre.mp4[0:00-1:30.003]
 > 1-eng.mp4[0:00-0:01.44] ~ 1-fre.mp4[0:14.1205-0:15.5605]
  >> 1-eng.mp4[0:00-1:05] ~ 1-fre.mp4[0:14.1205-1:21.7627]
  >>> 1-eng.mp4[0:00-1:05] ~ 1-fre.mp4[0:14.0405-1:21.7627]
DIGIDUB PROJECT
VERSION 1
TITLE Digimon S2E01 - Enter Flamedramon
VIDEO 1-eng.mp4
AUDIO 1-fre.mp4
OUTPUT 1-out.mkv
BEGIN MATCHLIST (1)
0:00.000-1:05.000~0:14.040-1:21.763
END MATCHLIST
```

Que se passe-t-il ?
Le programme commence par extraire les frames (c'est-à-dire les images) des 2 vidéos.
Puis il lance une détection des silences, des images complètement noires (transition entre 
deux scènes) et des changements de scène.
Ces informations permettent de découper la vidéo anglaise en séquences dont on va chercher
des correspondances dans la version française.

Le bloc suivant décrit le résultat de cette recherche:

```
S: 1-eng.mp4[0:00-1:05] A:1-fre.mp4[0:00-1:30.003]
 > 1-eng.mp4[0:00-0:01.44] ~ 1-fre.mp4[0:14.1205-0:15.5605]
  >> 1-eng.mp4[0:00-1:05] ~ 1-fre.mp4[0:14.1205-1:21.7627]
  >>> 1-eng.mp4[0:00-1:05] ~ 1-fre.mp4[0:14.0405-1:21.7627]
```

Ligne 1: on cherche une correspondance dans la partie de la vidéo anglaise démarrant à 0:00 et
terminant à 1:05 (donc la vidéo complète) dans l'entièreté de la vidéo française.

Ligne 2: une première correspondance est trouvée entre la séquence 0:00-0:01.44 de la vidéo 
anglaise et la séquence 0:14.1205-0:15.5605 de la vidéo française.

Ligne 3: cette correspondance est étendu, scène par scène, jusqu'à couvrir l'enitèreté de la 
vidéo anglaise.

Ligne 4: quelques derniers ajustements sont effectués, la plage dans la vidéo française 
passe de 0:14.**1205**-1:21.7627 à 0:14.**0405**-1:21.7627 ; on a donc rajouté quelques frames
de la vidéo française.

Cette dernière ligne met en avant deux subtilités supplémentaires de la tâche que l'on souhaite réaliser.

Tout d'abord, les deux vidéos n'ont pas le même framerate. 
La version anglaise tourne à 25 images par secondes tandis que la version française est à 
24.999. 
On a donc environ une image de différence toutes les 40 secondes.
(25 images par secondes = une image toutes les 0.04 secondes ; que l'on multiplie par 1000 pour
obtenir 25000 images contre 24999).
Si l'on veut obtenir un résultat précis, on ne peut donc pas se contenter de faire une comparaison
d'image une à une.

L'autre subtilité est encore plus surprenante !
Si l'on regarde la correspondance trouvée par le logiciel, on peut remarquer que le segment
dans la vidéo anglaise dure 65 secondes, tandis que celui dans la version française dure
un peu plus de 67 secondes (de 14 secondes à 1 minute 21).
Ce n'est pas une erreur : la version française est bien plus "lente" que la version anglaise
et, au moment de refaire le mixage, le logiciel va accélérer la piste française pour que 
ces 67 secondes en deviennent 65.

Les dernières lignes de la sortie correspondent au fichier du projet.
Ces lignes, contrairement aux précédentes, sont écrites sur la sortie standard.
On peut donc les écrire dans un fichier en faisant une redirection.

```
digidub-cli.exe create --detect-matches --output 1-out.mkv -i 1-eng.mp4 -i 1-fre.mp4 > 1.txt
```

Attention cependant, digidub suppose que les fichiers sont écrits en UTF-8.
Il faut donc prendre garde si l'on utilise PowerShell à ce que le fichier ne soit pas écrit en UTF-16.

Avec ce fichier de projet, il est possible d'effectuer le mixage avec la commande suivante.

```
digidub-cli.exe export 1.txt
```

Cette commande produit le fichier `1-out.mkv` avec les deux pistes audio.

## Edition de la liste des correspondances

Dans le cadre de ce tutoriel, la correspondance trouvée par le logiciel convient très bien.
De manière générale, le logiciel fait globalement un travail convenable ; mais il est cependant
fréquent de devoir faire quelques retouches à la main.

C'est là qu'intervient `matcheditor`.
Cet outil permet de visualiser les correspondances trouvées par `digidub-cli`, de les modifier
et d'en chercher des supplémentaires.

`matcheditor` permet d'ouvrir un fichier de projet et affiche alors côte-à-côte les deux vidéos.

![Screenshot of matcheditor](screenshots/matcheditor-1.png)

Après l'ouverture du projet, il est possible de parcourir les correspondances en cliquant sur les 
liens "< Prev" et "Next >" respectivement au milieu à gauche et au milieu à droite de la fenêtre.
Alternativement, le menu "View > Match list" permet d'afficher une petite fenêtre flottante listant
l'ensemble des correspondances.

Les frames appartenant à la correspondance courante sont affichées avec un fond vert dans la liste 
des frames.
Il est possible de d'éditer le début et la fin du match en faisant un clic droit sur une frame, 
puis en sélectionnant "Set as match begin" ou "Set as match end" dans le menu contextuel.

Le lien "Preview", au centre de la fenêtre, permet de prévisualiser la correspondance actuellement
sélectionnée.

Le menu "Edit > Delete current match" permet de supprimer le match courant.
Alternativement, on peut supprimer un match depuis la fenêtre des matchs avec la touche Suppr.

Une fois les modifications effectuées, il faut penser à les sauvegarder via le menu "File > Save".

Enfin, la vidéo peut être exportée directement depuis `matcheditor` via "File > Export". 
