# Protocolul BitTorrent

## Variabile folosite:

* Lista de fisiere mentinuta de tracker este reprezentata de un vector de structuri files_list (
vector de saved_file + nr-ul de fisiere)
* saved_file reprezinta informatiile detinute despre un fisier de tracker: nume + swarm(alcatuit din rang-ul clientului, nr-ul de segmente si hashurile 
segmentelor detinute de client) + nr_clienti.
* Clientul retine fisierele cunoscute in client_saved_file (nume fisier + nr. segmente + 
hashurile segmentelor cunoscute) si fisierele dorite intr-un vector de saved_file (pus impreuna cu 
nr-ul lor in structura wanted).
* De asemenea, folosind MPI-ul se trimite intai tipul de mesaj (REQUEST, UPDATE, END_DOWNLOAD, 
END_ALL_DOWNLOADS) prin care se specifica tipul de operatie realizat de tracker.

## Tagurile folosite in MPI_Recv si MPI_Send sunt:

* INIT_TAG: partea de initializare, comunicarea intre client si tracker
* DOWNLOAD_1_TAG: partea de download, comunicarea intre client si tracker, aici se specifica si 
tipul de operatie realizat de tracker, in functie de ce trimite clientul: REQUEST, UPDATE
* DOWNLOAD_2_TAG: partea de download, comunicarea intre client si client, cererea 
* UPLOAD_TAG: partea de download, comunicarea intre client si client, raspunsul la cerere
* UPDATE_TAG: partea de actualizare, comunicarea intre client si tracker
* END_TAG: partea de finalizare, cu tipul de mesaje: END_DOWNLOAD, END_ALL_DOWNLOADS


## Functii:

* init_peer: apelata la inceputul functiei peer, citeste fisierul de intrare, salveaza fisierele 
in client_saved_file si ii transmite tracker-ului ce fisiere are si segmentele lor si asteapta 
ACK-ul
* init_tracker: asteapta mesajul initial al fiecarui client, care va contine lista de fisiere 
detinute, pentru fiecare fisier detinut de un client, trece clientul respectiv in swarm-ul 
fisierului, se actualizeaza files_list, cand a primit de la toti clientii, raspunde fiecaruia cu 
cate un "ACK"
* download_thread_func: completez lista de fisiere dorite in vectorul de saved_file din structura 
wanted, se apeleaza peer_download la fiecare 10 
segmente si trimite actualizari tracker-ului tot la fiecare 10 segmente; La final ii trimite 
tracker-ului un mesaj prin care il informeaza ca a terminat toate descarcarile
* peer_download: se face majoritatea logicii de download a fisierelor dorite: ii cere tracker-ului 
lista de seeds/peers pentru fisierele pe care le doreste, precum si segmentele pe care le detine 
fiecare, se uita la segmentele care ii lipsesc din fisierele pe care le doreste, cauta in lista de 
la tracker seeds/peers (cu find_clients_with_segment), si incepe sa trimita cereri catre peers/seeds; daca s-a terminat de download tot 
fisierul, informeaza tracker-ul ca are tot fisierul si il salveaza cu numele corespunzator; functia 
returneaza download_segments, iar in download_thread_function, daca acesta este egal cu 10, 
realizez partea de actualizare si de reapelare a functiei, altfel inseamna ca s-a terminat de 
descarcat 
* peer_update: informeaza tracker-ul despre segmentele pe care le detine
* find_clients_with_segment: apelata de peer_download: construieste un vector cu rangurile 
clientilor care detin un anumit segment, (lista de seeds/peers care au segmentul).
* alte functii care realizeaza partea de gasire/stergere fisiere, segmente, clienti, etc.

## Partea de eficienta:

*	Se foloseste randomizarea, atunci cand se doreste un segment, clientul cauta cu 
find_clients_with_segment lista de seeds/peers care au segmentul, apoi ia aleatoriu un client, ii 
trimite acelui seed/peer o cerere pentru segment, asteapta sa primeasca de la seed/peer segmentul 
cerut si marcheaza segmentul ca primit. Acest lucru se face pentru fiecare segment dorit. 
Actualizarea continua a swarm-urilor fisierelor in tracker face ca descarcarea fisierelor sa se 
faca intr-un mod eficient, prin faptul ca se cer segmente de la mai multi clienti.
