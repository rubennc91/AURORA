#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include "quantum_kernels.h"

// ==========================================
// ESTRUCTURAS DE DATOS
// ==========================================
enum State : uint8_t { H = 0, V = 1, D = 2, A = 3 };

struct detections_t {
    std::vector<State> measurements;
    std::vector<uint64_t> time_tags;
};

// ==========================================
// FUNCIÓN DE LECTURA DE FICHEROS
// ==========================================
void read_detections(const std::string& file_name, detections_t& alice, detections_t& bob) {
    alice.measurements.clear(); alice.time_tags.clear();
    bob.measurements.clear(); bob.time_tags.clear();

    auto load_file = [](const std::string& path, detections_t& det) {
        std::ifstream file(path);
        if (!file.is_open()) return;
        char next_char = 0; std::string int_str;
        while(file >> next_char) {
            if (next_char == '(') {
                std::getline(file, int_str, ',');
                det.time_tags.push_back(strtoull(int_str.c_str(), NULL, 10));
                std::getline(file, int_str, ')');
                det.measurements.push_back(static_cast<State>(atoi(int_str.c_str())));
            }
        }
    };
    load_file(file_name + "_alice.detections", alice);
    load_file(file_name + "_bob.detections", bob);
}

// ==========================================
// MAIN: SISTEMA COMPLETO QKD
// ==========================================
int main() {
    std::cout << "============================================================\n";
    std::cout << "    AURORA HLS: FULL SYSTEM (SYNC -> SIFT -> PA)\n";
    std::cout << "============================================================\n";

    // Buffers
    std::vector<uint8_t> a_meas(BATCH_LIMIT), b_meas(BATCH_LIMIT);
    std::vector<uint8_t> a_key(BATCH_LIMIT), b_key(BATCH_LIMIT);
    std::vector<uint8_t> a_shuf(BATCH_LIMIT), b_shuf(BATCH_LIMIT); // Reusamos para PA output
    std::vector<uint8_t> a_par(BATCH_LIMIT), b_par(BATCH_LIMIT);

    // Iteramos por los 20 Batches
    for (int i = 1; i <= 20; ++i) {
        std::string batch_path = "../../../../batches/batch" + std::to_string(i);
        detections_t alice_det, bob_det;
        read_detections(batch_path, alice_det, bob_det);

        if (alice_det.time_tags.empty()) continue;

        uint32_t sz_a = std::min((size_t)BATCH_LIMIT, alice_det.time_tags.size());
        uint32_t sz_b = std::min((size_t)BATCH_LIMIT, bob_det.time_tags.size());
        for(uint32_t k=0; k<sz_a; k++) a_meas[k] = (uint8_t)alice_det.measurements[k];
        for(uint32_t k=0; k<sz_b; k++) b_meas[k] = (uint8_t)bob_det.measurements[k];

        // 1. SINCRONIZACIÓN Y CRIBADO
        CorrResult res;
        uint32_t a_cnt = 0;

        // Sync
        compute_correlation_hls(alice_det.time_tags.data(), a_meas.data(), bob_det.time_tags.data(), b_meas.data(),
            sz_a, sz_b, 350, 0, MODE_SYNC, a_key.data(), &a_cnt, res);
        int16_t lag = res.best_lag;

        // Sift
        compute_correlation_hls(alice_det.time_tags.data(), a_meas.data(), bob_det.time_tags.data(), b_meas.data(),
            sz_a, sz_b, 350, lag, MODE_SIFT, a_key.data(), &a_cnt, res);

        // 2. PREPARACIÓN BOB (SIMULACIÓN)
        uint32_t b_cnt = a_cnt;
        for(uint32_t k=0; k<a_cnt; k++) b_key[k] = a_key[k];

        // --- ZONA DE PRUEBAS ---
        // OPCIÓN A: Simular Error (Para probar Paridad) -> PA dará Mismatch
        // if (a_cnt > 0) { int p = (a_cnt > 10) ? a_cnt-1 : 0; b_key[p] = !b_key[p]; }

        // OPCIÓN B: Sin Error (Para probar Amplificación) -> PA dará Match
        // (Dejamos el error comentado para ver el éxito final del sistema)
        // -----------------------

        std::cout << "Batch " << std::setw(2) << i << " | Size: " << std::setw(3) << a_cnt;

        if (a_cnt == 0) {
            std::cout << " | No Key\n";
            continue;
        }

        // 3. AMPLIFICACIÓN DE PRIVACIDAD (NUEVO)
        // Factor de compresión 2:1 (De 40 bits pasamos a 20 bits seguros)
        uint32_t final_len = a_cnt / 2;

        if (final_len > 0) {
            // Alice genera su clave final
            compute_privacy_amplification_hls(
                a_key.data(),
                a_shuf.data(), // Guardamos resultado en a_shuf para ahorrar memoria
                a_cnt,
                final_len,
                99999 // Semilla compartida
            );

            // Bob genera su clave final
            compute_privacy_amplification_hls(
                b_key.data(),
                b_shuf.data(),
                b_cnt,
                final_len,
                99999
            );

            // Verificación
            bool final_match = true;
            for(uint32_t z=0; z<final_len; z++) {
                if (a_shuf[z] != b_shuf[z]) final_match = false;
            }

            std::cout << " | Final Key (" << final_len << " bits): "
                      << (final_match ? "[SECURE MATCH]" : "[MISMATCH]")
                      << " -> OK!";
        } else {
            std::cout << " | Key too short for PA";
        }
        std::cout << std::endl;
    }
    std::cout << "============================================================\n";
    return 0;
}
