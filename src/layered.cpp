#include "layered.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <unordered_set>
#include <utility>

namespace pele {
namespace {

using i32s = std::vector<int>;
using f64s = std::vector<double>;
using u8s = std::vector<std::uint8_t>;

constexpr double kInf = std::numeric_limits<double>::infinity();

constexpr int kMaxSweeps = 24;

// An edge gets a bend point in every rank it skips. Past this many, the longest edges are drawn
// as one direct curve instead, which keeps huge graphs from exhausting time and memory.
constexpr int kMaxDummies = 20000;

// The room between two edges that meet one side of a node, and the least that keeps their arrowheads apart.
constexpr double kPortPitch = 16;
constexpr double kMinPitch = 8;

constexpr double kMinSlope = 0.125;
constexpr double kMaxGap = 3;

constexpr double kTrackMargin = 18;
constexpr double kTrackPitch = 10;
constexpr double kTrackSpread = 14;
constexpr double kLevelWide = 2;
constexpr double kLevelMargin = 28;
constexpr double kLevelPitch = 12;
constexpr double kLevelLead = 12;
constexpr double kLevelClear = 8;
constexpr double kLevelTouch = 4;
constexpr double kCrossedSlope = 0.5;
constexpr double kInLine = 0.5;
constexpr double kSnap = 12;
constexpr double kLeanClear = 40;
constexpr int kMaxWeighed = 400;

enum end_kind : std::uint8_t { held = 0, leaves = 1, arrives = 2 };

bool is_dummy(int kind) {
  return kind == static_cast<int>(node_kind::dummy) || kind == static_cast<int>(node_kind::label);
}

constexpr int k_node = static_cast<int>(node_kind::node);
constexpr int k_dummy = static_cast<int>(node_kind::dummy);
constexpr int k_label = static_cast<int>(node_kind::label);
constexpr int k_start_port = static_cast<int>(node_kind::start_port);
constexpr int k_end_port = static_cast<int>(node_kind::end_port);


// A single-pass insertion sort by key, ties by item, as the original does.
void sort_range(i32s& list, int from, int to, const i32s& key) {
  for (int i = from + 1; i < to; i++) {
    const int item = list[i];
    const int k = key[item];
    int j = i - 1;
    while (j >= from && (key[list[j]] > k || (key[list[j]] == k && list[j] > item))) {
      list[j + 1] = list[j];
      j--;
    }
    list[j + 1] = item;
  }
}

// The pieces of edges that each cross one gap.
struct pieces_t {
  f64s from;
  f64s to;
  i32s rank;
  i32s lower;
  i32s track;
  i32s levels;
  i32s wide;
};

struct graph {
  int n = 0;
  int real = 0;
  int max_rank = 0;
  f64s w;
  u8s kind;
  i32s rank;
  // Segment endpoints, and where each segment attaches on them.
  i32s ef;
  i32s et;
  f64s efd;
  f64s etd;
  i32s succ_start;
  i32s succ_e;
  i32s pred_start;
  i32s pred_e;
  i32s layer_start;
  i32s order;
  i32s pos;
  bool offsets = false;
};

// Half the room a node, bend or port takes across the flow, with its share of the gap beside it.
double half_room(const graph& g, const layered_options& opt, int v) {
  return g.w[v] / 2 + (g.kind[v] == k_node ? opt.node_sep : opt.edge_sep) / 2;
}

// The ends of edges that may move along one side of a node (0 the side its edges arrive at, 1 the
// other), in the order of the nodes at their other ends, so that no two cross.
double side_ends(const graph& g, int v, int side, double span, const u8s& kinds, i32s& ids, f64s& room) {
  const i32s& start = side == 0 ? g.pred_start : g.succ_start;
  const i32s& list = side == 0 ? g.pred_e : g.succ_e;
  const i32s& other = side == 0 ? g.ef : g.et;
  const i32s& pos = g.pos;
  ids.clear();
  for (int k = start[v]; k < start[v + 1]; k++)
    if (kinds[list[k]] != held) ids.push_back(list[k]);
  if (ids.size() < 2) return 0;
  std::sort(ids.begin(), ids.end(), [&](int a, int b) {
    const int d = pos[other[a]] - pos[other[b]];
    return d != 0 ? d < 0 : a < b;
  });
  auto fill = [&](int mode) -> double {
    double total = 0;
    room.assign(ids.size(), 0);
    room[0] = 0;
    for (std::size_t k = 1; k < ids.size(); k++) {
      const bool same = kinds[ids[k]] == kinds[ids[k - 1]];
      const bool lv = same && kinds[ids[k]] == leaves;
      double share;
      if (mode == 0) share = 1;
      else if (mode == 1) share = lv ? 0.5 : 1;
      else share = same ? 0 : 1;
      room[k] = share;
      total += room[k];
    }
    return total;
  };
  double total = fill(0);
  if (span / total < kMinPitch) total = fill(1);
  if (span / total < kMinPitch) total = fill(2);
  if (total == 0) return 0;
  const double pitch = std::min(kPortPitch, span / total);
  for (std::size_t k = 1; k < ids.size(); k++) room[k] *= pitch;
  return total * pitch;
}

// Gives the edges that meet one side of a node places of their own along it, around its middle.
void spread(graph& g, const std::vector<layered_node>& nodes, const u8s& efk, const u8s& etk) {
  i32s ids;
  f64s room;
  for (int v = 0; v < g.real; v++) {
    const double span = std::min(nodes[v].span, g.w[v]);
    if (span <= 0 || nodes[v].kind != node_kind::node) continue;
    for (int side = 0; side < 2; side++) {
      const double total = side_ends(g, v, side, span, side == 0 ? etk : efk, ids, room);
      if (total == 0) continue;
      f64s& dx = side == 0 ? g.etd : g.efd;
      double at = -total / 2;
      for (std::size_t k = 0; k < ids.size(); k++) {
        at += room[k];
        dx[ids[k]] = at;
      }
    }
  }
}

// The segment of an edge that reaches the next point held in place, on the way up from segment
// `e` (side 0) or down: past the bends in ranks that hold nothing else.
int follow(const graph& g, const u8s* empty, int e, int side) {
  if (!empty) return e;
  for (int hops = 0; hops < 100000; hops++) {
    const int u = side == 0 ? g.ef[e] : g.et[e];
    if (g.kind[u] != k_dummy || !(*empty)[g.rank[u]]) break;
    e = side == 0 ? g.pred_e[g.pred_start[u]] : g.succ_e[g.succ_start[u]];
  }
  return e;
}

// How many ends that may move along it each node has on the side its edges arrive at, and on the other.
void free_ends(const graph& g, const u8s& efk, const u8s& etk, i32s (&free)[2]) {
  free[0].assign(g.n, 0);
  free[1].assign(g.n, 0);
  for (std::size_t e = 0; e < g.ef.size(); e++) {
    if (etk[e] != held) free[0][g.et[e]]++;
    if (efk[e] != held) free[1][g.ef[e]]++;
  }
}

// Brings the labels and bends of each edge into line with an end of the edge, so that it runs
// straight from that end and turns once, at the other.
void align_labels(const graph& g, const std::vector<layered_node>& nodes, f64s& xs, const i32s (&free)[2], const layered_options& opt,
                  const u8s* empty) {
  const double sep = opt.edge_sep;
  const int n = g.n;
  const int real = g.real;
  const int max_rank = g.max_rank;
  auto placed = [&](int v) -> bool {
    return g.kind[v] == k_label || (g.kind[v] == k_dummy && !(empty != nullptr && (*empty)[g.rank[v]] == 1));
  };
  // Where segment `e` ends on the way up (side 0) or down, and how many ends share that side of
  // the node there: none for a point that is not on a node.
  auto end = [&](int e, int side) -> std::pair<double, int> {
    const int u = side == 0 ? g.ef[e] : g.et[e];
    const double x = side == 0 ? xs[u] + g.efd[e] : xs[u] + g.etd[e];
    return {x, u < real && nodes[u].kind == node_kind::node ? free[1 - side][u] : 0};
  };
  // The room the neighbours in its rank leave a label or bend.
  auto room = [&](int v) -> std::pair<double, double> {
    const int s = g.layer_start[g.rank[v]];
    const int i = s + g.pos[v];
    auto clear = [&](int other) -> double {
      return g.w[other] / 2 + g.w[v] / 2 +
             (g.kind[other] == k_node ? opt.node_sep / 2 + sep / 2 : g.kind[other] == g.kind[v] ? sep : sep / 2);
    };
    return {i > s ? xs[g.order[i - 1]] + clear(g.order[i - 1]) : -kInf,
            i + 1 < g.layer_start[g.rank[v] + 1] ? xs[g.order[i + 1]] - clear(g.order[i + 1]) : kInf};
  };
  // The place between `lo` and `hi` that is in line with one of two ends, or else nearest the first choice.
  auto choose = [&](double x, std::pair<double, int> up, std::pair<double, int> down, double lo, double hi) -> double {
    const bool up_first = up.second != down.second ? up.second < down.second : std::abs(up.first - x) <= std::abs(down.first - x);
    const double choices[2] = {up_first ? up.first : down.first, up_first ? down.first : up.first};
    for (double choice : choices)
      if (choice >= lo && choice <= hi) return choice;
    return std::max(lo, std::min(hi, choices[0]));
  };
  u8s done(n, 0);
  i32s chain;
  for (int r = 1; r < max_rank; r++) {
    for (int i = g.layer_start[r]; i < g.layer_start[r + 1]; i++) {
      if (!placed(g.order[i]) || done[g.order[i]]) continue;
      // The edge's labels and bends from its first node to its last, and the room they share.
      int top = g.order[i];
      while (is_dummy(g.kind[g.ef[g.pred_e[g.pred_start[top]]]])) top = g.ef[g.pred_e[g.pred_start[top]]];
      int bottom = top;
      double lo = -kInf;
      double hi = kInf;
      chain.clear();
      for (int d = top; is_dummy(g.kind[d]); d = g.et[g.succ_e[g.succ_start[d]]]) {
        bottom = d;
        if (!placed(d)) continue;
        done[d] = 1;
        chain.push_back(d);
        const auto rm = room(d);
        if (rm.first > lo) lo = rm.first;
        if (rm.second < hi) hi = rm.second;
      }
      if (lo <= hi) {
        const double x = choose(xs[chain[0]], end(g.pred_e[g.pred_start[top]], 0), end(g.succ_e[g.succ_start[bottom]], 1), lo, hi);
        for (int d : chain) xs[d] = x;
        continue;
      }
      for (int d : chain) {
        const auto rm = room(d);
        if (rm.first > rm.second) continue;
        xs[d] = choose(xs[d], end(follow(g, empty, g.pred_e[g.pred_start[d]], 0), 0), end(follow(g, empty, g.succ_e[g.succ_start[d]], 1), 1),
                       rm.first, rm.second);
      }
    }
  }
}

// Once the nodes have their places, moves an end along its side when that lets its edge run straight.
void settle(graph& g, const std::vector<layered_node>& nodes, const f64s& xs, const u8s& efk, const u8s& etk, const i32s (&free)[2],
            const u8s* empty) {
  const int real = g.real;
  i32s ids;
  f64s room;
  i32s all;
  f64s sum;
  i32s count;
  // Where the end of segment `e` on node `v` should be, from the middle of its side.
  auto wanted = [&](int e, int side, int v, double span, bool again) -> double {
    if (free[side][v] < 2) return 0;
    e = follow(g, empty, e, side);
    const int u = side == 0 ? g.ef[e] : g.et[e];
    const double point = side == 0 ? xs[u] + g.efd[e] : xs[u] + g.etd[e];
    // Once the other end has its place, this one comes into line with it if it can.
    if (again && std::abs(point - xs[v]) <= span / 2 + kInLine) return std::max(-span / 2, std::min(span / 2, point - xs[v]));
    // The part of the other side that its end may move along, if it may move.
    double middle = point;
    double width = 0;
    if (u < real && nodes[u].kind == node_kind::node && (side == 0 ? efk : etk)[e] != held && free[1 - side][u] >= 2) {
      width = std::max(0.0, std::min(nodes[u].span, g.w[u]));
      if (width > 0) middle = xs[u];
    }
    const double lo = std::max(xs[v] - span / 2, middle - width / 2);
    const double hi = std::min(xs[v] + span / 2, middle + width / 2);
    // Two sides with no part in line: the end leans towards the other, if its side is long enough.
    if (lo > hi + kInLine) {
      const double lean = std::max(0.0, span / 2 - kLeanClear);
      return std::max(-lean, std::min(lean, point - xs[v]));
    }
    // Both ends settle on one place in the part the two sides share.
    const double place = (xs[v] * width + middle * span) / (width + span);
    return std::max(lo, std::min(hi, place)) - xs[v];
  };
  for (int pass = 0, moved = 1; moved && pass < 6; pass++) {
    moved = 0;
    for (int v = 0; v < real; v++) {
      const double span = std::min(nodes[v].span, g.w[v]);
      if (span <= 0 || nodes[v].kind != node_kind::node) continue;
      for (int side = 0; side < 2; side++) {
        const u8s& kinds = side == 0 ? etk : efk;
        f64s& dx = side == 0 ? g.etd : g.efd;
        const i32s& other = side == 0 ? g.ef : g.et;
        const i32s& start = side == 0 ? g.pred_start : g.succ_start;
        const i32s& list = side == 0 ? g.pred_e : g.succ_e;
        side_ends(g, v, side, span, kinds, ids, room);
        if (ids.empty()) continue;
        if (ids.size() == 1) room.assign(1, 0);
        // Every end on the side in order, the held ones among them, which divide the rest into runs.
        all.clear();
        for (int k = start[v]; k < start[v + 1]; k++) all.push_back(list[k]);
        std::sort(all.begin(), all.end(), [&](int a, int b) {
          const int d = g.pos[other[a]] - g.pos[other[b]];
          return d != 0 ? d < 0 : a < b;
        });
        int k = 0;
        double lo = -span / 2;
        for (int i = 0; i <= static_cast<int>(all.size()); i++) {
          const bool is_held = i < static_cast<int>(all.size()) && kinds[all[i]] == held;
          if (i < static_cast<int>(all.size()) && !is_held) continue;
          // The run of free ends before this point.
          int size = 0;
          for (int j = i - 1; j >= 0 && kinds[all[j]] != held; j--) size++;
          const double hi = is_held ? dx[all[i]] - kPortPitch : span / 2;
          if (size > 0) {
            // The places nearest the wanted ones that keep the order and the room between
            // neighbours: an isotonic fit, by pooling neighbours that would otherwise be too close.
            sum.clear();
            count.clear();
            double before = 0;
            for (int j = 0; j < size; j++) {
              if (j > 0) before += room[k + j];
              sum.push_back(wanted(ids[k + j], side, v, span, pass > 0) - before);
              count.push_back(1);
              // Ends that share a place for want of room stay together.
              bool together = j > 0 && room[k + j] == 0;
              while (sum.size() > 1 && (together || sum[sum.size() - 2] / count[count.size() - 2] > sum[sum.size() - 1] / count[count.size() - 1])) {
                together = false;
                const double top = sum.back();
                sum.pop_back();
                const int pooled = count.back();
                count.pop_back();
                sum.back() += top;
                count.back() += pooled;
              }
            }
            const double total = before;
            int j = 0;
            before = 0;
            for (std::size_t b = 0; b < sum.size(); b++) {
              // With too little room between two held ends, the run sits in the middle of what there is.
              const double level = hi - total < lo ? (lo + hi - total) / 2 : std::max(lo, std::min(hi - total, sum[b] / count[b]));
              for (int c = 0; c < count[b]; c++, j++) {
                if (j > 0) before += room[k + j];
                if (std::abs(dx[ids[k + j]] - level - before) > 0.01) moved = 1;
                dx[ids[k + j]] = level + before;
              }
            }
            k += size;
          }
          if (is_held) lo = dx[all[i]] + kPortPitch;
        }
      }
    }
  }
}

// Brings edges that are nearly straight into line, by moving a node a little across the flow
// along with everything it is in line with already, where the nodes beside them leave room.
void snap(const graph& g, f64s& xs, const u8s* empty, const layered_options& opt) {
  const int n = g.n;
  auto passed = [&](int u) -> bool { return empty != nullptr && g.kind[u] == k_dummy && (*empty)[g.rank[u]] == 1; };
  // Each edge from one point that is held in place to the next, past the bends in ranks that hold nothing else.
  i32s a;
  i32s b;
  f64s da;
  f64s db;
  for (std::size_t e = 0; e < g.ef.size(); e++) {
    if (passed(g.ef[e])) continue;
    const int f = follow(g, empty, static_cast<int>(e), 1);
    a.push_back(g.ef[e]);
    b.push_back(g.et[f]);
    da.push_back(g.efd[e]);
    db.push_back(g.etd[f]);
  }
  auto off = [&](std::size_t k) -> double { return xs[b[k]] + db[k] - xs[a[k]] - da[k]; };
  // The nodes in line with each other move as one.
  i32s block(n);
  std::vector<i32s> members(n);
  for (int i = 0; i < n; i++) {
    block[i] = i;
    members[i].push_back(i);
  }
  auto join = [&](int x, int y) {
    int keep = block[x];
    int drop = block[y];
    if (keep == drop) return;
    if (members[keep].size() < members[drop].size()) std::swap(keep, drop);
    for (int v : members[drop]) {
      block[v] = keep;
      members[keep].push_back(v);
    }
    members[drop].clear();
  };
  i32s near;
  for (std::size_t k = 0; k < a.size(); k++) {
    const double d = std::abs(off(k));
    if (d < 0.01) join(a[k], b[k]);
    else if (d <= kSnap) near.push_back(static_cast<int>(k));
  }
  if (near.empty()) return;
  std::sort(near.begin(), near.end(), [&](int x, int y) {
    const double d = std::abs(off(x)) - std::abs(off(y));
    return d != 0 ? d < 0 : x < y;
  });
  auto gap = [&](int v) -> double { return half_room(g, opt, v); };
  // Whether every node of a block can move by `shift` and keep its room from those beside it.
  auto can_move = [&](int id, double shift) -> bool {
    for (int v : members[id]) {
      const int s = g.layer_start[g.rank[v]];
      const int end = g.layer_start[g.rank[v] + 1];
      const int i = s + g.pos[v];
      if (i > s && block[g.order[i - 1]] != id && xs[v] + shift - xs[g.order[i - 1]] < gap(v) + gap(g.order[i - 1]) - 0.01) return false;
      if (i + 1 < end && block[g.order[i + 1]] != id && xs[g.order[i + 1]] - xs[v] - shift < gap(v) + gap(g.order[i + 1]) - 0.01) return false;
    }
    return true;
  };
  for (int k : near) {
    const double d = off(k);
    if (std::abs(d) < 0.01) {
      join(a[k], b[k]);
      continue;
    }
    if (std::abs(d) > kSnap || block[a[k]] == block[b[k]]) continue;
    // The smaller of the two blocks moves, if it can; else the other.
    const int upper = block[a[k]];
    const int lower = block[b[k]];
    const int first = members[upper].size() <= members[lower].size() ? upper : lower;
    const int ids[2] = {first, first == upper ? lower : upper};
    for (int id : ids) {
      const double shift = id == upper ? d : -d;
      if (!can_move(id, shift)) continue;
      for (int v : members[id]) xs[v] += shift;
      join(a[k], b[k]);
      break;
    }
  }
}

// Marks the ranks that hold nothing but the bends of edges, if there are any.
bool empty_ranks(const graph& g, u8s& empty) {
  empty.assign(g.max_rank + 1, 0);
  bool any = false;
  for (int r = 1; r < g.max_rank; r++) {
    bool only = g.layer_start[r + 1] > g.layer_start[r];
    for (int i = g.layer_start[r]; only && i < g.layer_start[r + 1]; i++)
      if (g.kind[g.order[i]] != k_dummy) only = false;
    if (only) {
      empty[r] = 1;
      any = true;
    }
  }
  return any;
}

}  // namespace

double ease(double across, double along) {
  return std::max(0.25, std::min(0.5, (4 * std::abs(along)) / (std::abs(across) != 0 ? std::abs(across) : 1)));
}

namespace {

// Gives every piece that does not run straight down a track across its gap, and counts the
// tracks of each gap. With `curved`, the tracks are levels for curves to turn onto.
void assign_tracks(pieces_t& pieces, i32s& track_count, bool curved, const f64s& room, f64s& tall) {
  f64s& from = pieces.from;
  f64s& to = pieces.to;
  i32s& rank = pieces.rank;
  i32s& track = pieces.track;
  i32s& levels = pieces.levels;
  i32s& wide = pieces.wide;
  const int total = static_cast<int>(from.size());
  std::map<int, i32s> by_rank;
  for (int k = 0; k < total; k++) {
    track.push_back(-1);
    levels.push_back(0);
    if (std::abs(from[k] - to[k]) < kInLine) continue;
    by_rank[rank[k]].push_back(k);
  }
  // The pieces are compared pair by pair, so where each begins and ends across the flow is worked out once.
  f64s lo_(total);
  f64s hi_(total);
  for (int k = 0; k < total; k++) {
    lo_[k] = std::min(from[k], to[k]);
    hi_[k] = std::max(from[k], to[k]);
  }
  auto within = [&](double x, int k) -> int { return x >= lo_[k] - kInLine && x <= hi_[k] + kInLine ? 1 : 0; };
  auto mates = [&](int a, int b) -> bool { return std::abs(from[a] - from[b]) < kInLine || std::abs(to[a] - to[b]) < kInLine; };
  auto overlap = [&](int a, int b) -> bool { return lo_[a] < hi_[b] - kInLine && lo_[b] < hi_[a] - kInLine; };
  auto clash = [&](int a, int b) -> bool { return overlap(a, b) && !mates(a, b); };
  auto meet = [&](int a, int b) -> bool {
    return overlap(a, b) || within(to[a], b) + within(from[b], a) + within(to[b], a) + within(from[a], b) > 0;
  };
  auto crossings = [&](int a, int b) -> int { return within(to[a], b) + within(from[b], a); };

  // Gives the pieces of one gap in `list` their tracks. Returns the number of tracks.
  auto lay = [&](i32s& list) -> int {
    // A plain order to start from: the pieces that reach furthest go first, so that those of one fan nest.
    std::stable_sort(list.begin(), list.end(), [&](int a, int b) {
      double d = (hi_[b] - lo_[b]) - (hi_[a] - lo_[a]);
      if (d != 0) return d < 0;
      d = lo_[a] - lo_[b];
      if (d != 0) return d < 0;
      return a < b;
    });
    const int count = static_cast<int>(list.size());
    std::vector<i32s> above;
    i32s group(count);
    for (int i = 0; i < count; i++) group[i] = i;
    auto find = [&](int i) -> int {
      while (group[i] != i) {
        const int gg = group[group[i]];
        group[i] = gg;
        i = gg;
      }
      return i;
    };
    i32s sequence = list;
    if (count <= kMaxWeighed) {
      // above[j] lists the pieces that should be above list[j].
      i32s waiting(count, 0);
      std::vector<i32s> after;
      for (int i = 0; i < count; i++) {
        above.emplace_back();
        after.emplace_back();
      }
      for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
          const int a = list[i];
          const int b = list[j];
          if (!meet(a, b)) continue;
          const int ri = find(i);
          const int rj = find(j);
          group[ri] = rj;
          const int ab = crossings(a, b);
          const int ba = crossings(b, a);
          if (ab == ba) continue;
          const int first = ab < ba ? i : j;
          const int second = ab < ba ? j : i;
          above[second].push_back(first);
          after[first].push_back(second);
          waiting[second]++;
        }
      }
      u8s placed(count, 0);
      i32s result;
      for (int done = 0; done < count; done++) {
        int pick = -1;
        for (int i = 0; i < count; i++) {
          if (placed[i]) continue;
          if (pick < 0 || waiting[i] < waiting[pick]) pick = i;
          if (waiting[i] == 0) break;
        }
        placed[pick] = 1;
        result.push_back(pick);
        for (int j : after[pick]) waiting[j]--;
      }
      sequence.clear();
      for (int i : result) sequence.push_back(list[i]);
    }
    // Each piece takes the first track below those it should be under that has room for it.
    std::map<int, int> place;
    for (int i = 0; i < count; i++) place[list[i]] = i;
    std::vector<i32s> rows;
    for (int k : sequence) {
      int t = 0;
      const std::size_t pi = static_cast<std::size_t>(place[k]);
      if (pi < above.size())
        for (int i : above[pi])
          if (track[list[i]] >= t) t = track[list[i]] + 1;
      for (;; t++) {
        if (static_cast<std::size_t>(t) >= rows.size()) rows.resize(t + 1);
        i32s& row = rows[t];
        bool is_free = true;
        for (int other : row)
          if (clash(k, other)) is_free = false;
        if (is_free) {
          row.push_back(k);
          break;
        }
      }
      track[k] = t;
    }
    std::map<int, int> first;
    std::map<int, int> last_of;
    for (int i = 0; i < count; i++) {
      const int k = list[i];
      const int root = find(i);
      auto fi = first.find(root);
      first[root] = std::min(fi != first.end() ? fi->second : std::numeric_limits<int>::max(), track[k]);
      auto li = last_of.find(root);
      last_of[root] = std::max(li != last_of.end() ? li->second : std::numeric_limits<int>::min(), track[k]);
    }
    for (int i = 0; i < count; i++) {
      const int k = list[i];
      const int root = find(i);
      levels[k] = last_of[root] - first[root] + 1;
      if (curved) track[k] -= first[root];
    }
    return static_cast<int>(rows.size());
  };

