#ifndef BTREE_H
#define BTREE_H

#include "storage.h"

typedef struct {
    bool encontrada;
    int64_t id_pagina_onde_esta;
    int32_t indice_na_pagina;
    deslocamento_disco_t deslocamento_registro;
    int32_t paginas_lidas;
} ResultadoBusca;

typedef struct {
    FILE* arquivo_db;
    SuperBloco superbloco;
    BufferPool pool;
} GerenciadorBTree;

void inicializar_btree(GerenciadorBTree* btree, FILE* arquivo);
void destruir_btree(GerenciadorBTree* btree);
ResultadoBusca buscar_chave(GerenciadorBTree* btree, int32_t chave_matricula);
bool inserir_chave(GerenciadorBTree* btree, int32_t chave_matricula, deslocamento_disco_t deslocamento_registro);
bool remover_chave(GerenciadorBTree* btree, int32_t chave_matricula);
int32_t busca_por_intervalo(GerenciadorBTree* btree, int32_t inicio, int32_t fim, int32_t* chaves_out);

#endif