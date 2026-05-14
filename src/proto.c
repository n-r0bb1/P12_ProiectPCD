/* proto.c – serializare / deserializare protocol */
#include "proto.h"//Include header-ul local al protocolului
                  //Acesta contine definitiile structurilor ProtoRequest si ProtoResponse
                  //precum si prototipurile functiilor definite in acest fisier

#include <stdio.h>//Include functii standard de input/output
                  //Este folosit in special pentru snprintf

#include <stdlib.h>//Include functii standard de utilitare
                   //Este folosit pentru strtol (conversie string -> numar)

#include <string.h>//Include functii pentru manipularea sirurilor de caractere
                   //Este folosit pentru strlen, strcmp si strtok_r

/* Transformă structura request într-un string pentru trimitere prin rețea */
int proto_serialize_request(const ProtoRequest *req, char *buf, size_t bufsz)
{
    /* Verificare parametri de intrare */
    if (!req || !buf || bufsz == 0)//Verificam daca pointerii sunt invalizi sau daca bufferul are dimensiune 0
                                   //Cazuri invalide:
                                   // - req == NULL (nu exista date de serializat)
                                   // - buf == NULL (nu exista buffer de iesire)
                                   // - bufsz == 0 (buffer inutilizabil)
    {
        return -1;//Returnam eroare deoarece nu putem construi mesajul
    }

    /* Construire mesaj tip text: REQ|video|chunk|threshold */
    int ret = snprintf(buf, bufsz, "REQ|%s|%d|%d\n",
                       req->video_path, req->chunk_size, req->threshold);
    //Construim un mesaj text formatat pentru transmisie in retea
    //Formatul este: REQ|<video_path>|<chunk_size>|<threshold>
    //snprintf este folosit pentru a preveni overflow in buffer

    /* Verificare dacă bufferul a fost suficient de mare */
    if (ret < 0 || (size_t)ret >= bufsz)//Verificam daca scrierea a esuat sau daca mesajul a fost trunchiat
    {
        return -1;//Returnam eroare deoarece serializarea nu este valida
    }

    return 0;//Returnam succes deoarece mesajul a fost construit corect
}

/* Parsează un request primit sub formă de string */
int proto_parse_request(const char *buf, ProtoRequest *out)
{
    /* Validare input */
    if (!buf || !out)//Verificam daca pointerii de intrare sunt validi
                     //Cazuri invalide:
                     // - buf == NULL (nu exista mesaj de parsare)
                     // - out == NULL (nu exista structura unde sa scriem rezultatul)
    {
        return -1;//Returnam eroare deoarece nu putem procesa datele
    }

    /* Copiere buffer pentru a-l putea modifica (strtok modifică stringul) */
    char tmp[PROTO_BUF_SIZE];//Buffer local folosit pentru a copia mesajul original
                            //Este necesar deoarece strtok_r modifica sirul de intrare

    if (snprintf(tmp, sizeof(tmp), "%s", buf) < 0)//Copiem continutul mesajului in bufferul local
    {
        return -1;//Eroare la copierea stringului
    }

    /* Eliminare newline de la final dacă există */
    size_t len = strlen(tmp);//Calculam lungimea stringului primit

    if (len > 0 && tmp[len - 1] == '\n')//Verificam daca ultimul caracter este newline
    {
        tmp[len - 1] = '\0';//Inlocuim newline cu terminator de string
    }

    /* Separare string după delimitatorul '|' */
    char *saveptr = NULL;//Pointer auxiliar folosit de strtok_r pentru starea internă
    char *tok = strtok_r(tmp, "|", &saveptr);//Extragem primul token din mesaj

    /* Primul câmp trebuie să fie "REQ" */
    if (!tok || strcmp(tok, "REQ") != 0)//Verificam daca mesajul incepe corect cu "REQ"
    {
        return -1;//Daca nu, mesaj invalid
    }

    /* Al doilea câmp: path video */
    tok = strtok_r(NULL, "|", &saveptr);//Extragem urmatorul token (calea video)

    if (!tok)//Verificam daca tokenul exista
    {
        return -1;//Eroare de format mesaj
    }

    /* Copiere sigură a path-ului în struct */
    if (snprintf(out->video_path, sizeof(out->video_path), "%s", tok) < 0)//Copiem path-ul in structura destinatie
    {
        return -1;//Eroare la copiere
    }

    /* Al treilea câmp: chunk_size */
    tok = strtok_r(NULL, "|", &saveptr);//Extragem chunk_size din mesaj

    if (!tok)//Verificam daca exista valoare
    {
        return -1;//Mesaj invalid
    }

    char *endptr = NULL;//Pointer folosit pentru validarea conversiei numerice
    long chunk = strtol(tok, &endptr, 10);//Convertim stringul in numar intreg

    /* Validare chunk_size (trebuie să fie > 0) */
    if (endptr == tok || *endptr != '\0' || chunk <= 0)//Verificam daca conversia a reusit si daca valoarea este valida
    {
        return -1;//Valoare invalida
    }

    out->chunk_size = (int)chunk;//Stocam valoarea validata in structura

    /* Al patrulea câmp: threshold */
    tok = strtok_r(NULL, "|", &saveptr);//Extragem threshold

    if (!tok)//Verificam existenta valorii
    {
        return -1;//Mesaj incomplet
    }

    endptr = NULL;//Resetam pointerul de validare
    long thr = strtol(tok, &endptr, 10);//Convertim threshold in numar

    /* Validare threshold (>= 0) */
    if (endptr == tok || *endptr != '\0' || thr < 0)//Verificam validitatea conversiei
    {
        return -1;//Valoare invalida
    }

    out->threshold = (int)thr;//Salvam valoarea in structura

    return 0;//Returnam succes daca parsarea a fost corecta
}

