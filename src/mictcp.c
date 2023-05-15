#include <mictcp.h>
#include <api/mictcp_core.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/queue.h>

#define NB_SOCKETS 32
#define BUFFER_CIRC_LEN 10

void buffer_put(mic_tcp_pdu bf);
int buffer_get(mic_tcp_pdu* buff);

void* process_sent_PDU(void * arg);

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

// Mutex et condition pour les fonctions accept et connect
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

// Numéro du thread Client
pthread_t send_th;

// Pour le buffer client
TAILQ_HEAD(tailhead, buffer_entry) buffer_head;
//struct tailhead *headp;
struct buffer_entry {
     mic_tcp_pdu bf;
     TAILQ_ENTRY(buffer_entry) entries;
};
pthread_mutex_t lock_buf;

/* Variable pour l'attente passive quand le buffer est vide */
pthread_cond_t buff_empty_cond;

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

    pthread_cond_init(&buff_empty_cond, 0);

    //Verifier que le mode est CLIENT et lancer la fonction
    // qui gere l'asynchronisme

    if(sm == CLIENT)
    {
        TAILQ_INIT(&buffer_head);
        pthread_create(&send_th, NULL, process_sent_PDU, "1");
    }

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
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");

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
    pdu.header.ack = 0;
    pdu.header.fin = 0;
    char * envoi = malloc(sizeof(char)*5);
    sprintf(envoi, "%.2lf", pertes_admiss);
    pdu.payload.data = envoi;
    pdu.payload.size = strlen(envoi);

    buffer_put(pdu);

    while(sockets[0].state != ESTABLISHED) {
        if (pthread_cond_wait(&cond, &mutex) != 0) {
            printf("Erreur de l'attente de la condition\n");
            exit(1);
        }
    }
    
    return 0;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    
    // Au lieu d'envoyer directement les PDU, on veut utiliser appbufferget/put
    // Comme l'asynchronisme est deja implementé dans le code du serveur,
    // on decice de l'implementer aussi pour le client en utilisant le buffer
    // a notre disposition
    
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

    buffer_put(pdu);
    
    return mesg_size;;
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
    payload.data = mesg;
    payload.size = max_mesg_size;
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

void buffer_put(mic_tcp_pdu bf)
{
    /* Prepare a buffer entry to store the data */
    struct buffer_entry * entry = malloc(sizeof(struct buffer_entry));

    entry->bf.header.source_port = bf.header.source_port;
    entry->bf.header.dest_port = bf.header.dest_port;
    entry->bf.header.seq_num = bf.header.seq_num;
    entry->bf.header.ack = bf.header.ack;
    entry->bf.header.syn = bf.header.syn;
    entry->bf.header.fin = bf.header.fin;

    entry->bf.payload.size = strlen(bf.payload.data);
    entry->bf.payload.data = malloc(entry->bf.payload.size*sizeof(char));
    strcpy(entry->bf.payload.data, bf.payload.data);

    /* Lock a mutex to protect the buffer from corruption */
    pthread_mutex_lock(&lock_buf);

    /* Insert the packet in the buffer, at the end of it */
    TAILQ_INSERT_TAIL(&buffer_head, entry, entries);

    /* Release the mutex */
    pthread_mutex_unlock(&lock_buf);

    /* We can now signal to any potential thread waiting that the buffer is
       no longer empty */
    pthread_cond_broadcast(&buff_empty_cond);
}

int buffer_get(mic_tcp_pdu* buff)
{
    /* A pointer to a buffer entry */
    struct buffer_entry * entry;

    /* The actual size passed to the application */
    int result = 0;

    /* Lock a mutex to protect the buffer from corruption */
    pthread_mutex_lock(&lock_buf);

    /* If the buffer is empty, we wait for insertion */
    while(buffer_head.tqh_first == NULL) {
          pthread_cond_wait(&buff_empty_cond, &lock_buf);
    }

    /* When we execute the code below, the following conditions are true:
       - The buffer contains at least 1 element
       - We hold the lock on the mutex
    */

    /* The entry we want is the first one in the buffer */
    entry = buffer_head.tqh_first;
    
    buff->header.source_port = entry->bf.header.source_port;
    buff->header.dest_port = entry->bf.header.dest_port;
    buff->header.seq_num = entry->bf.header.seq_num;
    buff->header.ack = entry->bf.header.ack;
    buff->header.syn = entry->bf.header.syn;
    buff->header.fin = entry->bf.header.fin;
    buff->payload.size = entry->bf.payload.size;

    /* We copy the actual data in the application allocated buffer */
    buff->payload.data = malloc(strlen(entry->bf.payload.data));
    strcpy(buff->payload.data, entry->bf.payload.data);

    /* We remove the entry from the buffer */
    TAILQ_REMOVE(&buffer_head, entry, entries);

    /* Release the mutex */
    pthread_mutex_unlock(&lock_buf);

    /* Clean up memory */
    free(entry->bf.payload.data);
    free(entry);

    return result;
}

/*
 * Création d’un PDU MIC-TCP à envoyer (mise à jour des numéros de séquence
 * et d'acquittement, etc.) avec les données utiles depuis le buffer d'envoi
 * du socket. Cette fonction utilise la fonction buffer_get().
 */
void* process_sent_PDU(void * arg)
{
    while(1)
    {
        // Construction du PDU à envoyer
        mic_tcp_pdu pdu = {0};

        //char tab[500];
        //pdu.payload.data = tab;

        buffer_get(&pdu);

        // Traitement du PDU si c'est un SYN
        if(pdu.header.syn == 1
            && pdu.header.ack == 0
            && pdu.header.fin == 0) {
            
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
        } else {

            // Envoi du PDU
            IP_send(pdu, dest_addr);

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
                        IP_send(pdu,dest_addr);
                        result = IP_recv(&pdu_recv, &addr_recv, 5);
                    }

                    
                }
            }
            
            actualise_buffer(1);

            num_seq = (num_seq + 1)%2;
        }
    }

}

/*
 * Renvoie le taux de perte actuel en lisant le buffer circulaire
 */
double perte_actuelle() 
{
    int nb_zeros = 0;
    for (int i=0 ; i<BUFFER_CIRC_LEN ; i++) {
        if (buffer_circ[i] == 0) {
            nb_zeros += 1;
        }
    }

    return nb_zeros/BUFFER_CIRC_LEN*100;
}

/*
 * Ajoute dans le buffer le statut de l'envoi du précédent PDU
 * Le statut sera un 0 si le PDU a été perdu, sinon 1
 */
void actualise_buffer(int code) 
{
    for (int i=0 ; i<BUFFER_CIRC_LEN-1 ; i++) {
        buffer_circ[i+1]=buffer_circ[i];
    }
    buffer_circ[0]=code;
}