#include <pthread.h>
#include <stdio.h>
#include <mictcp.h>
#include <api/mictcp_core.h>
#define NBR_SOCKETS 1024
#define TIMEOUT_DEFAUT 1000
#define WINDOW_SIZE 10 // taille de la fenêtre pour la reprise des pertes
#define LOSS_ACCEPTABILITY 2 // sur 100

//================================== STRUCTURES =============================

typedef struct { // buffer circulaire pour reprise des pertes
	char table[WINDOW_SIZE];
	char last_index;
} circularBuffer;


typedef struct
{ //Socket amélioré comprenant l'adresse distante, numéros de séquence et buffer circulaire
	mic_tcp_sock socket;
	mic_tcp_sock_addr dist_addr;
	char NoSeqLoc;	// = -1;
	char NoSeqDist; // = -1;
	circularBuffer buffer;
} enhanced_socket;


typedef struct
{
	int recpt;
	int socket;
	mic_tcp_pdu pdu_r;
}arg_thread;


//================================== VARIABLES GLOBALES =============================

static int socket_desc = 0; // fd du socket incrémenté à chaque nouveau socket
static enhanced_socket tab_sockets[NBR_SOCKETS]; 
int timeout = TIMEOUT_DEFAUT;
pthread_t envoi_syn_ack_tid;
pthread_t envoi_fin_ack_tid;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t end_accept_cond = PTHREAD_COND_INITIALIZER;
char debug=0; // 1 pour voir affichages de debug, 0 pour les cacher
char version=4; // numéro de version (1,2,3,4)

//================================== SIGNATURES DES FONCTIONS PRIVEES =============================

int valid_socket(int socket);
int bound_socket(int socket);
int same_addr(mic_tcp_sock_addr *addr1, mic_tcp_sock_addr *addr2);
void display_mic_tcp_pdu(mic_tcp_pdu pdu, char* prefix);
void display_enhanced_socket(enhanced_socket sock, char* prefix);
void display_mic_tcp_sock_addr(mic_tcp_sock_addr addr, char* prefix);
void * attente_ack(void * arg);
void error(char * message, int line);
void addValueCircularBuff(circularBuffer* buffer, char Value );
int accept_loss(int socket);
void set_mic_tcp_pdu(mic_tcp_pdu* pdu, unsigned short source_port, unsigned short dest_port, unsigned int seq_num, unsigned int ack_num, unsigned char syn, unsigned char ack, unsigned char fin, char* data, int size);
void process_syn_pdu(mic_tcp_pdu pdu,mic_tcp_sock_addr addr, int mic_sock);
void * envoi_syn_ack(void * arg);
void * envoi_fin_ack(void * arg);
//================================== FONCTIONS DE MICTCP =============================


/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
	printf("[MIC-TCP] Appel de la fonction: ");
	printf(__FUNCTION__);
	printf("\n");

	enhanced_socket *socket = &tab_sockets[socket_desc];

	socket->socket.fd = socket_desc++;
	socket->socket.state = IDLE;

    display_enhanced_socket(*socket, "état du socket");
	socket->NoSeqLoc =0;
	int result = initialize_components(sm); /* Appel obligatoire */
	if (result < 0)
	{
		return -1;
	}
	
	return socket->socket.fd;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
	printf("[MIC-TCP] Appel de la fonction: ");
	printf(__FUNCTION__);
	printf("\n");

	if (valid_socket(socket) && tab_sockets[socket].socket.state == IDLE)
	{
		tab_sockets[socket].socket.addr = addr;
		tab_sockets[socket].socket.state = WAITING;
        display_enhanced_socket(tab_sockets[socket],"");

		return 0;
	}

	return -1;
}

