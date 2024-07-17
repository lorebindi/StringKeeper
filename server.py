#!/usr/bin/env python3

import sys, socket, logging, argparse, os, struct, signal
import threading, concurrent.futures, subprocess

# valori di default verso cui connettersi 
HOST = "127.0.0.1"  # The server's hostname or IP address
PORT = 57308        # The port used by the server
Max_sequence_length = 2048

# configurazione del logging
# il logger scrive su un file con nome uguale al nome del file eseguibile
logging.basicConfig(filename=os.path.basename(sys.argv[0])[:-3] + '.log',
                    level=logging.DEBUG, datefmt='%d/%m/%y %H:%M:%S',
                    format='%(asctime)s - %(levelname)s - %(message)s')

# Riceve esattamente n byte dal socket conn e li restituisce
# il tipo restituto è "bytes": una sequenza immutabile di valori 0-255
def recv_all(conn,n):
  chunks = b''
  bytes_recd = 0
  while bytes_recd < n:
    chunk = conn.recv(min(n - bytes_recd, 1024))
    if len(chunk) == 0:
      raise RuntimeError("socket connection broken")
    chunks += chunk
    bytes_recd = bytes_recd + len(chunk)
  return chunks

def main(max_thread, r, w, v):
  # Se capolet non è già presente la creo 
  if(not os.path.exists('capolet')): 
    os.mkfifo('capolet')
  # Se caposc non è già presente la creo 
  if(not os.path.exists('caposc')): 
    os.mkfifo('caposc')

  # Lancio archivio
  if(v):
    p = subprocess.Popen(["valgrind","--leak-check=full", 
                      "--show-leak-kinds=all", 
                      "--log-file=valgrind-%p.log", 
                      "./archivio.out", str(r), str(w)])
  else:
    p = subprocess.Popen(["./archivio.out", str(r), str(w)])

  # Apertura capolet in scrittura
  capolet = os.open('capolet', os.O_WRONLY)
  # Apertura caposc in scrittura
  caposc = os.open('caposc', os.O_WRONLY)

  # creiamo il server socket
  with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    try:  
      s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)            
      s.bind((HOST, PORT))
      s.listen()
      with concurrent.futures.ThreadPoolExecutor(max_thread) as executor:
        while True:
          # mi metto in attesa di una connessione
          conn, addr = s.accept()
          # l'esecuzione di submit non è bloccante
          # fino a quando ci sono thread liberi
          executor.submit(gestisci_connessione, conn, addr, caposc, capolet)
    except KeyboardInterrupt:
      pass
    s.shutdown(socket.SHUT_RDWR)
  p.send_signal(signal.SIGTERM)
  os.unlink('capolet')
  os.unlink('caposc')
  return

# gestisci una singola connessione con un client
def gestisci_connessione(conn,addr, caposc, capolet): 
  # in questo caso potrei usare direttamente conn
  # e l'uso di with serve solo a garantire che
  # conn venga chiusa all'uscita del blocco
  # ma in generale with esegue le necessarie
  # inzializzazione e il clean-up finale
  with conn:
    # Ricevo un byte per distinguere tra i due client
    data = recv_all(conn,1)           
    type = data.decode()
    # Client di tipo A
    if(type == "A"):
      # Inizio ricezione dati
      # Leggo 4 bytes per sapere la lunghezza della stringa
      data = recv_all(conn,4)
      assert(len(data) == 4) 
      lenght = struct.unpack("!i", data)[0]
      # Ricevo la stringa
      bstr = recv_all(conn,lenght)
      assert(len(bstr)<=Max_sequence_length), "Stringa ricevuta troppo grande"
      assert(len(bstr)==lenght), "Lunghezza attesa e lunghezza della stringa non coincidono"
      # Scrivo su capolet la lunghezza e la stringa
      #nbytes = os.write(capolet, struct.pack("<i",lenght))
      #assert (nbytes == 4)
      #rint(" [Server] Ho inviato la lunghezza: ", len(struct.pack("<i",lenght)))
      nbytes = os.write(capolet, struct.pack("<i",lenght)+bstr)
      assert (len(bstr)+4 == nbytes)
      #Scrittura sul file server.log
      logging.debug(f"Tipo conessione: A, byte scritti su capolet {lenght+4}")

    # Client di tipo B
    if(type == "B"): 
      count_byte = 0
      while(True):
        # Inizio ricezione dati.
        # Ricevo 4 bytes per sapere la lunghezza della stringa che seguirà. 
        data = recv_all(conn,4)
        assert(len(data) == 4), "Non ho ricevuto i 4 bytes della lunghezza."        
        lenght = struct.unpack("!i", data)[0]
        # Condizione di uscita -> ricevo una stringa di lunghezza zero.
        if (lenght == 0): break
        # Ricevo la stringa.
        bstr = recv_all(conn,lenght) 
        #Conversione stringa
        assert(len(bstr) == lenght), "La lunghezza dichiarata e quella della stringa non coincidono."
        # Scrittura su caposc della lunghezza e della stringa stessa.
        nbytes = os.write(caposc, struct.pack("<i",lenght)+bstr)
        assert (len(bstr)+4 == nbytes), "Non sono stati scritti su capolet tutti i bytes richiesti."
        # Aggiorno il numero di byte che sono stati scritti su caposc
        count_byte += lenght + 4
      # Scrittura sul file server.log
      logging.debug(f"Tipo conessione: B, byte scritti su caposc {count_byte}")

if __name__ == '__main__':
  parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
  parser.add_argument('max_thread', help="numero massimo di thread che può utilizzare il server", type = int) 
  parser.add_argument('-r', help="numero thread lettori", type = int, default=3)  
  parser.add_argument('-w', help="numero thread scrittori", type = int, default=3)  
  parser.add_argument('-v', help=" con valgrind o senza", action="store_true")
  args = parser.parse_args()
  assert args.max_thread > 0, "Il numero di thread lettori deve essere positivo"
  assert args.r > 0, "Il numero di thread lettori deve essere positivo"
  assert args.w > 0, "Il numero di thread scrittori deve essere positivo"
  main(args.max_thread, args.r, args.w, args.v)

            