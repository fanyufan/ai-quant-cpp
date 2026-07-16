// 18-强化学习与高频探索 / CASE-迷宫问题/maze.py 的 C++ 实现
// 6 节点固定奖励矩阵的表格 Q-learning（确定性格式，等价于值迭代演示）

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <matplot/matplot.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

constexpr int N = 6;
constexpr double GAMMA = 0.8;

// 奖励矩阵：-1 表示不可达，100 表示目标状态 5 的奖励
const double R[N][N] = {
    {-1, -1, -1, -1,  0, -1},
    {-1, -1, -1,  0, -1, 100},
    {-1, -1, -1,  0, -1, -1},
    {-1,  0,  0, -1,  0, -1},
    { 0, -1, -1,  0, -1, 100},
    {-1,  0, -1, -1,  0, 100}
};

using QTable = std::vector<std::vector<double>>;

QTable train() {
    QTable Q(N, std::vector<double>(N, 0.0));

    for (int count = 0; count < 1000; ++count) {
        for (int state = 0; state < N; ++state) {
            for (int action = 0; action < N; ++action) {
                if (R[state][action] == -1.0) {
                    Q[state][action] = 0.0;
                } else {
                    double max_next = *std::max_element(Q[action].begin(), Q[action].end());
                    Q[state][action] = R[state][action] + GAMMA * max_next;
                }
            }
        }
    }
    return Q;
}

std::vector<int> optimal_path(const QTable& Q, int start = 0, int goal = 5, int max_len = 20) {
    std::vector<int> path;
    int state = start;
    path.push_back(state);
    for (int i = 0; i < max_len; ++i) {
        if (state == goal) break;
        int best_action = static_cast<int>(std::max_element(Q[state].begin(), Q[state].end()) - Q[state].begin());
        // 避免在原地循环（目标自环 5->5）
        if (best_action == state && state == goal) break;
        if (R[state][best_action] < 0) break; // 不可达则停止
        state = best_action;
        path.push_back(state);
        if (state == goal) break;
    }
    return path;
}

void print_report(const QTable& Q, const std::vector<int>& path) {
    fmt::print("\n{0}\n", std::string(60, '='));
    fmt::print("  迷宫 Q-learning 结果 (gamma={})\n", GAMMA);
    fmt::print("{0}\n", std::string(60, '='));
    fmt::print("  归一化 Q 表 (Q/6):\n");
    for (int i = 0; i < N; ++i) {
        fmt::print("  状态 {}: ", i);
        for (int j = 0; j < N; ++j) {
            fmt::print("{:>7.1f} ", Q[i][j] / 6.0);
        }
        fmt::print("\n");
    }
    fmt::print("\n  最优路径 (状态 {} -> {}): ", 0, 5);
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) fmt::print(" -> ");
        fmt::print("{}", path[i]);
    }
    fmt::print("\n{0}\n", std::string(60, '='));
}

json to_json(const QTable& Q, const std::vector<int>& path) {
    json j;
    j["gamma"] = GAMMA;
    j["iterations"] = 1000;
    j["states"] = N;
    j["goal"] = 5;
    j["optimal_path"] = path;

    json qnorm = json::array();
    for (int i = 0; i < N; ++i) {
        json row = json::array();
        for (int j = 0; j < N; ++j) {
            row.push_back(std::round(Q[i][j] / 6.0 * 10.0) / 10.0);
        }
        qnorm.push_back(row);
    }
    j["q_table_normalized"] = qnorm;

    json qraw = json::array();
    for (int i = 0; i < N; ++i) {
        json row = json::array();
        for (int j = 0; j < N; ++j) {
            row.push_back(std::round(Q[i][j] * 10.0) / 10.0);
        }
        qraw.push_back(row);
    }
    j["q_table_raw"] = qraw;
    return j;
}

void save_json(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << j.dump(2);
    fmt::print("  JSON 已保存: {}\n", path);
}

void save_chart(const QTable& Q) {
    namespace mp = matplot;
    auto fig = mp::figure(false);
    fig->size(1000, 600);
    auto ax = fig->current_axes();
    ax->hold(mp::on);

    std::vector<double> xs;
    std::vector<double> max_q;
    for (int i = 0; i < N; ++i) {
        xs.push_back(static_cast<double>(i));
        max_q.push_back(*std::max_element(Q[i].begin(), Q[i].end()) / 6.0);
    }
    ax->bar(xs, max_q)->display_name("max Q/6");
    ax->xticks(xs);
    ax->xlabel("状态");
    ax->ylabel("最大 Q 值 / 6");
    ax->title("迷宫各状态最大归一化 Q 值");
    ax->grid(mp::on);

    std::string path = "outputs/week9/maze_qlearning.png";
    fig->save(path);
    fmt::print("  图表已保存: {}\n", path);
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::filesystem::create_directories("outputs/week9");

    fmt::print("[开始] 迷宫 Q-learning ({} 状态 × {} 迭代)\n", N, 1000);
    auto Q = train();
    auto path = optimal_path(Q);

    print_report(Q, path);
    save_json("outputs/week9/maze_qlearning.json", to_json(Q, path));
    save_chart(Q);

    fmt::print("\n[结果] {{\"status\": \"success\", \"goal_reached\": {}, \"path_length\": {}}}\n",
               !path.empty() && path.back() == 5, path.size());
    return 0;
}
