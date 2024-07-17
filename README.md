### Descrizione del Progetto

StringKeeper è un **progetto universitario** realizzato per il corso di Laboratorio II della triennale di 
informatica presso l'Università di Pisa. Il progetto prevede la gestione e il conteggio di stringhe utilizzando 
un sistema multithread in C. Il programma implementa una tabella hash per memorizzare le stringhe e monitorarne 
il numero di occorrenze. Il sistema interagisce con un server Python, che riceve le stringhe dai client e le 
inoltra al programma principale.
Funzionalità Principali

- Memorizzazione e Conteggio: Memorizza le stringhe in una tabella hash e ne conteggia le occorrenze.
- Multithreading: Supporta operazioni simultanee di lettura e scrittura attraverso thread dedicati.
- Gestione FIFO: Utilizza FIFO (named pipes) per la comunicazione tra il server e il programma.
- Gestione dei Segnali: Gestisce segnali come SIGINT e SIGTERM per operazioni di terminazione pulita e reportistica.

### Struttura del Progetto

- archivio.c: Implementazione principale con gestione della tabella hash e thread.
- server.py: Server Python che gestisce le connessioni dei client e inoltra i dati.
- client1: Client per l'invio di stringhe singole al server.
- client2: Client per l'invio di più file di testo tramite thread.