  // The least distance between a and b as plain curves across a gap that leaves them `height` to turn in.
  constexpr int kSamples = 16;
  auto sample = [&](int k, double h, f64s& out) {
    const double c = ease(to[k] - from[k], h);
    for (int i = 0; i <= kSamples; i++) {
      const double t = static_cast<double>(i) / kSamples;
      const double u = 1 - t;
      out[2 * i] = from[k] + (to[k] - from[k]) * t * t * (3 - 2 * t);
      out[2 * i + 1] = h * (3 * c * t * u * u + 3 * (1 - c) * t * t * u + t * t * t);
    }
  };
  f64s line_a(2 * kSamples + 2);
  f64s line_b(2 * kSamples + 2);
  auto reach = [&](const f64s& points, const f64s& line) -> double {
    double least = kInf;
    for (int i = 0; i <= kSamples; i++) {
      const double px = points[2 * i];
      const double py = points[2 * i + 1];
      for (int j = 0; j < kSamples; j++) {
        const double dx = line[2 * j + 2] - line[2 * j];
        const double dy = line[2 * j + 3] - line[2 * j + 1];
        const double den = dx * dx + dy * dy;
        const double t = std::max(0.0, std::min(1.0, ((px - line[2 * j]) * dx + (py - line[2 * j + 1]) * dy) / (den != 0 ? den : 1)));
        const double d = std::hypot(px - line[2 * j] - t * dx, py - line[2 * j + 1] - t * dy);
        if (d < least) least = d;
      }
    }
    return least;
  };
  auto nearest = [&](int a, int b, double height) -> double {
    const double h = std::max(height / 2, height - 2 * kLevelLead);
    sample(a, h, line_a);
    sample(b, h, line_b);
    return std::min(reach(line_a, line_b), reach(line_b, line_a));
  };

