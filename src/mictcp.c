#include <mictcp.h>
#include <api/mictcp_core.h>
#include <pthread.h>
#include <stdio.h>

#define NB_SOCKETS 32
#define BUFFER_CIRC_LEN 10

double perte_actuelle();
void actualise_buffer(int code);

int id_socket = 0;
mic_tcp_sock sockets[NB_SOCKETS];

// Adresse de destination
mic_tcp_sock_addr dest_addr = {0};

// Numéros de séquence et d'acquittement
int num_seq = 0;
int num_ack = 0;

// PDU d'acquittement stocké
mic_tcp_pdu acquittement = {0};

// Taux de pertes admissible
double pertes_admiss = 11.0;
double pertes_admiss_serv[2] = {10.0,25.0}; //plage de valeurs des pertes admiss par le serveur

// Tableau circulaire pour le calcul du taux de perte
int buffer_circ[BUFFER_CIRC_LEN] = {1,1,1,1,1,1,1,1,1,1};

// Mutex et condition pour la fonction accept
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(initialize_components(sm) == -1) { /* Appel obligatoire */
        return -1;
    }
    set_loss_rate(10);

    sockets[id_socket].fd = id_socket;
    sockets[id_socket].state = IDLE;
    
    return id_socket++;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   
    if (socket < 0 || socket > id_socket) return -1;

    addr.port = socket;
    sockets[socket].addr = addr;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    /*
    // Définition du PDU en réception
    mic_tcp_pdu pdu_recv;
    mic_tcp_sock_addr addr_recv;

    // Attente du PDU + activation du Timer
    int result = IP_recv(&pdu_recv, &addr_recv, 5);
    int cpt = 0;

    //Attente de SYN
    while ((result == -1 
            || pdu_recv.header.syn == 0) && (cpt < 10)) {
        // Renvoi du PDU à l'expiration du Timer (only 10 times)
        IP_send(pdu,addr);
        result = IP_recv(&pdu_recv, &addr_recv, 5);
        cpt++;
    }
    if (cpt == 10) return -1;

    // Envoi du SYN ACK
    pdu.header.syn = 1;
    pdu.header.ack = 1;

    IP_send(pdu,addr);

    //Attente de ACK
    while ((result == -1 
            || pdu_recv.header.ack == 0) && (cpt < 10)) {
        // Renvoi du PDU à l'expiration du Timer (only 10 times)
        IP_send(pdu,addr);
        result = IP_recv(&pdu_recv, &addr_recv, 5);
        cpt++;
    }
    if (cpt == 10) return -1; */

    while(sockets[0].state != ESTABLISHED) {
        if (pthread_cond_wait(&cond, &mutex) != 0) {
            printf("Erreur de l'attente de la condition\n");
            exit(1);
        }
    }
    
    return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

    // Sauvegarde de l'adresse de connexion
    dest_addr.ip_addr = addr.ip_addr;
    dest_addr.port = addr.port;

    // Initialisation du PDU SYN
    mic_tcp_pdu pdu = {0};
    pdu.header.source_port = sockets[socket].addr.port;
    pdu.header.dest_port = dest_addr.port;
    pdu.header.syn = 1;
    char * envoi = malloc(sizeof(char)*5);
    sprintf(envoi, "%.2lf", pertes_admiss);
    pdu.payload.data = envoi;
    pdu.payload.size = strlen(envoi);

    // Envoi du PDU SYN
    IP_send(pdu,dest_addr);

    // Définition du PDU en réception
    mic_tcp_pdu pdu_recv = {0};
    char pointeur_tmp[8];
    pdu_recv.payload.data = pointeur_tmp;
    pdu_recv.payload.size = strlen(pointeur_tmp);
    mic_tcp_sock_addr addr_recv = {0};

    // Attente du PDU + activation du Timer
    int result = IP_recv(&pdu_recv, &addr_recv, 5);
    while ((result == -1
            || pdu_recv.header.syn == 0 
            || pdu_recv.header.ack == 0)) {
        IP_send(pdu,dest_addr);
        result = IP_recv(&pdu_recv, &addr_recv, 5);
    }

    double value_perte = atof(pdu_recv.payload.data);
    pertes_admiss = value_perte;

    // Envoi du ACK
    pdu.header.syn = 0;
    pdu.header.ack = 1;

    IP_send(pdu,dest_addr);

    return 0;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    
    // Construction du PDU à envoyer
    mic_tcp_pdu pdu = {0};
    pdu.header.source_port = sockets[mic_sock].addr.port;
    pdu.header.dest_port = dest_addr.port;
    pdu.header.seq_num = num_seq;
    pdu.header.ack = 0;
    pdu.header.syn = 0;
    pdu.header.fin = 0;
    pdu.payload.data = mesg;
    pdu.payload.size = mesg_size;

    // Envoi du PDU
    int octets = IP_send(pdu, dest_addr);

    // Définition du PDU en réception
    mic_tcp_pdu pdu_recv = {0};
    mic_tcp_sock_addr addr_recv = {0};

    // Attente du PDU + activation du Timer
    int result = IP_recv(&pdu_recv, &addr_recv, 5);

    // Vérification si le PDU a été perdu
    if ((result == -1 || 
            (pdu_recv.header.ack != 1 && pdu_recv.header.ack_num != num_seq))) {
        
        actualise_buffer(0);

        // Vérification si la perte est admissible ou pas
        if ((perte_actuelle()> pertes_admiss)) {

            while ((result == -1 || 
            (pdu_recv.header.ack != 1 && pdu_recv.header.ack_num != num_seq))) {
                // Renvoi du PDU à l'expiration du Timer
                octets = IP_send(pdu,dest_addr);
                result = IP_recv(&pdu_recv, &addr_recv, 5);
            }

            buffer_circ[0] = 1;
        }
        
    }

    num_seq = (num_seq + 1)%2;
    
    return octets;
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");

    int delivered_size = -1;

    // Récupération du payload depuis le buffer du socket
    mic_tcp_payload payload = {0};
    payload.size = max_mesg_size;
    payload.data = mesg;
    delivered_size = app_buffer_get(payload);   

    // Retour du message à l'application
    return delivered_size;
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");

    return 0;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    
    /*  
    *   Le serveur reçoit un message
    *   On met à jour les numéros de séquence et d'acquittement
    *   On met le message dans le buffer
    */
    if (pdu.header.ack == 0 
        && pdu.header.syn == 0
        && pdu.header.fin == 0) {

        if (num_ack == pdu.header.seq_num) {
            acquittement.header.source_port = sockets[0].addr.port;
            acquittement.header.dest_port = addr.port;
            acquittement.header.ack = 1;
            acquittement.header.ack_num = pdu.header.seq_num;
            acquittement.payload.data = "";
            acquittement.payload.size = 0;
            
            num_ack = (num_ack + 1)%2;
            
            app_buffer_put(pdu.payload);
        }
        
        IP_send(acquittement, addr);
    }

    // Le serveur reçoit un SYN
    if (pdu.header.ack == 0  
        && pdu.header.syn == 1
        && pdu.header.fin == 0) {

        sockets[0].state = SYN_RECEIVED;

        mic_tcp_pdu pdu_recv = {0};
        mic_tcp_sock_addr addr_recv = {0};

        mic_tcp_pdu pdu_syn_ack = {0};
        char pointeur_tmp[8];
        pdu_syn_ack.payload.data = pointeur_tmp;
        
        // Envoi du SYN ACK
        pdu_syn_ack.header.syn = 1;
        pdu_syn_ack.header.ack = 1;

        double value_perte = atof(pdu.payload.data);

        // Négociation du % de pertes
        if (value_perte > pertes_admiss_serv[0]
            && value_perte < pertes_admiss_serv[1]) {

            strcpy(pdu_syn_ack.payload.data, pdu.payload.data);
            pdu_syn_ack.payload.size = strlen(pdu_syn_ack.payload.data);

        } else if (value_perte < pertes_admiss_serv[0]){

            char envoi[8];
            sprintf(envoi, "%.2lf", pertes_admiss_serv[0]);
            pdu_syn_ack.payload.data = envoi;
            pdu_syn_ack.payload.size = strlen(envoi);

        } else if(value_perte > pertes_admiss_serv[1]){

            char envoi[8];
            sprintf(envoi, "%.2lf", pertes_admiss_serv[1]);
            pdu_syn_ack.payload.data = envoi;
            pdu_syn_ack.payload.size = strlen(envoi);

        }


        IP_send(pdu_syn_ack,addr);
        int result = IP_recv(&pdu_recv, &addr_recv, 5);
        
        // Réception de ACK
        while ((result == -1 || (pdu_recv.header.ack != 1))) {
            // Renvoi du PDU à l'expiration du Timer
            IP_send(pdu_syn_ack,addr);
            result = IP_recv(&pdu_recv, &addr_recv, 5);
        }

        // Signal pour la condition
        if (pthread_mutex_lock(&mutex) != 0) {
            printf("erreur de lock du mutex\n");
            exit(1);
        }
        sockets[0].state = ESTABLISHED;
        if (pthread_cond_signal(&cond) != 0) {
            printf("Erreur de l'envoi du signal\n");
            exit(1);
        }
        if (pthread_mutex_unlock(&mutex) != 0) {
            printf("erreur de unlock du mutex\n");
            exit(1);
        }
    }

    // Le serveur reçoit un FIN
    if (pdu.header.ack == 0 
        && pdu.header.syn == 0
        && pdu.header.fin == 1) {

    }

    
}

double perte_actuelle() {
    int nb_zeros = 0;
    for (int i=0 ; i<BUFFER_CIRC_LEN ; i++) {
        if (buffer_circ[i] == 0) {
            nb_zeros += 1;
        }
    }

    return nb_zeros/BUFFER_CIRC_LEN*100;
}

void actualise_buffer(int code) {
    for (int i=0 ; i<BUFFER_CIRC_LEN-1 ; i++) {
        buffer_circ[i+1]=buffer_circ[i];
    }
    buffer_circ[0]=code;
}