# Jogo da Vida Paralelo com MPI

Implementação distribuída do Jogo da Vida de Conway em C. A grade é dividida
entre processos MPI, que calculam gerações em paralelo e trocam linhas de
fronteira para manter a evolução consistente.

## Conceitos demonstrados

- Decomposição de domínio por linhas
- Processos distribuídos com MPI
- Troca de células de fronteira com `MPI_Sendrecv`
- Linhas fantasma (*ghost rows*)
- Coleta do estado global com `MPI_Gather`
- Sincronização com barreiras
- Medição do tempo de execução paralelo

## Requisitos

- Compilador C compatível com C99
- Uma implementação MPI, como OpenMPI ou MPICH

Em Ubuntu/Debian, o OpenMPI pode ser instalado com:

```bash
sudo apt install build-essential openmpi-bin libopenmpi-dev
```

## Compilação

```bash
git clone https://github.com/guilhermelerner/TrabalhoFinalJogodaVidaParalela.git
cd TrabalhoFinalJogodaVidaParalela
mpicc -O2 -Wall -Wextra game_of_life_mpi.c -o game_of_life
```

## Execução

O código foi estruturado para 16 processos:

```bash
mpirun -np 16 ./game_of_life
```

Em um ambiente controlado, ajuste as constantes da grade e o número de gerações
diretamente no início de `game_of_life_mpi.c` antes de recompilar.

## Como funciona

Cada processo mantém sua parte real da grade e duas faixas extras de células.
Em cada geração, processos vizinhos trocam essas fronteiras. Após a atualização
local, o processo raiz reúne as partes para reconstruir e exibir o estado global.

## Observações

- O número de linhas da grade precisa ser compatível com a divisão de processos.
- O desempenho depende do equilíbrio entre o custo de cálculo e a comunicação.
- O arquivo executável incluído no repositório pode não funcionar em outro
  sistema; prefira recompilar a partir do código-fonte.