/* Serializare response către string pentru client */
int proto_serialize_response(const ProtoResponse *res, char *buf, size_t bufsz)
{
    /* Validare parametri */
    if (!res || !buf || bufsz == 0)//Verificam validitatea parametrilor de intrare
    {
        return -1;//Eroare daca datele nu sunt valide
    }

    /* Format: RES|<valoare>\n */
    int ret = snprintf(buf, bufsz, "RES|%ld\n", res->no_motion_count);
    //Construim mesajul de raspuns pentru client
    //Contine tipul RES si valoarea numarului de frame-uri fara miscare

    /* Verificare overflow buffer */
    if (ret < 0 || (size_t)ret >= bufsz)//Verificam daca scrierea a fost valida
    {
        return -1;//Eroare daca bufferul este prea mic
    }

    return 0;//Returnam succes
}

/* Parsează response primit de la server */
int proto_parse_response(const char *buf, ProtoResponse *out)
{
    /* Validare input */
    if (!buf || !out)//Verificam daca pointerii sunt validi
    {
        return -1;//Eroare daca lipsesc datele
    }

    /* Copiere buffer pentru modificare */
    char tmp[PROTO_BUF_SIZE];//Buffer local pentru procesare

    if (snprintf(tmp, sizeof(tmp), "%s", buf) < 0)//Copiem mesajul original
    {
        return -1;//Eroare la copiere
    }

    /* Eliminare newline final */
    size_t len = strlen(tmp);//Calculam lungimea mesajului

    if (len > 0 && tmp[len - 1] == '\n')//Verificam newline
    {
        tmp[len - 1] = '\0';//Il eliminam
    }

    /* Tokenizare mesaj */
    char *saveptr = NULL;//Stare strtok_r
    char *tok = strtok_r(tmp, "|", &saveptr);//Extragem primul token

    /* Verificare tip mesaj */
    if (!tok || strcmp(tok, "RES") != 0)//Verificam tipul mesajului
    {
        return -1;//Mesaj invalid
    }

    /* Citire valoare numerică */
    tok = strtok_r(NULL, "|", &saveptr);//Extragem valoarea numerica

    if (!tok)//Verificam existenta
    {
        return -1;//Eroare
    }

    char *endptr = NULL;//Pointer validare
    long count = strtol(tok, &endptr, 10);//Conversie string -> long

    /* Validare valoare numerică */
    if (endptr == tok || *endptr != '\0' || count < 0)//Verificam corectitudinea conversiei
    {
        return -1;//Valoare invalida
    }

    out->no_motion_count = count;//Salvam rezultatul in structura

    return 0;//Returnam succes
}