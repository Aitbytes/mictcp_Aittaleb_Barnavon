# BE RESEAU
## TPs BE Reseau - 3 MIC Barnavon Jules, Aittaleb Mohamed


## Contenu du dépôt 
### Fichiers fournis
Ce dépôt inclut le code source initial fourni pour démarrer le BE. Plus précisément : 
  - README.md (ce fichier) 
  - tsock_texte et tsock_video : lanceurs pour les applications de test fournies. 
  - dossier include : contenant les définitions des éléments fournis que vous aurez à manipuler dans le cadre du BE.
  - dossier src : contenant l'implantation des éléments fournis et à compléter dans le cadre du BE.
  - src/mictcp.c : fichier contenant le code source
  - config.sh

## Lancer l'application :
Pour lancer l'application, il suffit de tapper la commande `make` pour compiler, puis au choix `./tsock_texte` ou `./tsock_video`

## Choix de la version, et de la tolerance aux pertes pour la version 3:
Vous disposez de deux options pour selectioner la version que vous voulez tester
1. le petit script `config.sh` utilise la commande sed pour modifier la version et la tolerance aux pertes dans le code de mictcp

Tout d'abord rendez le executable : 

`chmod +x config.sh`

puis éxecutez le avec les paramêtres suivants:

`./config.sh <numero de la version> <pourcentage de pertes tolérées>`

Exemple : pour passer à la version 3 et tolérer 60% de perte


`./config.sh 3 60` 