  for (auto& entry : by_rank) {
    const int r = entry.first;
    i32s& all = entry.second;
    if (!curved) {
      track_count[r] = lay(all);
      continue;
    }
    if (static_cast<int>(all.size()) > kMaxWeighed) continue;
    // The pieces that turn onto levels: first those that run far across and have another such
    // piece to be told apart from, then any two that would pass too near each other as plain curves.
    i32s far;
    for (int k : all)
      if (wide[k] == 1) far.push_back(k);
    lay(far);
    i32s level;
    u8s in_level(total, 0);
    for (int k : far)
      if (levels[k] > 1) {
        level.push_back(k);
        in_level[k] = 1;
      }
    for (std::size_t i = 0; i < all.size(); i++) {
      for (std::size_t j = i + 1; j < all.size(); j++) {
        const int a = all[i];
        const int b = all[j];
        if ((in_level[a] && in_level[b]) || !clash(a, b) || (from[a] < to[a]) != (from[b] < to[b])) continue;
        if (nearest(a, b, room[r]) < (wide[a] == 1 || wide[b] == 1 ? kLevelClear : kLevelTouch)) {
          if (!in_level[a]) {
            in_level[a] = 1;
            level.push_back(a);
          }
          if (!in_level[b]) {
            in_level[b] = 1;
            level.push_back(b);
          }
        }
      }
    }
    for (int k : far) {
      track[k] = -1;
      levels[k] = 0;
    }
    if (level.empty()) continue;
    int crossing = 0;
    int alongside = 0;
    for (std::size_t i = 0; i < level.size(); i++) {
      for (std::size_t j = i + 1; j < level.size(); j++) {
        const int a = level[i];
        const int b = level[j];
        if (!overlap(a, b)) continue;
        if ((from[a] < to[a]) != (from[b] < to[b])) crossing++;
        else if (!mates(a, b)) alongside++;
      }
    }
    if (crossing > alongside) {
      for (int k : all)
        if ((hi_[k] - lo_[k]) * kCrossedSlope > tall[r]) tall[r] = (hi_[k] - lo_[k]) * kCrossedSlope;
      continue;
    }
    // With them go the short pieces that could cross one of them, so that each has its place
    // among the levels and crosses none.
    auto beside = [&](int k) -> bool {
      for (int j : level)
        if (meet(k, j)) return true;
      return false;
    };
    i32s members;
    for (int k : all)
      if (in_level[k] || (wide[k] != 1 && beside(k))) members.push_back(k);
    lay(members);
    int most = 0;
    for (int k : members)
      if (levels[k] > most) most = levels[k];
    track_count[r] = most;
  }
}

