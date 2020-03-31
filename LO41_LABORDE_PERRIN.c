#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <semaphore.h>
#include <signal.h>

#define cle 317

#define RESET "\x1B[0m"
#define DEFAULT "\033[0m"
#define HIGHLIGHT "\033[1m"
#define UNDERLINE "\033[4m"
#define BLINK "\033[5m"
#define BLACK "\033[30m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define PURPLE "\033[35m"
#define CYAN "\033[36m"
#define WHITE "\033[37m"
#define nb 100

/**
 * Structure d'un atelier
 */
typedef struct{
    char nom[50];
    int stock_matiere;
    int stock_produit;
    int commande;
    int coeff;
    int capacite;
    int temps_production;
    long id;
    long id_prec;
    int position;
}atelier;

/**
 * Structure des messages
 */
typedef struct {
    long id;
    int tubeP_envoi[2];
    int tubeP_reception[2];
}
tMessage;

/**
 * Structure des livraisons client
 */
typedef struct{
	long id;
	int commandeC;
}
tLivraison;

int commande_client;

/**
 * Tableau de mutex : chaque atelier possède un mutex
 */
pthread_mutex_t mutex[nb];
pthread_cond_t reception[nb];
pthread_cond_t envoie[nb];
sem_t my_sem[nb];
int msgid_global;

/**
 * Gestion du Ctrl+C
 */
