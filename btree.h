#ifndef BTREE_H
#define BTREE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

// Garante a importação das estruturas do Buffer Pool antes de usá-las aqui
#include "storage.h"

#define MAX_CHAVES 199 
#define MIN_CHAVES 99  

typedef struct {
    FILE* arquivo_db;
    SuperBloco superbloco;
    BufferPool pool; // Agora o compilador vai mapear este campo corretamente
    CRITICAL_SECTION mutex_banco;
} GerenciadorBTree;

typedef struct {
    deslocamento_disco_t deslocamento_registro; 
    int64_t id_pagina_onde_esta;                
    int32_t indice_na_pagina;                   
    int32_t paginas_lidas;                      
    bool encontrada;                            
} ResultadoBusca;

void inicializar_btree(GerenciadorBTree* btree, FILE* arquivo);
void destruir_btree(GerenciadorBTree* btree);
ResultadoBusca buscar_chave(GerenciadorBTree* btree, int32_t chave_matricula);
bool inserir_chave(GerenciadorBTree* btree, int32_t chave_matricula, deslocamento_disco_t deslocamento_registro);
bool remover_chave(GerenciadorBTree* btree, int32_t chave_matricula);

#endif