// Moves the bends in ranks that hold only bends onto the straight line between the nearest points
// of their edges that are held in place, and marks the ones that can be left out of the route.
void straighten(const graph& g, f64s& xs, const u8s& empty, const i32s& first_dummy, const i32s& span, double sep, u8s& on_line) {
  const int max_rank = g.max_rank;
  f64s want(xs.size(), 0);
  // The points that each bend's edge runs between, across the ranks that hold only bends.
  f64s from(xs.size(), 0);
  f64s to(xs.size(), 0);
  for (std::size_t ei = 0; ei < first_dummy.size(); ei++) {
    const int d0 = first_dummy[ei];
    if (d0 < 0) continue;
    const int end = d0 + span[ei] - 1;
    // A bend's rank is its place in the run, counted from the rank after the edge's first node.
    const int base = g.rank[d0];
    for (int d = d0; d < end; d++) {
      if (!empty[base + d - d0]) continue;
      int last = d;
      while (last + 1 < end && empty[base + last + 1 - d0]) last++;
      const int above = g.pred_e[g.pred_start[d]];
      const int below = g.succ_e[g.succ_start[last]];
      const double x0 = xs[g.ef[above]] + g.efd[above];
      const double x1 = xs[g.et[below]] + g.etd[below];
      const int steps = last - d + 2;
      for (int k = d; k <= last; k++) {
        want[k] = x0 + ((x1 - x0) * (k - d + 1)) / steps;
        from[k] = x0;
        to[k] = x1;
      }
      d = last;
    }
  }

  // Only twins stay: bends of edges that run between the same two points, which would otherwise be
  // drawn as one. They take places a little apart, around the line.
  i32s twins;
  for (int r = 1; r < max_rank; r++) {
    if (!empty[r]) continue;
    const int s = g.layer_start[r];
    const int end = g.layer_start[r + 1];
    i32s row(g.order.begin() + s, g.order.begin() + end);
    std::stable_sort(row.begin(), row.end(), [&](int a, int b) {
      double d = from[a] - from[b];
      if (d != 0) return d < 0;
      d = to[a] - to[b];
      if (d != 0) return d < 0;
      return a < b;
    });
    for (std::size_t i = 0; i < row.size();) {
      twins.clear();
      std::size_t j = i;
      for (; j < row.size() && std::abs(from[row[j]] - from[row[i]]) < 1 && std::abs(to[row[j]] - to[row[i]]) < 1; j++) twins.push_back(row[j]);
      // The twins keep the order the rank gave them, so that two edges do not swap sides on the way.
      std::stable_sort(twins.begin(), twins.end(), [&](int a, int b) { return g.pos[a] < g.pos[b]; });
      for (std::size_t k = 0; k < twins.size(); k++) {
        const int d = twins[k];
        xs[d] = want[d] + (static_cast<double>(k) - static_cast<double>(twins.size() - 1) / 2) * sep;
        if (twins.size() == 1 || sep == 0) on_line[d] = 1;
      }
      i = j;
    }
  }
}

// In a rank that holds labels and bends but no nodes, a bend is there to take its edge straight
// through between the labels. Where the edge would pass clear of every label anyway, the bend is
// left out, and the edge turns in the whole height between the ranks on either side.
void past_labels(const graph& g, f64s& xs, const i32s& first_dummy, const i32s& span, double clear, u8s& on_line) {
  const int max_rank = g.max_rank;
  u8s labelled(max_rank + 1, 0);
  bool any = false;
  for (int r = 1; r < max_rank; r++) {
    bool labels = false;
    bool others = false;
    for (int i = g.layer_start[r]; i < g.layer_start[r + 1]; i++) {
      const int kind = g.kind[g.order[i]];
      if (kind == k_label) labels = true;
      else if (kind != k_dummy) others = true;
    }
    if (labels && !others) {
      labelled[r] = 1;
      any = true;
    }
  }
  if (!any) return;
  for (std::size_t ei = 0; ei < first_dummy.size(); ei++) {
    const int d0 = first_dummy[ei];
    if (d0 < 0) continue;
    for (int d = d0, end = d0 + span[ei] - 1; d < end; d++) {
      if (g.kind[d] != k_dummy || !labelled[g.rank[d]]) continue;
      // The points on either side that the edge is held at, past any bends already left out.
      int first = d;
      int last = d;
      while (first > d0 && on_line[first - 1]) first--;
      while (last + 1 < end && on_line[last + 1]) last++;
      const int above = g.pred_e[g.pred_start[first]];
      const int below = g.succ_e[g.succ_start[last]];
      const double from = xs[g.ef[above]] + g.efd[above];
      const double to = xs[g.et[below]] + g.etd[below];
      const double lo = std::min(from, to) - clear;
      const double hi = std::max(from, to) + clear;
      bool is_free = true;
      const int r = g.rank[d];
      for (int i = g.layer_start[r]; is_free && i < g.layer_start[r + 1]; i++) {
        const int label = g.order[i];
        if (g.kind[label] == k_label && xs[label] + g.w[label] / 2 > lo && xs[label] - g.w[label] / 2 < hi) is_free = false;
      }
      if (!is_free) continue;
      on_line[d] = 1;
      // The bends left out between the two points lie on the line between them.
      const int steps = last - first + 2;
      for (int k = first; k <= last; k++) xs[k] = from + ((to - from) * (k - first + 1)) / steps;
    }
  }
}

