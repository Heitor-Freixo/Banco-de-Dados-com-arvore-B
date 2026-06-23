#include "btree.h"
#include <string.h>

void inicializar_btree(GerenciadorBTree* btree, FILE* arquivo) {
    btree->arquivo_db = arquivo;
    // Carrega o superbloco para a estrutura de controle
    ler_superbloco(arquivo, &btree->superbloco);
}