/*
 * Met le socket en état d'acceptation de connexion et bloque jusqu'a ce que la connexion soit établie
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr *addr)
{
	printf("[MIC-TCP] Appel de la fonction: ");
	printf(__FUNCTION__);
	printf("\n");

	if (valid_socket(socket))
	{
		tab_sockets[socket].socket.state = ACCEPTING;
		display_enhanced_socket(tab_sockets[socket], "état du socket en attendant l'établissement de connection");
		if (version<4){ // Si phase de connexion pas requise
			tab_sockets[socket].socket.state = ESTABLISHED;
			tab_sockets[socket].NoSeqDist=0;
			tab_sockets[socket].NoSeqLoc=0;

		} 
			while (tab_sockets[socket].socket.state != ESTABLISHED){ //attente jusqu'a l'etablissement de la connexion
			if(pthread_mutex_lock(&mutex)!= 0){error("erreur lock mutex",__LINE__);} //lock mutex
			pthread_cond_wait(&end_accept_cond,&mutex);
			if(pthread_mutex_unlock(&mutex)!= 0){error("erreur unlock mutex",__LINE__);} //unlock mutex
		}
		display_enhanced_socket(tab_sockets[socket], "État du socket aprés la connexion :");
		return 0;
	}
	
	
	return -1;
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */


