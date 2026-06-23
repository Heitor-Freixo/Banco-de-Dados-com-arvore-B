#ifndef BTREE_H
#define BTREE_H

#include "storage.h"
#include <windows.h>

#define MAX_CHAVES 199 
#define MIN_CHAVES 99  // Céu da metade de M (200 / 2 - 1)

typedef struct {
    FILE* arquivo_db;
    SuperBloco superbloco;
    CRITICAL_SECTION mutex_banco;
} GerenciadorBTree;

typedef struct {
    deslocamento_disco_t deslocamento_registro; 
    int64_t id_pagina_onde_esta;                
    int32_t indice_na_pagina;                   
    int32_t paginas_lidas;                      
    bool encontrada;                            
} ResultadoBusca;

// Funções existentes
void inicializar_btree(GerenciadorBTree* btree, FILE* arquivo);
void destruir_btree(GerenciadorBTree* btree);
ResultadoBusca buscar_chave(GerenciadorBTree* btree, int32_t chave_matricula);
bool inserir_chave(GerenciadorBTree* btree, int32_t chave_matricula, deslocamento_disco_t deslocamento_registro);

// Nova Função do Passo 5 (Remoção)
bool remover_chave(GerenciadorBTree* btree, int32_t chave_matricula);

#endif // BTREE_H