#include <mictcp.h>
#include <api/mictcp_core.h>

#define NB_SOCKETS 32

int id_socket = 0;
mic_tcp_sock sockets[NB_SOCKETS];

// Adresse de destination
mic_tcp_sock_addr dest_addr;

// Numéros de séquence et d'acquittement
int num_seq = 0;
int num_ack = 0;

// PDU d'acquittement stocké
mic_tcp_pdu acquittement;

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
    set_loss_rate(0);

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

   return 0;
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    return 0;
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
    
    return 0;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
    return 0;
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    /*
    // Sauvegarde de l'adresse de connexion
    dest_addr.ip_addr = addr.ip_addr;
    dest_addr.port = addr.port;

    // Initialisation du PDU SYN
    mic_tcp_pdu pdu;
    pdu.header.source_port = sockets[socket].addr.port;
    pdu.header.dest_port = dest_addr.port;
    pdu.header.syn = 1;

    // Envoi du PDU SYN
    IP_send(pdu,dest_addr);

    // Définition du PDU en réception
    mic_tcp_pdu pdu_recv;
    mic_tcp_sock_addr addr_recv;

    // Attente du PDU + activation du Timer
    int result = IP_recv(&pdu_recv, &addr_recv, 5);
    int cpt = 0;
    while ((result == -1 
            || pdu_recv.header.syn == 0 
            || pdu_recv.header.ack == 0) && (cpt < 10)
            ) {
        // Renvoi du PDU à l'expiration du Timer (only 10 times)
        IP_send(pdu,dest_addr);
        result = IP_recv(&pdu_recv, &addr_recv, 5);
        cpt++;
    }
    // Si on réalise la boucle 10 fois, on arrête la fonction
    if (cpt == 10) return -1;

    // Envoi du ACK
    pdu.header.syn = 0;
    pdu.header.ack = 1;

    IP_send(pdu,dest_addr);
    */
    return 0;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    
    if (sockets[0].state == IDLE ||
        sockets[0].state == ESTABLISHED ||
        sockets[0].state == ACK_RECEIVED) {
        
        // Construction du PDU à envoyer
        mic_tcp_pdu pdu;
        pdu.header.source_port = sockets[mic_sock].addr.port;
        pdu.header.dest_port = dest_addr.port;
        pdu.header.seq_num = num_seq;
        pdu.payload.data = mesg;
        pdu.payload.size = mesg_size;

        // Envoi du PDU
        IP_send(pdu, dest_addr);

        sockets[0].state = ACK_WAITING;

        int cpt = 0;
        while ((sockets[0].state != ACK_RECEIVED)) {
            // Renvoi du PDU à l'expiration du Timer (only 10 times)
            IP_send(pdu,dest_addr);
        }
    } else {
        return -1;
    }
    
    return 0;
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
    mic_tcp_payload payload;
    payload.size = max_mesg_size;
    delivered_size = app_buffer_get(payload);
    
    // Retour du message à l'application
    mesg = payload.data;
    
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

    return -1;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
    //printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    
    // Le client reçoit un ACK
    if (pdu.header.ack == 1 
        && pdu.header.syn == 0
        && pdu.header.fin == 0) {
        
        if ((sockets[0].state == ACK_WAITING) && (pdu.header.ack_num == num_seq)) {
            sockets[0].state = ACK_RECEIVED;
            num_seq = (num_seq + 1)%2;
        }
    }

    // Le client reçoit un SYN-ACK
    if (pdu.header.ack == 1 
        && pdu.header.syn == 1
        && pdu.header.fin == 0) {

        sockets[0].state = SYNACK_RECEIVED;

    }

    // Le client reçoit un FIN-ACK
    if (pdu.header.ack == 1 
        && pdu.header.syn == 0
        && pdu.header.fin == 1) {

        sockets[0].state = FINACK_RECEIVED;

    }
    
    /*  
    *   Le serveur reçoit un message
    *   On met à jour les numéros de séquence et d'acquittement
    *   On met le message dans le buffer
    */
    if (pdu.header.ack == 0 
        && pdu.header.syn == 0
        && pdu.header.fin == 0) {

        printf("message recu, bonne boucle\n");

        if (num_ack == pdu.header.seq_num) {
            acquittement.header.source_port = sockets[0].addr.port;
            acquittement.header.dest_port = addr.port;
            acquittement.header.ack_num = pdu.header.seq_num;

            num_ack = (num_ack + 1)%2;
            
            app_buffer_put(pdu.payload);
        }
        
        IP_send(acquittement, addr);
    }

    // Le serveur reçoit un SYN
    if (pdu.header.ack == 1 
        && pdu.header.syn == 0
        && pdu.header.fin == 1) {

        sockets[0].state = SYN_RECEIVED;

    }

    // Le serveur reçoit un FIN
    if (pdu.header.ack == 1 
        && pdu.header.syn == 0
        && pdu.header.fin == 1) {

        sockets[0].state = FIN_RECEIVED;

    }

    
}