int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{
	tab_sockets[socket].dist_addr = addr;
	tab_sockets[socket].NoSeqLoc = 0;


	if (version>2){ //initialisation du buffer
		circularBuffer* buffer = &tab_sockets[socket].buffer;
		buffer->last_index = 0;
		for (int i = 0; i<WINDOW_SIZE;i++){
			buffer->table[i]=0;
		}
	}
	
	if (version<4){ // pas de phase de connexion
		tab_sockets[socket].socket.state = ESTABLISHED;
		display_enhanced_socket(tab_sockets[socket], "État du socket aprés l'établissement de la connection :");
		return 0;
	}


	tab_sockets[socket].socket.state = SYN_SENT;
	// créer pdu syn
	mic_tcp_pdu pdu;

	set_mic_tcp_pdu(
		&pdu,
		tab_sockets[socket].socket.addr.port,
		tab_sockets[socket].dist_addr.port,
		tab_sockets[socket].NoSeqLoc,
		-1,
		1,0,0,
		NULL,0
	);

    display_mic_tcp_pdu(pdu, "creation du pdu SYN:");

	display_mic_tcp_sock_addr(addr, "envoi du pdu SYN vers l'adresse :");

	
	IP_send(pdu, addr);
    display_enhanced_socket(tab_sockets[socket], "état du socket en attente du syn ack :");

	pthread_t attente_ack_tid;
	arg_thread* args = malloc(sizeof(arg_thread));

	while (1){

		args->recpt=-1;
		printf(debug?"avant creation thread TAA\n":"");
		pthread_create(&attente_ack_tid, NULL,attente_ack,(void *)args); //création thread d'attente de l'ACK
		printf(debug?"aprés creation thread TAA\n":"");

		usleep(timeout);
		if (pthread_cancel(attente_ack_tid)) printf(debug?"destruction du TAA":"");

		if (args->recpt == -1){ //si pas d'ACK reçu
			if (tab_sockets[socket].socket.state == ESTABLISHED){
				break;	
			}
			IP_send(pdu, tab_sockets[socket].dist_addr);
			printf(debug?"SYN ACK pas encore recu, envoi d'un doublon du syn\n":"");
			continue;
		}
		display_mic_tcp_pdu(args->pdu_r,"pdu reçu :");

		if (args->pdu_r.header.ack == 1 && args->pdu_r.header.syn == 1 /*verification pdu SYN ACK*/){
			printf(debug?"Bon PDU SYN ACK recu \n":"");

			tab_sockets[socket].NoSeqDist = args->pdu_r.header.seq_num; // récupérer n° de seq du server 

			set_mic_tcp_pdu(
			&pdu,
			tab_sockets[socket].socket.addr.port,
			tab_sockets[socket].dist_addr.port,
			-1,
			args->pdu_r.header.seq_num,
			0,1,0,
			NULL,0
			);
	
		    display_mic_tcp_pdu(pdu, "creation du pdu ACK:");

			display_mic_tcp_sock_addr(addr, "envoi du pdu ACK vers l'adresse :");

			IP_send(pdu, addr);

			// mic_tcp_pdu* args = malloc(sizeof(mic_tcp_pdu));
			// args->header=pdu.header;
			// args->payload=pdu.payload;

			tab_sockets[socket].socket.state = ESTABLISHED;
			
/*on attend naivement l'arrivee d'un nouveau syn ack au cas ou le ack a ete perdu, puis on relance la boucle, pour verifier*/
			continue;
			
		}
		else {
			printf(debug?"Le pdu recu n'est pas un SYN ACK\n":"");
			continue;
		}
		
       
	}
	display_enhanced_socket(tab_sockets[socket], "État du socket aprés l'établissement de la connection :");
	return 0;
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */

int mic_tcp_send(int mic_sock, char *mesg, int mesg_size)
{

	if (tab_sockets[mic_sock].socket.state != ESTABLISHED)
	{
		printf("L'utilisateur n’est pas connecté \n");
		exit(1);
	}
	// créer pdu
	mic_tcp_pdu pdu;
	set_mic_tcp_pdu(
		&pdu,
		tab_sockets[mic_sock].socket.addr.port,
		tab_sockets[mic_sock].dist_addr.port,
		 tab_sockets[mic_sock].NoSeqLoc,
		-1,
		0,0,0,
		mesg,mesg_size
	);
	display_mic_tcp_pdu(pdu, "creation du pdu data:");

	display_mic_tcp_sock_addr(tab_sockets[mic_sock].dist_addr, "envoi du pdu data vers l'adresse :");
	display_enhanced_socket(tab_sockets[mic_sock],"etat du socket");
	int sent_size = IP_send(pdu, tab_sockets[mic_sock].dist_addr);
	printf(debug?"Packet envoye\n":"");	
	if (version<2) return sent_size; // pas de fiabilité

	tab_sockets[mic_sock].socket.state = WAITING;
	  
	pthread_t attente_ack_tid;
	arg_thread* args = malloc(sizeof(arg_thread));

	while (1)
	{
		args->recpt=-1;
		printf(debug?"avant creation thread TAA\n":"");
		pthread_create(&attente_ack_tid, NULL,attente_ack,(void *)args); // attente de l'ACK
		printf(debug?"aprés creation thread TAA\n":"");

		usleep(timeout);
		if (pthread_cancel(attente_ack_tid)) printf(debug?"destruction du TAA":"");

		if (args->recpt == -1){ // Si on ne reçoit pas l'ACK
			if (version>2){
				if (accept_loss(mic_sock)) { // Si on peut accepter la perte, on ne retransmet pas
					addValueCircularBuff(&tab_sockets[mic_sock].buffer,0); // on ajoute la valeur 0 au buffer circulaire pour signifier que l'on a pas transmis le pdu
					printf(debug?"===========Perte Acceptée============\n":"");
					tab_sockets[mic_sock].socket.state = ESTABLISHED;
					

					return sent_size;
				} else {
					printf(debug?"============Perte inacceptable=============\n":"");
				}
			}
			sent_size = IP_send(pdu, tab_sockets[mic_sock].dist_addr);
			printf(debug?"Pas de pdu recu, envoi d'un doublon\n":"");
			continue;
		}
		display_mic_tcp_pdu(args->pdu_r,"pdu reçu :");
 
		if (args->pdu_r.header.ack == 1 && (tab_sockets[mic_sock].NoSeqLoc ==args->pdu_r.header.ack_num)){ // On vérifie le pdu reçu
			printf(debug?"le bon Ack a été reçu\n":"");
			tab_sockets[mic_sock].socket.state = ESTABLISHED;

			tab_sockets[mic_sock].NoSeqLoc = ((tab_sockets[mic_sock].NoSeqLoc + 1) % 2); //maj du no de seq uniquement lorsque ACK reçu (= synchronisation du noseq entre puits et src)
			display_enhanced_socket(tab_sockets[mic_sock], "État du socket aprés la reception du ack");
			if (version>2){ // on signifie que le pdu a bien été transmis
				addValueCircularBuff(&tab_sockets[mic_sock].buffer, 1);
			}
			return sent_size;
		
		}else if(args->pdu_r.header.ack == 1 && args->pdu_r.header.syn == 1){
			printf(debug?"PDU SYN ACK recu a nouveau (Doublon) \n":"");
			mic_tcp_pdu pdu_d;

			set_mic_tcp_pdu(
				&pdu_d,
				tab_sockets[mic_sock].socket.addr.port,
				tab_sockets[mic_sock].dist_addr.port,
				-1,
				args->pdu_r.header.seq_num,
				0,1,0,
				NULL,0
			);
	
		    display_mic_tcp_pdu(pdu_d, "creation du pdu ACK:");

			display_mic_tcp_sock_addr(tab_sockets[mic_sock].dist_addr, "envoi du pdu ACK vers l'adresse :");
			IP_send(pdu_d, tab_sockets[mic_sock].dist_addr);

		}else {
			printf(debug?"mauvais pdu reçu\n":"");
		}
		

	}

}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv(int socket, char *mesg, int max_mesg_size)
{
	printf("[MIC-TCP] Appel de la fonction: ");
	printf(__FUNCTION__);
	printf("\n");

	if (!valid_socket(socket))
	{
		return -1;
	}

	int delivered_size;
	mic_tcp_payload *payload = malloc(sizeof(mic_tcp_payload));
	payload->data = mesg;
	payload->size = max_mesg_size;
    printf(debug?"procède au app_buffer_get\n":"");
	delivered_size = app_buffer_get(*payload);
	printf(debug?"payload récupéré\n":"");
	return delivered_size;
}

/*
 * Permet de traiter un PDU SYN reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) 
 */
void process_syn_pdu(mic_tcp_pdu pdu,mic_tcp_sock_addr addr, int mic_sock){
	printf("[MIC-TCP] Appel de la fonction: ");
	printf(__FUNCTION__);
	printf("\n");

	tab_sockets[mic_sock].socket.state = SYN_RECEIVED;
	tab_sockets[mic_sock].dist_addr=addr;
	tab_sockets[mic_sock].NoSeqDist=pdu.header.seq_num;
	tab_sockets[mic_sock].NoSeqLoc=0; //Premier numéro de seq toujours à 0
	mic_tcp_pdu pdu_r;

	set_mic_tcp_pdu(&pdu_r, 
					tab_sockets[mic_sock].socket.addr.port,
					tab_sockets[mic_sock].dist_addr.port,
					tab_sockets[mic_sock].NoSeqLoc, 
					tab_sockets[mic_sock].NoSeqDist,
					 1,1,0,NULL,0
	);
			
	display_mic_tcp_pdu(pdu_r, "Construction du Pdu SYN ACK : \n");
	display_mic_tcp_sock_addr(tab_sockets[mic_sock].dist_addr, "Envoi du PDU SYN ACK à l'adresse:");

	IP_send(pdu_r, addr);



	arg_thread* args = malloc(sizeof(arg_thread));
	args->socket=mic_sock;
	args->pdu_r=pdu_r;

	printf(debug?"avant creation thread TESA\n":"");
	pthread_create(&envoi_syn_ack_tid, NULL,envoi_syn_ack,(void *)args); // lancement du thread d'envoi de SYN ACK
	printf(debug?"aprés creation thread TESA\n":"");


}

void process_fin_pdu(mic_tcp_pdu pdu,mic_tcp_sock_addr addr, int mic_sock){
	printf("[MIC-TCP] Appel de la fonction: ");
	printf(__FUNCTION__);
	printf("\n");

	tab_sockets[mic_sock].socket.state = CLOSING;
	mic_tcp_pdu pdu_r;

	set_mic_tcp_pdu(&pdu_r, 
					tab_sockets[mic_sock].socket.addr.port,
					tab_sockets[mic_sock].dist_addr.port,
					tab_sockets[mic_sock].NoSeqLoc, 
					tab_sockets[mic_sock].NoSeqDist,
					 0,1,1,NULL,0
	);
			
	display_mic_tcp_pdu(pdu_r, "Construction du Pdu FIN ACK : \n");
	display_mic_tcp_sock_addr(tab_sockets[mic_sock].dist_addr, "Envoi du PDU FIN ACK à l'adresse:");

	IP_send(pdu_r, addr);



	arg_thread* args = malloc(sizeof(arg_thread));
	args->socket=mic_sock;
	args->pdu_r=pdu_r;

	printf(debug?"avant creation thread TEFA\n":"");
	pthread_create(&envoi_fin_ack_tid, NULL,envoi_fin_ack,(void *)args);
	printf(debug?"aprés creation thread TEFA\n":"");


}

/*
 * Permet de traiter un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis d'insérer les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put(). Elle est appelée par initialize_components()
 * Quitte l'application en cas d'erreur
 */

void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr) //addr = adresse distante
{

	int mic_sock;  

    display_mic_tcp_pdu(pdu, "pdu reçu");

	for (mic_sock = 0; (mic_sock < socket_desc) && !(tab_sockets[mic_sock].socket.addr.port==pdu.header.dest_port); mic_sock++)
		; // trouver le socket destinataire
	if (mic_sock == socket_desc)
	{ // si aucun socket trouvé, alors retourne une erreur
		printf("[MIC-TCP] Socket non trouvé\n");
		exit(1);
	}

	if (version<2) {
		app_buffer_put(pdu.payload); // envoyer la data dans le buffer
		printf(debug?"data dans le buffer\n":"");
		return;
	}
	
	if (pdu.header.syn == 1 && tab_sockets[mic_sock].socket.state == ACCEPTING) // si PDU SYN
	{	
		process_syn_pdu(pdu,addr, mic_sock);
	}
	else if (pdu.header.syn == 1 && tab_sockets[mic_sock].socket.state == SYN_RECEIVED) // si Doublon PDU SYN
	{	
		printf(debug?"Doublon PDU SYN recu\n":"");
	}
	if (pdu.header.fin == 1 && tab_sockets[mic_sock].socket.state == ESTABLISHED) // si PDU FIN
	{	
		printf(debug?"PDU FIN recu\n":"");
		process_fin_pdu(pdu,addr, mic_sock);
	}
	else if (pdu.header.syn == 1 && tab_sockets[mic_sock].socket.state == CLOSING) // si Doublon PDU FIN
	{	
		printf(debug?"Doublon PDU FIN recu\n":"");
	}

	else if (pdu.header.ack == 1 && tab_sockets[mic_sock].socket.state == SYN_RECEIVED)
	{ // Si ACK de connection reçu

		printf(debug?"PDU ACK recu \n":"");
		tab_sockets[mic_sock].socket.state = ESTABLISHED;
		if(pthread_mutex_lock(&mutex)!= 0){error("erreur lock mutex",__LINE__);} //lock mutex
        pthread_cond_broadcast(&end_accept_cond); // Rendre la main au client une fois le accept terminé
        if(pthread_mutex_unlock(&mutex)!= 0){error("erreur unlock mutex",__LINE__);} //unlock mutex

		printf("[MIC-TCP] Connexion établie\n");
	}
	else if (pdu.header.ack == 1 && tab_sockets[mic_sock].socket.state == CLOSING)
	{ // Si ACK de fin de connection reçu

		printf(debug?"PDU ACK recu \n":"");
		tab_sockets[mic_sock].socket.state = CLOSED;
		

		printf("[MIC-TCP] Connexion fermée\n");
		exit(1); // on quitte l'application
	}
	else if(pdu.header.ack == 0 && pdu.header.seq_num == tab_sockets[mic_sock].NoSeqDist && tab_sockets[mic_sock].socket.state == ESTABLISHED)
	{ // Si PDU de DATA 
		printf(debug?"PDU data recu \n":"");
		mic_tcp_pdu pdu_d;
		set_mic_tcp_pdu(
			&pdu_d,
			tab_sockets[mic_sock].socket.addr.port,
			tab_sockets[mic_sock].dist_addr.port,
			-1,
			pdu.header.seq_num,
			0,1,0,
			NULL,0
		);
	
		display_mic_tcp_pdu(pdu_d, "creation du pdu ACK:");

		display_mic_tcp_sock_addr(addr, "envoi du pdu ACK vers l'adresse :");
		IP_send(pdu_d, addr);


		tab_sockets[mic_sock].NoSeqDist = ((tab_sockets[mic_sock].NoSeqDist) + 1) % 2; //MàJ n° seq distant
		app_buffer_put(pdu.payload); // envoyer la data dans le buffer
		printf(debug?"data dans le buffer\n":"");
	}
	else if(pdu.header.ack == 0 && pdu.header.seq_num != tab_sockets[mic_sock].NoSeqDist && tab_sockets[mic_sock].socket.state == ESTABLISHED)
	{ // Si Doublon PDU de DATA  envoyer doublon PDU ACK
		printf(debug?"PDU data recu (Doublon)\n":"");
		mic_tcp_pdu pdu_d;
		set_mic_tcp_pdu(
			&pdu_d,
			tab_sockets[mic_sock].socket.addr.port,
			tab_sockets[mic_sock].dist_addr.port,
			-1,
			pdu.header.seq_num,
			0,1,0,
			NULL,0
		);
	
		display_mic_tcp_pdu(pdu_d, "recreation du pdu ACK:");

		display_mic_tcp_sock_addr(addr, "renvoi du pdu ACK vers l'adresse :");
		IP_send(pdu_d, addr);


	}else{

		printf(debug?"PDU inattendu recu :\n":"");
		display_enhanced_socket(tab_sockets[mic_sock], "l'état du socket");
	}
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close(int socket)
{
	printf("[MIC-TCP] Appel de la fonction :  ");
	printf(__FUNCTION__);
	printf("\n");

	if (version<4){ // pas de phase de fermeture de connexion
		tab_sockets[socket].socket.state = CLOSED;
		display_enhanced_socket(tab_sockets[socket], "État du socket aprés fermeture de la connection :");
		return 0;
	}

	if (valid_socket(socket) && tab_sockets[socket].socket.state == ESTABLISHED)
	{
		tab_sockets[socket].socket.state = CLOSING;
		mic_tcp_pdu pdu; // pdu fin
		set_mic_tcp_pdu(
		&pdu,
		tab_sockets[socket].socket.addr.port,
		tab_sockets[socket].dist_addr.port,
		tab_sockets[socket].NoSeqLoc,
		-1,
		0,0,1,
		NULL,0
	);

    display_mic_tcp_pdu(pdu, "creation du pdu FIN:");

	display_mic_tcp_sock_addr(tab_sockets[socket].dist_addr, "envoi du pdu FIN vers l'adresse :");

	
	IP_send(pdu, tab_sockets[socket].dist_addr);
    display_enhanced_socket(tab_sockets[socket], "état du socket en attente du syn ack :");

	pthread_t attente_ack_tid;
	arg_thread* args = malloc(sizeof(arg_thread));

	while (1){

		args->recpt=-1;
		printf(debug?"avant creation thread TAA\n":"");
		pthread_create(&attente_ack_tid, NULL,attente_ack,(void *)args);
		printf(debug?"aprés creation thread TAA\n":"");

		usleep(timeout);
		if (pthread_cancel(attente_ack_tid)) printf(debug?"destruction du TAA\n":"");

		if (args->recpt == -1){
			if (tab_sockets[socket].socket.state == CLOSED){
				break;	
			}
			IP_send(pdu, tab_sockets[socket].dist_addr);
			printf(debug?"ACK pas encore reçu, envoi d'un doublon du FIN\n":"");
			continue;
		}
		display_mic_tcp_pdu(args->pdu_r,"pdu reçu :");

		if (args->pdu_r.header.ack == 1 && args->pdu_r.header.fin == 1 ){
			//printf("Bon PDU FIN ACK recu \n");

			set_mic_tcp_pdu(
			&pdu,
			tab_sockets[socket].socket.addr.port,
			tab_sockets[socket].dist_addr.port,
			-1,
			args->pdu_r.header.seq_num,
			0,1,0,
			NULL,0
			);
	
		    display_mic_tcp_pdu(pdu, "creation du pdu ACK:");

			display_mic_tcp_sock_addr(tab_sockets[socket].dist_addr, "envoi du pdu ACK vers l'adresse :");

			IP_send(pdu, tab_sockets[socket].dist_addr);

			tab_sockets[socket].socket.state = CLOSED;
			

			break;
			
		}
		else {
			continue;
		}
		
       
	}
	display_enhanced_socket(tab_sockets[socket], "État du socket aprés la fermeture de la connexion :");
	printf("[MIC-TCP] Connexion fermée\n");
	tab_sockets[socket].socket.fd = -1; // suppression du socket
	return 0;
	}
	return -1;
}



//================================== CORPS DES FONCTIONS PRIVEES =============================

// revoie 1 si le socket existe et 0 sinon
int valid_socket(int socket)
{
	if (socket > socket_desc - 1 || tab_sockets[socket].socket.fd == -1)
	{
		printf(debug?"socket inexistant\n":"");
		return 0;
	}
	return 1;
}

// renvoie 1 si le socket a été bind et 0 sinon
int bound_socket(int socket)
{
	if (tab_sockets[socket].socket.state == IDLE || tab_sockets[socket].socket.state == CLOSING || tab_sockets[socket].socket.state == CLOSED )
	{
		// printf("socket not bound\n");
		return 0;
	}
	return 1;
}

// compare 2 adresses : renvoie 1 si elles sont égales et 0 sinon
int same_addr(mic_tcp_sock_addr *addr1, mic_tcp_sock_addr *addr2)
{
	if (addr1->port == addr2->port && addr1->ip_addr_size == addr2->ip_addr_size && !memcmp(addr1->ip_addr, addr2->ip_addr, addr1->ip_addr_size))
	{
		return 1;
	}
	return 0;
}

// assigne chaque champ d'un pdu à sa valeur passée en argument
void set_mic_tcp_pdu(mic_tcp_pdu* pdu, unsigned short source_port, unsigned short dest_port, unsigned int seq_num, unsigned int ack_num, unsigned char syn, unsigned char ack, unsigned char fin, char* data, int size) {
  pdu->header.source_port = source_port;
  pdu->header.dest_port = dest_port;
  pdu->header.seq_num = seq_num;
  pdu->header.ack_num = ack_num;
  pdu->header.syn = syn;
  pdu->header.ack = ack;
  pdu->header.fin = fin;
  pdu->payload.data = data;
  pdu->payload.size = size;
}
	
// fonction de debug qui affiche en détail un pdu
void display_mic_tcp_pdu(mic_tcp_pdu pdu, char* prefix) {
	if (!debug) return ;
	printf("------------------------------------------\n%s\n", prefix);
	printf("ACK Flag: %d\n", pdu.header.ack);
	printf("FIN Flag: %d\n", pdu.header.fin);
	printf("SYN Flag: %d\n", pdu.header.syn);

	printf("Source Port: %hu\n", pdu.header.source_port);
	printf("Destination Port: %hu\n", pdu.header.dest_port);
	printf("Sequence Number: %u\n", pdu.header.seq_num);
	printf("Acknowledgment Number: %u\n", pdu.header.ack_num);
	printf("Payload Size: %d\n", pdu.payload.size);
	if (pdu.payload.size != 0) {
	printf("Payload Data: %.*s\n", pdu.payload.size, pdu.payload.data);
	} else {
	printf("Payload Data is NULL\n");
	}
    printf("----------------------------------\n");
}

// fonction de debug qui affiche en détail un socket amélioré
void display_enhanced_socket(enhanced_socket sock,char* prefix) {
	if (!debug) return ;
	printf("----------------------------------\n");
	printf("%s\n", prefix);
	printf("Socket File Descriptor: %d\n", sock.socket.fd);
	
	const char* protocol_states[] = {"IDLE", "CLOSED", "SYN_SENT", "SYN_RECEIVED", "ESTABLISHED", "CLOSING", "ACCEPTING", "WAITING"};
	
	
	printf("Socket Protocol State: %s\n", protocol_states[sock.socket.state]);
	
	printf("Socket Address IP: %.*s\n", sock.socket.addr.ip_addr_size, sock.socket.addr.ip_addr);
	printf("Socket Address Port: %hu\n", sock.socket.addr.port);
	printf("Remote Address IP: %.*s\n", sock.dist_addr.ip_addr_size, sock.dist_addr.ip_addr);
	printf("Remote Address Port: %hu\n", sock.dist_addr.port);
	printf("Local Sequence Number: %d\n", sock.NoSeqLoc);
	printf("Remote Sequence Number: %d\n", sock.NoSeqDist);
	if (version>2){
		printf("Buffer: --------------- \n");
		for (int i =0;i<WINDOW_SIZE; i++){
			printf(" %d ;",sock.buffer.table[i]);
		}
		printf("\n-----------------\n");
	}
	printf("\n----------------------------------\n");
}

// fonction de debug qui affiche en détail une adresse
void display_mic_tcp_sock_addr(mic_tcp_sock_addr addr, char* prefix) {
	if (!debug) return ;
	printf("----------------------------------\n");
	printf("%s\n", prefix);
	printf("IP Address: %.*s\n", addr.ip_addr_size, addr.ip_addr);
  	printf("Port: %hu\n", addr.port);
	printf("----------------------------------\n");
}

// fonction appelée par les threads d'attente d'ACK
void * attente_ack(void * arg) {
	printf(debug?"début du TAA: thread d'attente de ACK'\n":"");
	arg_thread* args = (arg_thread*)arg;
	

	args->recpt=IP_recv(&args->pdu_r,NULL, 0) ;
	printf(debug?"TAA: reception effectuee, autodestruction entamee\n":"");
	pthread_exit(NULL);
}

// fonction appelée par le thread d'envoi de SYN ACK
void * envoi_syn_ack(void * arg) {
	printf(debug?"début du TESA : thread d'envoi de SYN ACK'\n":"");
	arg_thread* args = (arg_thread*)arg;
	while (1)
		{
			if (tab_sockets[args->socket].socket.state == SYN_RECEIVED)
			{

				display_mic_tcp_pdu(args->pdu_r, "renvoi du pdu syn ack");
				display_mic_tcp_sock_addr(tab_sockets[args->socket].dist_addr,"a l'adresse");
				IP_send(args->pdu_r, tab_sockets[args->socket].dist_addr);
				printf(debug?"TESA: je renvoie le Syn ack\n":"");
			}
			else if (tab_sockets[args->socket].socket.state == ESTABLISHED)
			{
				printf(debug?"!Destruction du TESA!":"");
				pthread_exit(NULL);
			}
			usleep(timeout);
		}
}

void * envoi_fin_ack(void * arg) {
	printf(debug?"début du TEFA : Thread d'Envoi de FIN ACK'\n":"");
	arg_thread* args = (arg_thread*)arg;
	while (1)
		{
			if (tab_sockets[args->socket].socket.state == CLOSING)
			{

				display_mic_tcp_pdu(args->pdu_r, "renvoi du pdu fin ack");
				display_mic_tcp_sock_addr(tab_sockets[args->socket].dist_addr,"a l'adresse");
				IP_send(args->pdu_r, tab_sockets[args->socket].dist_addr);
				printf(debug?"TEFA: je renvoie le FIN ack\n":"");
			}
			else if (tab_sockets[args->socket].socket.state == CLOSED)
			{
				printf("!Destruction du TEFA!");
				pthread_exit(NULL);
			}
			usleep(timeout);
		}
}

// fonction affichant les erreurs utilisée avec les mutex
void error(char * message, int line){
    printf("%s at line %d\n",message,line);
    exit(1);
}


// fonction qui détermine si une perte est acceptable ou pas. Renvoie 1 si la perte est acceptable et 0 sinon
int accept_loss(int socket){
	int sum = 0;
	for(int i = 0; i < WINDOW_SIZE; i++){ // somme de tous les packets reçus
		sum += tab_sockets[socket].buffer.table[i];
	}

	if(sum < WINDOW_SIZE*(100-LOSS_ACCEPTABILITY)/100){// si le nombre de packets reçus est inférieur au nombre acceptable
		return 0; // on accepte pas la perte
	}
	return 1;
}

// fonction ajoutant une valeur au buffer circulaire passé en argument
void addValueCircularBuff(circularBuffer* buffer, char Value ){
	buffer->table[buffer->last_index+1%WINDOW_SIZE]=Value;
	buffer->last_index = buffer->last_index + 1 % WINDOW_SIZE;
}