void reduce_crossings(graph& g, const std::vector<layered_node>& nodes) {
  const int n = g.n;
  const int real = g.real;
  const int max_rank = g.max_rank;
  const i32s& layer_start = g.layer_start;
  i32s& order = g.order;
  i32s& pos = g.pos;
  const std::size_t segments = g.ef.size();
  int widest = 0;
  for (int r = 0; r <= max_rank; r++) widest = std::max(widest, layer_start[r + 1] - layer_start[r]);
  // With offsets, edges that reach one node at different offsets take separate places along it.
  i32s place;
  if (g.offsets) place.assign(segments, 0);
  const bool has_place = g.offsets;
  std::size_t size = 1;
  while (size < (has_place ? static_cast<std::size_t>(widest) + segments : static_cast<std::size_t>(widest))) size <<= 1;
  i32s tree(2 * size, 0);
  i32s targets(segments, 0);

  auto count = [&]() -> long long {
    long long total = 0;
    for (int r = 0; r < max_rank; r++) {
      int south_len = layer_start[r + 2] - layer_start[r + 1];
      if (has_place) {
        int places = 0;
        for (int i = layer_start[r + 1]; i < layer_start[r + 2]; i++) {
          const int from = g.pred_start[order[i]];
          const int to = g.pred_start[order[i] + 1];
          bool mixed = false;
          for (int k = from + 1; k < to; k++)
            if (g.etd[g.pred_e[k]] != g.etd[g.pred_e[from]]) mixed = true;
          if (mixed) {
            i32s ids(g.pred_e.begin() + from, g.pred_e.begin() + to);
            std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) { return g.etd[a] < g.etd[b]; });
            for (std::size_t k = 0; k < ids.size(); k++) {
              if (k > 0 && g.etd[ids[k]] != g.etd[ids[k - 1]]) places++;
              place[ids[k]] = places;
            }
          } else {
            for (int k = from; k < to; k++) place[g.pred_e[k]] = places;
          }
          places++;
        }
        south_len = places;
      }
      if (south_len < 2) continue;
      int t = 0;
      for (int i = layer_start[r]; i < layer_start[r + 1]; i++) {
        const int v = order[i];
        const int from = g.succ_start[v];
        const int to = g.succ_start[v + 1];
        const int t0 = t;
        for (int k = from; k < to; k++) targets[t++] = has_place ? place[g.succ_e[k]] : pos[g.et[g.succ_e[k]]];
        const int d = t - t0;
        if (d < 2) continue;
        bool mixed = false;
        if (has_place)
          for (int k = from + 1; k < to; k++)
            if (g.efd[g.succ_e[k]] != g.efd[g.succ_e[from]]) mixed = true;
        if (has_place && mixed) {
          i32s ids(g.succ_e.begin() + from, g.succ_e.begin() + to);
          std::stable_sort(ids.begin(), ids.end(), [&](int a, int b) {
            const double dd = g.efd[a] - g.efd[b];
            if (dd != 0) return dd < 0;
            return place[a] < place[b];
          });
          for (int k = 0; k < d; k++) targets[t0 + k] = place[ids[k]];
        } else if (d <= 12) {
          for (int a = t0 + 1; a < t; a++) {
            const int item = targets[a];
            int b = a - 1;
            while (b >= t0 && targets[b] > item) {
              targets[b + 1] = targets[b];
              b--;
            }
            targets[b + 1] = item;
          }
        } else {
          std::sort(targets.begin() + t0, targets.begin() + t);
        }
      }
      if (t < 2) continue;
      int leaves = 1;
      while (leaves < south_len) leaves <<= 1;
      std::fill(tree.begin(), tree.begin() + 2 * leaves, 0);
      for (int k = 0; k < t; k++) {
        int i = targets[k] + leaves;
        tree[i]++;
        while (i > 1) {
          if ((i & 1) == 0) total += tree[i + 1];
          i >>= 1;
          tree[i]++;
        }
      }
    }
    return total;
  };

  long long best = count();
  if (best == 0) return;
  i32s best_order = order;
  f64s bary(n, 0);
  i32s moving(widest, 0);

  auto sweep = [&](bool down, bool bias_right) {
    const i32s& start = down ? g.pred_start : g.succ_start;
    const i32s& list = down ? g.pred_e : g.succ_e;
    const i32s& other = down ? g.ef : g.et;
    const f64s& other_dx = down ? g.efd : g.etd;
    for (int step = 1; step <= max_rank; step++) {
      const int r = down ? step : max_rank - step;
      const int s = layer_start[r];
      const int e = layer_start[r + 1];
      int movable = 0;
      for (int i = s; i < e; i++) {
        const int v = order[i];
        const int pin = v < real ? nodes[v].pin : 0;
        if (pin != 0) {
          bary[v] = pin * 1e9;
          moving[movable++] = v;
          continue;
        }
        const int from = start[v];
        const int to = start[v + 1];
        if (from == to) {
          bary[v] = -1;
          continue;
        }
        double sum = 0;
        for (int k = from; k < to; k++) {
          const int u = other[list[k]];
          sum += pos[u];
          if (g.offsets && g.w[u] > 0) sum += other_dx[list[k]] / g.w[u];
        }
        bary[v] = sum / (to - from);
        moving[movable++] = v;
      }
      if (movable < 2) continue;
      if (bias_right) {
        std::sort(moving.begin(), moving.begin() + movable, [&](int a, int b) {
          const double d = bary[a] - bary[b];
          if (d != 0) return d < 0;
          return pos[b] < pos[a];
        });
      } else {
        std::sort(moving.begin(), moving.begin() + movable, [&](int a, int b) {
          const double d = bary[a] - bary[b];
          if (d != 0) return d < 0;
          return pos[a] < pos[b];
        });
      }
      int k = 0;
      for (int i = s; i < e; i++)
        if (bary[order[i]] != -1) order[i] = moving[k++];
      for (int i = s; i < e; i++) pos[order[i]] = i - s;
    }
  };

  // How many pairs of nodes in one rank are not in the order they were given.
  auto out_of_order = [&]() -> long long {
    long long total = 0;
    for (int r = 0; r <= max_rank; r++) {
      for (int i = layer_start[r]; i < layer_start[r + 1]; i++) {
        if (order[i] >= real) continue;
        for (int j = i + 1; j < layer_start[r + 1]; j++)
          if (order[j] < order[i]) total++;
      }
    }
    return total;
  };
  auto settle_positions = [&]() {
    for (int r = 0; r <= max_rank; r++)
      for (int i = layer_start[r]; i < layer_start[r + 1]; i++) pos[order[i]] = i - layer_start[r];
  };

  // The sweeps are run twice from the same start, going down first and going up first.
  const i32s initial = order;
  const long long start_count = best;
  const bool small = n <= 600;
  i32s chosen = best_order;
  long long chosen_out = std::numeric_limits<long long>::max();
  for (int attempt = 0; attempt < (small ? 2 : 1); attempt++) {
    if (attempt > 0) {
      order = initial;
      settle_positions();
    }
    long long found = start_count;
    i32s found_order = initial;
    for (int i = 0, stale = 0; stale < 4 && i < kMaxSweeps; i++, stale++) {
      sweep((i + attempt) % 2 == 0, i % 4 >= 2);
      const long long c = count();
      if (c < found) {
        found = c;
        found_order = order;
        stale = 0;
        if (c == 0) break;
      }
    }
    order = found_order;
    const long long out = small ? out_of_order() : 0;
    if (attempt == 0 || found < best || (found == best && out < chosen_out)) {
      best = found;
      chosen = found_order;
      chosen_out = out;
    }
  }
  order = chosen;
  settle_positions();
  best_order = chosen;

  // A drawing and its mirror image cross as often too. Of those two, likewise.
  if (!small) return;
  for (int i = 0; i < real; i++)
    if (nodes[i].pin != 0) return;
  if (chosen_out == 0) return;
  for (int r = 0; r <= max_rank; r++) std::reverse(order.begin() + layer_start[r], order.begin() + layer_start[r + 1]);
  settle_positions();
  if (count() > best || out_of_order() >= chosen_out) order = best_order;
}

// Brandes-Köpf horizontal coordinate assignment: four aligned layouts, balanced.
f64s position(graph& g, const layered_options& opt) {
  // The neighbour lists are sorted in place below; later steps rely on that order.
  const int n = g.n;
  const int max_rank = g.max_rank;
  const i32s& layer_start = g.layer_start;
  const i32s& order = g.order;
  const i32s& pos = g.pos;
  const std::size_t segments = g.ef.size();
  f64s gap(n);
  for (int i = 0; i < n; i++) gap[i] = half_room(g, opt, i);

  // Neighbours in left-to-right order, so the medians can be read off directly.
  i32s key(segments);
  for (std::size_t e = 0; e < segments; e++) key[e] = pos[g.et[e]];
  for (int v = 0; v < n; v++)
    if (g.succ_start[v + 1] - g.succ_start[v] > 1) sort_range(g.succ_e, g.succ_start[v], g.succ_start[v + 1], key);
  for (std::size_t e = 0; e < segments; e++) key[e] = pos[g.ef[e]];
  for (int v = 0; v < n; v++)
    if (g.pred_start[v + 1] - g.pred_start[v] > 1) sort_range(g.pred_e, g.pred_start[v], g.pred_start[v + 1], key);

  // An edge between two real nodes may not cross a dummy-to-dummy segment; mark those that would.
  u8s conflict(segments, 0);
  for (int r = 1; r <= max_rank; r++) {
    const int prev_len = layer_start[r] - layer_start[r - 1];
    const int s = layer_start[r];
    const int end = layer_start[r + 1];
    int k0 = 0;
    int scan = s;
    for (int i = s; i < end; i++) {
      const int v = order[i];
      int inner = -1;
      if (is_dummy(g.kind[v])) {
        const int u = g.ef[g.pred_e[g.pred_start[v]]];
        if (is_dummy(g.kind[u])) inner = u;
      }
      const int k1 = inner >= 0 ? pos[inner] : prev_len;
      if (inner >= 0 || i == end - 1) {
        for (int j = scan; j <= i; j++) {
          const int w = order[j];
          const bool w_dummy = is_dummy(g.kind[w]);
          for (int k = g.pred_start[w]; k < g.pred_start[w + 1]; k++) {
            const int e = g.pred_e[k];
            const int up = pos[g.ef[e]];
            if ((up < k0 || k1 < up) && !(w_dummy && is_dummy(g.kind[g.ef[e]]))) conflict[e] = 1;
          }
        }
        scan = i + 1;
        k0 = k1;
      }
    }
  }

  std::vector<f64s> results;
  i32s root(n);
  i32s align(n);
  // Offset of each node from its block's root, so that aligned edges meet at their attachment points.
  f64s shift(n);
  i32s e_from(n);
  i32s e_to(n);
  f64s e_w(n);
  i32s indeg(n);
  i32s out_start(n + 1);
  i32s out_e(n);
  i32s fill(n);
  i32s topo(n);

  for (int variant = 0; variant < 4; variant++) {
    const bool up = variant < 2;
    const bool right = (variant & 1) == 1;
    const i32s& start = up ? g.pred_start : g.succ_start;
    const i32s& list = up ? g.pred_e : g.succ_e;
    const i32s& other = up ? g.ef : g.et;
    const f64s& own_dx = up ? g.etd : g.efd;
    const f64s& other_dx = up ? g.efd : g.etd;
    for (int i = 0; i < n; i++) {
      root[i] = i;
      align[i] = i;
      shift[i] = 0;
    }

    for (int step = 0; step <= max_rank; step++) {
      const int r = up ? step : max_rank - step;
      const int s = layer_start[r];
      const int end = layer_start[r + 1];
      const int adjacent = up ? r - 1 : r + 1;
      const int adjacent_last = adjacent >= 0 && adjacent <= max_rank ? layer_start[adjacent + 1] - layer_start[adjacent] - 1 : 0;
      int prev_idx = -1;
      for (int i = right ? end - 1 : s; right ? i >= s : i < end; right ? i-- : i++) {
        const int v = order[i];
        const int from = start[v];
        const int d = start[v + 1] - from;
        if (d == 0) continue;
        const int lo = from + ((d - 1) >> 1);
        const int hi = from + (d >> 1);
        for (int pass = 0; pass < 2; pass++) {
          if (pass == 1 && lo == hi) break;
          const int e = list[(pass == 0) != right ? lo : hi];
          const int w = other[e];
          const int idx = right ? adjacent_last - pos[w] : pos[w];
          if (align[v] == v && prev_idx < idx && conflict[e] == 0) {
            align[w] = v;
            root[v] = root[w];
            align[v] = root[v];
            shift[v] = shift[w] + other_dx[e] - own_dx[e];
            prev_idx = idx;
          }
        }
      }
    }

    int block_edges = 0;
    std::fill(indeg.begin(), indeg.end(), 0);
    std::fill(out_start.begin(), out_start.end(), 0);
    for (int r = 0; r <= max_rank; r++) {
      const int s = layer_start[r];
      const int end = layer_start[r + 1];
      for (int i = s + 1; i < end; i++) {
        const int a = right ? order[i] : order[i - 1];
        const int b = right ? order[i - 1] : order[i];
        const double sep = gap[a] + gap[b];
        e_from[block_edges] = root[a];
        e_to[block_edges] = root[b];
        e_w[block_edges] = right ? sep - shift[a] + shift[b] : sep + shift[a] - shift[b];
        indeg[root[b]]++;
        out_start[root[a] + 1]++;
        block_edges++;
      }
    }
    for (int i = 0; i < n; i++) out_start[i + 1] += out_start[i];
    std::copy(out_start.begin(), out_start.begin() + n, fill.begin());
    for (int e = 0; e < block_edges; e++) out_e[fill[e_from[e]]++] = e;

    f64s x(n, 0);
    int cnt = 0;
    for (int i = 0; i < n; i++)
      if (root[i] == i && indeg[i] == 0) topo[cnt++] = i;
    for (int q = 0; q < cnt; q++) {
      const int v = topo[q];
      for (int k = out_start[v]; k < out_start[v + 1]; k++) {
        const int e = out_e[k];
        const int w = e_to[e];
        if (x[v] + e_w[e] > x[w]) x[w] = x[v] + e_w[e];
        if (--indeg[w] == 0) topo[cnt++] = w;
      }
    }
    for (int q = cnt - 1; q >= 0; q--) {
      const int v = topo[q];
      double mn = kInf;
      for (int k = out_start[v]; k < out_start[v + 1]; k++) {
        const int e = out_e[k];
        const double cand = x[e_to[e]] - e_w[e];
        if (cand < mn) mn = cand;
      }
      if (mn != kInf && mn > x[v]) x[v] = mn;
    }
    f64s xs(n);
    for (int i = 0; i < n; i++) xs[i] = (right ? -x[root[i]] : x[root[i]]) + shift[i];
    results.push_back(std::move(xs));
  }

  int narrowest = 0;
  double best_width = kInf;
  double mins[4];
  double maxs[4];
  for (int k = 0; k < 4; k++) {
    double lo = kInf;
    double hi = -kInf;
    const f64s& xs = results[k];
    for (int i = 0; i < n; i++) {
      const double half = g.w[i] / 2;
      if (xs[i] - half < lo) lo = xs[i] - half;
      if (xs[i] + half > hi) hi = xs[i] + half;
    }
    mins[k] = lo;
    maxs[k] = hi;
    if (hi - lo < best_width) {
      best_width = hi - lo;
      narrowest = k;
    }
  }
  for (int k = 0; k < 4; k++) {
    const double delta = (k & 1) == 0 ? mins[narrowest] - mins[k] : maxs[narrowest] - maxs[k];
    if (delta != 0) {
      f64s& xs = results[k];
      for (int i = 0; i < n; i++) xs[i] += delta;
    }
  }

  f64s out(n);
  for (int i = 0; i < n; i++) {
    double a = results[0][i];
    double b = results[1][i];
    double c = results[2][i];
    double d = results[3][i];
    double t;
    if (a > b) { t = a; a = b; b = t; }
    if (c > d) { t = c; c = d; d = t; }
    if (a > c) { t = a; a = c; c = t; }
    if (b > d) { t = b; b = d; d = t; }
    if (b > c) { t = b; b = c; c = t; }
    out[i] = (b + c) / 2;
  }
  return out;
}

}  // namespace

