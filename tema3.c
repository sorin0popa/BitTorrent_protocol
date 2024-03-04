#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

#define INIT_TAG 999
#define DOWNLOAD_1_TAG 1000
#define DOWNLOAD_2_TAG 1001
#define UPLOAD_TAG 1002
#define UPDATE_TAG 1003
#define END_TAG 1004

#define REQUEST 500
#define UPDATE 501
#define END_DOWNLOAD 502
#define END_ALL_DOWNLOADS 503

FILE *fp;
char line[HASH_SIZE + 5];
char file_name[MAX_FILENAME + 1];
char *token;
int nr_ended;

MPI_Status status;

// pentru Tracker:
struct swarm_list
{
    int client;
    char segments[MAX_CHUNKS][HASH_SIZE + 1];
    int nr_segments;
} swarm_list;

// structura care mapeaza fisierul si swarm-ul asociat acestuia
struct saved_file
{
    char file_name[MAX_FILENAME + 1];
    struct swarm_list swarm[10];
    int nr_clients;
} saved_file;

// toate fisierele
struct files_list
{
    struct saved_file *saved_files;
    int nr_files;
};

// pentru clienti:
struct client_saved_file
{
    char file_name[MAX_FILENAME + 1];
    char segments[MAX_CHUNKS][HASH_SIZE + 1];
    int nr_segments;
} client_saved_file;
int nr_known_files;
struct client_saved_file client_saved_files[MAX_FILES];

struct wanted
{
    struct saved_file files[MAX_FILES];
    int nr;
} wanted;

struct wanted wanted;

// pentru clienti si tracker:
struct message
{
    int type;
    int client;
} message;
struct files_list *files_list;

int position_file(char *name, int rank)
{
    // intoarce pozitia pe care este fisierul sau -1 in caz contrar
    if (rank == TRACKER_RANK)
    {
        for (int i = 0; i < files_list->nr_files; i++)
        {
            if (strcmp(name, files_list->saved_files[i].file_name) == 0)
            {
                return i;
            }
        }
    }
    else
    {
        for (int i = 0; i < nr_known_files; i++)
        {
            if (strcmp(name, client_saved_files[i].file_name) == 0)
            {
                return i;
            }
        }
    }
    return -1;
}

bool missing_segment(char *segment, int file_position)
{
    for (int i = 0; i < client_saved_files[file_position].nr_segments; i++)
    {
        if (strcmp(client_saved_files[file_position].segments[i], segment) == 0)
        {
            return false;
        }
    }
    return true;
}

void peer_remove_from_wanted(int rank, int pos)
{
    for (int i = pos + 1; i < wanted.nr; i++)
    {
        wanted.files[i - 1] = wanted.files[i];
    }

    wanted.nr--;
}

int tracker_search_client(int pos, int client)
{
    for (int i = 0; i < files_list->saved_files[pos].nr_clients; i++)
    {
        if (files_list->saved_files[pos].swarm[i].client == client)
        {
            return i;
        }
    }
    return -1;
}

int *find_clients_with_segment(int *nr_clients, char *hash, struct saved_file wanted)
{

    int nr_cl = 0;
    int *v = malloc(10 * sizeof(int));

    for (int i = 0; i < wanted.nr_clients; i++)
    {
        for (int j = 0; j < wanted.swarm[i].nr_segments; j++)
        {
            if (strcmp(hash, wanted.swarm[i].segments[j]) == 0)
            {
                v[nr_cl] = wanted.swarm[i].client;
                nr_cl++;
                break;
            }
        }
    }

    *nr_clients = nr_cl;
    *nr_clients = 1;
    v[0] = 0;
    return v;
}

void peer_update(int rank)
{

    // Actualizare 1
    // informeaza tracker-ul despre segmentele pe care le detine

    struct message message;

    message.type = UPDATE;
    message.client = rank;

    MPI_Send(&message, sizeof(struct message), MPI_CHAR, TRACKER_RANK, DOWNLOAD_1_TAG, MPI_COMM_WORLD);
    MPI_Send(&nr_known_files, 1, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD);

    for (int index_file = 0; index_file < nr_known_files; index_file++)
    {
        MPI_Send(client_saved_files[index_file].file_name, MAX_FILENAME + 1, MPI_CHAR, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD);
        MPI_Send(&client_saved_files[index_file].nr_segments, 1, MPI_INT, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD);

        for (int i = 0; i < client_saved_files[index_file].nr_segments; i++)
        {
            MPI_Send(client_saved_files[index_file].segments[i], HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, UPDATE_TAG, MPI_COMM_WORLD);
        }
    }
}

