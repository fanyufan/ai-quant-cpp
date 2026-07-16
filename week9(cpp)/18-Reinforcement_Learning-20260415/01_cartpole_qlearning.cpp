// 18-强化学习与高频探索 / CASE-cartpole-qlearning 的 C++ 实现
// 手写 CartPole-v1 物理环境 + 离散状态 Q-learning 训练

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <matplot/matplot.h>
#include <nlohmann/json.hpp>

#include "rl/discretizer.hpp"
#include "rl/qlearning.hpp"

using json = nlohmann::json;

namespace {

// CartPole-v1 物理环境
class CartPoleEnv {
public:
    CartPoleEnv() : rng_(std::random_device{}()) { reset(); }

    std::vector<double> reset() {
        std::uniform_real_distribution<double> dist(-0.05, 0.05);
        x_ = dist(rng_);
        x_dot_ = dist(rng_);
        theta_ = dist(rng_);
        theta_dot_ = dist(rng_);
        steps_ = 0;
        return state();
    }

    // action: 0 = 向左推, 1 = 向右推
    struct StepResult {
        std::vector<double> obs;
        double reward = 0.0;
        bool done = false;
    };

    StepResult step(size_t action) {
        double force = (action == 1) ? force_mag_ : -force_mag_;
        double cos_t = std::cos(theta_);
        double sin_t = std::sin(theta_);

        double temp = (force + polemass_length_ * theta_dot_ * theta_dot_ * sin_t) / total_mass_;
        double theta_acc = (gravity_ * sin_t - cos_t * temp)
                           / (length_ * (4.0 / 3.0 - masspole_ * cos_t * cos_t / total_mass_));
        double x_acc = temp - polemass_length_ * theta_acc * cos_t / total_mass_;

        x_ += tau_ * x_dot_;
        x_dot_ += tau_ * x_acc;
        theta_ += tau_ * theta_dot_;
        theta_dot_ += tau_ * theta_acc;

        ++steps_;
        bool done = std::abs(x_) > x_threshold_ || std::abs(theta_) > theta_threshold_;
        bool truncated = steps_ >= max_steps_;
        return {state(), 1.0, done || truncated};
    }

    std::vector<double> state() const {
        return {x_, x_dot_, theta_, theta_dot_};
    }

    int steps() const { return steps_; }

private:
    static constexpr double gravity_ = 9.8;
    static constexpr double masscart_ = 1.0;
    static constexpr double masspole_ = 0.1;
    static constexpr double total_mass_ = masscart_ + masspole_;
    static constexpr double length_ = 0.5;
    static constexpr double polemass_length_ = masspole_ * length_;
    static constexpr double force_mag_ = 10.0;
    static constexpr double tau_ = 0.02;
    static constexpr double x_threshold_ = 4.8;
    static constexpr double theta_threshold_ = 0.418; // ~24 degrees
    static constexpr int max_steps_ = 500;

