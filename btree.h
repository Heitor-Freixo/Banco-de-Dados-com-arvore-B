#ifndef BTREE_H
#define BTREE_H

#include "storage.h"

// Estrutura de controle da Árvore B em memória/execução
typedef struct {
    FILE* arquivo_db;
    SuperBloco superbloco;
} GerenciadorBTree;

// Uma assinatura simples apenas para o arquivo não ficar totalmente vazio
void inicializar_btree(GerenciadorBTree* btree, FILE* arquivo);

#endif // BTREE_H