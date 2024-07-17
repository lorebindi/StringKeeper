#define _GNU_SOURCE   /* See feature_test_macros(7) */
#include <stdio.h>    // permette di usare scanf printf etc ...
#include <stdlib.h>   // conversioni stringa/numero exit() etc ...
#include <stdbool.h>  // gestisce tipo bool (variabili booleane)
#include <assert.h>   // permette di usare la funzione assert
#include <string.h>   // confronto/copia/etc di stringhe
#include <errno.h>
#include <search.h>
#include <signal.h>
#include <unistd.h>  // per sleep 
#include <math.h>
#include "xerrori.h"

// macro per indicare la posizione corrente
#define QUI __LINE__,__FILE__
//dimensione della tabella hash
#define Num_elem 1000000
//lunghezza dei buffer produttori/consumatori
#define PC_buffer_len 10 
// nomi FIFO
#define Caposc "caposc"
#define Capolet "capolet"
// nome file
#define Filelettori "lettori.log"
// stringa delimitatori
#define Delim ".,:; \n\r\t"

// Variabile per tenere conto del numero di strighe distinte nell HT.
volatile sig_atomic_t nStringhe = 0;

// Struct per la gestione dell'accesso alla hash table
typedef struct 
{
  int readers;
  bool writing;
  pthread_cond_t cond;   // Condition variable per attesa passiva per hash table.
  pthread_mutex_t mutex; // Mutex per la gestione di letture e scritture su hash table.
}rw_HT;

// Dati thread caposcrittore.
typedef struct
{
  char **buffer_s; // Buffer condiviso con scrittori.
  int *csindex; // Indice buffer thread caposcrittore.
  sem_t *sem_free_slots_s;
  sem_t *sem_data_items_s;
}csdati;

// Dati thread scrittori.
typedef struct 
{
  rw_HT *access_HT;
  char **buffer_s; // Buffer condiviso con caposcrittore.
  int *sindex; // Indice buffer thread scrittori.
  pthread_mutex_t *bmutex_s; // Mutex per l'accesso al buffer.
  sem_t *sem_free_slots_s;
  sem_t *sem_data_items_s;
}sdati;

// Dati thread capolettore.
typedef struct 
{
  char **buffer_l; // Buffer condiviso con thread lettori
  int *clindex; // Indice buffer thread capolettore
  sem_t *sem_free_slots_l;
  sem_t *sem_data_items_l;
}cldati;

// Dati thread lettori.
typedef struct 
{
  rw_HT *access_HT;
  FILE *f; // Riferimento al file "lettori.log".
  char **buffer_l; // Buffer condiviso con capolettore.
  int *lindex; // Indice buffer thread lettori.
  pthread_mutex_t *bmutex_l; // Mutex per l'accesso al buffer.
  pthread_mutex_t *fmutex_l; // Mutex per l'accesso al file in scrittura.
  sem_t *sem_free_slots_l;
  sem_t *sem_data_items_l;
}ldati;

// Dati thread signal.
typedef struct {
  pthread_t *tcs; // Riferimento al thread caposcrittore.
  pthread_t *tcl; // Riferimento al thread capolettore.
  pthread_t *ts; // Riferimenti ai thread scrittori.
  pthread_t *tl; // Riferimenti ai thread lettori.
  int nts; // Numero thread scrittori.
  int ntl; // Numero thread lettori.
} tsignal_dati;


// inizio uso da parte di un reader sulla hash table.
void read_lock(rw_HT *z)
{
  pthread_mutex_lock(&z->mutex);
  while(z->writing==true)
    pthread_cond_wait(&z->cond, &z->mutex);   // attende fine scrittura
  z->readers++;
  pthread_mutex_unlock(&z->mutex);
}

// fine uso da parte di un reader sulla hash table.
void read_unlock(rw_HT *z)
{
  assert(z->readers>0);  // ci deve essere almeno un reader (me stesso)
  assert(!z->writing);   // non ci devono essere writer 
  pthread_mutex_lock(&z->mutex);
  z->readers--;                  // cambio di stato   
  if(z->readers==0) 
    pthread_cond_signal(&z->cond); // da segnalare ad un solo writer
  pthread_mutex_unlock(&z->mutex);
}
  
