#include <cstddef>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include "quantum_kernels.h"

// --- Estructuras y Constantes ---

enum State : uint8_t {
    H = 0,
    V = 1,
    D = 2,
    A = 3,
};

const uint64_t correlation_window_half_width_ps = 350;

struct correlation_time_t {
    int64_t time_lag;
    size_t n_correlations;
};

struct detections_t {
    std::vector<State> measurements;
    std::vector<uint64_t> time_tags;
};

// --- Funciones Auxiliares (Definiciones completas) ---

size_t check_correlations(
        const std::vector<uint64_t>& alice_time_tags,
        const std::vector<uint64_t>& bob_time_tags,
        const uint64_t correlation_window_half_width_ps,
        const int64_t delta_time
    ) {

    size_t n_correlations = 0;
    size_t alice_idx = 0;
    size_t bob_idx = 0;
    size_t alice_size = alice_time_tags.size();
    size_t bob_size = bob_time_tags.size();

    if (delta_time >= 0) {
        for (; bob_idx < bob_size; ++bob_idx) {
            for (; alice_idx < alice_size; ++alice_idx) {
                if (alice_time_tags[alice_idx] > bob_time_tags[bob_idx] + delta_time + correlation_window_half_width_ps) break;

                if (alice_time_tags[alice_idx] - correlation_window_half_width_ps <= bob_time_tags[bob_idx] + delta_time
                    && bob_time_tags[bob_idx] + delta_time <= alice_time_tags[alice_idx] + correlation_window_half_width_ps) {
                    ++n_correlations;
                    break;
                }
            }
        }
    } else {
        for (; bob_idx < bob_size; ++bob_idx) {
            for (; alice_idx < alice_size; ++alice_idx) {
                if (alice_time_tags[alice_idx] - delta_time > bob_time_tags[bob_idx] + correlation_window_half_width_ps) break;

                if (alice_time_tags[alice_idx] - correlation_window_half_width_ps - delta_time <= bob_time_tags[bob_idx]
                    && bob_time_tags[bob_idx] <= alice_time_tags[alice_idx] + correlation_window_half_width_ps - delta_time) {
                    ++n_correlations;
                    break;
                }
            }
        }
    }
    return n_correlations;
}

correlation_time_t find_time_lag(
        const std::vector<uint64_t>& alice_time_tags,
        const std::vector<uint64_t>& bob_time_tags,
        uint64_t correlation_window_half_width_ps
    ) {

    // Integración con el Kernel de AURORA para HLS
    CorrResult aurora_result;

    // Limitamos el tamaño para evitar desbordamiento del array en HLS
    uint32_t size_a = std::min((uint32_t)alice_time_tags.size(), (uint32_t)BATCH_LIMIT);
    uint32_t size_b = std::min((uint32_t)bob_time_tags.size(), (uint32_t)BATCH_LIMIT);

    // Llamada a la función de hardware
    compute_correlation_hls(alice_time_tags.data(), bob_time_tags.data(), size_a, size_b, correlation_window_half_width_ps, aurora_result);

    correlation_time_t max_correlation;
    max_correlation.time_lag = aurora_result.best_lag;
    max_correlation.n_correlations = aurora_result.max_count;

    return max_correlation;
}

std::vector<std::pair<size_t, size_t>> find_corrrelated_indices (
        const std::vector<uint64_t>& alice_time_tags,
        const std::vector<uint64_t>& bob_time_tags,
        const uint64_t correlation_window_half_width_ps,
        const int64_t delta_time
    ) {

    std::vector<std::pair<size_t, size_t>> indices;
    size_t alice_idx = 0;
    size_t bob_idx = 0;
    size_t alice_size = alice_time_tags.size();
    size_t bob_size = bob_time_tags.size();

    if (delta_time >= 0) {
        for (; bob_idx < bob_size; ++bob_idx) {
            for (; alice_idx < alice_size; ++alice_idx) {
                if (alice_time_tags[alice_idx] > bob_time_tags[bob_idx] + delta_time + correlation_window_half_width_ps) break;

                if (alice_time_tags[alice_idx] - correlation_window_half_width_ps <= bob_time_tags[bob_idx] + delta_time
                    && bob_time_tags[bob_idx] + delta_time <= alice_time_tags[alice_idx] + correlation_window_half_width_ps) {
                    indices.push_back(std::pair<size_t, size_t>(alice_idx, bob_idx));
                    ++alice_idx;
                    break;
                }
            }
        }
    } else {
        for (; bob_idx < bob_size; ++bob_idx) {
            for (; alice_idx < alice_size; ++alice_idx) {
                if (alice_time_tags[alice_idx] - delta_time > bob_time_tags[bob_idx] + correlation_window_half_width_ps) break;

                if (alice_time_tags[alice_idx] - correlation_window_half_width_ps - delta_time <= bob_time_tags[bob_idx]
                    && bob_time_tags[bob_idx] <= alice_time_tags[alice_idx] + correlation_window_half_width_ps - delta_time) {
                    indices.push_back(std::pair<size_t, size_t>(alice_idx, bob_idx));
                    ++alice_idx;
                    break;
                }
            }
        }
    }
    return indices;
}

