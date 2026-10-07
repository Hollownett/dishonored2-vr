#include "../src/arm_ik.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using namespace d2vr;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void verify(Vec3 target, Vec3 pole, double a=0.31, double b=0.27) {
    Vec3 shoulder{0.2,1.4,-0.1};
    auto pose = solve_arm(shoulder, shoulder+target, pole, a, b);
    require(finite(pose.elbow) && finite(pose.wrist), "IK produced non-finite joints");
    require(std::abs(length(pose.elbow-shoulder)-a) < 1e-8, "Upper arm length changed");
    require(std::abs(length(pose.wrist-pose.elbow)-b) < 1e-8, "Forearm length changed");
    double distance = length(target);
    if (distance >= std::max(1e-6, std::abs(a-b)) && distance <= a+b) {
        require(length(pose.wrist-(shoulder+target)) < 1e-8, "Reachable hand moved");
        require(!pose.reach_clamped, "Reachable hand flagged as clamped");
    } else require(pose.reach_clamped, "Unreachable hand not flagged");
}

int main() {
    try {
        verify({0,0,0}, {0,0,0});
        verify({0,0,0}, {0,0,0}, 0.3,0.3);
        verify({0,0,-0.3}, {0,0,-1});
        verify({0,0,-4}, {0,-1,0});
        verify({0,0,-0.58}, {0,-1,0});
        verify({0,0,-0.04}, {0,-1,0});
        verify({0,0,-0.3}, {1e300,1e300,1e300});
        auto left = solve_arm({0,0,0}, {0,0,-0.4}, {-1,0,0}, 0.3,0.3);
        auto right = solve_arm({0,0,0}, {0,0,-0.4}, {1,0,0}, 0.3,0.3);
        require(left.elbow.x < 0 && right.elbow.x > 0, "Elbow side ignored");
        std::mt19937 rng(20261007);
        std::uniform_real_distribution<double> values(-1,1), lengths(0.1,0.5);
        for (int i=0; i<20000; ++i) {
            verify({values(rng), values(rng), values(rng)},
                   {values(rng), values(rng), values(rng)}, lengths(rng), lengths(rng));
        }
        for (double bad : {0.0, -0.2, 3.0, std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected=false;
            try { solve_arm({}, {0,0,-0.3}, {0,-1,0}, bad, 0.3); }
            catch (const std::invalid_argument&) { rejected=true; }
            require(rejected, "Invalid calibration accepted");
        }
        bool rejected=false;
        try { solve_arm({}, {std::numeric_limits<double>::infinity(),0,0}, {}, 0.3,0.3); }
        catch (const std::invalid_argument&) { rejected=true; }
        require(rejected, "Invalid tracking accepted");
        std::cout << "PASS: 20,000 random poses, reach limits, singularities, elbow sides, invalid data\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