// inizio uso da parte di writer sulla hash table.  
void write_lock(rw_HT *z)
{
  pthread_mutex_lock(&z->mutex);
  while(z->writing || z->readers>0)
    // attende fine scrittura o lettura
    pthread_cond_wait(&z->cond, &z->mutex);   
  z->writing = true;
  pthread_mutex_unlock(&z->mutex);
}

// fine uso da parte di un writer sulla hash table.
void write_unlock(rw_HT *z)
{
  assert(z->writing);
  pthread_mutex_lock(&z->mutex);
  z->writing=false;               // cambio stato
  // segnala a tutti quelli in attesa 
  pthread_cond_broadcast(&z->cond);  
  pthread_mutex_unlock(&z->mutex);
}

/* crea un oggetto di tipo entry con chiave s e valore n */
ENTRY *crea_entry(char *s, int n) 
{
  ENTRY *e = malloc(sizeof(ENTRY));
  if(e==NULL) termina("errore malloc entry 1");
  e->key = strdup(s); // salva copia di s
  e->data = (int *) malloc(sizeof(int));
  if(e->key==NULL || e->data==NULL)
    termina("errore malloc entry 2");
  *((int *)e->data) = n;
  return e;
}

// Distrugge oggetti di tipo entry
void distruggi_entry(ENTRY *e)
{
  free(e->key); free(e->data); free(e);
}

/* Se la stringa s non è contenuta nella tabella hash deve essere inserita con 
valore associato uguale a 1. Se s è già contenuta nella tabella allora 
l'intero associato deve essere incrementato di 1.*/
void aggiungi (char *s) 
{
    ENTRY *e = crea_entry(s, 1);
    ENTRY *r = hsearch(*e,FIND);
    if(r==NULL) { // la entry è nuova
      r = hsearch(*e,ENTER);
      if(r==NULL) termina("errore o tabella piena");
      nStringhe = nStringhe +1 ; //Incremento il numero di stringhe distinte nella HT.
    }
    else {
      // la stringa è gia' presente incremento il valore
      assert(strcmp(e->key,r->key)==0);
      int *d = (int *) r->data;
      *d +=1;
      distruggi_entry(e); // questa non la devo memorizzare
    }
}

/* Restituisce l'intero associato ad s se è contenuta nella tabella, altrimenti 0.*/
int conta (char *s)
{
    ENTRY *e = crea_entry(s, 1);
    ENTRY *r = hsearch(*e,FIND);
    if(r==NULL){ // la stringa non è contenuta nella hash table
      distruggi_entry(e); 
      return 0;
    }
    else {
      distruggi_entry(e);
      return *((int *)r->data);
    }
}

ssize_t readn(int fd, void *ptr, size_t n) {  
   size_t   nleft;
   ssize_t  nread;
 
   nleft = n;
   while (nleft > 0) {
     if((nread = read(fd, ptr, nleft)) < 0) {
        if (nleft == n) return -1; /* error, return -1 */
        else break; /* error, return amount read so far */
     } else if (nread == 0) break; /* EOF */
     nleft -= nread;
     ptr   += nread;
   }
   return(n - nleft); /* return >= 0 */
}

// Body thread caposcrittore: 
void *csbody(void *v) 
{
  csdati *d=(csdati *) v;

  int lenght=0;
  ssize_t r;
  //Apertura FIFO "caposc" in sola lettura
  int fd = open(Caposc,O_RDONLY);
  if(fd<0) termina("[Thread cs] Errore apertura fifo");

  while(true){
    // Lettura lunghezza della prossima sequenza
    r = readn(fd,&lenght,4);
    if(r>0){
      // Lettura della sequenza
      char *s = malloc(lenght+1);
      r = readn(fd,s,lenght);
      if (r!=lenght) termina("[Thread cs] Errore scrittura pipe\n");
      // Concatenazione con il carattere finale 0
      s[lenght] = '\0';
      char *saveptr = s;
      // Tokenizzazione.
      char *token = strtok_r(s,Delim, &saveptr);
      while(token!=NULL){
        // Scrittura sul buffer della copia del token.
        xsem_wait(d->sem_free_slots_s, QUI);
        d->buffer_s[*(d->csindex)%PC_buffer_len] = strdup(token);
        *(d->csindex) += 1;
        xsem_post(d->sem_data_items_s, QUI);
        token = strtok_r(NULL, Delim, &saveptr);
      }
    free(s);
    }
    else{
      // Se non leggo nulla la FIFO è stata chiusa in scittura -> chiudo la FIFO in lettura
      if(close(fd)!=0) termina ("Errore chiusura FIFO caposc in lettura\n");
      // Scrivo "." sul buffer
      xsem_wait(d->sem_free_slots_s, QUI);
      d->buffer_s[*(d->csindex)%PC_buffer_len] = ".";
      *(d->csindex) += 1;
      xsem_post(d->sem_data_items_s, QUI);
      break;
    }
  }
  return NULL;
}