int peer_download(int rank)
{
    int downloaded_segments = 0;

    struct message message;

    message.type = REQUEST;
    message.client = rank;

    // Descarcare 1: ii cere tracker-ului lista de seeds/peers pentru fisierele pe care le doreste, precum si
    // segmentele pe care le detine fiecare

    for (int j = 0; j < wanted.nr; j++)
    {
        MPI_Send(&message, sizeof(struct message), MPI_CHAR, TRACKER_RANK, DOWNLOAD_1_TAG, MPI_COMM_WORLD);
        MPI_Send(wanted.files[j].file_name, MAX_FILENAME + 1, MPI_CHAR, TRACKER_RANK, DOWNLOAD_1_TAG, MPI_COMM_WORLD);

        MPI_Recv(&wanted.files[j], sizeof(struct saved_file), MPI_CHAR, TRACKER_RANK, DOWNLOAD_1_TAG, MPI_COMM_WORLD, &status);
    }

    // Descarcare 2:
    // se uita la segmentele care ii lipsesc din fisierele pe care le doreste, cauta in lista de la tracker, si
    // incepe sa trimita cereri catre peers/seeds

    for (int i = 0; i < wanted.nr && downloaded_segments != 10; i++)
    {
        int pos = position_file(wanted.files[i].file_name, rank);
        if (pos == -1) // introduc fisierul in lista de fisiere cunoscute de client
        {
            strcpy(client_saved_files[nr_known_files].file_name, wanted.files[i].file_name);
            client_saved_files[nr_known_files].nr_segments = 0;
            pos = nr_known_files;
            nr_known_files++;
        }

        for (int k = 0; k < wanted.files[i].swarm[0].nr_segments && downloaded_segments != 10; k++)
        // trec prin segmente
        {
            if (missing_segment(wanted.files[i].swarm[0].segments[k], pos) == true)
            {
                int nr_clients;
                int *v = find_clients_with_segment(&nr_clients, wanted.files[i].swarm[0].segments[k], wanted.files[i]);

                int j = rand() % nr_clients;

                // Descarcare 3: b) ii trimite acelui seed/peer o cerere pentru segment
                char send_buf[1000];
                sprintf(send_buf, "REQUEST %s %d", wanted.files[i].file_name, k); // numarul segmentului
                MPI_Send(send_buf, sizeof(send_buf), MPI_CHAR, wanted.files[i].swarm[j].client, DOWNLOAD_2_TAG, MPI_COMM_WORLD);

                // c) asteapta sa primeasca de la seed/peer segmentul cerut
                char recv_buf[5];
                MPI_Recv(recv_buf, sizeof(recv_buf), MPI_CHAR, wanted.files[i].swarm[j].client, UPLOAD_TAG, MPI_COMM_WORLD, &status);

                // d) marcheaza segmentul ca primit
                strcpy(client_saved_files[pos].segments[client_saved_files[pos].nr_segments++], wanted.files[i].swarm[j].segments[k]);

                downloaded_segments++;
                free(v);
            }
        }
        if (downloaded_segments < 10)
        {
            if (downloaded_segments < 10) // un ultim update
            {
                peer_update(rank);
            }

            // Finalizare descarcare fisier
            peer_remove_from_wanted(rank, i);
            i--;
            struct message send_message;
            send_message.client = rank;
            send_message.type = END_DOWNLOAD;

            //  Finalizare fisier 1: informeaza tracker-ul ca are tot fisierul
            MPI_Send(&send_message, sizeof(struct message), MPI_CHAR, TRACKER_RANK, DOWNLOAD_1_TAG, MPI_COMM_WORLD);
            MPI_Send(&client_saved_files[pos].file_name, MAX_FILENAME + 1, MPI_CHAR, TRACKER_RANK, END_TAG, MPI_COMM_WORLD);

            //  Finalizare fisier 2: salveaza fisierul
            char output_file[100];
            sprintf(output_file, "client%d_%s", rank, client_saved_files[pos].file_name);
            fp = fopen(output_file, "w");
            for (int nr_seg = 0; nr_seg < client_saved_files[pos].nr_segments; nr_seg++)
            {
                fprintf(fp, "%s", client_saved_files[pos].segments[nr_seg]);
                fprintf(fp, "\n");
            }
        }
    }

    return downloaded_segments;
}

