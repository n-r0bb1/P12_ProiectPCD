// aggregator.c – aduna rezultatele partiale de la workers
#include "aggregator.h" //Include fisierul header ce contine declaratiile necesare
    //Acesta furnizeaza prototipul functiei si dependentele asociate

long aggregator_sum(const long *partials,size_t count) // Functia aduna toate rezultatele
                                                       // Primeste un vector de valori partiale si numarul acestora
{
  long total = 0L; // Variabila pentru adunare suma finala
                   // Aceasta va acumula toate valorile din vectorul partials

  if (!partials || count == 0) // Verificam daca pointerul este invalid sau daca nu exista
                  // elementer de procesat Cazuri invalide:
                  //  - partials == NULL (nu exista date)
                  //  - count == 0 (nu exista elemente de procesat)
  {
    return 0L; // Se returneaza 0 in cazul in care pointerul e invalid sau daca
               // nu exista elemente de procesat Functia nu poate continua fara
               // date valide
  }

  for (size_t i = 0; i < count;++i) // Parcurgem pe rand toate elementele din vectorul partials
            // Indexul i reprezinta pozitia curenta in vector
  {
    total =
        total + partials[i]; // Adunam partialele
                             // Fiecare element este adaugat la suma cumulata
  }
  return total; // Se returneaza suma totala
                // Rezultatul reprezinta suma tuturor valorilor din vector
}