/*body thread scrittori: leggono dal buffer condiviso con il thread caposcrittore
una stringa e poi la scrivono nella hash table. Se viene letto -1 il thread si interrompe 
senza aggiornare l'indice così che tutti gli altri leggano il -1 per terminare*/
void *sbody(void *v) 
{
  sdati *d=(sdati *) v;
  char *s = NULL;
  while(true){
    // Lettura da buffer regolata da bmutex
    xsem_wait(d->sem_data_items_s, QUI); 
    xpthread_mutex_lock(d->bmutex_s, QUI);
    s = d->buffer_s[*(d->sindex)%PC_buffer_len]; // Copia della stringa
    // Condizione di uscita: la stringa letta è ".".
    if(strcmp(s,".") == 0){
      xpthread_mutex_unlock(d->bmutex_s, QUI);
      xsem_post(d->sem_data_items_s, QUI);
      break;
    }
    d->buffer_s[*(d->sindex)%PC_buffer_len] = NULL;
    *(d->sindex) += 1;  // Aggiornamento indice
    xpthread_mutex_unlock(d->bmutex_s, QUI);
    xsem_post(d->sem_free_slots_s, QUI);
    // Scrittura dentro la Hash Map
    write_lock(d->access_HT);
    aggiungi(s);
    write_unlock(d->access_HT);
    free(s);
  }
  return NULL;
}

/* Body thread capolettore: simile a caposcrittore tranne che riceve il suo input
 dalla FIFO capolet e sul buffer condiviso scrive i token e non la copia dei token*/
void *clbody(void *v)
{
  cldati *d=(cldati *) v;

  int lenght = 0;
  ssize_t r;
  //Apertura FIFO "capolet" in sola lettura
  int fd = open(Capolet,O_RDONLY);
  if(fd<0) termina(" [Thread cl] Errore apertura fifo");
  while(true){
    // Lettura lunghezza della prossima sequenza
    r = readn(fd,&lenght,4);
    if(r==4){
      // Lettura della sequenza
      char *s = malloc(lenght+1);
      r = readn(fd,s,lenght);
      if (r!=lenght){
        termina("[Thread cl] Errore scrittura pipe");
      }
      // Concatenazione con il carattere finale 0
      s[lenght] = '\0';
      char *saveptr = s;
      // Tokenizzazione.
      char *token = strtok_r(s,Delim, &saveptr);
      while(token!=NULL){
        // Scrittura sul buffer della copia del token.
        xsem_wait(d->sem_free_slots_l, QUI);
        d->buffer_l[*(d->clindex)%PC_buffer_len] = strdup(token);
        *(d->clindex) += 1;
        xsem_post(d->sem_data_items_l, QUI);
        token = strtok_r(NULL, Delim, &saveptr);
      }
      free(s);
    }
    else{
      // Se non leggo nulla la FIFO è stata chiusa in scittura -> chiudo la FIFO in lettura
      if(close(fd)!=0) termina ("Errore chiusura FIFO capolet in lettura\n");
      // Scrivo "." sul buffer
      xsem_wait(d->sem_free_slots_l, QUI);
      d->buffer_l[*(d->clindex)%PC_buffer_len] = ".";
      *(d->clindex) += 1;
      xsem_post(d->sem_data_items_l, QUI);
      break;
    }
  }
  return NULL;
}

/*body thread lettori: leggono dal buffer la stringa, la cercano nella hash table
 e scrivono nel file lettori.log l'associazione (stringa, numero). Se viene letto -1 
 il thread si interrompe senza aggiornare l'indice così che tutti gli altri leggano 
 il -1 per terminare */
