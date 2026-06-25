#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "btree.h"

void executar_benchmark(GerenciadorBTree* btree) {
    printf("\n==================================================\n");
    printf("   INICIANDO CARGA MASSIVA DE 1.000.000 REGISTROS \n");
    printf("==================================================\n\n");

    for (int32_t i = 1; i <= 1000000; i++) {
        inserir_chave(btree, i, (deslocamento_disco_t)i * 8);
        
        if (i % 20000 == 0) {
            int percentual = i / 10000;
            printf("\rProgresso: [");
            for (int p = 0; p < 50; p++) {
                if (p < percentual / 2) printf("#");
                else printf("-");
            }
            printf("] %d%% ", percentual);
            fflush(stdout);
        }
    }

    printf("\n\n==================================================\n");
    printf("               RELATORIO DO BENCHMARK             \n");
    printf("==================================================\n");
    printf(" -> Chaves inseridas  : 1.000.000\n");
    printf(" -> Leituras em Disco : %llu paginas\n", metrica_leituras_disco);
    printf(" -> Escritas em Disco : %llu paginas\n", metrica_escritas_disco);
    printf(" -> I/O Logico Total  : %llu operacoes\n", metrica_leituras_disco + metrica_escritas_disco);
    printf("==================================================\n\n");
}

void executar_modo_manual(GerenciadorBTree* btree) {
    char comando[32];
    int32_t chave;
    
    printf("\n==================================================\n");
    printf("         MODO MANUAL ATIVADO - CONSOLE SGBD       \n");
    printf(" Comandos disponiveis:                            \n");
    printf("   inserir <chave>  - Insere uma chave na arvore  \n");
    printf("   buscar <chave>   - Procura por uma chave       \n");
    printf("   status           - Exibe as metricas de I/O    \n");
    printf("   sair             - Retorna ao menu principal   \n");
    printf("==================================================\n\n");

    while (1) {
        printf("sgbd> ");
        fflush(stdout);
        if (scanf("%31s", comando) != 1) break;

        if (strcmp(comando, "sair") == 0) {
            printf("[+] Retornando ao menu principal...\n\n");
            break;
        } 
        else if (strcmp(comando, "inserir") == 0) {
            if (scanf("%d", &chave) == 1) {
                if (inserir_chave(btree, chave, (deslocamento_disco_t)chave * 8)) {
                    printf("[OK] Chave %d inserida.\n", chave);
                } else {
                    printf("[ERRO] Falha ao inserir a chave %d.\n", chave);
                }
            }
        } 
        else if (strcmp(comando, "buscar") == 0) {
            if (scanf("%d", &chave) == 1) {
                ResultadoBusca res = buscar_chave(btree, chave);
                if (res.encontrada) {
                    printf("[SUCESSO] Chave %d encontrada!\n", chave);
                    printf(" -> ID Pagina: %lld | Indice: %d | Paginas Lidas: %d\n", 
                           res.id_pagina_onde_esta, res.indice_na_pagina, res.paginas_lidas);
                } else {
                    printf("[AVISO] Chave %d nao localizada na arvore.\n", chave);
                }
            }
        } 
        else if (strcmp(comando, "status") == 0) {
            printf("\n--- METRICAS ATUAIS DO SISTEMA ---\n");
            printf(" -> Leituras em Disco : %llu paginas\n", metrica_leituras_disco);
            printf(" -> Escritas em Disco : %llu paginas\n", metrica_escritas_disco);
            printf("----------------------------------\n\n");
        } 
        else {
            printf("[ERRO] Comando desconhecido. Use: inserir, buscar, status ou sair.\n");
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }
    }
}

int main() {
    FILE* arquivo = fopen("banco_oficial.db", "rb+");
    if (!arquivo) arquivo = fopen("banco_oficial.db", "wb+");
    if (!arquivo) {
        printf("[ERRO FATAL] Nao foi possivel acessar o arquivo banco_oficial.db!\n");
        return 1;
    }

    GerenciadorBTree btree;
    inicializar_btree(&btree, arquivo);

    int opcao = 0;
    while (opcao != 4) {
        printf("==================================================\n");
        printf("           SGBD INTERATIVO (B-TREE + BUFFER)      \n");
        printf("==================================================\n");
        printf(" 1. Executar Modo Benchmark (Carga 1M)\n");
        printf(" 2. Entrar no Modo Manual (Console CLI)\n");
        printf(" 3. Exportar para csv\n");
        printf(" 4. Salvar e sair\n");
        printf("==================================================\n");
        printf("Escolha uma opcao: ");
        fflush(stdout);
        
        if (scanf("%d", &opcao) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            opcao = 0;
            printf("\n[!] Entrada invalida. Escolha um numero.\n\n");
            continue;
        }

        switch (opcao) {
            case 1:
                executar_benchmark(&btree);
                break;
            case 2:
                executar_modo_manual(&btree);
                break;
            case 3:
                printf("\n[+] Exportando banco para 'dados_exportados.csv'...\n");
                exportar_btree_para_csv(&btree, "dados_exportados.csv");
                break;
            case 4:
                printf("\n[+] Encerrando...\n");
                break;
            default:
                printf("\n[!] Opcao invalida. Tente novamente.\n\n");
                break;
        }
    }

    destruir_btree(&btree);
    fclose(arquivo);
    
    printf("[SUCESSO] Sistema encerrado com integridade referencial.\n");
    return 0;
}