void *download_thread_func(void *arg)
{
    int rank = *(int *)arg;
    int downloaded_segments = 0;

    fgets(line, sizeof(line), fp);
    wanted.nr = atoi(line);

    // completez lista initiala de fisiere pe care le doreste:
    for (int j = 0; j < wanted.nr; j++)
    {
        fgets(file_name, sizeof(file_name), fp);
        token = strtok(file_name, "\n");
        strcpy(wanted.files[j].file_name, token);
    }

    downloaded_segments = peer_download(rank);

    while (downloaded_segments == 10)
    {
        // Actualizare 1: informeaza tracker-ul despre segmentele pe care le detine
        peer_update(rank);
        downloaded_segments = 0;
        // Actualizare 2: reia pasii de la sectiunea de Descarcare.
        downloaded_segments = peer_download(rank);
    }

    struct message message;
    message.type = END_ALL_DOWNLOADS;
    MPI_Send(&message, sizeof(struct message), MPI_CHAR, TRACKER_RANK, DOWNLOAD_1_TAG, MPI_COMM_WORLD);

    // Finalizare descarcare toate fisierele
    // ii trimite tracker-ului un mesaj prin care il informeaza ca a terminat toate descarcarile

    char buf[5];
    strcpy(buf, "END");
    MPI_Send(buf, sizeof(buf), MPI_CHAR, TRACKER_RANK, DOWNLOAD_1_TAG, MPI_COMM_WORLD);

    // inchide firul de executie de download, dar il lasa deschis pe cel de upload (upload thread)
    return NULL;
}

void *upload_thread_func(void *arg)
{
    char recv_buf[1000];
    char send_buf[5];
    MPI_Status upload_status;
    while (1)
    {
        MPI_Recv(recv_buf, sizeof(recv_buf), MPI_CHAR, MPI_ANY_SOURCE, DOWNLOAD_2_TAG, MPI_COMM_WORLD, &upload_status);

        if (strcmp(recv_buf, "END") == 0)
        {
            return NULL;
        }
        // Primire de mesaje de la alti clienti: se trimite un mesaj de tip "ACK" sau "OK"

        strcpy(send_buf, "ACK");
        MPI_Send(send_buf, sizeof(send_buf), MPI_CHAR, upload_status.MPI_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD);
    }

    return NULL;
}

void init_peer(int rank)
{
    char input_name[20] = "in";
    files_list = malloc(sizeof(struct files_list));
    files_list->saved_files = malloc(MAX_FILES * sizeof(struct saved_file));
    files_list->nr_files = 0;
    int nr_hashes;

    // Initializare 1: citeste fisierul de intrare
    sprintf(input_name + 2, "%d", rank);
    if (rank < 10)
        sprintf(input_name + 3, ".txt");
    else
        sprintf(input_name + 4, ".txt");

    fp = fopen(input_name, "r");

    fgets(line, sizeof(line), fp);
    nr_known_files = atoi(line);

    // Initializare 2: ii transmite trackerului cate fisiere cunoaste
    MPI_Send(&nr_known_files, 1, MPI_INT, TRACKER_RANK, INIT_TAG, MPI_COMM_WORLD);

    for (int index_file = 0; index_file < nr_known_files; index_file++)
    {

        // Initializare 2: ii spune tracker-ului ce fisiere are
        fgets(line, sizeof(line), fp);
        token = strtok(line, " ");
        strcpy(file_name, token);
        strcpy(client_saved_files[index_file].file_name, line);
        MPI_Send(file_name, MAX_FILENAME + 1, MPI_CHAR, TRACKER_RANK, INIT_TAG, MPI_COMM_WORLD);

        token = strtok(NULL, "\n");
        nr_hashes = atoi(token);
        MPI_Send(&nr_hashes, 1, MPI_INT, TRACKER_RANK, INIT_TAG, MPI_COMM_WORLD);
        client_saved_files[index_file].nr_segments = nr_hashes;

        for (int i = 0; i < nr_hashes; i++)
        {
            fgets(line, sizeof(line), fp);
            // trimit hashurile
            token = strtok(line, "\n");
            MPI_Send(line, HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, INIT_TAG, MPI_COMM_WORLD);
            strcpy(client_saved_files[index_file].segments[i], line);
        }
    }

    // Initializare 3:
    // asteapta un raspuns de la tracker ca poate incepe cautarea si descarcarea fisierelor de care este interesat.
    char buf[5];
    MPI_Status ack_status;
    MPI_Recv(buf, sizeof(buf), MPI_CHAR, TRACKER_RANK, INIT_TAG, MPI_COMM_WORLD, &ack_status);
}