void *lbody(void *v)
{
  ldati *d=(ldati *) v;
  char *s = NULL;
  while(true){
    // Lettura da buffer regolata da bmutex
    xsem_wait(d->sem_data_items_l, QUI); 
    xpthread_mutex_lock(d->bmutex_l, QUI);
    s = d->buffer_l[*(d->lindex)%PC_buffer_len]; // Copia della stringa
    // Condizione di uscita: la stringa letta è ".".
    if(strcmp(s,".") == 0){
      xpthread_mutex_unlock(d->bmutex_l, QUI);
      xsem_post(d->sem_data_items_l, QUI);
      break;
    }
    d->buffer_l[*(d->lindex)%PC_buffer_len] = NULL;
    *(d->lindex) += 1;  // Aggiornamento indice
    xpthread_mutex_unlock(d->bmutex_l, QUI);
    xsem_post(d->sem_free_slots_l, QUI);
    // Lettura dalla la Hash Map
    read_lock(d->access_HT);
    int n = conta(s);
    read_unlock(d->access_HT);
    // Scrittura sul file
    xpthread_mutex_lock(d->fmutex_l, QUI);
    fprintf(d->f,"%s %d\n", s, n);
    xpthread_mutex_unlock(d->fmutex_l, QUI);
    free(s);
  }
  return NULL;
}

void *t_sig_body(void *v){
  tsignal_dati *d=(tsignal_dati *) v;
  sigset_t mask;
  sigfillset(&mask);
  int s;

  while(true) {
    int e = sigwait(&mask,&s);
    if(e<0) perror("Errore sigwait");
    // Handler per SIGINT
    if(s == SIGINT){
      int temp = (int)nStringhe;
      fprintf(stderr,"%d",temp);
    }
    // Handler per SIGTERM
    if(s == SIGTERM){
      // Attesa terminazione thread caposcrittore
      xpthread_join(*(d->tcs),NULL,QUI);
      // Attesa terminazione thread scrittori
      for(int i=0; i<(d->nts); i++) 
          xpthread_join(d->ts[i],NULL,QUI);
      // Attesa terminazione thread capolettore
      xpthread_join(*(d->tcl),NULL,QUI);
      // Attesa terminazione thread lettori
      for(int i=0; i<(d->ntl); i++) 
          xpthread_join(d->tl[i],NULL,QUI);
      //Scrittura su stdout
      int temp = (int)nStringhe;
      fprintf(stdout,"%d",temp);
      // Deallocazione HT.
      hdestroy();
      // Terminazione thread
      return NULL;
    }
  }
}