    std::mt19937 rng_;
    double x_ = 0.0, x_dot_ = 0.0, theta_ = 0.0, theta_dot_ = 0.0;
    int steps_ = 0;
};

struct EpisodeResult {
    int episode = 0;
    int steps = 0;
    double total_reward = 0.0;
    double epsilon = 0.0;
    double lr = 0.0;
};

std::vector<EpisodeResult> train(CartPoleEnv& env,
                                 quant::rl::QTable& qtable,
                                 quant::rl::Discretizer& disc,
                                 size_t episodes,
                                 double gamma,
                                 double init_lr,
                                 double init_epsilon,
                                 int max_step) {
    quant::rl::EpsilonGreedyAgent agent(qtable, init_epsilon);
    std::vector<EpisodeResult> results;
    results.reserve(episodes);

    double lr = init_lr;
    double eps = init_epsilon;

    for (size_t ep = 0; ep < episodes; ++ep) {
        auto obs = env.reset();
        size_t state = disc.state(obs);
        int step = 0;
        double total_reward = 0.0;
        bool done = false;

        while (!done) {
            size_t action = agent.act(state);
            auto res = env.step(action);
            double reward = res.reward;
            done = res.done;
            size_t next_state = disc.state(res.obs);

            double future = done ? 0.0 : *std::max_element(qtable.values(next_state).begin(),
                                                            qtable.values(next_state).end());
            auto& qvals = qtable.values(state);
            if (qvals.empty()) qvals.resize(qtable.n_actions(), 0.0);
            double old = qvals[action];
            qvals[action] += lr * (reward + gamma * future - old);

            state = next_state;
            total_reward += reward;
            ++step;
            if (max_step > 0 && step >= max_step) done = true;
        }

        results.push_back({static_cast<int>(ep), step, total_reward, eps, lr});
        fmt::print("Episode {}: {} steps (avg reward {:.1f}). epsilon={:.3f}, lr={:.3f}\n",
                   ep, step, total_reward, eps, lr);

        // Decay schedules matching Python: max(min, 1.0 - log10((t+1)/25))
        eps = std::max(0.01, std::min(1.0, 1.0 - std::log10((static_cast<double>(ep) + 1.0) / 25.0)));
        lr = std::max(0.1, std::min(0.5, 1.0 - std::log10((static_cast<double>(ep) + 1.0) / 25.0)));
        agent.set_epsilon(eps);
    }
    return results;
}

void print_summary(const std::vector<EpisodeResult>& results) {
    if (results.empty()) return;
    int total_steps = 0;
    double total_reward = 0.0;
    for (const auto& r : results) {
        total_steps += r.steps;
        total_reward += r.total_reward;
    }
    double last_100_avg = 0.0;
    size_t start = results.size() > 100 ? results.size() - 100 : 0;
    for (size_t i = start; i < results.size(); ++i) last_100_avg += results[i].steps;
    last_100_avg /= static_cast<double>(results.size() - start);

    fmt::print("\n{0}\n", std::string(60, '='));
    fmt::print("  CartPole Q-learning 训练完成\n");
    fmt::print("{0}\n", std::string(60, '='));
    fmt::print("  总回合数: {}\n", results.size());
    fmt::print("  平均每回合步数: {:.1f}\n", static_cast<double>(total_steps) / results.size());
    fmt::print("  最近 100 回合平均步数: {:.1f}\n", last_100_avg);
    fmt::print("  累计总奖励: {:.1f}\n", total_reward);
    fmt::print("{0}\n", std::string(60, '='));
}

json to_json(const std::vector<EpisodeResult>& results, size_t states_visited) {
    json j;
    j["env"] = "CartPole-v1";
    j["algorithm"] = "Q-learning";
    j["episodes"] = results.size();
    j["states_visited"] = states_visited;
    json episodes = json::array();
    for (const auto& r : results) {
        json e;
        e["episode"] = r.episode;
        e["steps"] = r.steps;
        e["total_reward"] = r.total_reward;
        e["epsilon"] = std::round(r.epsilon * 1000.0) / 1000.0;
        e["learning_rate"] = std::round(r.lr * 1000.0) / 1000.0;
        episodes.push_back(e);
    }
    j["episodes"] = episodes;
    return j;
}

void save_json(const std::string& path, const json& j) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << j.dump(2);
    fmt::print("  JSON 已保存: {}\n", path);
}

void save_chart(const std::vector<EpisodeResult>& results) {
    namespace mp = matplot;
    auto fig = mp::figure(false);
    fig->size(1200, 600);
    auto ax = fig->current_axes();
    ax->hold(mp::on);

    std::vector<double> xs(results.size()), ys(results.size()), avg(results.size());
    double sum = 0.0;
    for (size_t i = 0; i < results.size(); ++i) {
        xs[i] = static_cast<double>(results[i].episode);
        ys[i] = static_cast<double>(results[i].steps);
        sum += ys[i];
        avg[i] = sum / static_cast<double>(i + 1);
    }
    ax->plot(xs, ys, ".")->display_name("每回合步数");
    ax->plot(xs, avg, "-")->line_width(2.0).display_name("累计平均步数");
    ax->xlabel("回合");
    ax->ylabel("步数");
    ax->title("CartPole Q-learning 训练曲线");
    ax->legend();
    ax->grid(mp::on);

    std::string path = "outputs/week9/cartpole_qlearning.png";
    fig->save(path);
    fmt::print("  图表已保存: {}\n", path);
}

} // namespace

int main(int argc, char* argv[]) {
    size_t episodes = 100;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--episodes" || arg == "-e") && i + 1 < argc) {
            episodes = static_cast<size_t>(std::atoi(argv[++i]));
        }
    }

    std::filesystem::create_directories("outputs/week9");

    fmt::print("[开始] CartPole Q-learning 训练 ({} 回合)\n", episodes);

    // 分箱配置与 Python 一致
    std::vector<size_t> bins = {5, 5, 8, 5};
    std::vector<double> lows = {-4.8, -4.0, -0.418, -0.87266}; // -50 degrees
    std::vector<double> highs = {4.8, 4.0, 0.418, 0.87266};

    CartPoleEnv env;
    quant::rl::Discretizer disc(bins, lows, highs);
    quant::rl::QTable qtable(2, 0.0, 0.0); // 2 actions, no random init

    auto results = train(env, qtable, disc, episodes,
                         /*gamma=*/0.95,
                         /*init_lr=*/0.5,
                         /*init_epsilon=*/1.0,
                         /*max_step=*/250);

    print_summary(results);

    auto j = to_json(results, qtable.size());
    save_json("outputs/week9/cartpole_qlearning.json", j);
    save_chart(results);

    fmt::print("\n[结果] {{\"status\": \"success\", \"episodes\": {}, \"final_avg_steps\": {:.1f}}}\n",
               results.size(), results.empty() ? 0.0 : results.back().steps);
    return 0;
}