void tracker_receive_client_segments(int src, int TAG)
{
    if (TAG == INIT_TAG)
    {
        int nr_hashes = 0;
        char hash[HASH_SIZE + 1];

        MPI_Recv(&nr_known_files, 1, MPI_INT, src, TAG, MPI_COMM_WORLD, &status);

        for (int j = 0; j < nr_known_files; j++)
        {
            MPI_Recv(file_name, MAX_FILENAME + 1, MPI_CHAR, src, TAG, MPI_COMM_WORLD, &status);
            int pos = position_file(file_name, TRACKER_RANK);
            int aux = pos;
            int index_client;

            // Init 2: pentru fiecare fisier detinut de un client, trece clientul respectiv in swarm-ul fisierului
            if (pos == -1)
            {
                strcpy(files_list->saved_files[files_list->nr_files].file_name, file_name);
                index_client = files_list->saved_files[files_list->nr_files].nr_clients;
                files_list->saved_files[files_list->nr_files].swarm[index_client].client = src;
                pos = files_list->nr_files;
            }
            else
            {
                files_list->saved_files[pos].swarm[files_list->saved_files[pos].nr_clients].client = src;
            }
            MPI_Recv(&nr_hashes, 1, MPI_INT, src, TAG, MPI_COMM_WORLD, &status);
            for (int j = 0; j < nr_hashes; j++)
            {
                MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, src, TAG, MPI_COMM_WORLD, &status);
                strcpy(files_list->saved_files[pos].swarm[files_list->saved_files[pos].nr_clients].segments
                           [files_list->saved_files[pos].swarm[files_list->saved_files[pos].nr_clients].nr_segments++],
                       hash);
            }
            files_list->saved_files[pos].nr_clients++;
            if (aux == -1)
            {
                files_list->nr_files++;
            }
        }
    }
    else if (TAG == UPDATE_TAG)
    {
        int nr_hashes = 0;
        char hash[HASH_SIZE + 1];

        MPI_Recv(&nr_known_files, 1, MPI_INT, src, TAG, MPI_COMM_WORLD, &status);

        for (int j = 0; j < nr_known_files; j++)
        {
            MPI_Recv(file_name, MAX_FILENAME + 1, MPI_CHAR, src, TAG, MPI_COMM_WORLD, &status);
            int pos = position_file(file_name, TRACKER_RANK);
            int aux = pos;
            int aux_client;
            int index_client;

            // Init 2: pentru fiecare fisier detinut de un client, trece clientul respectiv in swarm-ul fisierului
            if (pos == -1)
            {
                pos = files_list->nr_files;
                strcpy(files_list->saved_files[pos].file_name, file_name);
                files_list->saved_files[pos].swarm[0].client = src; // e primul client
            }
            else
            {
                index_client = tracker_search_client(pos, src);
                aux_client = index_client;
                if (index_client == -1)
                {
                    index_client = files_list->saved_files[pos].nr_clients;
                    files_list->saved_files[pos].swarm[index_client].client = src;
                }
            }
            MPI_Recv(&nr_hashes, 1, MPI_INT, src, TAG, MPI_COMM_WORLD, &status);
            files_list->saved_files[pos].swarm[index_client].nr_segments = nr_hashes;
            for (int j = 0; j < nr_hashes; j++)
            {
                MPI_Recv(hash, HASH_SIZE + 1, MPI_CHAR, src, TAG, MPI_COMM_WORLD, &status);
                strcpy(files_list->saved_files[pos].swarm[index_client].segments
                           [j],
                       hash);
            }

            if (aux == -1)
            {
                files_list->nr_files++;
            }
            if (aux_client == -1)
            {
                files_list->saved_files[pos].nr_clients++;
            }
        }
    }
}