layered_node make_layered_node(double w, double h, node_kind kind) {
  layered_node n;
  n.w = w;
  n.h = h;
  n.kind = kind;
  return n;
}

layered_edge make_layered_edge(int tail, int head, int minlen, double label_w, double label_h) {
  layered_edge e;
  e.tail = tail;
  e.head = head;
  e.minlen = minlen;
  e.label_w = label_w;
  e.label_h = label_h;
  return e;
}

void orient(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges) {
  const int n = static_cast<int>(nodes.size());
  const int m = static_cast<int>(edges.size());
  i32s start(n + 1, 0);
  for (int i = 0; i < m; i++) {
    layered_edge& e = edges[i];
    e.reversed = nodes[e.head].kind == node_kind::start_port || nodes[e.tail].kind == node_kind::end_port;
    if (!e.reversed) start[e.tail + 1]++;
  }
  for (int i = 0; i < n; i++) start[i + 1] += start[i];
  i32s out(start[n]);
  {
    i32s fill(start.begin(), start.begin() + n);
    for (int i = 0; i < m; i++)
      if (!edges[i].reversed) out[fill[edges[i].tail]++] = i;
  }
  u8s state(n, 0);
  i32s cursor(start.begin(), start.begin() + n);
  i32s stack;
  for (int root = 0; root < n; root++) {
    if (state[root] != 0) continue;
    stack.push_back(root);
    state[root] = 1;
    while (!stack.empty()) {
      const int v = stack.back();
      if (cursor[v] < start[v + 1]) {
        layered_edge& e = edges[out[cursor[v]++]];
        const int w = e.head;
        if (state[w] == 1) e.reversed = true;
        else if (state[w] == 0) {
          state[w] = 1;
          stack.push_back(w);
        }
      } else {
        state[v] = 2;
        stack.pop_back();
      }
    }
  }
}

void assign_ranks(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges, int step) {
  const int n = static_cast<int>(nodes.size());
  const int m = static_cast<int>(edges.size());
  i32s from(m);
  i32s to(m);
  i32s len(m);
  i32s out_start(n + 1, 0);
  i32s in_start(n + 1, 0);
  for (int i = 0; i < m; i++) {
    const layered_edge& e = edges[i];
    const int a = e.reversed ? e.head : e.tail;
    const int b = e.reversed ? e.tail : e.head;
    from[i] = a;
    to[i] = b;
    len[i] = std::max(1, e.minlen) * step;
    out_start[a + 1]++;
    in_start[b + 1]++;
  }
  for (int i = 0; i < n; i++) {
    out_start[i + 1] += out_start[i];
    in_start[i + 1] += in_start[i];
  }
  i32s out_e(m);
  i32s in_e(m);
  {
    i32s of(out_start.begin(), out_start.begin() + n);
    i32s inf(in_start.begin(), in_start.begin() + n);
    for (int i = 0; i < m; i++) {
      out_e[of[from[i]]++] = i;
      in_e[inf[to[i]]++] = i;
    }
  }

  i32s r(n, 0);
  i32s indeg(n);
  for (int i = 0; i < n; i++) indeg[i] = in_start[i + 1] - in_start[i];
  i32s topo(n);
  int count = 0;
  for (int i = 0; i < n; i++)
    if (indeg[i] == 0) topo[count++] = i;
  for (int q = 0; q < count; q++) {
    const int v = topo[q];
    for (int k = out_start[v]; k < out_start[v + 1]; k++) {
      const int ei = out_e[k];
      const int w = to[ei];
      if (r[v] + len[ei] > r[w]) r[w] = r[v] + len[ei];
      if (--indeg[w] == 0) topo[count++] = w;
    }
  }

  // Longest-path ranking leaves loosely attached nodes far from their neighbours.
  // Move each node toward the side that has more edges.
  for (int pass = 0; pass < 8; pass++) {
    bool changed = false;
    for (int q = count - 1; q >= 0; q--) {
      const int v = topo[q];
      const int ins = in_start[v + 1] - in_start[v];
      const int outs = out_start[v + 1] - out_start[v];
      if (ins == 0 && outs == 0) continue;
      double lo = -kInf;
      double hi = kInf;
      for (int k = in_start[v]; k < in_start[v + 1]; k++) {
        const int ei = in_e[k];
        lo = std::max(lo, static_cast<double>(r[from[ei]] + len[ei]));
      }
      for (int k = out_start[v]; k < out_start[v + 1]; k++) {
        const int ei = out_e[k];
        hi = std::min(hi, static_cast<double>(r[to[ei]] - len[ei]));
      }
      int next = r[v];
      if (outs > ins && hi != kInf) next = static_cast<int>(hi);
      else if (ins > outs && lo != -kInf) next = static_cast<int>(lo);
      if (next != r[v]) {
        r[v] = next;
        changed = true;
      }
    }
    if (!changed) break;
  }

  int mn = std::numeric_limits<int>::max();
  int mx = std::numeric_limits<int>::min();
  bool start_ports = false;
  for (int i = 0; i < n; i++) {
    const node_kind kind = nodes[i].kind;
    if (kind == node_kind::start_port) start_ports = true;
    if (kind == node_kind::start_port || kind == node_kind::end_port) continue;
    if (r[i] < mn) mn = r[i];
    if (r[i] > mx) mx = r[i];
  }
  if (mn == std::numeric_limits<int>::max()) {
    mn = 0;
    mx = 0;
  }
  for (int i = 0; i < n; i++) {
    if (nodes[i].kind == node_kind::start_port) r[i] = mn - step;
    else if (nodes[i].kind == node_kind::end_port) r[i] = mx + step;
  }
  if (start_ports) mn -= step;
  for (int i = 0; i < n; i++) nodes[i].rank = r[i] - mn;
}

