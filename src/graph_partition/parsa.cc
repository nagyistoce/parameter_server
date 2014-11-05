#include "graph_partition/parsa.h"
#include "base/bitmap.h"
namespace PS {

void parsa::partition() {
  data_thr_ = unique_ptr<std::thread>(new std::thread(Parsa::loadInputGraph, this));
  data_thr_.detach();

  MatrixPtrList<Empty> X;
  SArray<int> map_U;
  for (int i = 0; ; ++i) {
    if (read_data_finished_ && data_buf_.empty()) break;
    data_buf_.pop(X);
    partitionU(X[0], X[1], &map_U);
  }
}

void Parsa::loadInputGraph() {
  conf_.input_graph().set_ignore_feature_group(true);
  StreamReader<Empty> reader(conf_.input_graph());
  MatrixPtrList<Empty> X;
  uint32 blk_size = conf_.block_size();
  while(!read_data_finished_) {
    // load the graph
    bool ret = reader.readMatrices(blk_size, &X);
    auto G = std::static_pointer_cast<SparseMatrix<Key,Empty>(X.back());
    // map the columns id into small integers
    size_t n = G->index().size(); CHECK_GT(n, 0);
    Key* idx = G->index().begin();
    for (size_t i = 0; i < n; ++i) {
      idx[i] %= conf_.V_size();
    }
    SizeR(0, conf_.V_size()).to(G->info().mutable_col());
    G->info().set_type(MatrixInfo::SPARSE_BINARY);
    G->value().clear();
    // to col major
    auto H = G->toColMajor();
    MatrixPtrList<Empty> Y(2);
    Y[0] = G; Y[1] = H;
    // push to data buff
    data_buf_.push(Y, Y[0]->memSize() + Y[1]->memSize());
    if (!ret) read_data_finished_ = true;
  }
}


void Parsa::partitionU(
    const Graph& row_major_blk, const Graph& col_major_blk, SArray<int>* map_U) {
  // initialization
  int n = row_major_blk->rows();
  map_U->resize(n);
  assigned_U_.clear();
  assigned_U_.resize(n);
  initCost(row_major_blk);

  // partitioning
  for (int i = 0; i < n; ++i) {
    // TODO sync if necessary

    // assing U_i to partition k
    int k = i % k_;
    int Ui = cost_[k].minIdx();
    assiggned_U_.set(Ui);
    (*map_U)[Ui] = k;

    // update
    updateCostAndNeighborSet(row_major_blk, col_major_blk, Ui, k);
  }
}

// init the cost of assigning U_i to partition k
void Parse::initCost(const Graph& row_major_blk) {
  int n = row_major_blk->rows();
  size_t* row_os = row_major_blk->offset.begin();
  int* row_idx = row_major_blk->index.begin();

  sdt::vector<int> cost(n);
  for (int k = 0; k < k_; ++k) {
    const auto& neighbor = neighbor_set_[k];
    for (int i = 0; i < n; ++ i) {
      for (size_t j = row_os[i]; j < row_os[i+1]; ++j) {
       cost[i] += !neighbor[row_idx[j]];
      }
    }
    cost_[k].init(cost);
  }
}

void Parsa::updateCostAndNeighborSet(
    const Graph& row_major_blk, const Graph& col_major_blk, int Ui, int partition) {
  auto& neighbor = neighbor_set_[partition];
  auto& cost = cost_[Ui];
  size_t* row_os = row_major_blk->offset.begin();
  size_t* col_os = col_major_blk->offset.begin();
  int* row_idx = row_major_blk->index.begin();
  int* col_idx = col_major_blk->index.begin();

  for (int s = 0; s < k_; ++s) cost_[s].remove(Ui);

  for (size_t i = row_os[Ui]; i < row_os[Ui+1]; ++i) {
    int Vj = row_idx[i];
    if (neighbor.assigned_V[Vj]) continue;
    neighbor.assigned_V.set(Vj);
    for (size_t s = col_os[Vj]; s < col_os[Vj+1]; ++s) {
      int Uk = col_idx[s];
      if (assigned_U_[Uk]) continue;
      cost.decrAndSort(Uk);
    }
  }
}

} // namespace PS