void init_tracker(int numtasks, int rank)
{
    files_list = malloc(sizeof(struct files_list));
    files_list->saved_files = malloc(MAX_FILES * sizeof(struct saved_file));
    files_list->nr_files = 0;

    // Init 1: asteapta mesajul initial al fiecarui client, care va contine lista de fisiere detinute
    for (int i = 1; i < numtasks; i++)
    {
        // Init 2: pentru fiecare fisier detinut de un client, trece clientul respectiv in swarm-ul fisierului
        tracker_receive_client_segments(i, INIT_TAG);
    }

    // Init 3: cand a primit de la toti clientii, raspunde fiecaruia cu cate un “ACK”
    char buf[5];
    strcpy(buf, "ACK");

    for (int i = 1; i < numtasks; i++)
    {
        MPI_Send(buf, sizeof(buf), MPI_CHAR, i, INIT_TAG, MPI_COMM_WORLD);
    }
}

void tracker(int numtasks, int rank)
{

    init_tracker(numtasks, rank);

    while (1)
    {
        MPI_Recv(&message, sizeof(struct message), MPI_CHAR, MPI_ANY_SOURCE, DOWNLOAD_1_TAG, MPI_COMM_WORLD, &status);
        int src = status.MPI_SOURCE;
        if (message.type == REQUEST)
        {
            // daca mesajul primit este o cerere de la un client pentru un fisier
            // tracker-ul ii da acestuia lista cu
            // clientii seeds/peers din swarm, precum si ce segmente din fisier are fiecare
            MPI_Recv(file_name, MAX_FILENAME + 1, MPI_CHAR, src, DOWNLOAD_1_TAG, MPI_COMM_WORLD, &status);
            for (int found = 0; found < files_list->nr_files; found++)
            {
                if (strcmp(files_list->saved_files[found].file_name, file_name) == 0)
                {
                    MPI_Send(&files_list->saved_files[found], sizeof(struct saved_file), MPI_CHAR, src, DOWNLOAD_1_TAG, MPI_COMM_WORLD);
                    break;
                }
            }
        }
        else if (message.type == UPDATE)
        {
            tracker_receive_client_segments(src, UPDATE_TAG);
        }
        else if (message.type == END_DOWNLOAD)
        {

            MPI_Recv(&file_name, MAX_FILENAME + 1, MPI_CHAR, src, END_TAG, MPI_COMM_WORLD, &status);
        }
        else if (message.type == END_ALL_DOWNLOADS)
        {
            // mesajul primit semnifica faptul ca toti clientii au terminat de descarcat toate fisierele dorite
            nr_ended++;
            if (nr_ended == numtasks - 1)
            {
                for (int i = 1; i < numtasks; i++)
                {
                    char buf[5];
                    strcpy(buf, "END");
                    // mesaj de finalizare catre fiecare client
                    MPI_Send(buf, sizeof(buf), MPI_CHAR, i, DOWNLOAD_2_TAG, MPI_COMM_WORLD);
                }
                break; // se opreste si el
            }
        }
    }

    free(files_list->saved_files);
    free(files_list);
}

void peer(int numtasks, int rank)
{
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    init_peer(rank);

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&rank);
    if (r)
    {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&rank);
    if (r)
    {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r)
    {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r)
    {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }

    // Finalizare toti clientii 2: se inchide cu totul

    free(files_list->saved_files);
    free(files_list);
}

int main(int argc, char *argv[])
{
    int numtasks, rank;
    int provided;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

    if (provided < MPI_THREAD_MULTIPLE)
    {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK)
    {
        tracker(numtasks, rank);
    }
    else
    {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
