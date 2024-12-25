#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>

/*
本コードの流れ:
1. MPI環境初期化
2. 各プロセスが所属するノード名を取得
3. 全プロセスでノード名を収集
4. ノードごとにランクを分類し、ノード横断型リングパターンを作成
   例: ノードaにランク[0,3], ノードbにランク[1,4], ノードcにランク[2,5]がある場合、
      順序: aの1番目のランク, bの1番目のランク, cの1番目のランク, aの2番目のランク, bの2番目のランク, cの2番目のランク...
   というように並べてリングを構成。
5. 各ランクはこの新たな並び順から、「次に送信するランク(next)」を判定
6. nextランクとの通信を行い、通信速度を計測
7. ノード間通信の場合、1秒待機
8. 全て完了後、ランク0で最終結果報告
*/

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0, size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // 約50MBのデータサイズ
    const size_t data_size = (size_t)500 * 1024 * 1024;
    char *send_buf = (char*)malloc(data_size);
    char *recv_buf = (char*)malloc(data_size);

    // 各ランクのホスト名取得
    char hostname[MPI_MAX_PROCESSOR_NAME];
    int name_len = 0;
    MPI_Get_processor_name(hostname, &name_len);

    // 全ノード名を集める
    char *all_hostnames = (char*)malloc((size_t)size * MPI_MAX_PROCESSOR_NAME);
    MPI_Allgather(hostname, MPI_MAX_PROCESSOR_NAME, MPI_CHAR,
                  all_hostnames, MPI_MAX_PROCESSOR_NAME, MPI_CHAR,
                  MPI_COMM_WORLD);

    // データ初期化（送信バッファ）
    for (size_t i = 0; i < data_size; i++) {
        send_buf[i] = (char)((i + rank) % 256);
    }

    // ノード名毎にランクをグループ化
    // マップ：hostname -> 動的配列 (rank一覧)
    // 後で順序付けのため、ホスト名をキーとしたソートも行う
    // まず全ホスト名を取得して、ホスト名->インデックスのmapを作ることを考える

    // 1. 重複なしのホスト名リストを作成
    //    ホスト名の集合を作るために、ランク0で収集後ブロードキャストするでもよいが
    //    全ランクで同じ操作でOK。個々が同じ処理できるようソートしてユニーク化。
    char **host_list = (char**)malloc(sizeof(char*) * size);
    for (int i = 0; i < size; i++) {
        host_list[i] = &all_hostnames[i * MPI_MAX_PROCESSOR_NAME];
    }

    // ソート
    for (int i = 0; i < size - 1; i++) {
        for (int j = i+1; j < size; j++) {
            if (strcmp(host_list[i], host_list[j]) > 0) {
                char *tmp = host_list[i];
                host_list[i] = host_list[j];
                host_list[j] = tmp;
            }
        }
    }

    // ユニーク化（重複除去）
    int unique_count = 0;
    for (int i = 0; i < size; i++) {
        if (i == 0 || strcmp(host_list[i], host_list[i-1]) != 0) {
            host_list[unique_count++] = host_list[i];
        }
    }

    // unique_countが実際のノード数
    int node_count = unique_count;

    // node毎にrank一覧を格納する動的配列を作る
    // node毎に可変長なので、まずメモリ確保は柔軟に行う
    // node_count個の動的配列を用意
    // ホスト名->インデックス変換が必要
    // linear searchで十分
    int *node_rank_counts = (int*)calloc(node_count, sizeof(int));

    // 一旦ランク->所属node_indexを求める
    int *rank_node_index = (int*)malloc(sizeof(int)*size);
    for (int r = 0; r < size; r++) {
        char *hn = &all_hostnames[r * MPI_MAX_PROCESSOR_NAME];
        // ノードインデックス検索
        int found_idx = -1;
        for (int n = 0; n < node_count; n++) {
            if (strcmp(hn, host_list[n]) == 0) {
                found_idx = n;
                break;
            }
        }
        rank_node_index[r] = found_idx;
        node_rank_counts[found_idx]++;
    }

    // ノードごとのランク配列
    int **node_ranks = (int**)malloc(sizeof(int*) * node_count);
    for (int n = 0; n < node_count; n++) {
        node_ranks[n] = (int*)malloc(sizeof(int)*node_rank_counts[n]);
    }

    // node_rank_countsを使った代入用カウンタ
    int *node_pos = (int*)calloc(node_count, sizeof(int));
    for (int r = 0; r < size; r++) {
        int ni = rank_node_index[r];
        node_ranks[ni][node_pos[ni]++] = r;
    }

    // 各ノード内部のrankは昇順ソート(念のため)
    for (int n = 0; n < node_count; n++) {
        // ソート
        int count = node_rank_counts[n];
        for (int i = 0; i < count - 1; i++) {
            for (int j = i+1; j < count; j++) {
                if (node_ranks[n][i] > node_ranks[n][j]) {
                    int tmp = node_ranks[n][i];
                    node_ranks[n][i] = node_ranks[n][j];
                    node_ranks[n][j] = tmp;
                }
            }
        }
    }

    // 次に通信順序を作る
    // a,b,c,...ノードがあるとして、
    // 各ノードの1番目のランクを順に並べる→これが最初のnode_count個
    // 次に各ノードの2番目のランク…という風にインターリーブ
    // 全ランクが使い切るまで続ける
    int max_ranks_per_node = 0;
    for (int n = 0; n < node_count; n++) {
        if (node_rank_counts[n] > max_ranks_per_node) {
            max_ranks_per_node = node_rank_counts[n];
        }
    }

    int *comm_order = (int*)malloc(sizeof(int)*size);
    int idx = 0;
    for (int lv = 0; lv < max_ranks_per_node; lv++) {
        for (int n = 0; n < node_count; n++) {
            if (lv < node_rank_counts[n]) {
                comm_order[idx++] = node_ranks[n][lv];
            }
        }
    }

    // comm_orderに従いリングを作るので、
    // comm_order中で自分が何番目かを探してnextを決定
    int my_pos = -1;
    for (int i = 0; i < size; i++) {
        if (comm_order[i] == rank) {
            my_pos = i;
            break;
        }
    }
    int next_pos = (my_pos + 1) % size;
    int prev_pos = (my_pos - 1 + size) % size;

    int next_rank = comm_order[next_pos];
    int prev_rank = comm_order[prev_pos];

    MPI_Barrier(MPI_COMM_WORLD); // 計測前に全プロセス同期

    if (rank == 0) {
        printf("Starting cross-node ring communication.\n");
        fflush(stdout);
    }

    // 通信相手のノード名取得
    char *my_node = &all_hostnames[rank * MPI_MAX_PROCESSOR_NAME];
    char *next_node = &all_hostnames[next_rank * MPI_MAX_PROCESSOR_NAME];

    // 途中経過表示(送信前)
    printf("Rank %d (%s) will send data to Rank %d (%s)\n", rank, my_node, next_rank, next_node);
    fflush(stdout);

    double start_time = MPI_Wtime();

    MPI_Request reqs[2];
    MPI_Status stats[2];

    double send_start = MPI_Wtime();

    MPI_Isend(send_buf, (int)data_size, MPI_BYTE, next_rank, 0, MPI_COMM_WORLD, &reqs[0]);
    MPI_Irecv(recv_buf, (int)data_size, MPI_BYTE, prev_rank, 0, MPI_COMM_WORLD, &reqs[1]);

    MPI_Waitall(2, reqs, stats);

    double send_end = MPI_Wtime();
    double elapsed = send_end - send_start;

    double bandwidth = (double)data_size / elapsed / (1024.0 * 1024.0);

    int is_inter_node = (strcmp(my_node, next_node) != 0);

    if (is_inter_node) {
        printf("Rank %d@%s-> Rank %d@%s: Inter-node communication done. \n", rank,my_node, next_rank,next_node );
        //printf("Rank %d -> Rank %d: Inter-node communication done.\n", rank, next_rank);
        printf("  Elapsed time = %f sec, Bandwidth = %f MB/s\n", elapsed, bandwidth);
        fflush(stdout);
        // ノード間通信なので1秒待つ
        sleep(1);
    } else {
        printf("Rank %d -> Rank %d: Intra-node communication done.\n", rank, next_rank);
        printf("  Elapsed time = %f sec, Bandwidth = %f MB/s\n", elapsed, bandwidth);
        fflush(stdout);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double total_end = MPI_Wtime();
    double total_elapsed = total_end - start_time;

    if (rank == 0) {
        printf("All ranks have participated in the cross-node ring communication.\n");
        printf("Total time: %f sec\n", total_elapsed);
        printf("Inter-node communications included a 1-second delay after each transfer.\n");
        fflush(stdout);
    }

    free(send_buf);
    free(recv_buf);
    free(all_hostnames);
    free(host_list);
    free(rank_node_index);
    free(node_rank_counts);
    for (int n = 0; n < node_count; n++) {
        free(node_ranks[n]);
    }
    free(node_ranks);
    free(node_pos);
    free(comm_order);

    MPI_Finalize();
    return 0;
}