void read_detections(
        const std::string& file_name,
        detections_t& alice_detections,
        detections_t& bob_detections
    ) {
    
    alice_detections.time_tags.clear();
    bob_detections.time_tags.clear();
    alice_detections.measurements.clear();
    bob_detections.measurements.clear();

    std::ifstream alice_stream(file_name + "_alice.detections");
    if (!alice_stream.is_open()) return;

    char next_char = 0;
    std::string int_str;

    while(alice_stream >> next_char) {
        if (next_char == '(') {
            std::getline(alice_stream, int_str, ',');
            alice_detections.time_tags.push_back(strtoull(int_str.c_str(), NULL, 10));
            std::getline(alice_stream, int_str, ')');
            int state = atoi(int_str.c_str());
            alice_detections.measurements.push_back(static_cast<State>(state));
        }
    }
    alice_stream.close();

    std::ifstream bob_stream(file_name + "_bob.detections");
    if (!bob_stream.is_open()) return;

    while(bob_stream >> next_char) {
        if (next_char == '(') {
            std::getline(bob_stream, int_str, ',');
            bob_detections.time_tags.push_back(strtoull(int_str.c_str(), NULL, 10));
            std::getline(bob_stream, int_str, ')');
            int state = atoi(int_str.c_str());
            bob_detections.measurements.push_back(static_cast<State>(state));
        }
    }
    bob_stream.close();
}

// --- Main con bucle de 20 batches ---

int main() {
    std::cout << "--- AURORA MULTI-BATCH TEST (1 to 20) ---" << std::endl;

    for (int i = 1; i <= 20; ++i) {
        // Construimos la ruta dinámica
        std::string batch_path = "../../../../batches/batch" + std::to_string(i);

        detections_t alice_detections, bob_detections;
        read_detections(batch_path, alice_detections, bob_detections);

        if (alice_detections.time_tags.empty() || bob_detections.time_tags.empty()) {
            std::cout << "Batch " << i << ": Skip (Files not found or empty)" << std::endl;
            continue;
        }

        // Llamamos a la función hardware (find_time_lag llama a compute_correlation_hls)
        correlation_time_t max_correlation = find_time_lag(alice_detections.time_tags, bob_detections.time_tags, correlation_window_half_width_ps);

        // Validación con software
        size_t correlations = check_correlations(alice_detections.time_tags, bob_detections.time_tags, correlation_window_half_width_ps, max_correlation.time_lag);
        std::vector<std::pair<size_t, size_t>> corrrelated_indices = find_corrrelated_indices(alice_detections.time_tags, bob_detections.time_tags, correlation_window_half_width_ps, max_correlation.time_lag);

        // Cálculo de QBER
        std::vector<State> true_coincidences;
        true_coincidences.reserve(corrrelated_indices.size());
        for (const auto& index_pair : corrrelated_indices)
            true_coincidences.push_back(alice_detections.measurements[index_pair.first]);

        std::vector<State> bob_true_coincidences;
        bob_true_coincidences.reserve(corrrelated_indices.size());
        for (const auto& index_pair : corrrelated_indices)
            bob_true_coincidences.push_back(bob_detections.measurements[index_pair.second]);

        size_t right_measurements = 0, wrong_measurements = 0, invalid_measurements = 0;

        for (size_t index = 0; index < corrrelated_indices.size(); ++index){
            if (true_coincidences[index] == bob_true_coincidences[index])
                ++right_measurements;
            else if ((true_coincidences[index] < 2 && bob_true_coincidences[index] < 2) || (true_coincidences[index] > 1 && bob_true_coincidences[index] > 1))
                ++wrong_measurements;
            else
                ++invalid_measurements;
        }

        double qber = (right_measurements + wrong_measurements > 0) ?
                      static_cast<double>(wrong_measurements) / static_cast<double>(right_measurements + wrong_measurements) : 0.0;

        std::cout << "Batch " << i << " -> [Lag: " << max_correlation.time_lag << "] "
                  << "[Corr: " << correlations << "] "
                  << "[QBER: " << qber << "]" << std::endl;
    }

    std::cout << "-----------------------------------------" << std::endl;
    return 0;
}
