README

Contenu du dépôt
Ce dépôt inclut:
- README.md (ce fichier) : décrit comment compiler et tester nos codes
- tsock_texte et tsock_video : lanceurs pour les applications de test fournies.
- dossier include : contenant les définitions des éléments
- dossier src : contenant l'implantation des éléments fournis 
- src/mictcp.c : contenant v1 à v4.2

Compilation du protocole mictcp et lancement des applications de test
Pour compiler mictcp et générer les exécutables des applications de test depuis le code source fourni, taper : make

Deux applicatoins de test sont fournies, tsock_texte et tsock_video, elles peuvent être lancées soit en mode puits, soit en mode source selon la syntaxe suivante:

Usage: ./tsock_texte [-p|-s]
Usage: ./tsock_video [[-p|-s] [-t (tcp|mictcp)]

tsock_video permet d'utiliser, au choix, notre protocole mictcp ou une émulation du comportement de tcp sur un réseau avec pertes.


