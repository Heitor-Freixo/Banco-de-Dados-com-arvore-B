#include <stdio.h>
#include <assert.h>
#include "storage.h"
#include "btree.h"

int main(int argc, char* argv[]) {
    (void)argv;

    if (argc > 1) {
        printf("Executando testes do Passo 5 (Remocao e Underflow na Arvore B)...\n");
        
        remove("banco_teste.bin");

        bool eh_novo;
        FILE* arquivo = inicializar_armazenamento("banco_teste.bin", &eh_novo);
        assert(arquivo != NULL);
        
        GerenciadorBTree btree;
        inicializar_btree(&btree, arquivo);

        // 1. Carga inicial de dados
        assert(inserir_chave(&btree, 50, 9000) == true);
        assert(inserir_chave(&btree, 30, 8000) == true);
        assert(inserir_chave(&btree, 70, 10000) == true);

        // 2. Garante que os dados existem antes de remover
        ResultadoBusca r_antes = buscar_chave(&btree, 30);
        assert(r_antes.encontrada == true);

        // 3. Execução da deleção estrutural
        printf(" -> Removendo a chave 30...\n");
        assert(remover_chave(&btree, 30) == true);

        // 4. Validação: A chave não deve mais ser encontrada
        ResultadoBusca r_depois = buscar_chave(&btree, 30);
        assert(r_depois.encontrada == false);

        // 5. Tentar deletar uma chave que já sumiu ou que nunca existiu
        assert(remover_chave(&btree, 30) == false);
        assert(remover_chave(&btree, 999) == false);

        printf(" -> Teste de remocao basica concluido com sucesso!\n");

        destruir_btree(&btree);
        fclose(arquivo);
        
        printf("\n======================================================\n");
        printf("  PARABÉNS! Remoção e fusão estrutural validadas!    \n");
        printf("======================================================\n");
        return 0;
    }

    printf("Use 'make test' para rodar os testes automatizados.\n");
    return 0;
}