void signalHandler(int signal)
{
	if(signal==SIGINT){
		printf("\nCHAINE DE PRODUCTION SUSPENDU\n");
		if(msgctl(msgid_global, IPC_RMID,NULL) ==-1){
            perror("Erreur lors de la suppression de la file");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
	}
}
		
		
void erreur(const char *msg){ perror(msg); }

/**
 * Etablissement de la liaison entre un atelier et son suivant
 * A l'aide de la file de message on fait passer les entrées/sorties du tube
 */
void message_tubeS(int tubeS_envoi[2], int tubeS_reception[2], atelier* s){

    int tailleMsg;
    int msgid = msgget(cle, 0);
    tailleMsg = sizeof(tMessage) - sizeof(long);

    tMessage demande;
    msgrcv(msgid, &demande, tailleMsg, s->id, 0);

    for (int i = 0; i < 2; i++) {
        tubeS_envoi[i] = demande.tubeP_reception[i];
        tubeS_reception[i] = demande.tubeP_envoi[i];
    }
}

/**
 * Etablissement de la liaison entre un atelier et son précédent
 * A l'aide de la file de message on fait passer les entrées/sorties du tube
 */
void message_tubeP(int tubeP_envoi[2], int tubeP_reception[2], atelier* s){
    int tailleMsg;
    int msgid = msgget(cle, 0);
    tailleMsg = sizeof(tMessage) - sizeof(long);

    tMessage demande;
    demande.id = s->id_prec;

    for (int i = 0; i < 2; i++) {
        demande.tubeP_envoi[i] = tubeP_envoi[i];
        demande.tubeP_reception[i] = tubeP_reception[i];
    }
    msgsnd(msgid, &demande, tailleMsg, 0);
}

/**
 * Dans cette fonction on attend la reception d'une commande
 * Tant que rien n'est lu dans le tube, on attend
 * On recoit une commande de l'atelier suivant
 */
int attendre_commande(int descripteur_lecture, atelier *s){
	int message;
	if(read(descripteur_lecture, &message, sizeof(int)) != sizeof(int)){ erreur("Erreur dans le lecture"); }
	printf("\n%s%s: Reception d'une demande de %d pieces\n%s",CYAN,s->nom, message,RESET);
	s->commande = message;
	return message;
}

/**
 * Dans cette fonction on envoi la commande demandée par un atelier
 * Dans le cas où la production n'est pas fini, on attend
 */
void envoi_commande(int descripteur_ecriture, int message, atelier *s){
	pthread_mutex_lock(&mutex[s->id]);
	if(s->stock_produit < message) {
		printf("\n%s%s: Attente de la production pour envoi%s...\n%s",GREEN,s->nom,BLINK,RESET);
		pthread_cond_wait(&envoie[s->id], &mutex[s->id]);
	}
	printf("%s\n%s: Stock produit: %d%s\n",GREEN, s->nom, s->stock_produit,RESET);
	s->stock_produit-= message;
	if(write(descripteur_ecriture, &message, sizeof(int)) != sizeof(int)){ erreur("Erreur dans l'ecriture"); }
	printf("%s  -->Envoie d'une commande à Atelier %ld%s\n",GREEN,s->id+1,RESET);
	
	pthread_mutex_unlock(&mutex[s->id]);
}

/**
 * Dans cette fonction un atelier passe une commande a un autre atelier
 * Il passe une commande pour pouvoir poursuivre la production et remplir son stock
 */
void passer_commande(int descripteur_ecriture, int message, atelier *s){
	if(write(descripteur_ecriture, &message, sizeof(int)) != sizeof(int)){ erreur("Erreur dans l'ecriture"); }
	printf("\n--> %s: Passe une commande à l'atelier precedent \n",s->nom); 
}

/**
 * Dans cette fonction on receptionne une commande
 * On envoi le signal à la production lorsqu'on a recu des nouvelles matières
 */
void reception_commande(int descripteur_lecture, atelier *s){
	int message;
	if(read(descripteur_lecture, &message, sizeof(int)) != sizeof(int)){ erreur("Erreur dans le lecture");}
	pthread_mutex_lock(&mutex[s->id]);
    s->stock_matiere += message;
    printf("\n%s%s%s: Reception de %d matiere(s) - Stock : %d%s\n",CYAN,UNDERLINE,s->nom, message,s->stock_matiere,RESET);
    pthread_cond_signal(&reception[s->id]);
    pthread_mutex_unlock(&mutex[s->id]);
}

/**
 * Fonction qui envoi une commande au client final
 * Dans le cas où la production de la commande n'est pas terminée, on attend
 */
void envoie_client(atelier *s)
{
    int tailleMsg;
    int msgid = msgget(cle, 0);
    tailleMsg = sizeof(tLivraison) - sizeof(long);
    tLivraison livraison;
    livraison.id = 150;
    livraison.commandeC = s->stock_produit;

    pthread_mutex_lock(&mutex[s->id]);
	if(s->stock_produit < s->commande) {
		printf("\n%s%s: Attente de la production pour envoi%s...\n%s",GREEN,s->nom,BLINK,RESET);
		pthread_cond_wait(&envoie[s->id], &mutex[s->id]);
	}
    printf("\n\n!! COMMANDE DE %d PIECE(S) TERMINEE !!\n", s->stock_produit);
    s->stock_produit = 0;
    msgsnd(msgid, &livraison, tailleMsg, 0);
    printf("%s  -->Envoie de la commande au client %s\n",GREEN,RESET);
    pthread_mutex_unlock(&mutex[s->id]);
}

/**
 * Cette fonction gere la production de chaque atelier
 * Dans le cas d'une rupture de stock, on attend de recevoir des matières
 */
void *fonc_atelier(void *p_data){
	atelier *s = p_data;
	while(1)
	{
        sem_wait (&my_sem[s->id]);
        pthread_mutex_lock(&mutex[s->id]);
        while(s->stock_produit < s->commande){
            if(s->stock_matiere < s->coeff){
                printf("\n%s%s%s: RUPTURE DE STOCK%s\n",HIGHLIGHT,RED,s->nom,RESET);
                pthread_cond_wait(&reception[s->id],&mutex[s->id]);
            }
            printf("\n    %s%s: Production en cours%s...%s\n",YELLOW,s->nom,BLINK,RESET);
            sleep(s->temps_production);
            s->stock_matiere -= s->coeff;
            s->stock_produit +=1;
        }
        pthread_cond_signal(&envoie[s->id]);
        pthread_mutex_unlock(&mutex[s->id]);
	}
	pthread_exit(NULL);
	
}

/**
 * Cette fonction s'occupe de la gestion de la ligne de production concue par l'utilisateur
 * Elle gère l'enoie et la réception de commandes
 */	
void *fonc_gestion(void *p_data){
    atelier *s = p_data;
    
    //creation d'un thread chargé de la production
    pthread_t thread_production;
    pthread_create(&thread_production,0, (void*(*)())fonc_atelier, s);

    //Etablissement des tubes grace au fil des messages
    int tubeP_envoi[2];
    int tubeP_reception[2];
    if (pipe(tubeP_envoi) == -1 || pipe(tubeP_reception) == -1){ erreur("Erreur dans le tube"); }
    if(s->position !=0){ message_tubeP(tubeP_envoi, tubeP_reception, s); } //Pas d'atelier précédent donc pas d'établissement de liaison

    int tubeS_envoi[2];
    int tubeS_reception[2];
    if(s->position !=1 ){ message_tubeS(tubeS_envoi, tubeS_reception, s); } //Pas d'atelier suivant donc pas d'établissement de liaison
    //Fin des etablissement des tubes

    switch(s->position){
        case 1: //atelier le plus loin du client dans la chaine de production 
		s->commande = commande_client;
		int rep = 1;
		while(rep == 1){
            s->commande = commande_client;
			sem_post (&my_sem[s->id]); //Lancement de la production
		   	while (s->stock_produit < commande_client){   
                if(s->commande*s->coeff > s->capacite)  
                    passer_commande(tubeP_envoi[1], (s->capacite/s->coeff)*s->coeff,s); //On rempli le stock
                else 
                    passer_commande(tubeP_envoi[1], s->commande*s->coeff,s);
                    reception_commande(tubeP_reception[0],s);
		    	}
		    	envoie_client(s);
                sleep(1);
                do{
                    printf("\nRelancer une production ?(1:oui 0:Non) \n  -> Reponse : ");
                    scanf("%d",&rep);
           		}while(!(rep==0 || rep==1));
            
                if(rep==1) {
                    printf("\nCombien de piece voulez-vous ?\n");
                    scanf("%d", &commande_client);
                    s->stock_produit=0;
                }
		
                if (rep == 0)
                    exit(EXIT_SUCCESS);
			}
            break;

        case 0 : //atelier le plus pres du client dans la chaine de production
            while (attendre_commande(tubeS_reception[0],s)){
                sem_post (&my_sem[s->id]); //Lancement de la production
                envoi_commande(tubeS_envoi[1], s->commande,s);
            }
            break;

        default: //ateliers intermediaires 
            while(attendre_commande(tubeS_reception[0],s)){
                sem_post (&my_sem[s->id]); //Lancement de la production
                while(s->commande > s->stock_produit){
                    if(s->commande*s->coeff > s->capacite)  
                        passer_commande(tubeP_envoi[1], (s->capacite/s->coeff)*s->coeff,s); //On rempli le stock
                    else
                        passer_commande(tubeP_envoi[1], s->commande*s->coeff,s);
                    reception_commande(tubeP_reception[0],s);
                }
                envoi_commande(tubeS_envoi[1], s->commande,s);
            }
            break;
    }
    pthread_join(thread_production, NULL);
    pthread_exit(NULL);
}

/**
 * Cette fonction permet la reception d'une commande par le client
 */
void *fonc_client()
{
	int tailleMsg;
    int msgid = msgget(cle, 0);
    tailleMsg = sizeof(tLivraison) - sizeof(long);

    tLivraison livraison;
    while(msgrcv(msgid, &livraison, tailleMsg, 150, 0))
        printf("\n%sClient: Reception de la commande de %d piece(s)%s\n",CYAN, livraison.commandeC, RESET);
    
	pthread_exit(NULL);
}

int main(void){
	
	int nb_atelier;
	int compteur=0;	

	//Gestion du signal CTRL+C
	struct sigaction action,oldAction;
	action.sa_handler = signalHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;
	sigaction(SIGINT,&action,&oldAction);	

    system("clear");

	printf("\n\t\t\t\t%sATELIER DE PRODUCTION%s\n",RED,RESET);
	printf("\n%sCréation de votre atelier : %s\n",UNDERLINE,RESET);
	printf("\n\tCombien d'ateliers voulez-vous mettre en place ? ");
	scanf("%d",&nb_atelier);
    
	while(nb_atelier <=1) {
		printf("\n>>Erreur : Veuillez rentrer un nombre d'ateliers supérieur à 1: ");
		scanf("%d", &nb_atelier);
	}
	
	atelier tabAtelier[nb_atelier];
    
	while(compteur<nb_atelier){
		printf("\n %sSaisir les informations de votre %de atelier : %s",YELLOW, compteur+1,RESET);
		printf("\n	>Nom de l'atelier: ");
		scanf("%s",tabAtelier[compteur].nom);//A REVOIR
        
        if(compteur==0)
        {
            printf("\n	>Capacite du stock de matiere: infini\n");
            tabAtelier[compteur].capacite = 10000000;
        }
        else{
            printf("\n	>Capacite du stock de matiere: ");
            scanf("%d", &tabAtelier[compteur].capacite);
            
            while(tabAtelier[compteur].capacite <= 0){
                printf("\n	>>Erreur : Veuillez entrer un nombre superieur à 0 : ");
                scanf("%d", &tabAtelier[compteur].capacite);
            }
        }
        
        printf("\n	>Nombre de matiere necessaire à la fabrication d'un produit: ");
		scanf("%d", &tabAtelier[compteur].coeff);
        
        while(tabAtelier[compteur].coeff > tabAtelier[compteur].capacite || tabAtelier[compteur].coeff <= 0){
            printf("\n	>>Erreur : Veuillez entrer un nombre positif inferieur ou égal à la capacite du stock de matiere (%d) : ",tabAtelier[compteur].capacite);
            scanf("%d", &tabAtelier[compteur].coeff);
        }
        
		printf("\n	>Temps de production: ");
		scanf("%d", &tabAtelier[compteur].temps_production);
        
		tabAtelier[compteur].stock_produit =0;
		tabAtelier[compteur].commande=0;
        tabAtelier[compteur].stock_matiere = tabAtelier[compteur].capacite;
		if(compteur == nb_atelier-1){ 
			tabAtelier[compteur].id = compteur+1;
			tabAtelier[compteur].id_prec=compteur;
			tabAtelier[compteur].position = 1;
		}
		else if(compteur==0){
			tabAtelier[compteur].id = compteur+1;
			tabAtelier[compteur].id_prec=-1;
			tabAtelier[compteur].position = 0;
		}
		else{
			tabAtelier[compteur].id = compteur+1;
			tabAtelier[compteur].id_prec=compteur;
			tabAtelier[compteur].position =-1;
		}
		compteur++;
	}

	system("clear");
	
    printf("\n\t\t\t%sMise en place de la chaine de production%s\n",RED,RESET);
    sleep(1);
    for(int i = 0; i<nb_atelier;i++)
    {
        printf("\n%s%s%s :", UNDERLINE,tabAtelier[i].nom,RESET);
        printf("\n  Capacité du stock de matiere : %d", tabAtelier[i].capacite);
        printf("\n  Matiere necessaire pour produire une unite : %d", tabAtelier[i].coeff);
        printf("\n  Temps de production : %d\n", tabAtelier[i].temps_production);
        sleep(1);
    }
    
    do{
        printf("\nCombien voulez-vous de piece(s) ?  -> ");
        scanf("%d",&commande_client);
    }while(commande_client <=0);

    printf("\n\t\t\t%sLancement de la production%s\n",RED,RESET);

    //Initialisation semaphore
	for(int j=0; j<nb_atelier;j++)
		sem_init(&my_sem[j],0,0);
    //Initialisation des conditions et des mutex	
    for(int k=0;k<nb_atelier;k++){
		pthread_mutex_init(&mutex[k],0);
		pthread_cond_init(&reception[k],0);
		pthread_cond_init(&envoie[k],0);
	}
	
    //Creation de la file de message
    int msgid = msgget(cle,IPC_CREAT|IPC_EXCL|0600);
    msgid = msgget(cle, 0);
    msgid_global = msgid;
    
      //Creation du thread client
	pthread_t t_client;
	pthread_create(&t_client,0,(void *(*)())fonc_client,0);

    //Creation de nos threads représentant chaque atelier
    pthread_t atelier[nb_atelier];

    for(int i = 0; i<nb_atelier;i++)
        pthread_create(atelier+i,0, (void*(*)())fonc_gestion, &tabAtelier[i]);

    for(int i = 0 ; i<nb_atelier;i++)
        pthread_join(atelier[i], NULL);
	pthread_join(t_client,NULL);

    //Supprimer la file de message
    if(msgctl(msgid, IPC_RMID,NULL) ==-1){
        perror("Erreur lors de la suppression de la file");
        exit(EXIT_FAILURE);
    }

    // libération des ressources
	for(int i =0; i<nb_atelier;i++){
        pthread_mutex_init(&mutex[i],0);
        pthread_cond_destroy(&envoie[i]);
        pthread_cond_destroy(&reception[i]);
    }

    exit(0);
}