int main(int argc, char *argv[]) 
{
  // blocco i segnali
  sigset_t mask;
  sigfillset(&mask);  // insieme di tutti i segnali
  pthread_sigmask(SIG_BLOCK,&mask,NULL); // blocco tutti i segnali

  //Apertura file lettori.log
  FILE *f;
  f=fopen(Filelettori, "wt");
  if(f==NULL) termina("Errore apertura file\n");
 
  // crea tabella hash.
  int ht = hcreate(Num_elem);
  if(ht==0 ) termina("Errore creazione HT\n");


  //Inizializzazione struct rw_ht per gestione accesso hash table.
  rw_HT *access_HT = malloc(sizeof (*access_HT));
  access_HT->readers = 0;
  access_HT->writing = false;
  xpthread_cond_init(&access_HT->cond,NULL,QUI);
  xpthread_mutex_init(&access_HT->mutex,NULL,QUI);

  // Lettura parametri da riga di comando
  int w = atoi(argv[1]); // w => numero thread scrittori.
  int r = atoi(argv[2]); // r => numero thread lettori.

  // Creazione thread capo scrittore
  pthread_t tcs;   // Identificatore del thread caposcrittore. 
  csdati dcs;      // Struct dei dati del thread caposcrittore.

  // Creazione thread scrittori
  pthread_t ts[w];   // Array di identificatori di thread scrittori.
  sdati ds[w];       // Array di struct che passerò ai thread scrittori.

  // Creazione thread capolettore
  pthread_t tcl;   // Identificatore del thread capolettore. 
  cldati dcl;      // Struct dei dati del thread capolettore.

  // Creazione thread lettori
  pthread_t tl[r];   // Array di identificatori di thread lettori.
  ldati dl[r];       // Array di struct che passerò ai thread lettori.

  // Creazione thread gestore dei segnali.
  pthread_t t_signal;
  tsignal_dati tsd;
  //Inizializzazione thread gestore segnali.
  tsd.tcs = &tcs;
  tsd.tcl = &tcl;
  tsd.ts = ts;
  tsd.tl = tl;
  tsd.nts = w;
  tsd.ntl = r;
  xpthread_create(&t_signal, NULL, &t_sig_body, &tsd, QUI);

  // Inizializzazione variabili condivise fra thread scrittori - caposcrittore.
  char **buffer_s = malloc(PC_buffer_len * sizeof(char *)); // Buffer condiviso scrittori-caposcrittore
  if(buffer_s == NULL) { termina("malloc fallita");}
  pthread_mutex_t bmutex_s; // Usato sul buffer dagli scrittori.
  xpthread_mutex_init(&bmutex_s, NULL, QUI);
  sem_t sem_data_items_s, sem_free_slots_s;
  xsem_init(&sem_data_items_s, 0, 0, QUI);
  xsem_init(&sem_free_slots_s, 0, PC_buffer_len, QUI);
  int sindex = 0; // Indice buffer scrittori.
  int csindex = 0; // Indice buffer caposcrittore.

  // Inizializzazione variabili condivise fra thread lettori - capolettore.
  char **buffer_l = malloc(PC_buffer_len * sizeof(char *)); // Buffer condiviso lettori-capolettore.
  if(buffer_s == NULL) { termina("malloc fallita");}
  pthread_mutex_t bmutex_l; // Usato sul buffer dai lettori.
  xpthread_mutex_init(&bmutex_l, NULL, QUI);
  sem_t sem_data_items_l, sem_free_slots_l;
  xsem_init(&sem_data_items_l, 0, 0, QUI);
  xsem_init(&sem_free_slots_l, 0, PC_buffer_len, QUI);
  int lindex = 0; // Indice buffer lettori.
  int clindex = 0; // Indice buffer capolettore.
  pthread_mutex_t fmutex_l; // Usato dai thread lettori per l'accesso in scrittura al file.
  xpthread_mutex_init(&fmutex_l, NULL, QUI);

  //Inizializzazione thread caposcrittore e partenza
  dcs.buffer_s = buffer_s;
  dcs.sem_data_items_s = &sem_data_items_s;
  dcs.sem_free_slots_s = &sem_free_slots_s;
  dcs.csindex = &csindex;
  xpthread_create(&tcs, NULL, &csbody, &dcs, QUI);

  //Inizializzazione thread scrittori e partenza
  for (int i=0; i<w; i++){
    ds[i].access_HT = access_HT;
    ds[i].buffer_s = buffer_s;
    ds[i].bmutex_s = &bmutex_s;
    ds[i].sem_data_items_s = &sem_data_items_s;
    ds[i].sem_free_slots_s = &sem_free_slots_s;
    ds[i].sindex = &sindex;
    xpthread_create(&ts[i], NULL, &sbody, &ds[i], QUI);
  }

  // Inizializzazione e partenza thread capolettore
  dcl.buffer_l = buffer_l;
  dcl.sem_data_items_l = &sem_data_items_l;
  dcl.sem_free_slots_l = &sem_free_slots_l;
  dcl.clindex = &clindex;
  xpthread_create(&tcl, NULL, &clbody, &dcl, QUI);

  // Inizializzazione e partenza thread lettori 
  for (int i=0; i<r; i++){
    dl[i].access_HT = access_HT;
    dl[i].f = f;
    dl[i].fmutex_l = &fmutex_l;
    dl[i].buffer_l = buffer_l;
    dl[i].bmutex_l = &bmutex_l;
    dl[i].sem_data_items_l = &sem_data_items_l;
    dl[i].sem_free_slots_l = &sem_free_slots_l;
    dl[i].lindex = &lindex;
    xpthread_create(&tl[i], NULL, &lbody, &dl[i], QUI);
  }

  // Attesa della terminazione thread dei segnali.
  xpthread_join(t_signal,NULL,QUI);

  fclose(f); // Chiusura file lettori.log
  // Liberazione memoria
  pthread_mutex_destroy(&access_HT->mutex);
  free(access_HT);
  free(buffer_l); 
  free(buffer_s);

  // Distruggo lock e semafori
  pthread_mutex_destroy(&bmutex_s);
  pthread_mutex_destroy(&bmutex_l);
  pthread_mutex_destroy(&fmutex_l);
  xsem_destroy(&sem_data_items_l,__LINE__, __FILE__);
  xsem_destroy(&sem_free_slots_l,__LINE__, __FILE__);    
  xsem_destroy(&sem_data_items_s,__LINE__, __FILE__);
  xsem_destroy(&sem_free_slots_s,__LINE__, __FILE__);

  return 0;
}
