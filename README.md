# BE RÉSEAUX
## TPs BE Reseau - 3 MIC Aittaleb Mohamed Barnavon Jules-iana

## Contenu du dépôt qui vous intéresse
Ce dépôt inclu notre code source final pour mictcp 
  - README.md (ce fichier) 
  - tsock_texte et tsock_video : lanceurs pour les applications de test fournies. 
  - src/mictcp.c : code source que l'on a développé
  - configurer.sh : script shell qui vous demandera la version que vous voudrez utiliser et la tolérance aux pertes que vous souhaitez pour la v3 et au-delà.

## Choix de la version
La méthode pour choisir la version que nous recommandons est d'utiliser le script `configurer.sh`, assurez-vous de le rendre exécutable par la commande :
`chmod +x configurer.sh`. Il ne fait que modifier une variable `version` qui contrôle la version dans `src/mictcp.c`.  

Le cas échéant, vous pouvez toujours remonter aux commits taggés avec le nom de la version appropriée. 

## Compiler
Si vous avez utilisé le script pour choisir la version, il n'est pas nécessaire de recompiler, sinon veuillez exécuter `make`

## Exécution
Pour exécuter, veuillez lancer simplement `tsock_text` ou `tsock_video`, faites cependant attention au bug décrit plus en bas.

## Notre avancement
Nous sommes arrivés jusqu'à développé une version fonctionnelle de la v4, (i.e avec fiabilité partielle et établissement de la connexion).

## Bug observé (IMPORTANT)
Avant d'exécuter `tsock_text`, assurez-vous de ne pas avoir exécuté `tsock_video` sur votre machine auparavant, nous avons observé que ceci faisait bugger
notre application. La seule solution que l'on a trouvée est de simplement reboot la machine.

## Choix remarquables 
### Paramètres de la fiabilité totale

Avant de discuter les choix associés à l'implémentation de la fiabilité partielle, il faut d'abord expliquer ceux associés à la fiabilité totale. Nous avons décidé d'associer à chaque socket deux numéros de séquence distincts. Un numéro de séquence local et un numéro de séquence distant. 
- Lorsqu'une application envoie un packet, elle place dans son en-tête le numéro de **séquence local** associe au socket. Ensuite seulement lorsqu'elle reçoit l'acquittement de ce dernier, elle incrémente ce numéro de séquence local.
- D'autre part, quand une application utilisant le protocole mictcp, entre en connexion avec une application distante. La première essaye de maintenir dans son numéro de séquence distant la valeur du numéro de séquence local de la seconde. Pour ce faire, elle l'incrémente à chaque réception d'un packet possédant un numéro de séquence différent.
Une des conséquences de notre choix est que chaque application est limitée à une seule connexion par socket. Une même source, ne peut pas recevoir de packets de deux sources d'adresses différentes.
On aurait pu, pour aller plus loin, associer à chaque socket une liste de numéros de séquence distants.

### Paramètres de la fiabilité partielle
Pour implanter la fiabilité partielle, nous avons repris notre solution implémentant la fiabilité totale, et nous avons ajouté les éléments suivants :

Chaque socket possède un buffer circulaire d'une taille prédéfinie N. C'est simplement une liste qui peut contenir un certain nombre N d'entiers (dans notre cas 0 ou 1). Lorsqu'elle cette liste est pleine, elle écrase les premiers entiers avec les nouvelles valeurs qu'on y insère. Elle sert à modéliser l'état de réception par le destinataire des N derniers packets. 
Lorsque le client reçoit un acquittement, il ajoute un 1 au buffer circulaire associe à son socket et incrémente le numéro de séquence.
Le cas échéant, il calcule la proportion de zéro dans ce buffer, et si elle est inférieure au pourcentage de pertes tolérable, il ajoute un autre 0 au buffer, et retourne au client la taille envoyée.
Seulement, cette fois, il n'incrémente pas le numéro de séquence local.
Si, en revanche, la proportion de perte enregistrée est au-dessus de la valeur tolérable, il renvoie le packet jusqu'à l'arrivée de son acquittement. 

Dans l'implantation de cette fiabilité partielle, nous avons jugé que seul celui qui envoie les packets (ici la source) est en position de contrôler cette fonctionnalité. C'est-à-dire, c'est lui qui maintient le buffer circulaire, et vérifie le respect du seuil de pertes. De ce fait, nous n'avons pas mis en place de mécanisme de négociation à proprement parler.
Cette approche nous a paru cohérente, elle donne le pouvoir au serveur de contrôler la qualité de service, et permettrait d'éviter des abus où le client voudrait contrôler la tolérance aux pertes et surchargerait le serveur avec des retransmissions.
 
### La réalisation des IP_recv
Si vous vous penchez sur notre code source, vous verrez que l'on a fait usage de la création de threads supplémentaires alors que nous ne sommes pas supposément allés jusqu'à la v4.2.
La raison de ceci est que nous avons rencontré des bugs assez troublant concernant les IP_recv.  Ces derniers adoptent un comportement différent d'une exécution à l'autre, parfois devenant bloquant, et parfois n'étant pas capable de recevoir de packet même s'ils en furent capables à l'exécution précédente.
Nous avons donc décidé de les traiter comme une source de perte en tant que tel, et nous les exécutons toujours dans un thread en parallèle côté puits. Côté source, étant donné que le `process_received_pdu` est lancé dans un thread à part, cela n'a pas été nécessaire.
Afin de les synchroniser avec le thread principal, nous avons simplement profité du timer de réception. En l'occurrence, le timer d'attente des acquittements est lancé après la création du thread, le thread place ce qu'il reçoit dans une variable partagée, puis est détruit à l'épuisement du timer.
Cela signifie en revanche que chaque traitement de packet reçu côté source doit attendre la fin du timer. Ce qui est une limite de notre solution.