layered_result layered_layout(std::vector<layered_node>& nodes, std::vector<layered_edge>& edges, const layered_options& opt) {
  const int real = static_cast<int>(nodes.size());
  if (real == 0) return {0, 0};
  const int m = static_cast<int>(edges.size());

  // Every edge gets a middle rank of its own when labels or parallel edges need the room.
  int step = 1;
  if (m > 0) {
    std::unordered_set<long long> seen;
    for (const layered_edge& e : edges) {
      const long long a = std::min(e.tail, e.head);
      const long long b = std::max(e.tail, e.head);
      const long long key = a * real + b;
      if (e.label_w > 0 || seen.count(key) > 0) {
        step = 2;
        break;
      }
      seen.insert(key);
    }
  }

  orient(nodes, edges);
  assign_ranks(nodes, edges, step);

  i32s span(m, 0);
  int dummies = 0;
  for (int ei = 0; ei < m; ei++) {
    const layered_edge& e = edges[ei];
    span[ei] = std::abs(nodes[e.head].rank - nodes[e.tail].rank);
    if (span[ei] > 1) dummies += span[ei] - 1;
  }
  u8s direct(m, 0);
  if (dummies > kMaxDummies) {
    i32s longest;
    for (int ei = 0; ei < m; ei++)
      if (span[ei] > 1) longest.push_back(ei);
    std::stable_sort(longest.begin(), longest.end(), [&](int a, int b) {
      const int d = span[b] - span[a];
      return d != 0 ? d < 0 : a < b;
    });
    for (int ei : longest) {
      if (dummies <= kMaxDummies) break;
      direct[ei] = 1;
      dummies -= span[ei] - 1;
    }
  }

  const int n = real + dummies;
  f64s W(n, 0);
  f64s H(n, 0);
  u8s KIND(n, 0);
  i32s RANK(n, 0);
  int max_rank = 0;
  for (int i = 0; i < real; i++) {
    const layered_node& node = nodes[i];
    W[i] = node.w;
    H[i] = node.h;
    KIND[i] = static_cast<std::uint8_t>(node.kind);
    RANK[i] = node.rank;
    if (node.rank > max_rank) max_rank = node.rank;
  }

  // The graph that gets ordered and positioned: every edge cut into rank-to-rank segments.
  int segments = 0;
  for (int ei = 0; ei < m; ei++)
    if (!direct[ei]) segments += span[ei];
  i32s EF(segments, 0);
  i32s ET(segments, 0);
  f64s EFD(segments, 0);
  f64s ETD(segments, 0);
  i32s first_dummy(m, -1);
  // Which way the edges with a label in each rank head: 1 down, 2 up, 3 both.
  u8s heads(max_rank + 1, 0);
  i32s label_node(m, -1);
  bool spans = false;
  for (int i = 0; i < real; i++)
    if (nodes[i].span > 0 && nodes[i].kind == node_kind::node) spans = true;
  // What each segment's end is to the node it meets, for the ends that may move along the node's side.
  u8s EFK;
  u8s ETK;
  if (spans) {
    EFK.assign(segments, 0);
    ETK.assign(segments, 0);
  }
  i32s first_seg(m, -1);
  i32s last_seg(m, -1);
  bool offsets = false;
  {
    int next = real;
    int ce = 0;
    for (int ei = 0; ei < m; ei++) {
      if (direct[ei]) continue;
      const layered_edge& e = edges[ei];
      const int a = e.reversed ? e.head : e.tail;
      const int b = e.reversed ? e.tail : e.head;
      const double a_dx = e.reversed ? e.head_dx : e.tail_dx;
      const double b_dx = e.reversed ? e.tail_dx : e.head_dx;
      if (a_dx != 0 || b_dx != 0) offsets = true;
      const int ra = RANK[a];
      const int rb = RANK[b];
      int label_rank = -1;
      if (e.label_w > 0) {
        // The label goes halfway along. An edge that passes ranks of nodes on its way has its
        // label in one of those, beside the nodes, where it takes no height of its own.
        label_rank = ra + ((rb - ra) >> 1);
        if (step == 2 && rb - ra >= 4) {
          if ((label_rank & 1) == 1) label_rank += e.reversed ? 1 : -1;
        } else if (step == 2 && (label_rank & 1) == 0) {
          label_rank += e.reversed ? 1 : -1;
        }
      }
      int prev = a;
      if (rb - ra > 1) first_dummy[ei] = next;
      first_seg[ei] = ce;
      if (spans && !(e.reversed ? e.head_fixed : e.tail_fixed)) EFK[ce] = e.reversed ? arrives : leaves;
      for (int r = ra + 1; r < rb; r++) {
        const int d = next++;
        RANK[d] = r;
        if (r == label_rank) {
          W[d] = e.label_w;
          H[d] = e.label_h;
          KIND[d] = static_cast<std::uint8_t>(k_label);
          label_node[ei] = d;
          heads[r] |= e.reversed ? 2 : 1;
        } else {
          KIND[d] = static_cast<std::uint8_t>(k_dummy);
        }
        EF[ce] = prev;
        ET[ce] = d;
        EFD[ce] = prev == a ? a_dx : 0;
        ce++;
        prev = d;
      }
      EF[ce] = prev;
      ET[ce] = b;
      EFD[ce] = prev == a ? a_dx : 0;
      ETD[ce] = b_dx;
      if (spans && !(e.reversed ? e.tail_fixed : e.head_fixed)) ETK[ce] = e.reversed ? leaves : arrives;
      last_seg[ei] = ce;
      ce++;
    }
  }

  graph g;
  g.n = n;
  g.real = real;
  g.max_rank = max_rank;
  g.w = W;
  g.kind = KIND;
  g.rank = RANK;
  g.ef = EF;
  g.et = ET;
  g.efd = EFD;
  g.etd = ETD;
  g.offsets = offsets;

  g.succ_start.assign(n + 1, 0);
  g.pred_start.assign(n + 1, 0);
  for (int e = 0; e < segments; e++) {
    g.succ_start[EF[e] + 1]++;
    g.pred_start[ET[e] + 1]++;
  }
  for (int i = 0; i < n; i++) {
    g.succ_start[i + 1] += g.succ_start[i];
    g.pred_start[i + 1] += g.pred_start[i];
  }
  g.succ_e.assign(segments, 0);
  g.pred_e.assign(segments, 0);
  {
    i32s sf(g.succ_start.begin(), g.succ_start.begin() + n);
    i32s pf(g.pred_start.begin(), g.pred_start.begin() + n);
    for (int e = 0; e < segments; e++) {
      g.succ_e[sf[EF[e]]++] = e;
      g.pred_e[pf[ET[e]]++] = e;
    }
  }

  g.layer_start.assign(max_rank + 2, 0);
  for (int i = 0; i < n; i++) g.layer_start[RANK[i] + 1]++;
  for (int r = 0; r <= max_rank; r++) g.layer_start[r + 1] += g.layer_start[r];
  g.order.assign(n, 0);
  g.pos.assign(n, 0);
  i32s& order = g.order;
  i32s& pos = g.pos;
  const i32s& layer_start = g.layer_start;

  // Initial order: depth-first from the top so that subtrees start out contiguous.
  {
    i32s by_rank(n);
    i32s fill(layer_start.begin(), layer_start.begin() + max_rank + 1);
    for (int i = 0; i < n; i++) by_rank[fill[RANK[i]]++] = i;
    std::copy(layer_start.begin(), layer_start.begin() + max_rank + 1, fill.begin());
    u8s visited(n, 0);
    i32s stack;
    for (int k = 0; k < n; k++) {
      if (visited[by_rank[k]]) continue;
      stack.push_back(by_rank[k]);
      while (!stack.empty()) {
        const int v = stack.back();
        stack.pop_back();
        if (visited[v]) continue;
        visited[v] = 1;
        order[fill[RANK[v]]++] = v;
        for (int j = g.succ_start[v + 1] - 1; j >= g.succ_start[v]; j--) {
          const int w = ET[g.succ_e[j]];
          if (!visited[w]) stack.push_back(w);
        }
      }
    }
  }

  bool pinned = false;
  for (int i = 0; i < real; i++)
    if (nodes[i].pin != 0) pinned = true;
  if (pinned) {
    for (int r = 0; r <= max_rank; r++) {
      i32s layer(order.begin() + layer_start[r], order.begin() + layer_start[r + 1]);
      std::map<int, int> index;
      for (std::size_t i = 0; i < layer.size(); i++) index[layer[i]] = static_cast<int>(i);
      auto pin_of = [&](int v) -> int { return v < real ? nodes[v].pin : 0; };
      std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) {
        const int d = pin_of(a) - pin_of(b);
        return d != 0 ? d < 0 : index[a] < index[b];
      });
      std::copy(layer.begin(), layer.end(), order.begin() + layer_start[r]);
    }
  }
  auto set_positions = [&]() {
    for (int r = 0; r <= max_rank; r++) {
      const int s = layer_start[r];
      for (int i = s, e = layer_start[r + 1]; i < e; i++) pos[order[i]] = i - s;
    }
  };
  set_positions();

  if (segments > 0 && max_rank > 0) {
    reduce_crossings(g, nodes);
    set_positions();
  }

  if (spans) spread(g, nodes, EFK, ETK);

  f64s xs = position(g, opt);

  u8s empty_store;
  const bool has_empty = empty_ranks(g, empty_store);
  const u8s* empty = has_empty ? &empty_store : nullptr;
  if (spans) {
    i32s free[2];
    free_ends(g, EFK, ETK, free);
    align_labels(g, nodes, xs, free, opt, empty);
    settle(g, nodes, xs, EFK, ETK, free, empty);
    snap(g, xs, empty, opt);
    for (int ei = 0; ei < m; ei++) {
      if (direct[ei]) continue;
      layered_edge& e = edges[ei];
      const double a_dx = g.efd[first_seg[ei]];
      const double b_dx = g.etd[last_seg[ei]];
      e.tail_dx = e.reversed ? b_dx : a_dx;
      e.head_dx = e.reversed ? a_dx : b_dx;
    }
  }

  // A rank that holds nothing but the bends of edges is only there because some other edge has a
  // label or a twin. Its bends move onto the straight line between the ends of their edges.
  u8s on_line(n, 0);
  if (has_empty && !opt.tracks) straighten(g, xs, empty_store, first_dummy, span, opt.apart ? 0 : opt.edge_sep, on_line);
  if (!opt.tracks) past_labels(g, xs, first_dummy, span, opt.edge_sep, on_line);
  // An edge is cut where it passes a rank that holds something, and each piece crosses one gap.
  i32s piece_start(m + 1, 0);
  const bool use_pieces = opt.tracks || (spans && !opt.straight);
  pieces_t pieces;
  i32s track_count(max_rank + 1, 0);
  if (use_pieces) {
    for (int ei = 0; ei < m; ei++) {
      piece_start[ei] = static_cast<int>(pieces.from.size());
      if (direct[ei]) continue;
      const int d0 = first_dummy[ei];
      // An edge that keeps a bend to stay clear of its twin is drawn through the bend, as it is.
      bool twin = false;
      for (int d = d0, end = d0 < 0 ? d0 : d0 + span[ei] - 1; d < end; d++) {
        // So is one that has a bend left out in a rank of labels: its curve crosses that rank.
        const bool bare = has_empty && empty_store[RANK[d]] == 1;
        if (!opt.tracks && bare != (on_line[d] == 1)) twin = true;
      }
      if (twin) continue;
      double x = xs[EF[first_seg[ei]]] + g.efd[first_seg[ei]];
      int rank = RANK[EF[first_seg[ei]]];
      for (int d = d0, end = d0 < 0 ? d0 : d0 + span[ei] - 1; d < end; d++) {
        if (has_empty && empty_store[RANK[d]]) continue;
        pieces.from.push_back(x);
        pieces.to.push_back(xs[d]);
        pieces.rank.push_back(rank);
        pieces.lower.push_back(d);
        x = xs[d];
        rank = RANK[d];
      }
      pieces.from.push_back(x);
      pieces.to.push_back(xs[ET[last_seg[ei]]] + g.etd[last_seg[ei]]);
      pieces.rank.push_back(rank);
      pieces.lower.push_back(-1);
    }
    piece_start[m] = static_cast<int>(pieces.from.size());
  }

  // Ranks that hold only border ports or their pass-through dummies need little room of their own.
  auto port_only = [&](int r, int kind) -> bool {
    bool found = false;
    for (int i = layer_start[r]; i < layer_start[r + 1]; i++) {
      const int k = KIND[order[i]];
      if (k == kind) found = true;
      else if (k != k_dummy) return false;
    }
    return found;
  };
  const int start_ranks = port_only(0, k_start_port) ? step : 0;
  const int end_ranks = port_only(max_rank, k_end_port) ? step : 0;
  // The longest way across that any edge travels in the gap below each rank.
  f64s run(max_rank + 1, 0);
  for (int e = 0; e < segments; e++) {
    const double across = std::abs(xs[EF[e]] + g.efd[e] - xs[ET[e]] - g.etd[e]);
    if (across > run[RANK[EF[e]]]) run[RANK[EF[e]]] = across;
  }
  f64s band_top(max_rank + 1, 0);
  f64s band_bottom(max_rank + 1, 0);
  auto usual = [&](int r) -> double { return (r < start_ranks || r >= max_rank - end_ranks ? opt.port_sep : opt.rank_sep) / step; };
  // The room under each rank.
  f64s gap_under(max_rank + 1, 0);
  for (int r = 0; r < max_rank; r++)
    gap_under[r] = use_pieces ? usual(r) : std::max(usual(r), std::min((kMaxGap * opt.rank_sep) / step, run[r] * kMinSlope));
  // A rank between two ranks of nodes, which holds labels and bends only.
  u8s between(max_rank + 1, 1);
  for (int i = 0; i < real; i++) between[RANK[i]] = 0;
  for (int r = 1; r < max_rank; r++) {
    if (opt.tracks || !between[r] || (heads[r] != 1 && heads[r] != 2)) continue;
    const double shift = std::min(opt.head_room / 2, gap_under[heads[r] == 1 ? r - 1 : r] / 2);
    gap_under[r - 1] += heads[r] == 1 ? -shift : shift;
    gap_under[r] += heads[r] == 1 ? shift : -shift;
  }
  // The part of its rank that an edge runs straight through at a bend or label.
  auto inset = [&](int d) -> double {
    return KIND[d] == k_dummy && between[RANK[d]] && !opt.tracks ? (band_bottom[RANK[d]] - band_top[RANK[d]]) / 4 : 0;
  };
  const double margin = opt.tracks ? kTrackMargin : kLevelMargin;
  const double least_pitch = opt.tracks ? kTrackPitch : kLevelPitch;
  const double most_pitch = opt.tracks ? kTrackSpread : kLevelPitch;
  i32s below(max_rank + 1, 0);
  if (use_pieces) {
    f64s room(max_rank + 1, 0);
    for (int r = 0; r < max_rank; r++) {
      if (has_empty && empty_store[r]) continue;
      int next = r + 1;
      room[r] = gap_under[r];
      while (next < max_rank && has_empty && empty_store[next]) room[r] += gap_under[next++];
      below[r] = next;
    }
    for (std::size_t k = 0; k < pieces.from.size(); k++)
      pieces.wide.push_back(std::abs(pieces.from[k] - pieces.to[k]) > kLevelWide * room[pieces.rank[k]] ? 1 : 0);
    f64s tall(max_rank + 1, 0);
    assign_tracks(pieces, track_count, !opt.tracks, room, tall);
    for (int r = 0; r < max_rank; r++) {
      if (track_count[r] > 0) gap_under[r] += std::max(0.0, 2 * margin + (track_count[r] - 1) * least_pitch - room[r]);
      else if (tall[r] > 0) gap_under[r] += std::max(0.0, std::min(kMaxGap * opt.rank_sep, tall[r]) - room[r]);
    }
  }
  double y = 0;
  for (int r = 0; r <= max_rank; r++) {
    double h = 0;
    for (int i = layer_start[r]; i < layer_start[r + 1]; i++)
      if (H[order[i]] > h) h = H[order[i]];
    band_top[r] = y;
    band_bottom[r] = y + h;
    y += h + gap_under[r];
  }
  const double height = y;
  // Where a piece runs across its gap, or would on another track of its group.
  auto track_y = [&](int piece, int on_track) -> double {
    const int r = pieces.rank[piece];
    const double top = band_bottom[r];
    const double bottom = band_top[below[r]];
    const int count = opt.tracks ? track_count[r] : pieces.levels[piece];
    const double pitch = count > 1 ? std::min(most_pitch, (bottom - top - 2 * margin) / (count - 1)) : 0;
    return (top + bottom) / 2 + (on_track - static_cast<double>(count - 1) / 2) * pitch;
  };

  double min_x = kInf;
  double max_x = -kInf;
  for (int i = 0; i < n; i++) {
    const double half = W[i] / 2;
    if (xs[i] - half < min_x) min_x = xs[i] - half;
    if (xs[i] + half > max_x) max_x = xs[i] + half;
  }
  // Where edges spread along their nodes, a rank whose nodes only send edges on stands on one
  // line, and a rank whose nodes only receive them hangs from one.
  u8s arrives_in(max_rank + 1, 0);
  u8s leaves_in(max_rank + 1, 0);
  for (int i = 0; i < real; i++) {
    if (KIND[i] != k_node || !spans) continue;
    if (g.pred_start[i + 1] > g.pred_start[i]) arrives_in[RANK[i]] = 1;
    if (g.succ_start[i + 1] > g.succ_start[i]) leaves_in[RANK[i]] = 1;
  }
  for (int i = 0; i < real; i++) {
    layered_node& node = nodes[i];
    const int r = node.rank;
    node.x = xs[i] - min_x;
    if (KIND[i] == k_node && leaves_in[r] && !arrives_in[r]) node.y = band_bottom[r] - node.h / 2;
    else if (KIND[i] == k_node && arrives_in[r] && !leaves_in[r]) node.y = band_top[r] + node.h / 2;
    else node.y = (band_top[r] + band_bottom[r]) / 2;
  }

  for (int ei = 0; ei < m; ei++) {
    layered_edge& e = edges[ei];
    const layered_node& first = nodes[e.reversed ? e.head : e.tail];
    const layered_node& last = nodes[e.reversed ? e.tail : e.head];
    f64s pts;
    pts.push_back(first.x + (e.reversed ? e.head_dx : e.tail_dx));
    pts.push_back(first.y + first.h / 2);
    const int d0 = first_dummy[ei];
    if (use_pieces && piece_start[ei + 1] > piece_start[ei]) {
      for (int k = piece_start[ei]; k < piece_start[ei + 1]; k++) {
        if (pieces.track[k] >= 0 && (opt.tracks || pieces.wide[k] == 1)) {
          const double across = track_y(k, pieces.track[k]);
          pts.push_back(pieces.from[k] - min_x);
          pts.push_back(across);
          pts.push_back(pieces.to[k] - min_x);
          pts.push_back(across);
        } else if (pieces.track[k] > 0) {
          // A short piece among levels runs straight until it is past the level before its own,
          // and turns in one S from there, so that it crosses none of them.
          pts.push_back(pieces.from[k] - min_x);
          pts.push_back(track_y(k, pieces.track[k] - 1));
        }
        const int d = pieces.lower[k];
        if (d < 0) continue;
        pts.push_back(xs[d] - min_x);
        pts.push_back(band_top[RANK[d]] + inset(d));
        if (band_bottom[RANK[d]] > band_top[RANK[d]]) {
          pts.push_back(xs[d] - min_x);
          pts.push_back(band_bottom[RANK[d]] - inset(d));
        }
      }
    } else if (d0 >= 0) {
      for (int d = d0, end = d0 + span[ei] - 1; d < end; d++) {
        if (on_line[d]) continue;
        const double x = xs[d] - min_x;
        const double top = band_top[RANK[d]];
        const double bottom = band_bottom[RANK[d]];
        pts.push_back(x);
        pts.push_back(top + inset(d));
        if (bottom > top) {
          pts.push_back(x);
          pts.push_back(bottom - inset(d));
        }
      }
    }
    pts.push_back(last.x + (e.reversed ? e.tail_dx : e.head_dx));
    pts.push_back(last.y - last.h / 2);
    if (e.reversed) {
      for (std::size_t i = 0, j = pts.size() - 2; i < j; i += 2, j -= 2) {
        std::swap(pts[i], pts[j]);
        std::swap(pts[i + 1], pts[j + 1]);
      }
    }
    e.points = std::move(pts);
    const int ln = label_node[ei];
    if (ln >= 0) {
      e.label_x = xs[ln] - min_x;
      e.label_y = (band_top[RANK[ln]] + band_bottom[RANK[ln]]) / 2;
    } else if (e.label_w > 0) {
      e.label_x = (e.points[0] + e.points[e.points.size() - 2]) / 2;
      e.label_y = (e.points[1] + e.points[e.points.size() - 1]) / 2;
    }
  }

  return {max_x - min_x, height};
}

}  // namespace pele
