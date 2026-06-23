#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <mpi.h>

// Definições globais do tamanho do problema
#define TOTAL_ROWS 32  // Altura total do tabuleiro completo
#define COLS 32        // Largura total do tabuleiro completo
#define GHOST_ROWS 5   // Quantidade de linhas extras para comunicação nas bordas
#define ITERATIONS 100 // Quantas gerações a simulação vai rodar

int main(int argc, char **argv) {
    // 1. INICIALIZAÇÃO DO AMBIENTE MPI
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); // Identificador único deste processo (0 a 15)
    MPI_Comm_size(MPI_COMM_WORLD, &size); // Quantidade total de processos ativos

    // Validação estrita: o código só funciona com a topologia de 16 processos exigida no enunciado
    if (size != 16) {
        if (rank == 0) {
            printf("Erro: O enunciado exige exatamente 16 processos. Você executou com %d.\n", size);
        }
        MPI_Finalize();
        return 1;
    }

    // 2. DECOMPOSIÇÃO DE DOMÍNIO 
    int local_rows = TOTAL_ROWS / size; // Quantidade de linhas que este processo vai REALMENTE calcular (32 / 16 = 2)
    
    // Espaço total na memória: Linhas reais + 5 fantasma em cima + 5 fantasma embaixo (Total: 2 + 5 + 5 = 12 linhas)
    int total_allocated_rows = local_rows + 2 * GHOST_ROWS;
    
    // Alocação das matrizes locais linearizadas (1D que simula 2D usando aritmética de ponteiros)
    int *grid = (int*) calloc(total_allocated_rows * COLS, sizeof(int));     // Grade atual
    int *new_grid = (int*) calloc(total_allocated_rows * COLS, sizeof(int)); // Próxima geração temporária

    // 3. INICIALIZAÇÃO DO PADRÃO ESTÁVEL (Glider / Planador)
    // Como cada processo tem apenas 2 linhas reais, o Glider (que usa 3 linhas) precisa ser dividido entre Rank 0 e Rank 1.
    if (rank == 0) {
        // Linha Real 0 do Rank 0 (O índice na memória pula as 5 linhas fantasmas do topo)
        grid[(GHOST_ROWS + 0) * COLS + 2] = 1;
        
        // Linha Real 1 do Rank 0
        grid[(GHOST_ROWS + 1) * COLS + 3] = 1;
    }
    if (rank == 1) {
        // Linha Real 2 do mapa global corresponde à Linha Real 0 do Rank 1
        grid[(GHOST_ROWS + 0) * COLS + 1] = 1;
        grid[(GHOST_ROWS + 0) * COLS + 2] = 1;
        grid[(GHOST_ROWS + 0) * COLS + 3] = 1;
    }

    // 4. MAPEAMENTO DE VIZINHANÇA VERTICAL (Topologia em Toroide / Anel)
    // O operador módulo (%) garante que o topo do Rank 0 se conecte à base do Rank 15 e vice-versa.
    int top_neighbor = (rank - 1 + size) % size;
    int bottom_neighbor = (rank + 1) % size;

    // Ponteiro que apenas o processo "Gerente" (Rank 0) usará para alocar o tabuleiro unificado
    int *full_grid = NULL;
    if (rank == 0) {
        full_grid = (int*) malloc(TOTAL_ROWS * COLS * sizeof(int));
    }

    // 5. LOOP PRINCIPAL DA EVOLUÇÃO
    for (int step = 0; step < ITERATIONS; step++) {
        
        // --- TROCA DE CÉLULAS FANTASMAS (Sincronização de Bordas) ---
        // Passo A: Envia as suas linhas reais superiores para o vizinho de cima, 
        // e recebe as linhas dele preenchendo a sua zona fantasma INFERIOR.
        MPI_Sendrecv(
            &grid[GHOST_ROWS * COLS], GHOST_ROWS * COLS, MPI_INT, top_neighbor, 0, 
            &grid[(GHOST_ROWS + local_rows) * COLS], GHOST_ROWS * COLS, MPI_INT, bottom_neighbor, 0, 
            MPI_COMM_WORLD, MPI_STATUS_IGNORE
        );

        // Passo B: Envia as suas linhas reais inferiores para o vizinho de baixo, 
        // e recebe as linhas dele preenchendo a sua zona fantasma SUPERIOR (índice 0).
        MPI_Sendrecv(
            &grid[local_rows * COLS], GHOST_ROWS * COLS, MPI_INT, bottom_neighbor, 1, 
            &grid[0], GHOST_ROWS * COLS, MPI_INT, top_neighbor, 1, 
            MPI_COMM_WORLD, MPI_STATUS_IGNORE
        );

        // --- CÁLCULO DAS REGRAS DO JOGO DA VIDA ---
        // Varre estritamente as linhas REAIS do processo (ignora as zonas fantasmas ao salvar o resultado)
        for (int r = GHOST_ROWS; r < GHOST_ROWS + local_rows; r++) {
            for (int c = 0; c < COLS; c++) {
                int vivos = 0;
                
                // Varre os 8 vizinhos ao redor da célula atual (matriz 3x3)
                for (int i = -1; i <= 1; i++) {
                    for (int j = -1; j <= 1; j++) {
                        if (i == 0 && j == 0) continue; // Ignora a própria célula central
                        
                        int nr = r + i; // Linha do vizinho (pode ler das ghost cells sem problemas)
                        int nc = (c + j + COLS) % COLS; // Toroide Horizontal: Conecta esquerda com direita
                        
                        vivos += grid[nr * COLS + nc]; // Soma 1 se o vizinho estiver vivo (1) ou 0 se morto (0)
                    }
                }

                // Aplicação das regras de Conway na matriz de destino temporária
                if (grid[r * COLS + c] == 1 && (vivos == 2 || vivos == 3)) {
                    new_grid[r * COLS + c] = 1;  // Sobrevivência
                } else if (grid[r * COLS + c] == 0 && vivos == 3) {
                    new_grid[r * COLS + c] = 1;  // Nascimento por reprodução
                } else {
                    new_grid[r * COLS + c] = 0;  // Morte por solidão ou superpopulação
                }
            }
        }

        // --- ATUALIZAÇÃO DA MEMÓRIA LOCAL ---
        // Copia os dados calculados da próxima geração de volta para a grade principal
        for (int i = 0; i < total_allocated_rows * COLS; i++) {
            grid[i] = new_grid[i];
        }

        // --- REUNIÃO E IMPRESSÃO DOS DADOS (Passo Gráfico) ---
        // MPI_Gather coleta o pedaço real de cada um dos 16 processos (ignorando fantasmas)
        // e junta tudo sequencialmente na memória 'full_grid' pertencente apenas ao Rank 0.
        MPI_Gather(
            &grid[GHOST_ROWS * COLS], local_rows * COLS, MPI_INT, 
            full_grid, local_rows * COLS, MPI_INT,                
            0, MPI_COMM_WORLD                                     
        );

        // O Rank 0 assume o papel de renderizar a tela
        if (rank == 0) {
            system("clear"); // Limpa o console para simular taxa de quadros 
            printf("=== Jogo da Vida - Geração %d / %d ===\n", step + 1, ITERATIONS);
            
            // Impressão da moldura superior
            for (int c = 0; c < COLS + 2; c++) printf("-");
            printf("\n");

            // Impressão da matriz completa de 32x32
            for (int r = 0; r < TOTAL_ROWS; r++) {
                printf("|"); // Moldura lateral esquerda
                for (int c = 0; c < COLS; c++) {
                    // Operador ternário: se 1 imprime '#' (vivo), se 0 imprime '.' (morto)
                    printf("%c", full_grid[r * COLS + c] ? '#' : '.'); 
                }
                printf("|\n"); // Moldura lateral direita
            }

            // Impressão da moldura inferior
            for (int c = 0; c < COLS + 2; c++) printf("-");
            printf("\n");

            usleep(150000); // Pausa de 150 mil microssegundos (0.15s) entre frames
        }
        
        // Barreira de sincronização: garante que nenhum processo passe para a próxima 
        // geração antes que o Rank 0 termine de desenhar a atual na tela.
        MPI_Barrier(MPI_COMM_WORLD);
    }

    // 6. DESALOCAÇÃO E FINALIZAÇÃO DO PROGRAMA
    if (rank == 0) {
        free(full_grid); // Libera o bloco unificado apenas no gerente
        printf("\nSimulação concluída com sucesso.\n");
    }
    free(grid);     // Libera buffers locais em todos os 16 processos
    free(new_grid);
    
    MPI_Finalize(); // Encerra o subsistema OpenMPI graciosamente
    return 0;
}